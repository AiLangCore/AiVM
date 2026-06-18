#ifndef AIVM_REMOTE_SESSION_H
#define AIVM_REMOTE_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "remote/aivm_remote_channel.h"

typedef enum {
    AIVM_REMOTE_SESSION_OK = 0,
    AIVM_REMOTE_SESSION_ERR_NULL = 1,
    AIVM_REMOTE_SESSION_ERR_CODEC = 2,
    AIVM_REMOTE_SESSION_ERR_STATE = 3,
    AIVM_REMOTE_SESSION_ERR_OVERFLOW = 4
} AivmRemoteSessionStatus;

typedef struct {
    uint16_t proto_version;
    uint32_t allowed_caps_count;
    char allowed_caps[AIVM_REMOTE_MAX_CAPS][AIVM_REMOTE_MAX_TEXT + 1];
} AivmRemoteServerConfig;

typedef struct {
    int handshake_complete;
    uint16_t negotiated_proto_version;
    uint32_t granted_caps_count;
    char granted_caps[AIVM_REMOTE_MAX_CAPS][AIVM_REMOTE_MAX_TEXT + 1];
} AivmRemoteServerSession;

void aivm_remote_server_session_init(AivmRemoteServerSession* session);

AivmRemoteSessionStatus aivm_remote_server_process_frame(
    const AivmRemoteServerConfig* config,
    AivmRemoteServerSession* session,
    const uint8_t* input,
    size_t input_length,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length
);

#endif
