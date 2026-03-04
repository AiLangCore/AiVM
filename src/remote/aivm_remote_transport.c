#include "remote/aivm_remote_transport.h"

#include <stdio.h>
#include <string.h>

enum {
    AIVM_REMOTE_TRANSPORT_MAX_FRAME = 65536
};

static uint32_t read_u32_le(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static void write_u32_le(uint8_t* bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xffU);
}

void aivm_remote_bridge_init(
    AivmRemoteBridge* bridge,
    const AivmRemoteServerConfig* config,
    AivmRemoteServerSession* session,
    AivmRemoteTransport transport)
{
    if (bridge == NULL) {
        return;
    }
    bridge->config = config;
    bridge->session = session;
    bridge->transport = transport;
}

int aivm_remote_bridge_process_once(AivmRemoteBridge* bridge)
{
    uint8_t in_frame[AIVM_REMOTE_TRANSPORT_MAX_FRAME];
    uint8_t out_frame[AIVM_REMOTE_TRANSPORT_MAX_FRAME];
    size_t in_length = 0U;
    size_t out_length = 0U;
    AivmRemoteSessionStatus status;
    if (bridge == NULL || bridge->config == NULL || bridge->session == NULL) {
        return 0;
    }
    if (bridge->transport.recv_frame == NULL || bridge->transport.send_frame == NULL) {
        return 0;
    }
    if (!bridge->transport.recv_frame(bridge->transport.context, in_frame, sizeof(in_frame), &in_length)) {
        return 0;
    }
    status = aivm_remote_server_process_frame(
        bridge->config,
        bridge->session,
        in_frame,
        in_length,
        out_frame,
        sizeof(out_frame),
        &out_length);
    if (status != AIVM_REMOTE_SESSION_OK) {
        return 0;
    }
    if (!bridge->transport.send_frame(bridge->transport.context, out_frame, out_length)) {
        return 0;
    }
    return 1;
}

int aivm_remote_stdio_recv_frame(void* context, uint8_t* out_bytes, size_t out_capacity, size_t* out_length)
{
    uint8_t header[4];
    uint32_t length;
    FILE* in = (FILE*)context;
    if (in == NULL) {
        in = stdin;
    }
    if (out_bytes == NULL || out_length == NULL) {
        return 0;
    }
    if (fread(header, 1U, 4U, in) != 4U) {
        return 0;
    }
    length = read_u32_le(header);
    if ((size_t)length > out_capacity) {
        return 0;
    }
    if (length > 0U && fread(out_bytes, 1U, (size_t)length, in) != (size_t)length) {
        return 0;
    }
    *out_length = (size_t)length;
    return 1;
}

int aivm_remote_stdio_send_frame(void* context, const uint8_t* bytes, size_t length)
{
    uint8_t header[4];
    FILE* out = (FILE*)context;
    if (out == NULL) {
        out = stdout;
    }
    if (bytes == NULL && length > 0U) {
        return 0;
    }
    if (length > UINT32_MAX) {
        return 0;
    }
    write_u32_le(header, (uint32_t)length);
    if (fwrite(header, 1U, 4U, out) != 4U) {
        return 0;
    }
    if (length > 0U && fwrite(bytes, 1U, length, out) != length) {
        return 0;
    }
    return fflush(out) == 0;
}
