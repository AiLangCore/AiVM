#ifndef AIRUN_STORAGE_PATH_H
#define AIRUN_STORAGE_PATH_H

#include <stddef.h>

static int native_storage_sanitize_segment(
    const char* input,
    char* out,
    size_t out_len)
{
    size_t input_index;
    size_t out_index = 0U;
    if (input == NULL || out == NULL || out_len < 2U || input[0] == '\0') {
        return 0;
    }
    for (input_index = 0U; input[input_index] != '\0'; input_index += 1U) {
        unsigned char ch = (unsigned char)input[input_index];
        if (out_index + 1U >= out_len) {
            return 0;
        }
        if ((ch >= (unsigned char)'a' && ch <= (unsigned char)'z') ||
            (ch >= (unsigned char)'A' && ch <= (unsigned char)'Z') ||
            (ch >= (unsigned char)'0' && ch <= (unsigned char)'9') ||
            ch == (unsigned char)'.' ||
            ch == (unsigned char)'_' ||
            ch == (unsigned char)'-') {
            out[out_index] = (char)ch;
        } else {
            out[out_index] = '_';
        }
        out_index += 1U;
    }
    out[out_index] = '\0';
    if ((out_index == 1U && out[0] == '.') ||
        (out_index == 2U && out[0] == '.' && out[1] == '.')) {
        return 0;
    }
    return 1;
}

#endif
