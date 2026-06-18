#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAX_RESULTS = 128,
    MAX_NAME = 128
};

typedef struct {
    char name[MAX_NAME];
    double operations_per_second;
} PerfEntry;

static char* read_file(const char* path)
{
    FILE* file;
    long length;
    char* bytes;
    size_t read_count;

    file = fopen(path, "rb");
    if (file == NULL) {
        (void)fprintf(stderr, "failed to open %s\n", path);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L) {
        (void)fclose(file);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    bytes = (char*)calloc((size_t)length + 1U, 1U);
    if (bytes == NULL) {
        (void)fclose(file);
        return NULL;
    }
    read_count = fread(bytes, 1U, (size_t)length, file);
    (void)fclose(file);
    if (read_count != (size_t)length) {
        free(bytes);
        return NULL;
    }
    return bytes;
}

static int parse_results(const char* json, PerfEntry* entries, size_t* out_count)
{
    const char* cursor = json;
    size_t count = 0U;

    if (json == NULL || entries == NULL || out_count == NULL) {
        return 0;
    }

    while ((cursor = strstr(cursor, "\"name\"")) != NULL) {
        const char* name_start = strchr(cursor + 6, '"');
        const char* name_end;
        const char* ops_key;
        const char* ops_start;
        char* ops_end = NULL;
        size_t name_length;

        if (count >= MAX_RESULTS || name_start == NULL) {
            return 0;
        }
        name_start += 1;
        name_end = strchr(name_start, '"');
        if (name_end == NULL) {
            return 0;
        }
        name_length = (size_t)(name_end - name_start);
        if (name_length == 0U || name_length >= MAX_NAME) {
            return 0;
        }
        memcpy(entries[count].name, name_start, name_length);
        entries[count].name[name_length] = '\0';

        ops_key = strstr(name_end, "\"operations_per_second\"");
        if (ops_key == NULL) {
            return 0;
        }
        ops_start = strchr(ops_key, ':');
        if (ops_start == NULL) {
            return 0;
        }
        ops_start += 1;
        entries[count].operations_per_second = strtod(ops_start, &ops_end);
        if (ops_end == ops_start) {
            return 0;
        }

        count += 1U;
        cursor = ops_end;
    }

    *out_count = count;
    return count > 0U;
}

static const PerfEntry* find_entry(const PerfEntry* entries, size_t count, const char* name)
{
    size_t index;
    for (index = 0U; index < count; index += 1U) {
        if (strcmp(entries[index].name, name) == 0) {
            return &entries[index];
        }
    }
    return NULL;
}

static int compare_results(
    const PerfEntry* baseline,
    size_t baseline_count,
    const PerfEntry* current,
    size_t current_count,
    double failure_percent)
{
    size_t index;
    int failed = 0;

    for (index = 0U; index < baseline_count; index += 1U) {
        const PerfEntry* current_entry = find_entry(current, current_count, baseline[index].name);
        double drop_percent;

        if (current_entry == NULL) {
            (void)fprintf(stderr, "missing benchmark result: %s\n", baseline[index].name);
            failed = 1;
            continue;
        }
        if (baseline[index].operations_per_second <= 0.0) {
            continue;
        }
        drop_percent =
            ((baseline[index].operations_per_second - current_entry->operations_per_second) /
             baseline[index].operations_per_second) * 100.0;
        if (drop_percent > failure_percent) {
            (void)fprintf(
                stderr,
                "performance regression: %s baseline=%.3f current=%.3f drop=%.2f%% threshold=%.2f%%\n",
                baseline[index].name,
                baseline[index].operations_per_second,
                current_entry->operations_per_second,
                drop_percent,
                failure_percent);
            failed = 1;
        }
    }

    return failed == 0;
}

int main(int argc, char** argv)
{
    char* baseline_json;
    char* current_json;
    PerfEntry baseline[MAX_RESULTS];
    PerfEntry current[MAX_RESULTS];
    size_t baseline_count = 0U;
    size_t current_count = 0U;
    double failure_percent;
    int ok;

    if (argc != 4) {
        (void)fprintf(stderr, "usage: aivm_perf_compare <baseline.json> <current.json> <failure-percent>\n");
        return 2;
    }

    baseline_json = read_file(argv[1]);
    current_json = read_file(argv[2]);
    failure_percent = strtod(argv[3], NULL);
    if (baseline_json == NULL || current_json == NULL || failure_percent <= 0.0) {
        free(baseline_json);
        free(current_json);
        return 2;
    }

    ok = parse_results(baseline_json, baseline, &baseline_count) &&
         parse_results(current_json, current, &current_count) &&
         compare_results(baseline, baseline_count, current, current_count, failure_percent);

    free(baseline_json);
    free(current_json);
    return ok ? 0 : 1;
}
