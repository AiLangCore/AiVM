#include <stdio.h>
#include <stdlib.h>

#include "aivm_parity.h"

static int read_file(const char* path, char* buffer, size_t capacity)
{
    FILE* file;
    size_t read_count;

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    read_count = fread(buffer, 1U, capacity - 1U, file);
    if (ferror(file) != 0) {
        fclose(file);
        return 0;
    }

    buffer[read_count] = '\0';
    fclose(file);
    return 1;
}

int main(int argc, char** argv)
{
    char left[65536];
    char right[65536];
    char left_norm[4096];
    char right_norm[4096];
    size_t diff_index = 0U;
    size_t left_length = 0U;
    size_t right_length = 0U;
    size_t line = 0U;
    size_t col = 0U;

    if (argc != 3) {
        fprintf(stderr, "usage: aivm_parity_cli <left> <right>\n");
        return 2;
    }

    if (!read_file(argv[1], left, sizeof(left))) {
        fprintf(stderr, "failed to read left input\n");
        return 2;
    }

    if (!read_file(argv[2], right, sizeof(right))) {
        fprintf(stderr, "failed to read right input\n");
        return 2;
    }

    if (aivm_parity_equal_normalized(left, right)) {
        printf("PARITY_OK\n");
        return 0;
    }

    (void)aivm_parity_normalize_text(left, left_norm, sizeof(left_norm));
    (void)aivm_parity_normalize_text(right, right_norm, sizeof(right_norm));
    (void)aivm_parity_first_diff(left, right, &diff_index, &left_length, &right_length);
    aivm_parity_line_col_for_index(left_norm, diff_index, &line, &col);

    printf("PARITY_DIFF index=%zu line=%zu col=%zu left_len=%zu right_len=%zu\n",
        diff_index,
        line,
        col,
        left_length,
        right_length);
    return 1;
}
