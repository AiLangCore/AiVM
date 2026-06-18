#ifndef AIVM_REMOTE_WS_FRAME_H
#define AIVM_REMOTE_WS_FRAME_H

#include <stddef.h>
#include <stdint.h>

enum {
    AIVM_WS_MAX_PAYLOAD = 65536
};

typedef struct {
    int fin;
    uint8_t opcode;
    size_t payload_length;
    uint8_t payload[AIVM_WS_MAX_PAYLOAD];
} AivmWsFrame;

/* Decode one masked client->server frame from bytes. Returns 1 on success. */
int aivm_ws_decode_client_frame(
    const uint8_t* bytes,
    size_t length,
    size_t* out_consumed,
    AivmWsFrame* out_frame
);

/* Encode one server->client binary frame (FIN=1, opcode=2). Returns 1 on success. */
int aivm_ws_encode_server_binary(
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length
);

/* Encode one server->client control frame. */
int aivm_ws_encode_server_control(
    uint8_t opcode,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length
);

#endif
