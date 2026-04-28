#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "remote/aivm_remote_session.h"
#include "remote/aivm_remote_ws_frame.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET aivm_socket_t;
#define AIVM_INVALID_SOCKET INVALID_SOCKET
#define aivm_socket_close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int aivm_socket_t;
#define AIVM_INVALID_SOCKET (-1)
#define aivm_socket_close close
#endif

enum {
    AIVM_WS_IO_BUFFER = 131072,
    AIVM_HTTP_BUFFER = 8192
};

typedef struct {
    uint32_t state[5];
    uint64_t bit_length;
    uint8_t buffer[64];
    size_t buffer_len;
} Sha1Ctx;

static uint32_t rol32(uint32_t value, uint32_t shift)
{
    return (value << shift) | (value >> (32U - shift));
}

static void sha1_init(Sha1Ctx* ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xEFCDAB89U;
    ctx->state[2] = 0x98BADCFEU;
    ctx->state[3] = 0x10325476U;
    ctx->state[4] = 0xC3D2E1F0U;
}

static void sha1_process_block(Sha1Ctx* ctx, const uint8_t* block)
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    uint32_t i;
    for (i = 0U; i < 16U; i += 1U) {
        w[i] = ((uint32_t)block[i * 4U] << 24U) |
               ((uint32_t)block[i * 4U + 1U] << 16U) |
               ((uint32_t)block[i * 4U + 2U] << 8U) |
               ((uint32_t)block[i * 4U + 3U]);
    }
    for (i = 16U; i < 80U; i += 1U) {
        w[i] = rol32(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (i = 0U; i < 80U; i += 1U) {
        uint32_t f, k, temp;
        if (i < 20U) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999U;
        } else if (i < 40U) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1U;
        } else if (i < 60U) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCU;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6U;
        }
        temp = rol32(a, 5U) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30U);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_update(Sha1Ctx* ctx, const uint8_t* data, size_t length)
{
    size_t i;
    for (i = 0U; i < length; i += 1U) {
        ctx->buffer[ctx->buffer_len++] = data[i];
        if (ctx->buffer_len == 64U) {
            sha1_process_block(ctx, ctx->buffer);
            ctx->buffer_len = 0U;
        }
    }
    ctx->bit_length += (uint64_t)length * 8U;
}

static void sha1_final(Sha1Ctx* ctx, uint8_t out[20])
{
    size_t i;
    uint8_t len_bytes[8];
    ctx->buffer[ctx->buffer_len++] = 0x80U;
    while (ctx->buffer_len != 56U) {
        if (ctx->buffer_len == 64U) {
            sha1_process_block(ctx, ctx->buffer);
            ctx->buffer_len = 0U;
        }
        ctx->buffer[ctx->buffer_len++] = 0x00U;
    }
    for (i = 0U; i < 8U; i += 1U) {
        len_bytes[7U - i] = (uint8_t)((ctx->bit_length >> (8U * i)) & 0xffU);
    }
    sha1_update(ctx, len_bytes, 8U);
    for (i = 0U; i < 5U; i += 1U) {
        out[i * 4U] = (uint8_t)((ctx->state[i] >> 24U) & 0xffU);
        out[i * 4U + 1U] = (uint8_t)((ctx->state[i] >> 16U) & 0xffU);
        out[i * 4U + 2U] = (uint8_t)((ctx->state[i] >> 8U) & 0xffU);
        out[i * 4U + 3U] = (uint8_t)(ctx->state[i] & 0xffU);
    }
}

static int base64_encode(const uint8_t* bytes, size_t length, char* out, size_t out_capacity)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0U;
    size_t out_idx = 0U;
    if (out == NULL || out_capacity == 0U) {
        return 0;
    }
    while (i < length) {
        uint32_t chunk = 0U;
        size_t rem = length - i;
        size_t n = rem >= 3U ? 3U : rem;
        chunk |= ((uint32_t)bytes[i]) << 16U;
        if (n > 1U) {
            chunk |= ((uint32_t)bytes[i + 1U]) << 8U;
        }
        if (n > 2U) {
            chunk |= bytes[i + 2U];
        }
        if (out_idx + 4U >= out_capacity) {
            return 0;
        }
        out[out_idx++] = alphabet[(chunk >> 18U) & 0x3fU];
        out[out_idx++] = alphabet[(chunk >> 12U) & 0x3fU];
        out[out_idx++] = (n > 1U) ? alphabet[(chunk >> 6U) & 0x3fU] : '=';
        out[out_idx++] = (n > 2U) ? alphabet[chunk & 0x3fU] : '=';
        i += n;
    }
    out[out_idx] = '\0';
    return 1;
}

