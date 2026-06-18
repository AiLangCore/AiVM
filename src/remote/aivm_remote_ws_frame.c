#include "remote/aivm_remote_ws_frame.h"

#include <string.h>

static uint16_t read_u16_be(const uint8_t* bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static uint64_t read_u64_be(const uint8_t* bytes)
{
    uint64_t value = 0U;
    size_t i;
    for (i = 0U; i < 8U; i += 1U) {
        value = (value << 8U) | (uint64_t)bytes[i];
    }
    return value;
}

static int write_u16_be(uint8_t* bytes, uint16_t value)
{
    if (bytes == NULL) {
        return 0;
    }
    bytes[0] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[1] = (uint8_t)(value & 0xffU);
    return 1;
}

int aivm_ws_decode_client_frame(
    const uint8_t* bytes,
    size_t length,
    size_t* out_consumed,
    AivmWsFrame* out_frame)
{
    uint8_t b0;
    uint8_t b1;
    int masked;
    uint64_t payload_length;
    size_t index = 0U;
    uint8_t mask[4];
    size_t i;

    if (bytes == NULL || out_consumed == NULL || out_frame == NULL) {
        return 0;
    }
    if (length < 2U) {
        return 0;
    }

    b0 = bytes[index++];
    b1 = bytes[index++];
    masked = (b1 & 0x80U) != 0U;
    payload_length = (uint64_t)(b1 & 0x7fU);
    if (!masked) {
        return 0;
    }
    if (payload_length == 126U) {
        if (index + 2U > length) {
            return 0;
        }
        payload_length = (uint64_t)read_u16_be(&bytes[index]);
        index += 2U;
    } else if (payload_length == 127U) {
        if (index + 8U > length) {
            return 0;
        }
        payload_length = read_u64_be(&bytes[index]);
        index += 8U;
    }
    if (payload_length > AIVM_WS_MAX_PAYLOAD) {
        return 0;
    }
    if (index + 4U > length) {
        return 0;
    }
    memcpy(mask, &bytes[index], 4U);
    index += 4U;
    if (index + (size_t)payload_length > length) {
        return 0;
    }

    memset(out_frame, 0, sizeof(*out_frame));
    out_frame->fin = ((b0 & 0x80U) != 0U) ? 1 : 0;
    out_frame->opcode = (uint8_t)(b0 & 0x0fU);
    out_frame->payload_length = (size_t)payload_length;
    for (i = 0U; i < (size_t)payload_length; i += 1U) {
        out_frame->payload[i] = (uint8_t)(bytes[index + i] ^ mask[i % 4U]);
    }

    *out_consumed = index + (size_t)payload_length;
    return 1;
}

int aivm_ws_encode_server_binary(
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    return aivm_ws_encode_server_control(0x2U, payload, payload_length, out_bytes, out_capacity, out_length);
}

int aivm_ws_encode_server_control(
    uint8_t opcode,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    size_t index = 0U;
    if (out_bytes == NULL || out_length == NULL) {
        return 0;
    }
    if (payload == NULL && payload_length > 0U) {
        return 0;
    }
    if (payload_length > AIVM_WS_MAX_PAYLOAD) {
        return 0;
    }
    if (out_capacity < 2U) {
        return 0;
    }

    out_bytes[index++] = (uint8_t)(0x80U | (opcode & 0x0fU));
    if (payload_length <= 125U) {
        out_bytes[index++] = (uint8_t)payload_length;
    } else {
        if (out_capacity < index + 3U) {
            return 0;
        }
        out_bytes[index++] = 126U;
        if (!write_u16_be(&out_bytes[index], (uint16_t)payload_length)) {
            return 0;
        }
        index += 2U;
    }

    if (out_capacity < index + payload_length) {
        return 0;
    }
    if (payload_length > 0U) {
        memcpy(&out_bytes[index], payload, payload_length);
        index += payload_length;
    }
    *out_length = index;
    return 1;
}
