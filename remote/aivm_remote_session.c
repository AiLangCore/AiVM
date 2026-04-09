#include "remote/aivm_remote_session.h"

#include <stdio.h>
#include <string.h>

static int cap_allowed(const AivmRemoteServerConfig* config, const char* cap)
{
    uint32_t i;
    if (config == NULL || cap == NULL) {
        return 0;
    }
    for (i = 0U; i < config->allowed_caps_count; i += 1U) {
        if (strcmp(config->allowed_caps[i], cap) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cap_granted(const AivmRemoteServerSession* session, const char* cap)
{
    uint32_t i;
    if (session == NULL || cap == NULL) {
        return 0;
    }
    for (i = 0U; i < session->granted_caps_count; i += 1U) {
        if (strcmp(session->granted_caps[i], cap) == 0) {
            return 1;
        }
    }
    return 0;
}

static AivmRemoteSessionStatus encode_error_frame(
    uint32_t id,
    uint32_t error_code,
    const char* message,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    AivmRemoteError error_msg;
    AivmRemoteCodecStatus status;
    memset(&error_msg, 0, sizeof(error_msg));
    error_msg.error_code = error_code;
    if (message != NULL) {
        (void)snprintf(error_msg.message, sizeof(error_msg.message), "%s", message);
    }
    status = aivm_remote_encode_error(id, &error_msg, out_bytes, out_capacity, out_length);
    return (status == AIVM_REMOTE_CODEC_OK) ? AIVM_REMOTE_SESSION_OK : AIVM_REMOTE_SESSION_ERR_CODEC;
}

void aivm_remote_server_session_init(AivmRemoteServerSession* session)
{
    if (session == NULL) {
        return;
    }
    memset(session, 0, sizeof(*session));
}

AivmRemoteSessionStatus aivm_remote_server_process_frame(
    const AivmRemoteServerConfig* config,
    AivmRemoteServerSession* session,
    const uint8_t* input,
    size_t input_length,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    uint8_t type;
    uint32_t id = 0U;
    if (config == NULL || session == NULL || input == NULL || out_bytes == NULL || out_length == NULL) {
        return AIVM_REMOTE_SESSION_ERR_NULL;
    }
    if (input_length < 1U) {
        return AIVM_REMOTE_SESSION_ERR_CODEC;
    }
    if (config->allowed_caps_count > AIVM_REMOTE_MAX_CAPS) {
        return AIVM_REMOTE_SESSION_ERR_OVERFLOW;
    }

    type = input[0];
    if (type == (uint8_t)AIVM_REMOTE_MSG_HELLO) {
        AivmRemoteHello hello;
        AivmRemoteWelcome welcome;
        AivmRemoteCodecStatus codec_status;
        uint32_t i;
        memset(&hello, 0, sizeof(hello));
        codec_status = aivm_remote_decode_hello(input, input_length, &id, &hello);
        if (codec_status != AIVM_REMOTE_CODEC_OK) {
            return AIVM_REMOTE_SESSION_ERR_CODEC;
        }
        memset(&welcome, 0, sizeof(welcome));
        welcome.proto_version = config->proto_version;
        for (i = 0U; i < hello.requested_caps_count; i += 1U) {
            if (cap_allowed(config, hello.requested_caps[i]) &&
                welcome.granted_caps_count < AIVM_REMOTE_MAX_CAPS) {
                (void)snprintf(
                    welcome.granted_caps[welcome.granted_caps_count],
                    sizeof(welcome.granted_caps[welcome.granted_caps_count]),
                    "%s",
                    hello.requested_caps[i]);
                welcome.granted_caps_count += 1U;
            }
        }
        session->handshake_complete = 1;
        session->negotiated_proto_version = welcome.proto_version;
        session->granted_caps_count = welcome.granted_caps_count;
        for (i = 0U; i < welcome.granted_caps_count; i += 1U) {
            (void)snprintf(
                session->granted_caps[i],
                sizeof(session->granted_caps[i]),
                "%s",
                welcome.granted_caps[i]);
        }
        codec_status = aivm_remote_encode_welcome(id, &welcome, out_bytes, out_capacity, out_length);
        return (codec_status == AIVM_REMOTE_CODEC_OK) ? AIVM_REMOTE_SESSION_OK : AIVM_REMOTE_SESSION_ERR_CODEC;
    }

    if (type == (uint8_t)AIVM_REMOTE_MSG_CALL) {
        AivmRemoteCall call;
        AivmRemoteResult result;
        AivmRemoteCodecStatus codec_status;
        memset(&call, 0, sizeof(call));
        codec_status = aivm_remote_decode_call(input, input_length, &id, &call);
        if (codec_status != AIVM_REMOTE_CODEC_OK) {
            return AIVM_REMOTE_SESSION_ERR_CODEC;
        }
        if (!session->handshake_complete) {
            return encode_error_frame(id, 401U, "remote session handshake is required before CALL.", out_bytes, out_capacity, out_length);
        }
        if (!cap_granted(session, call.cap)) {
            return encode_error_frame(id, 403U, "remote capability is not granted for this session.", out_bytes, out_capacity, out_length);
        }
        if (strcmp(call.cap, "cap.remote") == 0 && strcmp(call.op, "echoInt") == 0) {
            result.value = call.value;
            codec_status = aivm_remote_encode_result(id, &result, out_bytes, out_capacity, out_length);
            return (codec_status == AIVM_REMOTE_CODEC_OK) ? AIVM_REMOTE_SESSION_OK : AIVM_REMOTE_SESSION_ERR_CODEC;
        }
        return encode_error_frame(id, 404U, "remote operation is not available.", out_bytes, out_capacity, out_length);
    }

    return encode_error_frame(0U, 400U, "remote frame type is not supported.", out_bytes, out_capacity, out_length);
}
