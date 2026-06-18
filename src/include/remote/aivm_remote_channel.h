#ifndef AIVM_REMOTE_CHANNEL_H
#define AIVM_REMOTE_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

enum {
    AIVM_REMOTE_MAX_CAPS = 16,
    AIVM_REMOTE_MAX_TEXT = 64,
    AIVM_REMOTE_MAX_ERROR_TEXT = 128
};

typedef enum {
    AIVM_REMOTE_MSG_HELLO = 0x01,
    AIVM_REMOTE_MSG_WELCOME = 0x02,
    AIVM_REMOTE_MSG_DENY = 0x03,
    AIVM_REMOTE_MSG_CALL = 0x10,
    AIVM_REMOTE_MSG_RESULT = 0x11,
    AIVM_REMOTE_MSG_ERROR = 0x12
} AivmRemoteMessageType;

typedef enum {
    AIVM_REMOTE_CODEC_OK = 0,
    AIVM_REMOTE_CODEC_ERR_NULL = 1,
    AIVM_REMOTE_CODEC_ERR_SHORT = 2,
    AIVM_REMOTE_CODEC_ERR_OVERFLOW = 3,
    AIVM_REMOTE_CODEC_ERR_TYPE = 4,
    AIVM_REMOTE_CODEC_ERR_FORMAT = 5
} AivmRemoteCodecStatus;

typedef struct {
    uint16_t proto_version;
    char client_name[AIVM_REMOTE_MAX_TEXT + 1];
    uint32_t requested_caps_count;
    char requested_caps[AIVM_REMOTE_MAX_CAPS][AIVM_REMOTE_MAX_TEXT + 1];
} AivmRemoteHello;

typedef struct {
    uint16_t proto_version;
    uint32_t granted_caps_count;
    char granted_caps[AIVM_REMOTE_MAX_CAPS][AIVM_REMOTE_MAX_TEXT + 1];
} AivmRemoteWelcome;

typedef struct {
    uint32_t error_code;
    char message[AIVM_REMOTE_MAX_ERROR_TEXT + 1];
} AivmRemoteDeny;

typedef struct {
    char cap[AIVM_REMOTE_MAX_TEXT + 1];
    char op[AIVM_REMOTE_MAX_TEXT + 1];
    int64_t value;
} AivmRemoteCall;

typedef struct {
    int64_t value;
} AivmRemoteResult;

typedef struct {
    uint32_t error_code;
    char message[AIVM_REMOTE_MAX_ERROR_TEXT + 1];
} AivmRemoteError;

AivmRemoteCodecStatus aivm_remote_encode_hello(
    uint32_t id,
    const AivmRemoteHello* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length
);
AivmRemoteCodecStatus aivm_remote_decode_hello(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteHello* out_message
);

AivmRemoteCodecStatus aivm_remote_encode_welcome(
    uint32_t id,
    const AivmRemoteWelcome* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length
);
AivmRemoteCodecStatus aivm_remote_decode_welcome(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteWelcome* out_message
);

AivmRemoteCodecStatus aivm_remote_encode_deny(
    uint32_t id,
    const AivmRemoteDeny* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length
);
AivmRemoteCodecStatus aivm_remote_decode_deny(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteDeny* out_message
);

AivmRemoteCodecStatus aivm_remote_encode_call(
    uint32_t id,
    const AivmRemoteCall* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length
);
AivmRemoteCodecStatus aivm_remote_decode_call(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteCall* out_message
);

AivmRemoteCodecStatus aivm_remote_encode_result(
    uint32_t id,
    const AivmRemoteResult* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length
);
AivmRemoteCodecStatus aivm_remote_decode_result(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteResult* out_message
);

AivmRemoteCodecStatus aivm_remote_encode_error(
    uint32_t id,
    const AivmRemoteError* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length
);
AivmRemoteCodecStatus aivm_remote_decode_error(
    const uint8_t* bytes,
    size_t length,
    uint32_t* out_id,
    AivmRemoteError* out_message
);

#endif
