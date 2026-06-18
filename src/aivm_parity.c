#include "aivm_parity.h"

#include <string.h>

size_t aivm_parity_normalize_text(const char* input, char* output, size_t output_capacity)
{
    size_t in_index;
    size_t out_index;

    if (input == NULL || output == NULL || output_capacity == 0U) {
        return 0U;
    }

    in_index = 0U;
    out_index = 0U;

    while (input[in_index] != '\0') {
        if (input[in_index] == '\r' && input[in_index + 1U] == '\n') {
            if (out_index + 1U >= output_capacity) {
                break;
            }
            output[out_index] = '\n';
            out_index += 1U;
            in_index += 2U;
            continue;
        }

        if (out_index + 1U >= output_capacity) {
            break;
        }
        output[out_index] = input[in_index];
        out_index += 1U;
        in_index += 1U;
    }

    while (out_index > 0U && output[out_index - 1U] == '\n') {
        out_index -= 1U;
    }

    output[out_index] = '\0';
    return out_index;
}

int aivm_parity_equal_normalized(const char* left, const char* right)
{
    char left_buffer[4096];
    char right_buffer[4096];

    if (left == NULL || right == NULL) {
        return 0;
    }

    (void)aivm_parity_normalize_text(left, left_buffer, sizeof(left_buffer));
    (void)aivm_parity_normalize_text(right, right_buffer, sizeof(right_buffer));

    return strcmp(left_buffer, right_buffer) == 0 ? 1 : 0;
}

int aivm_parity_first_diff(
    const char* left,
    const char* right,
    size_t* out_index,
    size_t* out_left_length,
    size_t* out_right_length)
{
    char left_buffer[4096];
    char right_buffer[4096];
    size_t index = 0U;
    size_t left_length;
    size_t right_length;

    if (left == NULL || right == NULL) {
        return 0;
    }

    left_length = aivm_parity_normalize_text(left, left_buffer, sizeof(left_buffer));
    right_length = aivm_parity_normalize_text(right, right_buffer, sizeof(right_buffer));

    while (index < left_length && index < right_length) {
        if (left_buffer[index] != right_buffer[index]) {
            if (out_index != NULL) {
                *out_index = index;
            }
            if (out_left_length != NULL) {
                *out_left_length = left_length;
            }
            if (out_right_length != NULL) {
                *out_right_length = right_length;
            }
            return 1;
        }
        index += 1U;
    }

    if (left_length != right_length) {
        if (out_index != NULL) {
            *out_index = index;
        }
        if (out_left_length != NULL) {
            *out_left_length = left_length;
        }
        if (out_right_length != NULL) {
            *out_right_length = right_length;
        }
        return 1;
    }

    return 0;
}

void aivm_parity_line_col_for_index(
    const char* text,
    size_t index,
    size_t* out_line,
    size_t* out_col)
{
    size_t line = 1U;
    size_t col = 1U;
    size_t i = 0U;

    if (text == NULL) {
        if (out_line != NULL) {
            *out_line = 0U;
        }
        if (out_col != NULL) {
            *out_col = 0U;
        }
        return;
    }

    while (text[i] != '\0' && i < index) {
        if (text[i] == '\n') {
            line += 1U;
            col = 1U;
        } else {
            col += 1U;
        }
        i += 1U;
    }

    if (out_line != NULL) {
        *out_line = line;
    }
    if (out_col != NULL) {
        *out_col = col;
    }
}
