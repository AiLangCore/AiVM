#ifndef AIVM_REMOTE_TRANSPORT_H
#define AIVM_REMOTE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "remote/aivm_remote_session.h"

typedef struct {
    void* context;
    int (*recv_frame)(void* context, uint8_t* out_bytes, size_t out_capacity, size_t* out_length);
    int (*send_frame)(void* context, const uint8_t* bytes, size_t length);
} AivmRemoteTransport;

typedef struct {
    const AivmRemoteServerConfig* config;
    AivmRemoteServerSession* session;
    AivmRemoteTransport transport;
} AivmRemoteBridge;

void aivm_remote_bridge_init(
    AivmRemoteBridge* bridge,
    const AivmRemoteServerConfig* config,
    AivmRemoteServerSession* session,
    AivmRemoteTransport transport
);

/*
 * Reads one frame from transport, processes via remote session engine, writes one response frame.
 * Returns 1 on success, 0 on IO failure or protocol processing failure.
 */
int aivm_remote_bridge_process_once(AivmRemoteBridge* bridge);

/* Stdio transport helpers: 4-byte little-endian length prefix + raw frame bytes. */
int aivm_remote_stdio_recv_frame(void* context, uint8_t* out_bytes, size_t out_capacity, size_t* out_length);
int aivm_remote_stdio_send_frame(void* context, const uint8_t* bytes, size_t length);

#endif
