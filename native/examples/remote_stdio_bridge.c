#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remote/aivm_remote_transport.h"

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

int main(void)
{
    AivmRemoteServerConfig config;
    AivmRemoteServerSession session;
    AivmRemoteBridge bridge;
    AivmRemoteTransport transport;
    const char* caps_env = getenv("AIVM_REMOTE_CAPS");

    memset(&config, 0, sizeof(config));
    config.proto_version = 1U;
    if (!parse_caps_csv(caps_env, config.allowed_caps, &config.allowed_caps_count)) {
        fprintf(stderr, "failed to parse AIVM_REMOTE_CAPS\n");
        return 2;
    }

    aivm_remote_server_session_init(&session);
    transport.context = NULL;
    transport.recv_frame = aivm_remote_stdio_recv_frame;
    transport.send_frame = aivm_remote_stdio_send_frame;
    aivm_remote_bridge_init(&bridge, &config, &session, transport);

    while (aivm_remote_bridge_process_once(&bridge)) {
        /* Run until EOF or transport failure. */
    }

    return 0;
}
