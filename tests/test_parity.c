#include <string.h>

#include "aivm_parity.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    char out[64];
    size_t length;
    size_t diff_index = 0U;
    size_t left_len = 0U;
    size_t right_len = 0U;
    size_t line = 0U;
    size_t col = 0U;

    length = aivm_parity_normalize_text("line1\r\nline2\r\n", out, sizeof(out));
    if (expect(length == 11U) != 0) {
        return 1;
    }
    if (expect(strcmp(out, "line1\nline2") == 0) != 0) {
        return 1;
    }

    if (expect(aivm_parity_equal_normalized("a\r\nb\n", "a\nb") == 1) != 0) {
        return 1;
    }

    if (expect(aivm_parity_equal_normalized("a\nb", "a\nb\n") == 1) != 0) {
        return 1;
    }

    if (expect(aivm_parity_equal_normalized("left", "right") == 0) != 0) {
        return 1;
    }
    if (expect(aivm_parity_first_diff("ab\ncd", "ab\nxd", &diff_index, &left_len, &right_len) == 1) != 0) {
        return 1;
    }
    if (expect(diff_index == 3U) != 0) {
        return 1;
    }
    if (expect(left_len == 5U && right_len == 5U) != 0) {
        return 1;
    }
    aivm_parity_line_col_for_index("ab\ncd", diff_index, &line, &col);
    if (expect(line == 2U && col == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_parity_first_diff("same", "same", &diff_index, &left_len, &right_len) == 0) != 0) {
        return 1;
    }

    return 0;
}
