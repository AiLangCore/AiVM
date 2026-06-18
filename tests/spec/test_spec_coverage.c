#include <stdio.h>
#include <string.h>

enum {
    MAX_LINE = 4096,
    MAX_ROWS = 256,
    MAX_FIELD = 2048
};

typedef struct {
    char id[MAX_FIELD];
    char spec[MAX_FIELD];
    char status[MAX_FIELD];
    char verification[MAX_FIELD];
    char notes[MAX_FIELD];
} CoverageRow;

static int path_exists(const char* root, const char* relative)
{
    char path[MAX_FIELD * 2];
    FILE* file;
    if (root == NULL || relative == NULL || root[0] == '\0' || relative[0] == '\0') {
        return 0;
    }
    (void)snprintf(path, sizeof(path), "%s/%s", root, relative);
    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    (void)fclose(file);
    return 1;
}

static void trim_newline(char* text)
{
    size_t length;
    if (text == NULL) {
        return;
    }
    length = strlen(text);
    while (length > 0U && (text[length - 1U] == '\n' || text[length - 1U] == '\r')) {
        text[length - 1U] = '\0';
        length -= 1U;
    }
}

static int split_tsv(char* line, CoverageRow* row)
{
    char* fields[5];
    size_t index;
    char* cursor = line;
    if (line == NULL || row == NULL) {
        return 0;
    }
    for (index = 0U; index < 5U; index += 1U) {
        fields[index] = cursor;
        if (index < 4U) {
            char* tab = strchr(cursor, '\t');
            if (tab == NULL) {
                return 0;
            }
            *tab = '\0';
            cursor = tab + 1;
        }
    }
    (void)snprintf(row->id, sizeof(row->id), "%s", fields[0]);
    (void)snprintf(row->spec, sizeof(row->spec), "%s", fields[1]);
    (void)snprintf(row->status, sizeof(row->status), "%s", fields[2]);
    (void)snprintf(row->verification, sizeof(row->verification), "%s", fields[3]);
    (void)snprintf(row->notes, sizeof(row->notes), "%s", fields[4]);
    trim_newline(row->notes);
    return 1;
}

static int verify_paths(const char* root, const CoverageRow* row)
{
    char verification[MAX_FIELD];
    char* cursor;
    if (row == NULL || root == NULL) {
        return 1;
    }
    if (row->verification[0] == '\0') {
        (void)fprintf(stderr, "spec coverage: row has no verification: %s\n", row->id);
        return 1;
    }
    (void)snprintf(verification, sizeof(verification), "%s", row->verification);
    cursor = verification;
    while (cursor != NULL && cursor[0] != '\0') {
        char* comma = strchr(cursor, ',');
        if (comma != NULL) {
            *comma = '\0';
        }
        if (!path_exists(root, cursor)) {
            (void)fprintf(stderr, "spec coverage: verification path missing for %s: %s\n", row->id, cursor);
            return 1;
        }
        cursor = comma == NULL ? NULL : comma + 1;
    }
    return 0;
}

static int required_spec_is_present(const CoverageRow* rows, size_t row_count, const char* spec)
{
    size_t index;
    for (index = 0U; index < row_count; index += 1U) {
        if (strcmp(rows[index].spec, spec) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    static const char* required_specs[] = {
        "SPEC/MEMORY.md",
        "SPEC/DEBUGGING.md",
        "SPEC/DEBUG_ARTIFACTS.md",
        "Docs/Syscalls.md",
        "Docs/Resource-Limits-And-Errors.md",
        "Docs/Native-Bridge.md",
        "Docs/Native-Test-Infrastructure.md"
    };
    CoverageRow rows[MAX_ROWS];
    char matrix_path[MAX_FIELD * 2];
    char line[MAX_LINE];
    FILE* matrix;
    size_t row_count = 0U;
    size_t index;
    size_t covered = 0U;
    size_t tracked_gaps = 0U;
    int failed = 0;
    const char* root;

    if (argc != 2) {
        (void)fprintf(stderr, "usage: %s <aivm-repo-root>\n", argv[0]);
        return 1;
    }
    root = argv[1];
    (void)snprintf(matrix_path, sizeof(matrix_path), "%s/tests/spec/coverage.tsv", root);
    matrix = fopen(matrix_path, "rb");
    if (matrix == NULL) {
        (void)fprintf(stderr, "spec coverage: missing matrix: %s\n", matrix_path);
        return 1;
    }

    if (fgets(line, sizeof(line), matrix) == NULL ||
        strcmp(line, "id\tspec\tstatus\tverification\tnotes\n") != 0) {
        (void)fprintf(stderr, "spec coverage: invalid matrix header\n");
        (void)fclose(matrix);
        return 1;
    }

    while (fgets(line, sizeof(line), matrix) != NULL) {
        CoverageRow* row;
        size_t duplicate;
        if (row_count >= MAX_ROWS) {
            (void)fprintf(stderr, "spec coverage: too many rows\n");
            failed = 1;
            break;
        }
        row = &rows[row_count];
        memset(row, 0, sizeof(*row));
        if (!split_tsv(line, row)) {
            (void)fprintf(stderr, "spec coverage: malformed row %zu\n", row_count + 2U);
            failed = 1;
            continue;
        }
        if (row->id[0] == '\0' || row->spec[0] == '\0' || row->status[0] == '\0') {
            (void)fprintf(stderr, "spec coverage: required field missing on row %zu\n", row_count + 2U);
            failed = 1;
        }
        for (duplicate = 0U; duplicate < row_count; duplicate += 1U) {
            if (strcmp(rows[duplicate].id, row->id) == 0) {
                (void)fprintf(stderr, "spec coverage: duplicate id: %s\n", row->id);
                failed = 1;
            }
        }
        if (!path_exists(root, row->spec)) {
            (void)fprintf(stderr, "spec coverage: spec path missing for %s: %s\n", row->id, row->spec);
            failed = 1;
        }
        if (strcmp(row->status, "covered") == 0) {
            covered += 1U;
            if (verify_paths(root, row) != 0) {
                failed = 1;
            }
        } else if (strcmp(row->status, "tracked-gap") == 0) {
            tracked_gaps += 1U;
            (void)fprintf(
                stderr,
                "spec coverage: tracked gap is not implemented: %s (%s)\n",
                row->id,
                row->notes);
            failed = 1;
            if (row->notes[0] == '\0') {
                (void)fprintf(stderr, "spec coverage: tracked gap has no note: %s\n", row->id);
                failed = 1;
            }
            if (verify_paths(root, row) != 0) {
                failed = 1;
            }
        } else {
            (void)fprintf(stderr, "spec coverage: invalid status for %s: %s\n", row->id, row->status);
            failed = 1;
        }
        row_count += 1U;
    }
    (void)fclose(matrix);

    for (index = 0U; index < sizeof(required_specs) / sizeof(required_specs[0]); index += 1U) {
        if (!required_spec_is_present(rows, row_count, required_specs[index])) {
            (void)fprintf(stderr, "spec coverage: required spec not represented: %s\n", required_specs[index]);
            failed = 1;
        }
    }

    if (covered == 0U) {
        (void)fprintf(stderr, "spec coverage: no covered rows\n");
        failed = 1;
    }

    (void)printf(
        "AiVM spec coverage: covered=%zu tracked_gaps=%zu total=%zu\n",
        covered,
        tracked_gaps,
        covered + tracked_gaps);
    return failed;
}