static int parse_caps_csv(
    const char* csv,
    char out_caps[AIVM_REMOTE_MAX_CAPS][AIVM_REMOTE_MAX_TEXT + 1],
    uint32_t* out_count)
{
    const char* cursor;
    uint32_t count = 0U;
    if (out_caps == NULL || out_count == NULL) {
        return 0;
    }
    *out_count = 0U;
    if (csv == NULL || *csv == '\0') {
        return 1;
    }
    cursor = csv;
    while (*cursor != '\0') {
        const char* end = cursor;
        size_t len;
        if (count >= AIVM_REMOTE_MAX_CAPS) {
            return 0;
        }
        while (*end != '\0' && *end != ',') {
            end += 1;
        }
        len = (size_t)(end - cursor);
        if (len > AIVM_REMOTE_MAX_TEXT) {
            return 0;
        }
        if (len > 0U) {
            memcpy(out_caps[count], cursor, len);
            out_caps[count][len] = '\0';
            count += 1U;
        }
        cursor = (*end == ',') ? (end + 1) : end;
    }
    *out_count = count;
    return 1;
}

static int send_all(aivm_socket_t fd, const uint8_t* bytes, size_t length)
{
    size_t sent = 0U;
    while (sent < length) {
#ifdef _WIN32
        int n = send(fd, (const char*)(bytes + sent), (int)(length - sent), 0);
#else
        ssize_t n = send(fd, bytes + sent, length - sent, 0);
#endif
        if (n <= 0) {
            return 0;
        }
        sent += (size_t)n;
    }
    return 1;
}

static int read_http_upgrade(aivm_socket_t fd, char* out_request, size_t out_capacity)
{
    size_t used = 0U;
    if (out_request == NULL || out_capacity == 0U) {
        return 0;
    }
    while (used + 1U < out_capacity) {
#ifdef _WIN32
        int n = recv(fd, out_request + used, (int)(out_capacity - used - 1U), 0);
#else
        ssize_t n = recv(fd, out_request + used, out_capacity - used - 1U, 0);
#endif
        if (n <= 0) {
            return 0;
        }
        used += (size_t)n;
        out_request[used] = '\0';
        if (strstr(out_request, "\r\n\r\n") != NULL) {
            return 1;
        }
    }
    return 0;
}

static int extract_ws_key(const char* request, char* out_key, size_t out_capacity)
{
    const char* key = strstr(request, "Sec-WebSocket-Key:");
    const char* start;
    const char* end;
    size_t len;
    if (key == NULL || out_key == NULL || out_capacity == 0U) {
        return 0;
    }
    start = key + strlen("Sec-WebSocket-Key:");
    while (*start == ' ' || *start == '\t') {
        start += 1;
    }
    end = strstr(start, "\r\n");
    if (end == NULL || end <= start) {
        return 0;
    }
    len = (size_t)(end - start);
    if (len + 1U > out_capacity) {
        return 0;
    }
    memcpy(out_key, start, len);
    out_key[len] = '\0';
    return 1;
}

static int send_ws_upgrade_response(aivm_socket_t fd, const char* ws_key)
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char combined[256];
    uint8_t digest[20];
    char accept_b64[64];
    char response[256];
    Sha1Ctx sha1;
    int n;
    if (ws_key == NULL) {
        return 0;
    }
    n = snprintf(combined, sizeof(combined), "%s%s", ws_key, guid);
    if (n <= 0 || (size_t)n >= sizeof(combined)) {
        return 0;
    }
    sha1_init(&sha1);
    sha1_update(&sha1, (const uint8_t*)combined, (size_t)n);
    sha1_final(&sha1, digest);
    if (!base64_encode(digest, sizeof(digest), accept_b64, sizeof(accept_b64))) {
        return 0;
    }
    n = snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept_b64);
    if (n <= 0 || (size_t)n >= sizeof(response)) {
        return 0;
    }
    return send_all(fd, (const uint8_t*)response, (size_t)n);
}

