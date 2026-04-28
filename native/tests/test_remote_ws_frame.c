#include "remote/aivm_remote_ws_frame.h"

#include <string.h>

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    /* Masked client binary frame: payload "abc", mask 01 02 03 04. */
    const uint8_t client_frame[] = {
        0x82U, 0x83U,
        0x01U, 0x02U, 0x03U, 0x04U,
        (uint8_t)('a' ^ 0x01U), (uint8_t)('b' ^ 0x02U), (uint8_t)('c' ^ 0x03U)
    };
    AivmWsFrame decoded;
    size_t consumed = 0U;
    uint8_t encoded[64];
    size_t encoded_len = 0U;

    if (expect(aivm_ws_decode_client_frame(client_frame, sizeof(client_frame), &consumed, &decoded) == 1) != 0) {
        return 1;
    }
    if (expect(consumed == sizeof(client_frame)) != 0) {
        return 1;
    }
    if (expect(decoded.fin == 1 && decoded.opcode == 0x2U && decoded.payload_length == 3U) != 0) {
        return 1;
    }
    if (expect(decoded.payload[0] == 'a' && decoded.payload[1] == 'b' && decoded.payload[2] == 'c') != 0) {
        return 1;
    }

    if (expect(aivm_ws_encode_server_binary((const uint8_t*)"xyz", 3U, encoded, sizeof(encoded), &encoded_len) == 1) != 0) {
        return 1;
    }
    if (expect(encoded_len == 5U) != 0) {
        return 1;
    }
    if (expect(encoded[0] == 0x82U && encoded[1] == 0x03U) != 0) {
        return 1;
    }
    if (expect(memcmp(&encoded[2], "xyz", 3U) == 0) != 0) {
        return 1;
    }

    /* Reject unmasked client frame. */
    {
        const uint8_t unmasked[] = { 0x82U, 0x03U, 'a', 'b', 'c' };
        if (expect(aivm_ws_decode_client_frame(unmasked, sizeof(unmasked), &consumed, &decoded) == 0) != 0) {
            return 1;
        }
    }

    return 0;
}
