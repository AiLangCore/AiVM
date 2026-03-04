#include "remote/aivm_remote_channel.h"

#include <string.h>

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    uint8_t buffer[1024];
    size_t length = 0U;
    uint32_t id = 0U;
    AivmRemoteHello hello;
    AivmRemoteHello hello_out;
    AivmRemoteWelcome welcome;
    AivmRemoteWelcome welcome_out;
    AivmRemoteCall call;
    AivmRemoteCall call_out;
    AivmRemoteResult result;
    AivmRemoteResult result_out;
    AivmRemoteError error_msg;
    AivmRemoteError error_out;
    AivmRemoteDeny deny;
    AivmRemoteDeny deny_out;
    AivmRemoteCodecStatus status;

    memset(&hello, 0, sizeof(hello));
    hello.proto_version = 1U;
    (void)strcpy(hello.client_name, "wasm-client");
    hello.requested_caps_count = 2U;
    (void)strcpy(hello.requested_caps[0], "cap.remote");
    (void)strcpy(hello.requested_caps[1], "cap.kv.get");
    status = aivm_remote_encode_hello(42U, &hello, buffer, sizeof(buffer), &length);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    memset(&hello_out, 0, sizeof(hello_out));
    status = aivm_remote_decode_hello(buffer, length, &id, &hello_out);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(id == 42U && hello_out.proto_version == 1U && hello_out.requested_caps_count == 2U) != 0) {
        return 1;
    }
    if (expect(strcmp(hello_out.client_name, "wasm-client") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(hello_out.requested_caps[0], "cap.remote") == 0) != 0) {
        return 1;
    }

    memset(&welcome, 0, sizeof(welcome));
    welcome.proto_version = 1U;
    welcome.granted_caps_count = 1U;
    (void)strcpy(welcome.granted_caps[0], "cap.remote");
    status = aivm_remote_encode_welcome(42U, &welcome, buffer, sizeof(buffer), &length);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    memset(&welcome_out, 0, sizeof(welcome_out));
    status = aivm_remote_decode_welcome(buffer, length, &id, &welcome_out);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(id == 42U && welcome_out.granted_caps_count == 1U) != 0) {
        return 1;
    }
    if (expect(strcmp(welcome_out.granted_caps[0], "cap.remote") == 0) != 0) {
        return 1;
    }

    memset(&call, 0, sizeof(call));
    (void)strcpy(call.cap, "cap.remote");
    (void)strcpy(call.op, "echoInt");
    call.value = 7;
    status = aivm_remote_encode_call(100U, &call, buffer, sizeof(buffer), &length);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    memset(&call_out, 0, sizeof(call_out));
    status = aivm_remote_decode_call(buffer, length, &id, &call_out);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(id == 100U && call_out.value == 7) != 0) {
        return 1;
    }
    if (expect(strcmp(call_out.cap, "cap.remote") == 0 && strcmp(call_out.op, "echoInt") == 0) != 0) {
        return 1;
    }

    result.value = 1234;
    status = aivm_remote_encode_result(100U, &result, buffer, sizeof(buffer), &length);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    status = aivm_remote_decode_result(buffer, length, &id, &result_out);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(id == 100U && result_out.value == 1234) != 0) {
        return 1;
    }

    memset(&error_msg, 0, sizeof(error_msg));
    error_msg.error_code = 901U;
    (void)strcpy(error_msg.message, "denied");
    status = aivm_remote_encode_error(100U, &error_msg, buffer, sizeof(buffer), &length);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    status = aivm_remote_decode_error(buffer, length, &id, &error_out);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(id == 100U && error_out.error_code == 901U && strcmp(error_out.message, "denied") == 0) != 0) {
        return 1;
    }

    memset(&deny, 0, sizeof(deny));
    deny.error_code = 403U;
    (void)strcpy(deny.message, "token invalid");
    status = aivm_remote_encode_deny(1U, &deny, buffer, sizeof(buffer), &length);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    status = aivm_remote_decode_deny(buffer, length, &id, &deny_out);
    if (expect(status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(id == 1U && deny_out.error_code == 403U && strcmp(deny_out.message, "token invalid") == 0) != 0) {
        return 1;
    }

    /* Type mismatch should fail deterministically. */
    buffer[0] = (uint8_t)AIVM_REMOTE_MSG_RESULT;
    status = aivm_remote_decode_call(buffer, length, &id, &call_out);
    if (expect(status == AIVM_REMOTE_CODEC_ERR_TYPE) != 0) {
        return 1;
    }

    /* Truncated header should fail deterministically. */
    status = aivm_remote_decode_call(buffer, 4U, &id, &call_out);
    if (expect(status == AIVM_REMOTE_CODEC_ERR_SHORT) != 0) {
        return 1;
    }

    return 0;
}