static int handle_ws_client(aivm_socket_t client_fd, const AivmRemoteServerConfig* config)
{
    char request[AIVM_HTTP_BUFFER];
    char ws_key[128];
    uint8_t recv_buffer[AIVM_WS_IO_BUFFER];
    size_t recv_used = 0U;
    AivmRemoteServerSession session;

    if (!read_http_upgrade(client_fd, request, sizeof(request)) ||
        !extract_ws_key(request, ws_key, sizeof(ws_key)) ||
        !send_ws_upgrade_response(client_fd, ws_key)) {
        return 0;
    }

    aivm_remote_server_session_init(&session);
    for (;;) {
        AivmWsFrame frame;
        size_t consumed = 0U;
#ifdef _WIN32
        int n = recv(client_fd, (char*)(recv_buffer + recv_used), (int)(sizeof(recv_buffer) - recv_used), 0);
#else
        ssize_t n = recv(client_fd, recv_buffer + recv_used, sizeof(recv_buffer) - recv_used, 0);
#endif
        if (n <= 0) {
            break;
        }
        recv_used += (size_t)n;

        for (;;) {
            uint8_t out_payload[AIVM_WS_MAX_PAYLOAD];
            size_t out_len = 0U;
            AivmRemoteSessionStatus status;
            uint8_t out_frame[AIVM_WS_MAX_PAYLOAD + 16U];
            size_t out_frame_len = 0U;

            if (!aivm_ws_decode_client_frame(recv_buffer, recv_used, &consumed, &frame)) {
                break;
            }
            if (consumed > 0U && consumed <= recv_used) {
                memmove(recv_buffer, recv_buffer + consumed, recv_used - consumed);
                recv_used -= consumed;
            }

            if (frame.opcode == 0x8U) {
                (void)aivm_ws_encode_server_control(0x8U, NULL, 0U, out_frame, sizeof(out_frame), &out_frame_len);
                (void)send_all(client_fd, out_frame, out_frame_len);
                return 1;
            }
            if (frame.opcode == 0x9U) {
                if (aivm_ws_encode_server_control(0xAU, frame.payload, frame.payload_length, out_frame, sizeof(out_frame), &out_frame_len)) {
                    (void)send_all(client_fd, out_frame, out_frame_len);
                }
                continue;
            }
            if (frame.opcode != 0x2U) {
                continue;
            }

            status = aivm_remote_server_process_frame(
                config,
                &session,
                frame.payload,
                frame.payload_length,
                out_payload,
                sizeof(out_payload),
                &out_len);
            if (status != AIVM_REMOTE_SESSION_OK) {
                return 0;
            }
            if (!aivm_ws_encode_server_binary(out_payload, out_len, out_frame, sizeof(out_frame), &out_frame_len) ||
                !send_all(client_fd, out_frame, out_frame_len)) {
                return 0;
            }
        }

        if (recv_used == sizeof(recv_buffer)) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    const char* port_env = getenv("AIVM_REMOTE_WS_PORT");
    const char* caps_env = getenv("AIVM_REMOTE_CAPS");
    int port = 8765;
    AivmRemoteServerConfig config;
    aivm_socket_t listener = AIVM_INVALID_SOCKET;
    struct sockaddr_in addr;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 2;
    }
#endif

    if (port_env != NULL && *port_env != '\0') {
        long p = strtol(port_env, NULL, 10);
        if (p > 0 && p <= 65535) {
            port = (int)p;
        }
    }

    memset(&config, 0, sizeof(config));
    config.proto_version = 1U;
    if (!parse_caps_csv(caps_env, config.allowed_caps, &config.allowed_caps_count)) {
        fprintf(stderr, "failed to parse AIVM_REMOTE_CAPS\n");
        return 2;
    }

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == AIVM_INVALID_SOCKET) {
        return 2;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(listener, 8) != 0) {
        aivm_socket_close(listener);
        return 2;
    }

    for (;;) {
        aivm_socket_t client = accept(listener, NULL, NULL);
        if (client == AIVM_INVALID_SOCKET) {
            break;
        }
        (void)handle_ws_client(client, &config);
        aivm_socket_close(client);
    }

    aivm_socket_close(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
