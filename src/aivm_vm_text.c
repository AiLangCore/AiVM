#include "aivm_vm_internal.h"
#include <string.h>

static int bytes_base64_decode_char(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (int)(ch - 'A');
    }
    if (ch >= 'a' && ch <= 'z') {
        return (int)(ch - 'a') + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return (int)(ch - '0') + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

int aivm_bytes_from_base64(
    const char* input,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    size_t input_len;
    size_t i;
    size_t out_index = 0U;
    if (out_length == NULL) {
        return 0;
    }
    *out_length = 0U;
    if (input == NULL) {
        return 0;
    }
    input_len = strlen(input);
    if (input_len == 0U) {
        return 1;
    }
    if ((input_len % 4U) != 0U) {
        return 0;
    }

    for (i = 0U; i < input_len; i += 4U) {
        int c0 = bytes_base64_decode_char(input[i]);
        int c1 = bytes_base64_decode_char(input[i + 1U]);
        int c2;
        int c3;
        uint32_t chunk;
        int pad = 0;
        if (c0 < 0 || c1 < 0) {
            return 0;
        }
        if (input[i + 2U] == '=') {
            c2 = 0;
            pad += 1;
            if (input[i + 3U] != '=') {
                return 0;
            }
            c3 = 0;
            pad += 1;
        } else {
            c2 = bytes_base64_decode_char(input[i + 2U]);
            if (c2 < 0) {
                return 0;
            }
            if (input[i + 3U] == '=') {
                c3 = 0;
                pad += 1;
            } else {
                c3 = bytes_base64_decode_char(input[i + 3U]);
                if (c3 < 0) {
                    return 0;
                }
            }
        }
        if (pad > 0 && i + 4U != input_len) {
            return 0;
        }
        chunk = ((uint32_t)c0 << 18U) |
                ((uint32_t)c1 << 12U) |
                ((uint32_t)c2 << 6U) |
                (uint32_t)c3;

        if (out_bytes != NULL && out_index < out_capacity) {
            out_bytes[out_index] = (uint8_t)((chunk >> 16U) & 0xffU);
        }
        out_index += 1U;
        if (pad < 2) {
            if (out_bytes != NULL && out_index < out_capacity) {
                out_bytes[out_index] = (uint8_t)((chunk >> 8U) & 0xffU);
            }
            out_index += 1U;
        }
        if (pad == 0) {
            if (out_bytes != NULL && out_index < out_capacity) {
                out_bytes[out_index] = (uint8_t)(chunk & 0xffU);
            }
            out_index += 1U;
        }
    }

    if (out_bytes != NULL && out_index > out_capacity) {
        return 0;
    }
    *out_length = out_index;
    return 1;
}

int aivm_bytes_to_base64(const uint8_t* input, size_t input_len, char* out_text, size_t out_capacity)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0U;
    size_t out_index = 0U;

    if (out_text == NULL || out_capacity == 0U) {
        return 0;
    }
    if (input_len == 0U) {
        out_text[0] = '\0';
        return 1;
    }
    if (input == NULL) {
        return 0;
    }

    while (i < input_len) {
        uint32_t chunk = 0U;
        size_t remain = input_len - i;
        size_t bytes_in_chunk = remain >= 3U ? 3U : remain;
        chunk |= (uint32_t)input[i] << 16U;
        if (bytes_in_chunk > 1U) {
            chunk |= (uint32_t)input[i + 1U] << 8U;
        }
        if (bytes_in_chunk > 2U) {
            chunk |= (uint32_t)input[i + 2U];
        }
        if (out_index + 4U >= out_capacity) {
            return 0;
        }
        out_text[out_index++] = alphabet[(chunk >> 18U) & 0x3fU];
        out_text[out_index++] = alphabet[(chunk >> 12U) & 0x3fU];
        out_text[out_index++] = (bytes_in_chunk > 1U) ? alphabet[(chunk >> 6U) & 0x3fU] : '=';
        out_text[out_index++] = (bytes_in_chunk > 2U) ? alphabet[chunk & 0x3fU] : '=';
        i += bytes_in_chunk;
    }
    out_text[out_index] = '\0';
    return 1;
}

int aivm_bytes_is_valid_utf8_without_nul(const uint8_t* data, size_t len)
{
    size_t i = 0U;
    if (data == NULL) {
        return len == 0U;
    }
    while (i < len) {
        uint8_t b0 = data[i];
        if (b0 == 0U) {
            return 0;
        }
        if (b0 <= 0x7FU) {
            i += 1U;
            continue;
        }
        if (b0 >= 0xC2U && b0 <= 0xDFU) {
            if (i + 1U >= len || (data[i + 1U] & 0xC0U) != 0x80U) {
                return 0;
            }
            i += 2U;
            continue;
        }
        if (b0 == 0xE0U) {
            if (i + 2U >= len || data[i + 1U] < 0xA0U || data[i + 1U] > 0xBFU ||
                (data[i + 2U] & 0xC0U) != 0x80U) {
                return 0;
            }
            i += 3U;
            continue;
        }
        if ((b0 >= 0xE1U && b0 <= 0xECU) || (b0 >= 0xEEU && b0 <= 0xEFU)) {
            if (i + 2U >= len ||
                (data[i + 1U] & 0xC0U) != 0x80U ||
                (data[i + 2U] & 0xC0U) != 0x80U) {
                return 0;
            }
            i += 3U;
            continue;
        }
        if (b0 == 0xEDU) {
            if (i + 2U >= len || data[i + 1U] < 0x80U || data[i + 1U] > 0x9FU ||
                (data[i + 2U] & 0xC0U) != 0x80U) {
                return 0;
            }
            i += 3U;
            continue;
        }
        if (b0 == 0xF0U) {
            if (i + 3U >= len || data[i + 1U] < 0x90U || data[i + 1U] > 0xBFU ||
                (data[i + 2U] & 0xC0U) != 0x80U ||
                (data[i + 3U] & 0xC0U) != 0x80U) {
                return 0;
            }
            i += 4U;
            continue;
        }
        if (b0 >= 0xF1U && b0 <= 0xF3U) {
            if (i + 3U >= len ||
                (data[i + 1U] & 0xC0U) != 0x80U ||
                (data[i + 2U] & 0xC0U) != 0x80U ||
                (data[i + 3U] & 0xC0U) != 0x80U) {
                return 0;
            }
            i += 4U;
            continue;
        }
        if (b0 == 0xF4U) {
            if (i + 3U >= len || data[i + 1U] < 0x80U || data[i + 1U] > 0x8FU ||
                (data[i + 2U] & 0xC0U) != 0x80U ||
                (data[i + 3U] & 0xC0U) != 0x80U) {
                return 0;
            }
            i += 4U;
            continue;
        }
        return 0;
    }
    return 1;
}

int aivm_hex4_to_u32(const char* text, uint32_t* out)
{
    size_t i;
    uint32_t value = 0U;
    if (text == NULL || out == NULL || strlen(text) != 4U) {
        return 0;
    }
    for (i = 0U; i < 4U; i += 1U) {
        char ch = text[i];
        uint32_t nibble;
        if (ch >= '0' && ch <= '9') {
            nibble = (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            nibble = (uint32_t)(10 + ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            nibble = (uint32_t)(10 + ch - 'A');
        } else {
            return 0;
        }
        value = (value << 4U) | nibble;
    }
    *out = value;
    return 1;
}
