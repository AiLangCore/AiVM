#ifndef AIVM_PARITY_H
#define AIVM_PARITY_H

#include <stddef.h>

size_t aivm_parity_normalize_text(const char* input, char* output, size_t output_capacity);
int aivm_parity_equal_normalized(const char* left, const char* right);
int aivm_parity_first_diff(
    const char* left,
    const char* right,
    size_t* out_index,
    size_t* out_left_length,
    size_t* out_right_length);
void aivm_parity_line_col_for_index(
    const char* text,
    size_t index,
    size_t* out_line,
    size_t* out_col);

#endif
