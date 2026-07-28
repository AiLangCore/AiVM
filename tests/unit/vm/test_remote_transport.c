#include "remote/aivm_remote_channel.h"
#include "remote/aivm_remote_transport.h"

#include <string.h>

typedef struct {
    uint8_t in_frame[512];
    size_t in_length;
    int recv_called;
    uint8_t out_frame[512];
    size_t out_length;
    int send_called;
} MemoryTransportState;

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static int mem_recv(void* context, uint8_t* out_bytes, size_t out_capacity, size_t* out_length)
{
    MemoryTransportState* state = (MemoryTransportState*)context;
    if (state == NULL || out_bytes == NULL || out_length == NULL) {
        return 0;
    }
    if (state->in_length > out_capacity) {
        return 0;
    }
    memcpy(out_bytes, state->in_frame, state->in_length);
    *out_length = state->in_length;
    state->recv_called += 1;
    return 1;
}

static int mem_send(void* context, const uint8_t* bytes, size_t length)
{
    MemoryTransportState* state = (MemoryTransportState*)context;
    if (state == NULL || bytes == NULL || length > sizeof(state->out_frame)) {
        return 0;
    }
    memcpy(state->out_frame, bytes, length);
    state->out_length = length;
    state->send_called += 1;
    return 1;
}

int main(void)
{
    AivmRemoteServerConfig config;
    AivmRemoteServerSession session;
    AivmRemoteBridge bridge;
    AivmRemoteTransport transport;
    MemoryTransportState state;
    AivmRemoteHello hello;
    AivmRemoteCall call;
    AivmRemoteWelcome welcome;
    AivmRemoteResult result;
    uint32_t id = 0U;
    size_t encoded_len = 0U;
    AivmRemoteCodecStatus codec_status;

    memset(&config, 0, sizeof(config));
    memset(&session, 0, sizeof(session));
    memset(&state, 0, sizeof(state));
    config.proto_version = 1U;
    config.allowed_caps_count = 1U;
    (void)strcpy(config.allowed_caps[0], "cap.remote");
    aivm_remote_server_session_init(&session);

    transport.context = &state;
    transport.recv_frame = mem_recv;
    transport.send_frame = mem_send;
    aivm_remote_bridge_init(&bridge, &config, &session, transport);

    memset(&hello, 0, sizeof(hello));
    hello.proto_version = 1U;
    (void)strcpy(hello.client_name, "bridge-client");
    hello.requested_caps_count = 1U;
    (void)strcpy(hello.requested_caps[0], "cap.remote");
    codec_status = aivm_remote_encode_hello(1U, &hello, state.in_frame, sizeof(state.in_frame), &encoded_len);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    state.in_length = encoded_len;
    if (expect(aivm_remote_bridge_process_once(&bridge) == 1) != 0) {
        return 1;
    }
    if (expect(state.recv_called == 1 && state.send_called == 1) != 0) {
        return 1;
    }
    codec_status = aivm_remote_decode_welcome(state.out_frame, state.out_length, &id, &welcome);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(id == 1U && welcome.granted_caps_count == 1U) != 0) {
        return 1;
    }

    memset(&call, 0, sizeof(call));
    (void)strcpy(call.cap, "cap.remote");
    (void)strcpy(call.op, "echoInt");
    call.value = 77;
    codec_status = aivm_remote_encode_call(2U, &call, state.in_frame, sizeof(state.in_frame), &encoded_len);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    state.in_length = encoded_len;
    if (expect(aivm_remote_bridge_process_once(&bridge) == 1) != 0) {
        return 1;
    }
    codec_status = aivm_remote_decode_result(state.out_frame, state.out_length, &id, &result);
    if (expect(codec_status == AIVM_REMOTE_CODEC_OK) != 0) {
        return 1;
    }
    if (expect(id == 2U && result.value == 77) != 0) {
        return 1;
    }

    return 0;
}
