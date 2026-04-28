#include "remote/aivm_remote_channel.h"

#include <string.h>

enum {
    AIVM_REMOTE_HEADER_SIZE = 9
};

typedef struct {
    uint8_t type;
    uint32_t id;
    uint32_t payload_length;
} AivmRemoteFrameHeader;

static void write_u16_le(uint8_t* out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffU);
    out[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void write_u32_le(uint8_t* out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffU);
    out[1] = (uint8_t)((value >> 8U) & 0xffU);
    out[2] = (uint8_t)((value >> 16U) & 0xffU);
    out[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static void write_i64_le(uint8_t* out, int64_t value)
{
    uint64_t u = (uint64_t)value;
    size_t i;
    for (i = 0U; i < 8U; i += 1U) {
        out[i] = (uint8_t)((u >> (8U * i)) & 0xffU);
    }
}

static uint16_t read_u16_le(const uint8_t* in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t* in)
{
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8U) |
           ((uint32_t)in[2] << 16U) |
           ((uint32_t)in[3] << 24U);
}

static int64_t read_i64_le(const uint8_t* in)
{
    uint64_t u = 0U;
    size_t i;
    for (i = 0U; i < 8U; i += 1U) {
        u |= ((uint64_t)in[i]) << (8U * i);
    }
    return (int64_t)u;
}

static AivmRemoteCodecStatus write_string(
    uint8_t* out,
    size_t out_capacity,
    size_t* io_index,
    const char* text,
    size_t max_text)
{
    size_t len;
    size_t index;
    if (out == NULL || io_index == NULL || text == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    len = strlen(text);
    if (len > max_text || len > 65535U) {
        return AIVM_REMOTE_CODEC_ERR_OVERFLOW;
    }
    index = *io_index;
    if (index + 2U + len > out_capacity) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    write_u16_le(&out[index], (uint16_t)len);
    index += 2U;
    if (len > 0U) {
        memcpy(&out[index], text, len);
        index += len;
    }
    *io_index = index;
    return AIVM_REMOTE_CODEC_OK;
}

static AivmRemoteCodecStatus read_string(
    const uint8_t* in,
    size_t in_length,
    size_t* io_index,
    char* out_text,
    size_t out_capacity,
    size_t max_text)
{
    uint16_t len;
    size_t index;
    if (in == NULL || io_index == NULL || out_text == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    if (out_capacity == 0U) {
        return AIVM_REMOTE_CODEC_ERR_OVERFLOW;
    }
    index = *io_index;
    if (index + 2U > in_length) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    len = read_u16_le(&in[index]);
    index += 2U;
    if ((size_t)len > max_text || (size_t)len + 1U > out_capacity) {
        return AIVM_REMOTE_CODEC_ERR_OVERFLOW;
    }
    if (index + (size_t)len > in_length) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    if (len > 0U) {
        memcpy(out_text, &in[index], len);
        index += (size_t)len;
    }
    out_text[len] = '\0';
    *io_index = index;
    return AIVM_REMOTE_CODEC_OK;
}

static AivmRemoteCodecStatus write_header(
    uint8_t type,
    uint32_t id,
    uint32_t payload_length,
    uint8_t* out_bytes,
    size_t out_capacity)
{
    if (out_bytes == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    if (out_capacity < AIVM_REMOTE_HEADER_SIZE) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    out_bytes[0] = type;
    write_u32_le(&out_bytes[1], id);
    write_u32_le(&out_bytes[5], payload_length);
    return AIVM_REMOTE_CODEC_OK;
}

static AivmRemoteCodecStatus read_header(
    const uint8_t* bytes,
    size_t length,
    AivmRemoteFrameHeader* out_header)
{
    if (bytes == NULL || out_header == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    if (length < AIVM_REMOTE_HEADER_SIZE) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    out_header->type = bytes[0];
    out_header->id = read_u32_le(&bytes[1]);
    out_header->payload_length = read_u32_le(&bytes[5]);
    if ((uint64_t)out_header->payload_length + (uint64_t)AIVM_REMOTE_HEADER_SIZE != (uint64_t)length) {
        return AIVM_REMOTE_CODEC_ERR_FORMAT;
    }
    return AIVM_REMOTE_CODEC_OK;
}

static AivmRemoteCodecStatus check_type(
    const AivmRemoteFrameHeader* header,
    AivmRemoteMessageType expected)
{
    if (header == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    if (header->type != (uint8_t)expected) {
        return AIVM_REMOTE_CODEC_ERR_TYPE;
    }
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_encode_hello(
    uint32_t id,
    const AivmRemoteHello* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    size_t i;
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (message == NULL || out_length == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    if (message->requested_caps_count > AIVM_REMOTE_MAX_CAPS) {
        return AIVM_REMOTE_CODEC_ERR_OVERFLOW;
    }
    if ((status = write_string(out_bytes, out_capacity, &index, message->client_name, AIVM_REMOTE_MAX_TEXT)) != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (index + 2U + 4U > out_capacity) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    write_u16_le(&out_bytes[index], message->proto_version);
    index += 2U;
    write_u32_le(&out_bytes[index], message->requested_caps_count);
    index += 4U;
    for (i = 0U; i < message->requested_caps_count; i += 1U) {
        status = write_string(out_bytes, out_capacity, &index, message->requested_caps[i], AIVM_REMOTE_MAX_TEXT);
        if (status != AIVM_REMOTE_CODEC_OK) {
            return status;
        }
    }
    status = write_header((uint8_t)AIVM_REMOTE_MSG_HELLO, id, (uint32_t)(index - AIVM_REMOTE_HEADER_SIZE), out_bytes, out_capacity);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    *out_length = index;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_decode_hello(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteHello* out_message)
{
    AivmRemoteFrameHeader header;
    size_t i;
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (out_id == NULL || out_message == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    status = read_header(bytes, length, &header);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = check_type(&header, AIVM_REMOTE_MSG_HELLO);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = read_string(bytes, length, &index, out_message->client_name, sizeof(out_message->client_name), AIVM_REMOTE_MAX_TEXT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (index + 2U + 4U > length) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    out_message->proto_version = read_u16_le(&bytes[index]);
    index += 2U;
    out_message->requested_caps_count = read_u32_le(&bytes[index]);
    index += 4U;
    if (out_message->requested_caps_count > AIVM_REMOTE_MAX_CAPS) {
        return AIVM_REMOTE_CODEC_ERR_OVERFLOW;
    }
    for (i = 0U; i < out_message->requested_caps_count; i += 1U) {
        status = read_string(bytes, length, &index, out_message->requested_caps[i], sizeof(out_message->requested_caps[i]), AIVM_REMOTE_MAX_TEXT);
        if (status != AIVM_REMOTE_CODEC_OK) {
            return status;
        }
    }
    if (index != length) {
        return AIVM_REMOTE_CODEC_ERR_FORMAT;
    }
    *out_id = header.id;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_encode_welcome(
    uint32_t id,
    const AivmRemoteWelcome* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    size_t i;
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (message == NULL || out_length == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    if (message->granted_caps_count > AIVM_REMOTE_MAX_CAPS) {
        return AIVM_REMOTE_CODEC_ERR_OVERFLOW;
    }
    if (index + 2U + 4U > out_capacity) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    write_u16_le(&out_bytes[index], message->proto_version);
    index += 2U;
    write_u32_le(&out_bytes[index], message->granted_caps_count);
    index += 4U;
    for (i = 0U; i < message->granted_caps_count; i += 1U) {
        status = write_string(out_bytes, out_capacity, &index, message->granted_caps[i], AIVM_REMOTE_MAX_TEXT);
        if (status != AIVM_REMOTE_CODEC_OK) {
            return status;
        }
    }
    status = write_header((uint8_t)AIVM_REMOTE_MSG_WELCOME, id, (uint32_t)(index - AIVM_REMOTE_HEADER_SIZE), out_bytes, out_capacity);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    *out_length = index;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_decode_welcome(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteWelcome* out_message)
{
    AivmRemoteFrameHeader header;
    size_t i;
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (out_id == NULL || out_message == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    status = read_header(bytes, length, &header);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = check_type(&header, AIVM_REMOTE_MSG_WELCOME);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (index + 2U + 4U > length) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    out_message->proto_version = read_u16_le(&bytes[index]);
    index += 2U;
    out_message->granted_caps_count = read_u32_le(&bytes[index]);
    index += 4U;
    if (out_message->granted_caps_count > AIVM_REMOTE_MAX_CAPS) {
        return AIVM_REMOTE_CODEC_ERR_OVERFLOW;
    }
    for (i = 0U; i < out_message->granted_caps_count; i += 1U) {
        status = read_string(bytes, length, &index, out_message->granted_caps[i], sizeof(out_message->granted_caps[i]), AIVM_REMOTE_MAX_TEXT);
        if (status != AIVM_REMOTE_CODEC_OK) {
            return status;
        }
    }
    if (index != length) {
        return AIVM_REMOTE_CODEC_ERR_FORMAT;
    }
    *out_id = header.id;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_encode_deny(
    uint32_t id,
    const AivmRemoteDeny* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (message == NULL || out_length == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    if (index + 4U > out_capacity) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    write_u32_le(&out_bytes[index], message->error_code);
    index += 4U;
    status = write_string(out_bytes, out_capacity, &index, message->message, AIVM_REMOTE_MAX_ERROR_TEXT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = write_header((uint8_t)AIVM_REMOTE_MSG_DENY, id, (uint32_t)(index - AIVM_REMOTE_HEADER_SIZE), out_bytes, out_capacity);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    *out_length = index;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_decode_deny(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteDeny* out_message)
{
    AivmRemoteFrameHeader header;
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (out_id == NULL || out_message == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    status = read_header(bytes, length, &header);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = check_type(&header, AIVM_REMOTE_MSG_DENY);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (index + 4U > length) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    out_message->error_code = read_u32_le(&bytes[index]);
    index += 4U;
    status = read_string(bytes, length, &index, out_message->message, sizeof(out_message->message), AIVM_REMOTE_MAX_ERROR_TEXT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (index != length) {
        return AIVM_REMOTE_CODEC_ERR_FORMAT;
    }
    *out_id = header.id;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_encode_call(
    uint32_t id,
    const AivmRemoteCall* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (message == NULL || out_length == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    status = write_string(out_bytes, out_capacity, &index, message->cap, AIVM_REMOTE_MAX_TEXT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = write_string(out_bytes, out_capacity, &index, message->op, AIVM_REMOTE_MAX_TEXT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (index + 8U > out_capacity) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    write_i64_le(&out_bytes[index], message->value);
    index += 8U;
    status = write_header((uint8_t)AIVM_REMOTE_MSG_CALL, id, (uint32_t)(index - AIVM_REMOTE_HEADER_SIZE), out_bytes, out_capacity);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    *out_length = index;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_decode_call(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteCall* out_message)
{
    AivmRemoteFrameHeader header;
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (out_id == NULL || out_message == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    status = read_header(bytes, length, &header);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = check_type(&header, AIVM_REMOTE_MSG_CALL);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = read_string(bytes, length, &index, out_message->cap, sizeof(out_message->cap), AIVM_REMOTE_MAX_TEXT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = read_string(bytes, length, &index, out_message->op, sizeof(out_message->op), AIVM_REMOTE_MAX_TEXT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (index + 8U != length) {
        return AIVM_REMOTE_CODEC_ERR_FORMAT;
    }
    out_message->value = read_i64_le(&bytes[index]);
    *out_id = header.id;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_encode_result(
    uint32_t id,
    const AivmRemoteResult* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    AivmRemoteCodecStatus status;
    if (message == NULL || out_bytes == NULL || out_length == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    if (out_capacity < AIVM_REMOTE_HEADER_SIZE + 8U) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    write_i64_le(&out_bytes[AIVM_REMOTE_HEADER_SIZE], message->value);
    status = write_header((uint8_t)AIVM_REMOTE_MSG_RESULT, id, 8U, out_bytes, out_capacity);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    *out_length = AIVM_REMOTE_HEADER_SIZE + 8U;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_decode_result(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteResult* out_message)
{
    AivmRemoteFrameHeader header;
    AivmRemoteCodecStatus status;
    if (out_id == NULL || out_message == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    status = read_header(bytes, length, &header);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = check_type(&header, AIVM_REMOTE_MSG_RESULT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (length != AIVM_REMOTE_HEADER_SIZE + 8U) {
        return AIVM_REMOTE_CODEC_ERR_FORMAT;
    }
    out_message->value = read_i64_le(&bytes[AIVM_REMOTE_HEADER_SIZE]);
    *out_id = header.id;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_encode_error(
    uint32_t id,
    const AivmRemoteError* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (message == NULL || out_length == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    if (index + 4U > out_capacity) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    write_u32_le(&out_bytes[index], message->error_code);
    index += 4U;
    status = write_string(out_bytes, out_capacity, &index, message->message, AIVM_REMOTE_MAX_ERROR_TEXT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = write_header((uint8_t)AIVM_REMOTE_MSG_ERROR, id, (uint32_t)(index - AIVM_REMOTE_HEADER_SIZE), out_bytes, out_capacity);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    *out_length = index;
    return AIVM_REMOTE_CODEC_OK;
}

AivmRemoteCodecStatus aivm_remote_decode_error(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteError* out_message)
{
    AivmRemoteFrameHeader header;
    size_t index = AIVM_REMOTE_HEADER_SIZE;
    AivmRemoteCodecStatus status;
    if (out_id == NULL || out_message == NULL) {
        return AIVM_REMOTE_CODEC_ERR_NULL;
    }
    status = read_header(bytes, length, &header);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    status = check_type(&header, AIVM_REMOTE_MSG_ERROR);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (index + 4U > length) {
        return AIVM_REMOTE_CODEC_ERR_SHORT;
    }
    out_message->error_code = read_u32_le(&bytes[index]);
    index += 4U;
    status = read_string(bytes, length, &index, out_message->message, sizeof(out_message->message), AIVM_REMOTE_MAX_ERROR_TEXT);
    if (status != AIVM_REMOTE_CODEC_OK) {
        return status;
    }
    if (index != length) {
        return AIVM_REMOTE_CODEC_ERR_FORMAT;
    }
    *out_id = header.id;
    return AIVM_REMOTE_CODEC_OK;
}
