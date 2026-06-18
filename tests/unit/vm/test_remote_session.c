#include "remote/aivm_remote_session.h"

#include <string.h>

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmRemoteServerConfig config;
    AivmRemoteServerSession session;
    AivmRemoteHello hello;
    AivmRemoteCall call;
    AivmRemoteCall denied_call;
    AivmRemoteCall unsupported_call;
    uint8_t in_bytes[1024];
    uint8_t out_bytes[1024];
    size_t in_len = 0U;
    size_t out_len = 0U;
    AivmRemoteCodecStatus codec_status;
    AivmRemoteSessionStatus session_status;
    uint32_t out_id = 0U;
    AivmRemoteWelcome welcome;
    AivmRemoteResult result;
    AivmRemoteError error_msg;

    memset(&config, 0, sizeof(config));
    config.proto_version = 1U;
    config.allowed_caps_count = 1U;
    (void)strcpy(config.allowed_caps[0], "cap.remote");
    aivm_remote_server_session_init(&session);

    /* CALL before HELLO must be rejected with deterministic error. */
    memset(&call, 0, sizeof(call));
    (void)strcpy(call.cap, "cap.remote");
    (void)strcpy(call.op, "echoInt");
    call.value = 9;
    codec_status = aivm_remote_encode_call(11U, &call, in_bytes, sizeof(in_bytes), &in_len);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    session_status = aivm_remote_server_process_frame(&config, &session, in_bytes, in_len, out_bytes, sizeof(out_bytes), &out_len);
    if (expect(session_status == AIVM_REMOTE_SESSION_OK) != 0) {
        return 1;
    }
    codec_status = aivm_remote_decode_error(out_bytes, out_len, &out_id, &error_msg);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(out_id == 11U && error_msg.error_code == 401U) != 0) {
        return 1;
    }

    /* HELLO => WELCOME with cap filtering. */
    memset(&hello, 0, sizeof(hello));
    hello.proto_version = 1U;
    (void)strcpy(hello.client_name, "client");
    hello.requested_caps_count = 2U;
    (void)strcpy(hello.requested_caps[0], "cap.remote");
    (void)strcpy(hello.requested_caps[1], "cap.denied");
    codec_status = aivm_remote_encode_hello(1U, &hello, in_bytes, sizeof(in_bytes), &in_len);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    session_status = aivm_remote_server_process_frame(&config, &session, in_bytes, in_len, out_bytes, sizeof(out_bytes), &out_len);
    if (expect(session_status == AIVM_REMOTE_SESSION_OK) != 0) {
        return 1;
    }
    memset(&welcome, 0, sizeof(welcome));
    codec_status = aivm_remote_decode_welcome(out_bytes, out_len, &out_id, &welcome);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(out_id == 1U && welcome.granted_caps_count == 1U) != 0) {
        return 1;
    }
    if (expect(strcmp(welcome.granted_caps[0], "cap.remote") == 0) != 0) {
        return 1;
    }

    /* Granted call => RESULT. */
    codec_status = aivm_remote_encode_call(2U, &call, in_bytes, sizeof(in_bytes), &in_len);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    session_status = aivm_remote_server_process_frame(&config, &session, in_bytes, in_len, out_bytes, sizeof(out_bytes), &out_len);
    if (expect(session_status == AIVM_REMOTE_SESSION_OK) != 0) {
        return 1;
    }
    codec_status = aivm_remote_decode_result(out_bytes, out_len, &out_id, &result);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(out_id == 2U && result.value == 9) != 0) {
        return 1;
    }

    /* Denied capability => ERROR 403. */
    memset(&denied_call, 0, sizeof(denied_call));
    (void)strcpy(denied_call.cap, "cap.denied");
    (void)strcpy(denied_call.op, "echoInt");
    denied_call.value = 3;
    codec_status = aivm_remote_encode_call(3U, &denied_call, in_bytes, sizeof(in_bytes), &in_len);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    session_status = aivm_remote_server_process_frame(&config, &session, in_bytes, in_len, out_bytes, sizeof(out_bytes), &out_len);
    if (expect(session_status == AIVM_REMOTE_SESSION_OK) != 0) {
        return 1;
    }
    codec_status = aivm_remote_decode_error(out_bytes, out_len, &out_id, &error_msg);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(out_id == 3U && error_msg.error_code == 403U) != 0) {
        return 1;
    }

    /* Unknown op under granted cap => ERROR 404. */
    memset(&unsupported_call, 0, sizeof(unsupported_call));
    (void)strcpy(unsupported_call.cap, "cap.remote");
    (void)strcpy(unsupported_call.op, "missing");
    unsupported_call.value = 1;
    codec_status = aivm_remote_encode_call(4U, &unsupported_call, in_bytes, sizeof(in_bytes), &in_len);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    session_status = aivm_remote_server_process_frame(&config, &session, in_bytes, in_len, out_bytes, sizeof(out_bytes), &out_len);
    if (expect(session_status == AIVM_REMOTE_SESSION_OK) != 0) {
        return 1;
    }
    codec_status = aivm_remote_decode_error(out_bytes, out_len, &out_id, &error_msg);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(out_id == 4U && error_msg.error_code == 404U) != 0) {
        return 1;
    }

    return 0;
}
