typedef enum {
    NATIVE_NET_HANDLE_KIND_NONE = 0,
    NATIVE_NET_HANDLE_KIND_TCP_LISTENER = 1,
    NATIVE_NET_HANDLE_KIND_TCP_STREAM = 2,
    NATIVE_NET_HANDLE_KIND_UDP_SOCKET = 3,
    NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM = 4
} NativeNetHandleKind;

typedef struct NativeNetHandleState
{
    int used;
    NativeNetHandleKind kind;
    NativeSocket socket;
    void* tls_state;
} NativeNetHandleState;

typedef struct NativeNetAsyncState
{
    int used;
    int kind; /* 1 connect, 2 read, 3 write */
    int status; /* 0 pending, 1 success, -1 failure, -2 canceled */
    int64_t socket_handle;
    int port;
    int max_bytes;
    uint8_t* pending_bytes;
    size_t pending_bytes_len;
    size_t pending_bytes_offset;
    int64_t result_int;
    uint8_t* result_bytes;
    size_t result_bytes_len;
    int use_tls;
    char host[256];
    char resolved_ip[INET_ADDRSTRLEN];
    char error[128];
    uint64_t generation;
    volatile int worker_done;
    int worker_joined;
    int worker_result_status;
    int canceled;
    NativeSocket worker_socket;
    void* worker_tls_state;
    char worker_error[128];
#ifdef _WIN32
    HANDLE worker_thread;
#else
    pthread_t worker_thread;
#endif
} NativeNetAsyncState;

typedef struct NativeNetAsyncConnectWorkerContext
{
    NativeNetAsyncState* op;
    uint64_t generation;
    int port;
    int use_tls;
    int has_resolved_addr;
    struct sockaddr_in resolved_addr;
    char resolved_ip[INET_ADDRSTRLEN];
    char host[256];
} NativeNetAsyncConnectWorkerContext;

typedef struct NativeNetLastFailure
{
    int valid;
    int kind;
    int use_tls;
    int port;
    char host[256];
    char resolved_ip[INET_ADDRSTRLEN];
    char error[128];
} NativeNetLastFailure;

#ifdef __APPLE__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
typedef struct NativeTlsStreamState
{
    SSLContextRef context;
} NativeTlsStreamState;
static OSStatus g_native_tls_last_status = noErr;
#pragma clang diagnostic pop
#endif

static NativeNetHandleState g_native_net_handles[NATIVE_NET_HANDLE_CAPACITY];
static NativeNetAsyncState g_native_net_async_ops[NATIVE_NET_ASYNC_CAPACITY];
static uint64_t g_native_net_async_next_generation = 1U;
static NativeNetLastFailure g_native_net_last_failure;
static uint8_t g_native_net_bytes_scratch[NATIVE_NET_BYTES_CHUNK];
static char g_native_net_text_scratch[NATIVE_NET_BYTES_CHUNK];
static char g_native_net_host_scratch[64];
static int native_net_handle_close(int64_t handle);
static void native_net_async_release_pending_bytes(NativeNetAsyncState* op);
static int native_net_socket_would_block(void);
static int native_net_platform_init(void);

static int native_net_dns_skip_name(const uint8_t* packet, size_t packet_len, size_t* io_offset)
{
    size_t offset;
    size_t jumps = 0U;
    if (packet == NULL || io_offset == NULL) {
        return 0;
    }
    offset = *io_offset;
    while (offset < packet_len) {
        uint8_t len = packet[offset];
        if (len == 0U) {
            offset += 1U;
            *io_offset = offset;
            return 1;
        }
        if ((len & 0xC0U) == 0xC0U) {
            if (offset + 1U >= packet_len) {
                return 0;
            }
            offset += 2U;
            *io_offset = offset;
            return 1;
        }
        offset += 1U;
        if (offset + len > packet_len) {
            return 0;
        }
        offset += (size_t)len;
        jumps += 1U;
        if (jumps > 128U) {
            return 0;
        }
    }
    return 0;
}

static int native_net_dns_encode_qname(const char* host, uint8_t* out, size_t out_cap, size_t* out_len)
{
    const char* cursor;
    size_t written = 0U;
    if (host == NULL || out == NULL || out_len == NULL) {
        return 0;
    }
    cursor = host;
    while (*cursor != '\0') {
        const char* dot = strchr(cursor, '.');
        size_t label_len = (dot == NULL) ? strlen(cursor) : (size_t)(dot - cursor);
        if (label_len == 0U || label_len > 63U || written + 1U + label_len + 1U > out_cap) {
            return 0;
        }
        out[written] = (uint8_t)label_len;
        written += 1U;
        memcpy(&out[written], cursor, label_len);
        written += label_len;
        if (dot == NULL) {
            break;
        }
        cursor = dot + 1;
    }
    if (written + 1U > out_cap) {
        return 0;
    }
    out[written] = 0U;
    written += 1U;
    *out_len = written;
    return 1;
}

static int native_net_dns_query_ipv4_with_resolver(
    const char* host,
    const char* resolver_ip,
    struct in_addr* out_addr)
{
    uint8_t query[512];
    uint8_t response[512];
    size_t qname_len = 0U;
    size_t query_len;
    NativeSocket socket_fd = NATIVE_INVALID_SOCKET;
    struct sockaddr_in resolver_addr;
    int send_rc;
    fd_set read_set;
    struct timeval timeout;
    int select_rc;
    ssize_t recv_len;
    size_t offset;
    uint16_t answer_count;
    uint16_t i;

    if (host == NULL || resolver_ip == NULL || out_addr == NULL) {
        return 0;
    }
    if (!native_net_platform_init()) {
        return 0;
    }
    memset(query, 0, sizeof(query));
    query[0] = 0x41U;
    query[1] = 0x49U;
    query[2] = 0x01U;
    query[5] = 0x01U;
    if (!native_net_dns_encode_qname(host, &query[12], sizeof(query) - 12U, &qname_len)) {
        return 0;
    }
    query_len = 12U + qname_len;
    if (query_len + 4U > sizeof(query)) {
        return 0;
    }
    query[query_len] = 0x00U;
    query[query_len + 1U] = 0x01U;
    query[query_len + 2U] = 0x00U;
    query[query_len + 3U] = 0x01U;
    query_len += 4U;

    memset(&resolver_addr, 0, sizeof(resolver_addr));
    resolver_addr.sin_family = AF_INET;
    resolver_addr.sin_port = htons(53U);
    if (inet_pton(AF_INET, resolver_ip, &resolver_addr.sin_addr) != 1) {
        return 0;
    }

    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd == NATIVE_INVALID_SOCKET) {
        return 0;
    }
    send_rc = (int)sendto(
        socket_fd,
        (const char*)query,
        (int)query_len,
        0,
        (const struct sockaddr*)&resolver_addr,
        (socklen_t)sizeof(resolver_addr));
    if (send_rc < 0) {
        native_socket_close(socket_fd);
        return 0;
    }

    FD_ZERO(&read_set);
    FD_SET(socket_fd, &read_set);
    timeout.tv_sec = 1;
    timeout.tv_usec = 500000;
#ifdef _WIN32
    select_rc = select(0, &read_set, NULL, NULL, &timeout);
#else
    select_rc = select((int)socket_fd + 1, &read_set, NULL, NULL, &timeout);
#endif
    if (select_rc <= 0) {
        native_socket_close(socket_fd);
        return 0;
    }

    recv_len = recvfrom(socket_fd, (char*)response, sizeof(response), 0, NULL, NULL);
    native_socket_close(socket_fd);
    if (recv_len < 12) {
        return 0;
    }
    if (response[0] != 0x41U || response[1] != 0x49U) {
        return 0;
    }
    if ((response[3] & 0x0FU) != 0U) {
        return 0;
    }
    answer_count = (uint16_t)(((uint16_t)response[6] << 8U) | (uint16_t)response[7]);
    offset = 12U;
    if (!native_net_dns_skip_name(response, (size_t)recv_len, &offset) || offset + 4U > (size_t)recv_len) {
        return 0;
    }
    offset += 4U;
    for (i = 0U; i < answer_count; i += 1U) {
        uint16_t type;
        uint16_t class_value;
        uint16_t rdlength;
        if (!native_net_dns_skip_name(response, (size_t)recv_len, &offset) || offset + 10U > (size_t)recv_len) {
            return 0;
        }
        type = (uint16_t)(((uint16_t)response[offset] << 8U) | (uint16_t)response[offset + 1U]);
        class_value = (uint16_t)(((uint16_t)response[offset + 2U] << 8U) | (uint16_t)response[offset + 3U]);
        rdlength = (uint16_t)(((uint16_t)response[offset + 8U] << 8U) | (uint16_t)response[offset + 9U]);
        offset += 10U;
        if (offset + rdlength > (size_t)recv_len) {
            return 0;
        }
        if (type == 1U && class_value == 1U && rdlength == 4U) {
            memcpy(out_addr, &response[offset], 4U);
            return 1;
        }
        offset += rdlength;
    }
    return 0;
}

static int native_net_dns_query_ipv4_fallback(const char* host, struct sockaddr_in* out_addr)
{
    static const char* resolvers[] = { "1.1.1.1", "8.8.8.8", "9.9.9.9" };
    size_t i;
    struct in_addr ipv4_addr;
    if (host == NULL || out_addr == NULL) {
        return 0;
    }
    for (i = 0U; i < (sizeof(resolvers) / sizeof(resolvers[0])); i += 1U) {
        if (native_net_dns_query_ipv4_with_resolver(host, resolvers[i], &ipv4_addr)) {
            out_addr->sin_family = AF_INET;
            out_addr->sin_addr = ipv4_addr;
            return 1;
        }
    }
    return 0;
}

static void native_net_record_last_failure(const NativeNetAsyncState* op, const char* message)
{
    if (op == NULL) {
        return;
    }
    memset(&g_native_net_last_failure, 0, sizeof(g_native_net_last_failure));
    g_native_net_last_failure.valid = 1;
    g_native_net_last_failure.kind = op->kind;
    g_native_net_last_failure.use_tls = op->use_tls;
    g_native_net_last_failure.port = op->port;
    (void)snprintf(g_native_net_last_failure.host, sizeof(g_native_net_last_failure.host), "%s", op->host);
    (void)snprintf(g_native_net_last_failure.resolved_ip, sizeof(g_native_net_last_failure.resolved_ip), "%s", op->resolved_ip);
    (void)snprintf(
        g_native_net_last_failure.error,
        sizeof(g_native_net_last_failure.error),
        "%s",
        (message == NULL || message[0] == '\0') ? "failed" : message);
}

static const char* native_net_error_stage(const char* error_text)
{
    if (error_text == NULL || error_text[0] == '\0') {
        return "unknown";
    }
    if (strstr(error_text, "dns_failed") != NULL) {
        return "dns";
    }
    if (strstr(error_text, "tls_") != NULL || strstr(error_text, "connect_tls_failed") != NULL) {
        return "tls";
    }
    if (strstr(error_text, "connect_errno_") != NULL || strstr(error_text, "connect_failed") != NULL) {
        return "connect";
    }
    if (strstr(error_text, "read_failed") != NULL) {
        return "read";
    }
    if (strstr(error_text, "write_failed") != NULL) {
        return "write";
    }
    if (strstr(error_text, "socket_errno_") != NULL) {
        return "socket";
    }
    return "unknown";
}

static const char* native_net_error_classification(const char* error_text)
{
    const char* stage = native_net_error_stage(error_text);
    if (strcmp(stage, "dns") == 0) {
        return "dns_resolution";
    }
    if (strcmp(stage, "connect") == 0) {
        return "tcp_connect";
    }
    if (strcmp(stage, "tls") == 0) {
        return "tls_handshake";
    }
    if (strcmp(stage, "socket") == 0) {
        return "socket_setup";
    }
    if (strcmp(stage, "read") == 0) {
        return "read_io";
    }
    if (strcmp(stage, "write") == 0) {
        return "write_io";
    }
    return "unknown";
}

static const char* native_net_resolution_state(const char* resolved_ip, const char* error_text)
{
    if (resolved_ip != NULL && resolved_ip[0] != '\0') {
        return "resolved";
    }
    if (strcmp(native_net_error_stage(error_text), "dns") == 0) {
        return "unresolved";
    }
    return "unknown";
}

#ifdef __APPLE__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static OSStatus native_tls_socket_read(SSLConnectionRef connection, void* data, size_t* data_length)
{
    ssize_t read_count;
    NativeSocket socket_fd;
    if (data_length == NULL) {
        return errSecParam;
    }
    socket_fd = (NativeSocket)(intptr_t)connection;
    read_count = recv(socket_fd, data, *data_length, 0);
    if (read_count > 0) {
        *data_length = (size_t)read_count;
        return noErr;
    }
    if (read_count == 0) {
        *data_length = 0U;
        return errSSLClosedGraceful;
    }
    if (native_net_socket_would_block()) {
        *data_length = 0U;
        return errSSLWouldBlock;
    }
    *data_length = 0U;
    return errSecIO;
}

static OSStatus native_tls_socket_write(SSLConnectionRef connection, const void* data, size_t* data_length)
{
    ssize_t write_count;
    NativeSocket socket_fd;
    if (data_length == NULL) {
        return errSecParam;
    }
    socket_fd = (NativeSocket)(intptr_t)connection;
    write_count = send(socket_fd, data, *data_length, 0);
    if (write_count >= 0) {
        *data_length = (size_t)write_count;
        return noErr;
    }
    if (native_net_socket_would_block()) {
        *data_length = 0U;
        return errSSLWouldBlock;
    }
    *data_length = 0U;
    return errSecIO;
}

static void native_tls_stream_destroy(NativeTlsStreamState* tls_state)
{
    if (tls_state == NULL) {
        return;
    }
    if (tls_state->context != NULL) {
        CFRelease(tls_state->context);
        tls_state->context = NULL;
    }
    free(tls_state);
}

static NativeTlsStreamState* native_tls_stream_create(NativeSocket socket_fd, const char* host)
{
    NativeTlsStreamState* tls_state;
    OSStatus status;
    tls_state = (NativeTlsStreamState*)calloc(1U, sizeof(*tls_state));
    if (tls_state == NULL) {
        g_native_tls_last_status = errSecAllocate;
        return NULL;
    }
    tls_state->context = SSLCreateContext(kCFAllocatorDefault, kSSLClientSide, kSSLStreamType);
    if (tls_state->context == NULL) {
        g_native_tls_last_status = errSecAllocate;
        native_tls_stream_destroy(tls_state);
        return NULL;
    }
    status = SSLSetIOFuncs(tls_state->context, native_tls_socket_read, native_tls_socket_write);
    if (status == noErr) {
        status = SSLSetConnection(tls_state->context, (SSLConnectionRef)(intptr_t)socket_fd);
    }
    if (status == noErr && host != NULL && host[0] != '\0') {
        status = SSLSetPeerDomainName(tls_state->context, host, strlen(host));
    }
    if (status == noErr) {
        status = SSLSetProtocolVersionMin(tls_state->context, kTLSProtocol12);
    }
    if (status != noErr) {
        g_native_tls_last_status = status;
        native_tls_stream_destroy(tls_state);
        return NULL;
    }
    g_native_tls_last_status = noErr;
    return tls_state;
}

static int native_tls_stream_handshake(NativeNetHandleState* state)
{
    NativeTlsStreamState* tls_state;
    OSStatus status;
    if (state == NULL || state->kind != NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM || state->tls_state == NULL) {
        return -1;
    }
    tls_state = (NativeTlsStreamState*)state->tls_state;
    for (;;) {
        status = SSLHandshake(tls_state->context);
        if (status == noErr) {
            g_native_tls_last_status = noErr;
            return 1;
        }
        if (status == errSSLWouldBlock) {
            g_native_tls_last_status = status;
            return 0;
        }
        if (status == errSSLServerAuthCompleted) {
            SecTrustRef trust = NULL;
            if (SSLCopyPeerTrust(tls_state->context, &trust) != noErr || trust == NULL) {
                g_native_tls_last_status = status;
                return -1;
            }
            if (!SecTrustEvaluateWithError(trust, NULL)) {
                CFRelease(trust);
                g_native_tls_last_status = errSSLXCertChainInvalid;
                return -1;
            }
            CFRelease(trust);
            continue;
        }
        g_native_tls_last_status = status;
        return -1;
    }
}
#pragma clang diagnostic pop
#endif

static void native_net_reset(void)
{
    size_t i;
    memset(&g_native_net_last_failure, 0, sizeof(g_native_net_last_failure));
    for (i = 0U; i < NATIVE_NET_HANDLE_CAPACITY; i += 1U) {
        if (g_native_net_handles[i].used) {
            (void)native_net_handle_close((int64_t)(i + 1U));
        }
    }
    for (i = 0U; i < NATIVE_NET_ASYNC_CAPACITY; i += 1U) {
        g_native_net_async_ops[i].canceled = 1;
        native_net_async_maybe_finalize_worker(&g_native_net_async_ops[i]);
        native_net_async_close_worker_socket(&g_native_net_async_ops[i]);
        native_net_async_release_pending_bytes(&g_native_net_async_ops[i]);
        if (g_native_net_async_ops[i].result_bytes != NULL) {
            free(g_native_net_async_ops[i].result_bytes);
        }
        memset(&g_native_net_async_ops[i], 0, sizeof(g_native_net_async_ops[i]));
    }
}

static int native_net_platform_init(void)
{
#ifdef _WIN32
    static int wsa_ready = 0;
    if (!wsa_ready) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            return 0;
        }
        wsa_ready = 1;
    }
#endif
    return 1;
}

static int native_net_socket_set_nonblocking(NativeSocket socket_fd)
{
#ifdef _WIN32
    u_long mode = 1UL;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0) {
        return 0;
    }
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static int native_net_socket_would_block(void)
{
#ifdef _WIN32
    int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS || err == WSAEALREADY;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EALREADY;
#endif
}

#ifdef __APPLE__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
static int native_net_stream_read(
    NativeNetHandleState* state,
    uint8_t* bytes,
    size_t max_bytes,
    size_t* out_read,
    int* out_closed)
{
    if (out_read == NULL || out_closed == NULL || state == NULL) {
        return -1;
    }
    *out_read = 0U;
    *out_closed = 0;
    if (state->kind == NATIVE_NET_HANDLE_KIND_TCP_STREAM) {
#ifdef _WIN32
        int read_count = recv(state->socket, (char*)bytes, (int)max_bytes, 0);
#else
        ssize_t read_count = recv(state->socket, bytes, max_bytes, 0);
#endif
        if (read_count > 0) {
            *out_read = (size_t)read_count;
            return 1;
        }
        if (read_count == 0) {
            *out_closed = 1;
            return 1;
        }
        return native_net_socket_would_block() ? 0 : -1;
    }
#ifdef __APPLE__
    if (state->kind == NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM) {
        size_t processed = max_bytes;
        OSStatus status;
        NativeTlsStreamState* tls_state = (NativeTlsStreamState*)state->tls_state;
        if (tls_state == NULL) {
            return -1;
        }
        status = SSLRead(tls_state->context, bytes, max_bytes, &processed);
        *out_read = processed;
        if (status == noErr) {
            return 1;
        }
        if (status == errSSLWouldBlock) {
            return processed > 0U ? 1 : 0;
        }
        if (status == errSSLClosedGraceful || status == errSSLClosedAbort) {
            *out_closed = 1;
            return 1;
        }
    }
#endif
    return -1;
}

static int native_net_stream_write(
    NativeNetHandleState* state,
    const uint8_t* bytes,
    size_t max_bytes,
    size_t* out_written)
{
    if (out_written == NULL || state == NULL) {
        return -1;
    }
    *out_written = 0U;
    if (state->kind == NATIVE_NET_HANDLE_KIND_TCP_STREAM) {
#ifdef _WIN32
        int write_count = send(state->socket, (const char*)bytes, (int)max_bytes, 0);
#else
        ssize_t write_count = send(state->socket, bytes, max_bytes, 0);
#endif
        if (write_count > 0) {
            *out_written = (size_t)write_count;
            return 1;
        }
        if (write_count == 0) {
            return -1;
        }
        return native_net_socket_would_block() ? 0 : -1;
    }
#ifdef __APPLE__
    if (state->kind == NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM) {
        size_t processed = max_bytes;
        OSStatus status;
        NativeTlsStreamState* tls_state = (NativeTlsStreamState*)state->tls_state;
        if (tls_state == NULL) {
            return -1;
        }
        status = SSLWrite(tls_state->context, bytes, max_bytes, &processed);
        *out_written = processed;
        if (status == noErr) {
            return 1;
        }
        if (status == errSSLWouldBlock) {
            return processed > 0U ? 1 : 0;
        }
    }
#endif
    return -1;
}

static int native_net_resolve_ipv4_detail(
    const char* host,
    uint16_t port,
    int socket_type,
    int passive,
    struct sockaddr_in* out_addr,
    char* error_text,
    size_t error_text_size)
{
    struct addrinfo hints;
    struct addrinfo* results = NULL;
    struct addrinfo* cursor = NULL;
    char port_text[16];
    int gai_result;
    const char* lookup_host;
    if (out_addr == NULL) {
        return 0;
    }
    if (error_text != NULL && error_text_size > 0U) {
        error_text[0] = '\0';
    }
    memset(out_addr, 0, sizeof(*out_addr));
    lookup_host = (host == NULL || host[0] == '\0' || strcmp(host, "*") == 0) ? NULL : host;
    if (lookup_host != NULL) {
        struct in_addr numeric_addr;
        if (inet_pton(AF_INET, lookup_host, &numeric_addr) == 1) {
            out_addr->sin_family = AF_INET;
            out_addr->sin_port = htons(port);
            out_addr->sin_addr = numeric_addr;
            return 1;
        }
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = socket_type;
    hints.ai_protocol = (socket_type == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    (void)snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    gai_result = getaddrinfo(
            lookup_host,
            port_text,
            &hints,
            &results);
    if (gai_result != 0) {
#ifdef __APPLE__
        if (!passive && lookup_host != NULL) {
            CFStringRef host_name = NULL;
            CFHostRef cf_host = NULL;
            Boolean resolved = false;
            CFStreamError cf_error;
            CFArrayRef addresses = NULL;
            CFIndex index;
            host_name = CFStringCreateWithCString(kCFAllocatorDefault, lookup_host, kCFStringEncodingUTF8);
            if (host_name != NULL) {
                cf_host = CFHostCreateWithName(kCFAllocatorDefault, host_name);
                CFRelease(host_name);
                host_name = NULL;
            }
            if (cf_host != NULL) {
                memset(&cf_error, 0, sizeof(cf_error));
                if (CFHostStartInfoResolution(cf_host, kCFHostAddresses, &cf_error)) {
                    addresses = CFHostGetAddressing(cf_host, &resolved);
                    if (resolved && addresses != NULL) {
                        for (index = 0; index < CFArrayGetCount(addresses); index += 1) {
                            CFDataRef address_data = (CFDataRef)CFArrayGetValueAtIndex(addresses, index);
                            const struct sockaddr* addr_ptr;
                            if (address_data == NULL || CFGetTypeID(address_data) != CFDataGetTypeID()) {
                                continue;
                            }
                            if (CFDataGetLength(address_data) < (CFIndex)sizeof(struct sockaddr_in)) {
                                continue;
                            }
                            addr_ptr = (const struct sockaddr*)CFDataGetBytePtr(address_data);
                            if (addr_ptr == NULL || addr_ptr->sa_family != AF_INET) {
                                continue;
                            }
                            memcpy(out_addr, addr_ptr, sizeof(*out_addr));
                            out_addr->sin_port = htons(port);
                            CFRelease(cf_host);
                            return 1;
                        }
                    }
                }
                CFRelease(cf_host);
            }
        }
#endif
        if (!passive && lookup_host != NULL && native_net_dns_query_ipv4_fallback(lookup_host, out_addr)) {
            out_addr->sin_port = htons(port);
            return 1;
        }
        if (error_text != NULL && error_text_size > 0U) {
            (void)snprintf(error_text, error_text_size, "dns_failed:%s", gai_strerror(gai_result));
        }
        return 0;
    }
    for (cursor = results; cursor != NULL; cursor = cursor->ai_next) {
        if (cursor->ai_family == AF_INET &&
            cursor->ai_addr != NULL &&
            cursor->ai_addrlen >= (socklen_t)sizeof(struct sockaddr_in)) {
            memcpy(out_addr, cursor->ai_addr, sizeof(*out_addr));
            freeaddrinfo(results);
            return 1;
        }
    }
    freeaddrinfo(results);
    if (!passive && lookup_host != NULL && native_net_dns_query_ipv4_fallback(lookup_host, out_addr)) {
        out_addr->sin_port = htons(port);
        return 1;
    }
    if (error_text != NULL && error_text_size > 0U) {
        (void)snprintf(error_text, error_text_size, "dns_failed:no_ipv4_result");
    }
    return 0;
}

static int native_net_resolve_ipv4(const char* host, uint16_t port, int socket_type, int passive, struct sockaddr_in* out_addr)
{
    return native_net_resolve_ipv4_detail(host, port, socket_type, passive, out_addr, NULL, 0U);
}

static NativeNetHandleState* native_net_handle_lookup(int64_t handle)
{
    size_t index;
    if (handle <= 0 || handle > (int64_t)NATIVE_NET_HANDLE_CAPACITY) {
        return NULL;
    }
    index = (size_t)(handle - 1);
    if (!g_native_net_handles[index].used) {
        return NULL;
    }
    return &g_native_net_handles[index];
}

static int64_t native_net_handle_allocate(NativeNetHandleKind kind, NativeSocket socket_fd)
{
    size_t i;
    if (kind == NATIVE_NET_HANDLE_KIND_NONE || socket_fd == NATIVE_INVALID_SOCKET) {
        return -1;
    }
    for (i = 0U; i < NATIVE_NET_HANDLE_CAPACITY; i += 1U) {
        if (!g_native_net_handles[i].used) {
            g_native_net_handles[i].used = 1;
            g_native_net_handles[i].kind = kind;
            g_native_net_handles[i].socket = socket_fd;
            g_native_net_handles[i].tls_state = NULL;
            return (int64_t)(i + 1U);
        }
    }
    return -1;
}
#ifdef __APPLE__
#pragma clang diagnostic pop
#endif

static int native_net_handle_close(int64_t handle)
{
    NativeNetHandleState* state = native_net_handle_lookup(handle);
    if (state == NULL) {
        return 0;
    }
#ifdef __APPLE__
    if (state->tls_state != NULL) {
        native_tls_stream_destroy((NativeTlsStreamState*)state->tls_state);
        state->tls_state = NULL;
    }
#endif
    if (state->socket != NATIVE_INVALID_SOCKET) {
        native_socket_close(state->socket);
    }
    state->used = 0;
    state->kind = NATIVE_NET_HANDLE_KIND_NONE;
    state->socket = NATIVE_INVALID_SOCKET;
    state->tls_state = NULL;
    return 1;
}

static NativeNetAsyncState* native_net_async_lookup(int64_t op_handle)
{
    size_t index;
    if (op_handle <= 0 || op_handle > (int64_t)NATIVE_NET_ASYNC_CAPACITY) {
        return NULL;
    }
    index = (size_t)(op_handle - 1);
    if (!g_native_net_async_ops[index].used) {
        return NULL;
    }
    return &g_native_net_async_ops[index];
}

static int64_t native_net_async_allocate(void)
{
    size_t i;
    for (i = 0U; i < NATIVE_NET_ASYNC_CAPACITY; i += 1U) {
        if (!g_native_net_async_ops[i].used) {
            memset(&g_native_net_async_ops[i], 0, sizeof(g_native_net_async_ops[i]));
            g_native_net_async_ops[i].used = 1;
            g_native_net_async_ops[i].status = 0;
            g_native_net_async_ops[i].generation = g_native_net_async_next_generation++;
            g_native_net_async_ops[i].worker_socket = NATIVE_INVALID_SOCKET;
            return (int64_t)(i + 1U);
        }
    }
    return -1;
}

static void native_net_async_release_pending_bytes(NativeNetAsyncState* op)
{
    if (op == NULL) {
        return;
    }
    if (op->pending_bytes != NULL) {
        free(op->pending_bytes);
        op->pending_bytes = NULL;
    }
    op->pending_bytes_len = 0U;
    op->pending_bytes_offset = 0U;
}

static void native_net_async_set_success_int(NativeNetAsyncState* op, int64_t value)
{
    if (op == NULL) {
        return;
    }
    native_net_async_release_pending_bytes(op);
    op->status = 1;
    op->result_int = value;
    op->error[0] = '\0';
    airun_log_message(AIRUN_LOG_INFO, "net.async", "op=%lld kind=%d status=success-int value=%lld",
        (long long)((op - g_native_net_async_ops) + 1),
        op->kind,
        (long long)value);
}

static void native_net_async_set_success_bytes(NativeNetAsyncState* op, const uint8_t* bytes, size_t len)
{
    if (op == NULL) {
        return;
    }
    native_net_async_release_pending_bytes(op);
    if (op->result_bytes != NULL) {
        free(op->result_bytes);
        op->result_bytes = NULL;
    }
    op->result_bytes_len = 0U;
    if (len > 0U && bytes != NULL) {
        op->result_bytes = (uint8_t*)malloc(len);
        if (op->result_bytes == NULL) {
            op->status = -1;
            (void)snprintf(op->error, sizeof(op->error), "alloc_failed");
            return;
        }
        memcpy(op->result_bytes, bytes, len);
        op->result_bytes_len = len;
    }
    op->status = 1;
    op->result_int = 0;
    op->error[0] = '\0';
    airun_log_message(AIRUN_LOG_INFO, "net.async", "op=%lld kind=%d status=success-bytes len=%llu",
        (long long)((op - g_native_net_async_ops) + 1),
        op->kind,
        (unsigned long long)len);
}

static void native_net_async_set_failure(NativeNetAsyncState* op, const char* message)
{
    if (op == NULL) {
        return;
    }
    native_net_async_release_pending_bytes(op);
    op->status = -1;
    op->result_int = 0;
    if (message == NULL || message[0] == '\0') {
        (void)snprintf(op->error, sizeof(op->error), "failed");
    } else {
        (void)snprintf(op->error, sizeof(op->error), "%s", message);
    }
    native_net_record_last_failure(op, op->error);
    airun_log_message(AIRUN_LOG_ERROR, "net.async", "op=%lld kind=%d status=failure error=%s",
        (long long)((op - g_native_net_async_ops) + 1),
        op->kind,
        op->error);
}

static void native_net_async_cancel_pending_socket(NativeNetAsyncState* op)
{
    if (op == NULL) {
        return;
    }
    if (op->kind == 1 && op->socket_handle > 0) {
        (void)native_net_handle_close(op->socket_handle);
    }
    op->socket_handle = 0;
}

static void native_net_async_close_worker_socket(NativeNetAsyncState* op)
{
    if (op == NULL) {
        return;
    }
#ifdef __APPLE__
    if (op->worker_tls_state != NULL) {
        native_tls_stream_destroy((NativeTlsStreamState*)op->worker_tls_state);
        op->worker_tls_state = NULL;
    }
#endif
    if (op->worker_socket != NATIVE_INVALID_SOCKET) {
        airun_log_message(AIRUN_LOG_TRACE, "net.async", "op=%lld closing-worker-socket",
            (long long)((op - g_native_net_async_ops) + 1));
        native_socket_close(op->worker_socket);
        op->worker_socket = NATIVE_INVALID_SOCKET;
    }
}

static void native_net_async_worker_connect_execute(NativeNetAsyncConnectWorkerContext* ctx)
{
    struct sockaddr_in addr;
    NativeSocket socket_fd = NATIVE_INVALID_SOCKET;
    void* tls_state = NULL;
    NativeNetAsyncState* op;
    int worker_result_status = -1;
    char worker_error[128];
    char resolved_ip[INET_ADDRSTRLEN];
    if (ctx == NULL || ctx->op == NULL) {
        return;
    }
    op = ctx->op;
    airun_log_message(AIRUN_LOG_INFO, "net.async", "op=%lld worker-start host=%s port=%d tls=%d gen=%llu",
        (long long)((op - g_native_net_async_ops) + 1),
        ctx->host,
        ctx->port,
        ctx->use_tls,
        (unsigned long long)ctx->generation);
    worker_error[0] = '\0';
    resolved_ip[0] = '\0';
    if (!native_net_platform_init()) {
        (void)snprintf(worker_error, sizeof(worker_error), "%s:platform_init", ctx->use_tls ? "connect_tls_failed" : "connect_failed");
        goto finalize;
    }
    if (ctx->port <= 0 || ctx->port > 65535) {
        (void)snprintf(worker_error, sizeof(worker_error), "%s:invalid_port", ctx->use_tls ? "connect_tls_failed" : "connect_failed");
        goto finalize;
    }
    if (ctx->has_resolved_addr) {
        memcpy(&addr, &ctx->resolved_addr, sizeof(addr));
        (void)snprintf(resolved_ip, sizeof(resolved_ip), "%s", ctx->resolved_ip);
    } else {
        if (!native_net_resolve_ipv4_detail(ctx->host, (uint16_t)ctx->port, SOCK_STREAM, 0, &addr, worker_error, sizeof(worker_error))) {
            if (worker_error[0] == '\0') {
                (void)snprintf(worker_error, sizeof(worker_error), "%s:dns_failed", ctx->use_tls ? "connect_tls_failed" : "connect_failed");
            } else if (ctx->use_tls) {
                char detail[128];
                (void)snprintf(detail, sizeof(detail), "connect_tls_failed:%.100s", worker_error);
                (void)snprintf(worker_error, sizeof(worker_error), "%s", detail);
            } else {
                char detail[128];
                (void)snprintf(detail, sizeof(detail), "connect_failed:%.100s", worker_error);
                (void)snprintf(worker_error, sizeof(worker_error), "%s", detail);
            }
            goto finalize;
        }
        if (inet_ntop(AF_INET, &addr.sin_addr, resolved_ip, sizeof(resolved_ip)) == NULL) {
            resolved_ip[0] = '\0';
        }
    }
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == NATIVE_INVALID_SOCKET) {
        (void)snprintf(worker_error, sizeof(worker_error), "%s:socket_errno_%d", ctx->use_tls ? "connect_tls_failed" : "connect_failed", errno);
        goto finalize;
    }
    if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        int connect_errno = errno;
        native_socket_close(socket_fd);
        socket_fd = NATIVE_INVALID_SOCKET;
        (void)snprintf(worker_error, sizeof(worker_error), "%s:connect_errno_%d", ctx->use_tls ? "connect_tls_failed" : "connect_failed", connect_errno);
        goto finalize;
    }
    if (ctx->use_tls) {
#ifdef __APPLE__
        NativeNetHandleState temp_state;
        memset(&temp_state, 0, sizeof(temp_state));
        temp_state.kind = NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM;
        temp_state.socket = socket_fd;
        tls_state = native_tls_stream_create(socket_fd, ctx->host);
        if (tls_state == NULL) {
            native_socket_close(socket_fd);
            socket_fd = NATIVE_INVALID_SOCKET;
            (void)snprintf(worker_error, sizeof(worker_error), "connect_tls_failed:tls_create");
            goto finalize;
        }
        temp_state.tls_state = tls_state;
        if (native_tls_stream_handshake(&temp_state) != 1) {
            native_tls_stream_destroy((NativeTlsStreamState*)tls_state);
            native_socket_close(socket_fd);
            tls_state = NULL;
            socket_fd = NATIVE_INVALID_SOCKET;
            (void)snprintf(worker_error, sizeof(worker_error), "connect_tls_failed:tls_handshake");
            goto finalize;
        }
#else
        native_socket_close(socket_fd);
        socket_fd = NATIVE_INVALID_SOCKET;
        (void)snprintf(worker_error, sizeof(worker_error), "connect_tls_failed:tls_unsupported");
        goto finalize;
#endif
    }
    worker_result_status = 1;
finalize:
    if (op->used == 0 || op->generation != ctx->generation || op->canceled != 0) {
        airun_log_message(AIRUN_LOG_INFO, "net.async", "op=%lld worker-discarded used=%d canceled=%d gen=%llu expected=%llu",
            (long long)((op - g_native_net_async_ops) + 1),
            op->used,
            op->canceled,
            (unsigned long long)op->generation,
            (unsigned long long)ctx->generation);
#ifdef __APPLE__
        if (tls_state != NULL) {
            native_tls_stream_destroy((NativeTlsStreamState*)tls_state);
            tls_state = NULL;
        }
#endif
        if (socket_fd != NATIVE_INVALID_SOCKET) {
            native_socket_close(socket_fd);
        }
        return;
    }
    op->worker_result_status = worker_result_status;
    (void)snprintf(op->worker_error, sizeof(op->worker_error), "%s", worker_error);
    (void)snprintf(op->resolved_ip, sizeof(op->resolved_ip), "%s", resolved_ip);
    op->worker_socket = socket_fd;
    op->worker_tls_state = tls_state;
    op->worker_done = 1;
    airun_log_message(AIRUN_LOG_INFO, "net.async", "op=%lld worker-finished result=%d error=%s",
        (long long)((op - g_native_net_async_ops) + 1),
        worker_result_status,
        worker_error);
}

#ifdef _WIN32
static DWORD WINAPI native_net_async_connect_thread(void* arg)
{
    NativeNetAsyncConnectWorkerContext* ctx = (NativeNetAsyncConnectWorkerContext*)arg;
    native_net_async_worker_connect_execute(ctx);
    free(ctx);
    return 0;
}
#else
static void* native_net_async_connect_thread(void* arg)
{
    NativeNetAsyncConnectWorkerContext* ctx = (NativeNetAsyncConnectWorkerContext*)arg;
    native_net_async_worker_connect_execute(ctx);
    free(ctx);
    return NULL;
}
#endif

static void native_net_async_finalize_connect_worker(NativeNetAsyncState* op)
{
    int64_t handle;
    NativeNetHandleState* state;
    if (op == NULL || op->worker_done == 0 || op->worker_joined != 0) {
        return;
    }
#ifdef _WIN32
    if (op->worker_thread != NULL) {
        (void)WaitForSingleObject(op->worker_thread, INFINITE);
        CloseHandle(op->worker_thread);
        op->worker_thread = NULL;
    }
#else
    (void)pthread_join(op->worker_thread, NULL);
#endif
    op->worker_joined = 1;
    if (op->status == -2 || op->canceled != 0) {
        native_net_async_close_worker_socket(op);
        return;
    }
    if (op->worker_result_status != 1) {
        native_net_async_set_failure(op, op->worker_error[0] != '\0' ? op->worker_error : (op->use_tls ? "connect_tls_failed" : "connect_failed"));
        return;
    }
    if (!native_net_socket_set_nonblocking(op->worker_socket)) {
        native_net_async_close_worker_socket(op);
        native_net_async_set_failure(op, op->use_tls ? "connect_tls_failed" : "connect_failed");
        return;
    }
    handle = native_net_handle_allocate(
        op->use_tls ? NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM : NATIVE_NET_HANDLE_KIND_TCP_STREAM,
        op->worker_socket);
    if (handle < 0) {
        native_net_async_close_worker_socket(op);
        native_net_async_set_failure(op, op->use_tls ? "connect_tls_failed" : "connect_failed");
        return;
    }
    state = native_net_handle_lookup(handle);
    if (state == NULL) {
        native_net_async_close_worker_socket(op);
        native_net_async_set_failure(op, op->use_tls ? "connect_tls_failed" : "connect_failed");
        return;
    }
    state->tls_state = op->worker_tls_state;
    op->worker_socket = NATIVE_INVALID_SOCKET;
    op->worker_tls_state = NULL;
    op->socket_handle = handle;
    native_net_async_set_success_int(op, handle);
}

static void native_net_async_maybe_finalize_worker(NativeNetAsyncState* op)
{
    if (op == NULL || op->kind != 1) {
        return;
    }
    if (op->worker_done != 0 && op->worker_joined == 0) {
        airun_log_message(AIRUN_LOG_TRACE, "net.async", "op=%lld finalize-worker",
            (long long)((op - g_native_net_async_ops) + 1));
        native_net_async_finalize_connect_worker(op);
    }
}

static void native_net_async_process(NativeNetAsyncState* op)
{
    NativeNetHandleState* state;
    int use_select;
    if (op == NULL || op->status != 0) {
        return;
    }
    airun_log_message(AIRUN_LOG_TRACE, "net.async", "op=%lld process kind=%d status=%d socket=%lld",
        (long long)((op - g_native_net_async_ops) + 1),
        op->kind,
        op->status,
        (long long)op->socket_handle);
    if (op->kind == 1) {
        native_net_async_maybe_finalize_worker(op);
        if (op->status != 0) {
            return;
        }
        if (op->socket_handle == 0) {
            return;
        }
    }
    state = native_net_handle_lookup(op->socket_handle);
    if (state == NULL ||
        (state->kind != NATIVE_NET_HANDLE_KIND_TCP_STREAM &&
         state->kind != NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM)) {
        native_net_async_set_failure(op, "invalid_handle");
        return;
    }
    use_select = (state->kind == NATIVE_NET_HANDLE_KIND_TCP_STREAM);
    if (use_select) {
        fd_set read_set;
        fd_set write_set;
        struct timeval timeout;
        int select_rc;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;
        if (op->kind == 3) {
            FD_SET(state->socket, &write_set);
        }
        if (op->kind == 2) {
            FD_SET(state->socket, &read_set);
        }
#ifdef _WIN32
        select_rc = select(0, &read_set, &write_set, NULL, &timeout);
#else
        select_rc = select((int)state->socket + 1, &read_set, &write_set, NULL, &timeout);
#endif
        if (select_rc <= 0) {
            return;
        }
    }
    if (op->kind == 2) {
        int max_bytes = op->max_bytes;
        size_t read_count = 0U;
        int io_status;
        int closed = 0;
        if (max_bytes <= 0) {
            max_bytes = 1;
        }
        if ((size_t)max_bytes > NATIVE_NET_BYTES_CHUNK) {
            max_bytes = (int)NATIVE_NET_BYTES_CHUNK;
        }
        io_status = native_net_stream_read(state, g_native_net_bytes_scratch, (size_t)max_bytes, &read_count, &closed);
        if (io_status == 0) {
            return;
        }
        if (io_status < 0) {
            native_net_async_set_failure(op, "read_failed");
            return;
        }
        native_net_async_set_success_bytes(op, g_native_net_bytes_scratch, read_count);
        return;
    }
    if (op->kind == 3) {
        size_t remaining = op->pending_bytes_len - op->pending_bytes_offset;
        size_t wrote = 0U;
        int io_status;
        if (remaining == 0U) {
            native_net_async_set_success_int(op, (int64_t)op->pending_bytes_len);
            return;
        }
        io_status = native_net_stream_write(
            state,
            op->pending_bytes + op->pending_bytes_offset,
            remaining,
            &wrote);
        if (io_status == 0) {
            return;
        }
        if (io_status < 0) {
            native_net_async_set_failure(op, "write_failed");
            return;
        }
        if (wrote == 0U) {
            native_net_async_set_failure(op, "write_failed");
            return;
        }
        op->pending_bytes_offset += wrote;
        if (op->pending_bytes_offset >= op->pending_bytes_len) {
            native_net_async_set_success_int(op, (int64_t)op->pending_bytes_len);
        }
    }
}

#ifdef _WIN32
static void native_process_refresh(NativeProcessState* process)
{
    DWORD wait_status;
    DWORD exit_code;
    if (process == NULL || process->finished || process->process_handle == NULL) {
        return;
    }
    wait_status = WaitForSingleObject(process->process_handle, 0);
    if (wait_status == WAIT_OBJECT_0) {
        process->finished = 1;
        if (GetExitCodeProcess(process->process_handle, &exit_code) != 0) {
            process->exit_code = (int)exit_code;
        } else {
            process->exit_code = -1;
        }
    }
}
#else
static void native_process_set_nonblocking(int fd)
{
    int flags;
    if (fd < 0) {
        return;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static void native_process_refresh(NativeProcessState* process)
{
    int status;
    pid_t wait_result;
    if (process == NULL || process->finished) {
        return;
    }
    wait_result = waitpid(process->pid, &status, WNOHANG);
    if (wait_result == process->pid) {
        process->finished = 1;
        if (WIFEXITED(status)) {
            process->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            process->exit_code = 128 + WTERMSIG(status);
        } else {
            process->exit_code = -1;
        }
    }
}
#endif

static int native_syscall_stdout_write_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    printf("%s\n", args[0].string_value);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_console_write(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    fputs(args[0].string_value, stdout);
    fflush(stdout);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_console_write_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    return native_syscall_stdout_write_line(target, args, arg_count, result);
}

static int native_syscall_console_write_err_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    fprintf(stderr, "%s\n", args[0].string_value);
    fflush(stderr);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_console_read_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static char line[4096];
    size_t len;
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (fgets(line, (int)sizeof(line), stdin) == NULL) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    len = strlen(line);
    while (len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
        line[len - 1U] = '\0';
        len -= 1U;
    }
    *result = aivm_value_string(line);
    return AIVM_SYSCALL_OK;
}

static char* g_native_stdin_all_scratch = NULL;
static size_t g_native_stdin_all_scratch_capacity = 0U;

static int native_syscall_console_read_all_stdin(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t used = 0U;
    int ch;
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (g_native_stdin_all_scratch == NULL) {
        g_native_stdin_all_scratch_capacity = 4096U;
        g_native_stdin_all_scratch = (char*)malloc(g_native_stdin_all_scratch_capacity);
        if (g_native_stdin_all_scratch == NULL) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
    }
    while ((ch = fgetc(stdin)) != EOF) {
        if (used + 2U > g_native_stdin_all_scratch_capacity) {
            char* grown;
            size_t new_capacity = g_native_stdin_all_scratch_capacity * 2U;
            if (new_capacity < used + 2U) {
                new_capacity = used + 2U;
            }
            grown = (char*)realloc(g_native_stdin_all_scratch, new_capacity);
            if (grown == NULL) {
                result->type = AIVM_VAL_VOID;
                return AIVM_SYSCALL_ERR_INVALID;
            }
            g_native_stdin_all_scratch = grown;
            g_native_stdin_all_scratch_capacity = new_capacity;
        }
        g_native_stdin_all_scratch[used++] = (char)ch;
    }
    g_native_stdin_all_scratch[used] = '\0';
    *result = aivm_value_string(g_native_stdin_all_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_vm_lookup_node_record(
    const AivmVm* vm,
    int64_t handle,
    const AivmNodeRecord** out_node)
{
    size_t index;
    if (vm == NULL || out_node == NULL || handle <= 0) {
        return 0;
    }
    index = (size_t)(handle - 1);
    if (index >= vm->node_count) {
        return 0;
    }
    *out_node = &vm->nodes[index];
    return 1;
}

static const char* native_vm_node_first_string_attr(const AivmVm* vm, int64_t node_handle)
{
    const AivmNodeRecord* node;
    size_t i;
    if (!native_vm_lookup_node_record(vm, node_handle, &node)) {
        return NULL;
    }
    for (i = 0U; i < node->attr_count; i += 1U) {
        const AivmNodeAttr* attr = &vm->node_attrs[node->attr_start + i];
        if (attr->kind == AIVM_NODE_ATTR_STRING && attr->string_value != NULL) {
            return attr->string_value;
        }
    }
    return NULL;
}

static AivmRemoteServerConfig g_remote_server_config;
static AivmRemoteServerSession g_remote_server_session;
static uint32_t g_remote_next_request_id = 1U;
static int g_remote_session_ready = 0;

static int remote_parse_caps_csv(
    const char* csv,
    char out_caps[AIVM_REMOTE_MAX_CAPS][AIVM_REMOTE_MAX_TEXT + 1],
    uint32_t* out_count)
{
    const char* cursor;
    uint32_t count = 0U;
    if (out_caps == NULL || out_count == NULL) {
        return 0;
    }
    if (csv == NULL || *csv == '\0') {
        *out_count = 0U;
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

static int remote_token_authorized(void)
{
    const char* expected = getenv("AIVM_REMOTE_EXPECTED_TOKEN");
    const char* provided = getenv("AIVM_REMOTE_SESSION_TOKEN");
    size_t expected_len;
    size_t provided_len;
    if (expected == NULL || provided == NULL) {
        return 0;
    }
    expected_len = strlen(expected);
    provided_len = strlen(provided);
    if (expected_len == 0U || provided_len == 0U) {
        return 0;
    }
    if (expected_len > 256U || provided_len > 256U) {
        return 0;
    }
    return strcmp(expected, provided) == 0;
}

static int remote_session_ensure_ready(void)
{
    uint8_t request_bytes[1024];
    uint8_t response_bytes[1024];
    size_t request_len = 0U;
    size_t response_len = 0U;
    AivmRemoteHello hello;
    AivmRemoteWelcome welcome;
    uint32_t response_id = 0U;
    AivmRemoteCodecStatus codec_status;
    AivmRemoteSessionStatus session_status;
    uint32_t i;
    const char* caps_env;

    if (g_remote_session_ready) {
        return 1;
    }

    memset(&g_remote_server_config, 0, sizeof(g_remote_server_config));
    g_remote_server_config.proto_version = 1U;
    caps_env = getenv("AIVM_REMOTE_CAPS");
    if (!remote_parse_caps_csv(
            caps_env,
            g_remote_server_config.allowed_caps,
            &g_remote_server_config.allowed_caps_count)) {
        return 0;
    }

    aivm_remote_server_session_init(&g_remote_server_session);
    memset(&hello, 0, sizeof(hello));
    hello.proto_version = 1U;
    (void)snprintf(hello.client_name, sizeof(hello.client_name), "%s", "airun-native");
    hello.requested_caps_count = g_remote_server_config.allowed_caps_count;
    for (i = 0U; i < hello.requested_caps_count; i += 1U) {
        (void)snprintf(
            hello.requested_caps[i],
            sizeof(hello.requested_caps[i]),
            "%s",
            g_remote_server_config.allowed_caps[i]);
    }

    codec_status = aivm_remote_encode_hello(
        1U,
        &hello,
        request_bytes,
        sizeof(request_bytes),
        &request_len);
    if (codec_status != AIVM_REMOTE_CODEC_OK) {
        return 0;
    }
    session_status = aivm_remote_server_process_frame(
        &g_remote_server_config,
        &g_remote_server_session,
        request_bytes,
        request_len,
        response_bytes,
        sizeof(response_bytes),
        &response_len);
    if (session_status != AIVM_REMOTE_SESSION_OK) {
        return 0;
    }
    memset(&welcome, 0, sizeof(welcome));
    codec_status = aivm_remote_decode_welcome(
        response_bytes,
        response_len,
        &response_id,
        &welcome);
    if (codec_status != AIVM_REMOTE_CODEC_OK || response_id != 1U) {
        return 0;
    }
    g_remote_session_ready = 1;
    return 1;
}

static int remote_session_invoke_call(
    const char* cap,
    const char* op,
    int64_t value,
    AivmValue* result)
{
    uint8_t request_bytes[1024];
    uint8_t response_bytes[1024];
    size_t request_len = 0U;
    size_t response_len = 0U;
    uint32_t request_id;
    uint32_t response_id = 0U;
    AivmRemoteCall call;
    AivmRemoteResult call_result;
    AivmRemoteError call_error;
    AivmRemoteCodecStatus codec_status;
    AivmRemoteSessionStatus session_status;

    if (cap == NULL || op == NULL || result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (!remote_session_ensure_ready()) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }

    memset(&call, 0, sizeof(call));
    (void)snprintf(call.cap, sizeof(call.cap), "%s", cap);
    (void)snprintf(call.op, sizeof(call.op), "%s", op);
    call.value = value;
    request_id = g_remote_next_request_id++;
    if (g_remote_next_request_id == 0U) {
        g_remote_next_request_id = 1U;
    }

    codec_status = aivm_remote_encode_call(
        request_id,
        &call,
        request_bytes,
        sizeof(request_bytes),
        &request_len);
    if (codec_status != AIVM_REMOTE_CODEC_OK) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    session_status = aivm_remote_server_process_frame(
        &g_remote_server_config,
        &g_remote_server_session,
        request_bytes,
        request_len,
        response_bytes,
        sizeof(response_bytes),
        &response_len);
    if (session_status != AIVM_REMOTE_SESSION_OK) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }

    codec_status = aivm_remote_decode_result(
        response_bytes,
        response_len,
        &response_id,
        &call_result);
    if (codec_status == AIVM_REMOTE_CODEC_OK && response_id == request_id) {
        *result = aivm_value_int(call_result.value);
        return AIVM_SYSCALL_OK;
    }
    codec_status = aivm_remote_decode_error(
        response_bytes,
        response_len,
        &response_id,
        &call_error);
    if (codec_status == AIVM_REMOTE_CODEC_OK && response_id == request_id) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }

    result->type = AIVM_VAL_VOID;
    return AIVM_SYSCALL_ERR_INVALID;
}

static int native_syscall_remote_call(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* cap;
    const char* op;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 3U ||
        args == NULL ||
        args[0].type != AIVM_VAL_STRING ||
        args[1].type != AIVM_VAL_STRING ||
        args[2].type != AIVM_VAL_INT ||
        args[0].string_value == NULL ||
        args[1].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    cap = args[0].string_value;
    op = args[1].string_value;
    if (strlen(cap) > 64U || strlen(op) > 64U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!remote_token_authorized()) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    return remote_session_invoke_call(cap, op, args[2].int_value, result);
}

static int native_syscall_net_tcp_listen(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    struct sockaddr_in addr;
    NativeSocket socket_fd;
    int64_t handle;
    int reuse = 1;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 2U ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_net_platform_init() ||
        args[1].int_value <= 0 || args[1].int_value > 65535 ||
        !native_net_resolve_ipv4(args[0].string_value, (uint16_t)args[1].int_value, SOCK_STREAM, 1, &addr)) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == NATIVE_INVALID_SOCKET) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(socket_fd, 16) != 0) {
        native_socket_close(socket_fd);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    handle = native_net_handle_allocate(NATIVE_NET_HANDLE_KIND_TCP_LISTENER, socket_fd);
    if (handle < 0) {
        native_socket_close(socket_fd);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_int(handle);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_dns_lookup(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    struct sockaddr_in addr;
    char error_text[128];
    char ip_text[INET_ADDRSTRLEN];
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_net_platform_init()) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    error_text[0] = '\0';
    if (!native_net_resolve_ipv4_detail(args[0].string_value, 443U, SOCK_STREAM, 0, &addr, error_text, sizeof(error_text))) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    if (inet_ntop(AF_INET, &addr.sin_addr, ip_text, sizeof(ip_text)) == NULL) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_string(ip_text);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_tcp_accept(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetHandleState* listener;
    NativeSocket accepted;
    int64_t handle;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    listener = native_net_handle_lookup(args[0].int_value);
    if (listener == NULL || listener->kind != NATIVE_NET_HANDLE_KIND_TCP_LISTENER) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    accepted = accept(listener->socket, NULL, NULL);
    if (accepted == NATIVE_INVALID_SOCKET) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    handle = native_net_handle_allocate(NATIVE_NET_HANDLE_KIND_TCP_STREAM, accepted);
    if (handle < 0) {
        native_socket_close(accepted);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_int(handle);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_tcp_connect(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    struct sockaddr_in addr;
    NativeSocket socket_fd;
    int64_t handle;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 2U ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_net_platform_init() ||
        args[1].int_value <= 0 || args[1].int_value > 65535 ||
        !native_net_resolve_ipv4(args[0].string_value, (uint16_t)args[1].int_value, SOCK_STREAM, 0, &addr)) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == NATIVE_INVALID_SOCKET) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        native_socket_close(socket_fd);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    handle = native_net_handle_allocate(NATIVE_NET_HANDLE_KIND_TCP_STREAM, socket_fd);
    if (handle < 0) {
        native_socket_close(socket_fd);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_int(handle);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_tcp_connect_tls(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    struct sockaddr_in addr;
    NativeSocket socket_fd;
    int64_t handle;
    NativeNetHandleState* state;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 2U ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_net_platform_init() ||
        args[1].int_value <= 0 || args[1].int_value > 65535 ||
        !native_net_resolve_ipv4(args[0].string_value, (uint16_t)args[1].int_value, SOCK_STREAM, 0, &addr)) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == NATIVE_INVALID_SOCKET) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        native_socket_close(socket_fd);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    handle = native_net_handle_allocate(NATIVE_NET_HANDLE_KIND_TCP_STREAM, socket_fd);
    if (handle < 0) {
        native_socket_close(socket_fd);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    state = native_net_handle_lookup(handle);
    if (state == NULL) {
        (void)native_net_handle_close(handle);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
#ifdef __APPLE__
    state->tls_state = native_tls_stream_create(socket_fd, args[0].string_value);
    if (state->tls_state == NULL) {
        (void)native_net_handle_close(handle);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    state->kind = NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM;
    if (native_tls_stream_handshake(state) != 1) {
        (void)native_net_handle_close(handle);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_int(handle);
    return AIVM_SYSCALL_OK;
#else
    (void)native_net_handle_close(handle);
    result->type = AIVM_VAL_VOID;
    return AIVM_SYSCALL_ERR_INVALID;
#endif
}

static int native_syscall_net_tcp_listen_tls(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 4U ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_INT ||
        args[2].type != AIVM_VAL_STRING || args[2].string_value == NULL ||
        args[3].type != AIVM_VAL_STRING || args[3].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    result->type = AIVM_VAL_VOID;
    return AIVM_SYSCALL_ERR_INVALID;
}

static int native_syscall_net_tcp_connect_tls_start(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    return native_syscall_net_start_op(target, args, arg_count, result);
}

static int native_syscall_net_tcp_read(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetHandleState* state;
    int max_bytes;
    size_t read_count;
    int closed;
    int io_status;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 2U || args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    state = native_net_handle_lookup(args[0].int_value);
    if (state == NULL ||
        (state->kind != NATIVE_NET_HANDLE_KIND_TCP_STREAM &&
         state->kind != NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM)) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    max_bytes = (int)args[1].int_value;
    if (max_bytes <= 0) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    if ((size_t)max_bytes > NATIVE_NET_BYTES_CHUNK) {
        max_bytes = (int)NATIVE_NET_BYTES_CHUNK;
    }
    io_status = native_net_stream_read(state, g_native_net_bytes_scratch, (size_t)max_bytes, &read_count, &closed);
    if (io_status <= 0 || read_count == 0U) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_bytes(g_native_net_bytes_scratch, read_count);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_tcp_write(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetHandleState* state;
    size_t wrote;
    int io_status;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 2U ||
        args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_BYTES) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    state = native_net_handle_lookup(args[0].int_value);
    if (state == NULL ||
        (state->kind != NATIVE_NET_HANDLE_KIND_TCP_STREAM &&
         state->kind != NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM)) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    if (args[1].bytes_value.length == 0U || args[1].bytes_value.data == NULL) {
        *result = aivm_value_int(0);
        return AIVM_SYSCALL_OK;
    }
    io_status = native_net_stream_write(state, args[1].bytes_value.data, args[1].bytes_value.length, &wrote);
    *result = aivm_value_int((io_status < 0) ? -1 : (int64_t)wrote);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_tcp_close(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    (void)native_net_handle_close(args[0].int_value);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_udp_bind(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    struct sockaddr_in addr;
    NativeSocket socket_fd;
    int64_t handle;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 2U ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_net_platform_init() ||
        args[1].int_value <= 0 || args[1].int_value > 65535 ||
        !native_net_resolve_ipv4(args[0].string_value, (uint16_t)args[1].int_value, SOCK_DGRAM, 1, &addr)) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd == NATIVE_INVALID_SOCKET) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        native_socket_close(socket_fd);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    handle = native_net_handle_allocate(NATIVE_NET_HANDLE_KIND_UDP_SOCKET, socket_fd);
    if (handle < 0) {
        native_socket_close(socket_fd);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_int(handle);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_udp_send(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetHandleState* state;
    struct sockaddr_in addr;
    int wrote;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 4U ||
        args[0].type != AIVM_VAL_INT ||
        args[1].type != AIVM_VAL_STRING || args[1].string_value == NULL ||
        args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_BYTES) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    state = native_net_handle_lookup(args[0].int_value);
    if (state == NULL || state->kind != NATIVE_NET_HANDLE_KIND_UDP_SOCKET ||
        args[2].int_value <= 0 || args[2].int_value > 65535 ||
        !native_net_resolve_ipv4(args[1].string_value, (uint16_t)args[2].int_value, SOCK_DGRAM, 0, &addr)) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
#ifdef _WIN32
    wrote = sendto(
        state->socket,
        (const char*)args[3].bytes_value.data,
        (int)args[3].bytes_value.length,
        0,
        (struct sockaddr*)&addr,
        (int)sizeof(addr));
#else
    wrote = (int)sendto(
        state->socket,
        args[3].bytes_value.data,
        args[3].bytes_value.length,
        0,
        (struct sockaddr*)&addr,
        sizeof(addr));
#endif
    *result = aivm_value_int((wrote < 0) ? -1 : (int64_t)wrote);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_udp_recv(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetHandleState* state;
    struct sockaddr_in peer_addr;
    socklen_t peer_len = (socklen_t)sizeof(peer_addr);
    int read_count;
    int max_bytes;
    int64_t node_handle;
    AivmNodeAttr attrs[4];
    uint32_t ip;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 2U || args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (g_native_active_vm == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    state = native_net_handle_lookup(args[0].int_value);
    if (state == NULL || state->kind != NATIVE_NET_HANDLE_KIND_UDP_SOCKET) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    max_bytes = (int)args[1].int_value;
    if (max_bytes <= 0) {
        max_bytes = 1;
    }
    if ((size_t)max_bytes > NATIVE_NET_BYTES_CHUNK) {
        max_bytes = (int)NATIVE_NET_BYTES_CHUNK;
    }
    memset(&peer_addr, 0, sizeof(peer_addr));
#ifdef _WIN32
    read_count = recvfrom(
        state->socket,
        (char*)g_native_net_bytes_scratch,
        max_bytes,
        0,
        (struct sockaddr*)&peer_addr,
        (int*)&peer_len);
#else
    read_count = (int)recvfrom(
        state->socket,
        g_native_net_bytes_scratch,
        (size_t)max_bytes,
        0,
        (struct sockaddr*)&peer_addr,
        &peer_len);
#endif
    if (read_count < 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    ip = ntohl(peer_addr.sin_addr.s_addr);
    (void)snprintf(
        g_native_net_host_scratch,
        sizeof(g_native_net_host_scratch),
        "%u.%u.%u.%u",
        (unsigned)((ip >> 24) & 0xFFU),
        (unsigned)((ip >> 16) & 0xFFU),
        (unsigned)((ip >> 8) & 0xFFU),
        (unsigned)(ip & 0xFFU));
    (void)snprintf(
        g_native_net_text_scratch,
        sizeof(g_native_net_text_scratch),
        "%u.%u.%u.%u",
        (unsigned)((ip >> 24) & 0xFFU),
        (unsigned)((ip >> 16) & 0xFFU),
        (unsigned)((ip >> 8) & 0xFFU),
        (unsigned)(ip & 0xFFU));
    attrs[0].key = "host";
    attrs[0].kind = AIVM_NODE_ATTR_STRING;
    attrs[0].string_value = g_native_net_host_scratch;
    attrs[1].key = "port";
    attrs[1].kind = AIVM_NODE_ATTR_INT;
    attrs[1].int_value = (int64_t)ntohs(peer_addr.sin_port);
    attrs[2].key = "length";
    attrs[2].kind = AIVM_NODE_ATTR_INT;
    attrs[2].int_value = (int64_t)read_count;
    if (!native_bytes_to_base64(g_native_net_bytes_scratch, (size_t)read_count, g_native_net_text_scratch, sizeof(g_native_net_text_scratch))) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    attrs[3].key = "dataBase64";
    attrs[3].kind = AIVM_NODE_ATTR_STRING;
    attrs[3].string_value = g_native_net_text_scratch;
    if (!native_vm_append_host_node(g_native_active_vm, "Map", "net_udp_recv", attrs, 4U, &node_handle)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_node(node_handle);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_async_poll(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetAsyncState* op;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    op = native_net_async_lookup(args[0].int_value);
    if (op != NULL && op->status == 0) {
        airun_log_message(AIRUN_LOG_TRACE, "net.async", "op=%lld poll-enter", (long long)args[0].int_value);
        native_net_async_process(op);
        if (op->status == 0) {
#ifdef _WIN32
            Sleep(1);
#else
            usleep(1000);
#endif
        }
    }
    *result = aivm_value_int((op == NULL) ? -3 : (int64_t)op->status);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_async_cancel(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetAsyncState* op;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    op = native_net_async_lookup(args[0].int_value);
    if (op == NULL || op->status != 0) {
        *result = aivm_value_bool(0);
        return AIVM_SYSCALL_OK;
    }
    op->canceled = 1;
    airun_log_message(AIRUN_LOG_INFO, "net.async", "op=%lld cancel", (long long)args[0].int_value);
    native_net_async_maybe_finalize_worker(op);
    native_net_async_cancel_pending_socket(op);
    native_net_async_release_pending_bytes(op);
    op->status = -2;
    *result = aivm_value_bool(1);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_async_await(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetAsyncState* op;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    op = native_net_async_lookup(args[0].int_value);
    if (op == NULL) {
        *result = aivm_value_int(-3);
        return AIVM_SYSCALL_OK;
    }
    while (op->status == 0) {
        native_net_async_process(op);
        native_net_async_maybe_finalize_worker(op);
        if (op->status != 0) {
            break;
        }
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }
    native_net_async_maybe_finalize_worker(op);
    *result = aivm_value_int((int64_t)op->status);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_async_result_int(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetAsyncState* op;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    op = native_net_async_lookup(args[0].int_value);
    native_net_async_maybe_finalize_worker(op);
    *result = aivm_value_int((op == NULL || op->status != 1) ? 0 : op->result_int);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_async_result_bytes(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetAsyncState* op;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    op = native_net_async_lookup(args[0].int_value);
    native_net_async_maybe_finalize_worker(op);
    if (op == NULL || op->status != 1 || op->result_bytes == NULL || op->result_bytes_len == 0U) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_bytes(op->result_bytes, op->result_bytes_len);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_async_error(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    NativeNetAsyncState* op;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    op = native_net_async_lookup(args[0].int_value);
    native_net_async_maybe_finalize_worker(op);
    *result = aivm_value_string((op == NULL || op->status != -1) ? "" : op->error);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_net_start_op(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int is_supported_target;
    int64_t op_handle;
    NativeNetAsyncState* op;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    is_supported_target =
        strcmp(target, "sys.net.tcp.connectStart") == 0 ||
        strcmp(target, "sys.net.tcp.connectTlsStart") == 0 ||
        strcmp(target, "sys.net.tcp.readStart") == 0 ||
        strcmp(target, "sys.net.tcp.writeStart") == 0;
    if (!is_supported_target) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    op_handle = native_net_async_allocate();
    if (op_handle < 0) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    op = native_net_async_lookup(op_handle);
    if (op == NULL) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    if (strcmp(target, "sys.net.tcp.connectStart") == 0 ||
        strcmp(target, "sys.net.tcp.connectTlsStart") == 0) {
        NativeNetAsyncConnectWorkerContext* worker_ctx;
        if (args == NULL || arg_count != 2U ||
            args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL ||
            args[1].type != AIVM_VAL_INT ||
            !native_net_platform_init() ||
            args[1].int_value <= 0 || args[1].int_value > 65535) {
            native_net_async_set_failure(op, "connect_failed");
            *result = aivm_value_int(op_handle);
            return AIVM_SYSCALL_OK;
        }
        op->kind = 1;
        op->use_tls = (strcmp(target, "sys.net.tcp.connectTlsStart") == 0);
        op->port = (int)args[1].int_value;
        (void)snprintf(op->host, sizeof(op->host), "%s", args[0].string_value);
        op->socket_handle = 0;
        op->worker_done = 0;
        op->worker_joined = 0;
        op->worker_result_status = 0;
        op->canceled = 0;
        op->worker_socket = NATIVE_INVALID_SOCKET;
        op->worker_tls_state = NULL;
        airun_log_message(AIRUN_LOG_INFO, "net.async", "op=%lld start target=%s host=%s port=%d tls=%d",
            (long long)op_handle,
            target,
            op->host,
            op->port,
            op->use_tls);
        worker_ctx = (NativeNetAsyncConnectWorkerContext*)calloc(1U, sizeof(*worker_ctx));
        if (worker_ctx == NULL) {
            native_net_async_set_failure(op, op->use_tls ? "connect_tls_failed" : "connect_failed");
            *result = aivm_value_int(op_handle);
            return AIVM_SYSCALL_OK;
        }
        worker_ctx->op = op;
        worker_ctx->generation = op->generation;
        worker_ctx->port = op->port;
        worker_ctx->use_tls = op->use_tls;
        (void)snprintf(worker_ctx->host, sizeof(worker_ctx->host), "%s", op->host);
        if (native_net_resolve_ipv4_detail(op->host, (uint16_t)op->port, SOCK_STREAM, 0, &worker_ctx->resolved_addr, op->worker_error, sizeof(op->worker_error))) {
            worker_ctx->has_resolved_addr = 1;
            if (inet_ntop(AF_INET, &worker_ctx->resolved_addr.sin_addr, worker_ctx->resolved_ip, sizeof(worker_ctx->resolved_ip)) == NULL) {
                worker_ctx->resolved_ip[0] = '\0';
            }
        } else {
            char detail[128];
            worker_ctx->has_resolved_addr = 0;
            if (op->worker_error[0] == '\0') {
                (void)snprintf(op->worker_error, sizeof(op->worker_error), "%s", op->use_tls ? "connect_tls_failed:dns_failed" : "connect_failed:dns_failed");
            } else if (op->use_tls) {
                (void)snprintf(detail, sizeof(detail), "connect_tls_failed:%.100s", op->worker_error);
                (void)snprintf(op->worker_error, sizeof(op->worker_error), "%s", detail);
            } else {
                (void)snprintf(detail, sizeof(detail), "connect_failed:%.100s", op->worker_error);
                (void)snprintf(op->worker_error, sizeof(op->worker_error), "%s", detail);
            }
            free(worker_ctx);
            native_net_async_set_failure(op, op->worker_error);
            *result = aivm_value_int(op_handle);
            return AIVM_SYSCALL_OK;
        }
#ifdef _WIN32
        op->worker_thread = CreateThread(NULL, 0, native_net_async_connect_thread, worker_ctx, 0, NULL);
        if (op->worker_thread == NULL) {
            free(worker_ctx);
            native_net_async_set_failure(op, op->use_tls ? "connect_tls_failed" : "connect_failed");
        }
#else
        if (pthread_create(&op->worker_thread, NULL, native_net_async_connect_thread, worker_ctx) != 0) {
            free(worker_ctx);
            native_net_async_set_failure(op, op->use_tls ? "connect_tls_failed" : "connect_failed");
        }
#endif
    } else if (strcmp(target, "sys.net.tcp.readStart") == 0) {
        NativeNetHandleState* state;
        if (args == NULL || arg_count != 2U || args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_INT) {
            native_net_async_set_failure(op, "read_failed");
            *result = aivm_value_int(op_handle);
            return AIVM_SYSCALL_OK;
        }
        state = native_net_handle_lookup(args[0].int_value);
        if (state == NULL ||
            (state->kind != NATIVE_NET_HANDLE_KIND_TCP_STREAM &&
             state->kind != NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM) ||
            !native_net_socket_set_nonblocking(state->socket)) {
            native_net_async_set_failure(op, "read_failed");
            *result = aivm_value_int(op_handle);
            return AIVM_SYSCALL_OK;
        }
        op->kind = 2;
        op->socket_handle = args[0].int_value;
        op->max_bytes = (int)args[1].int_value;
        airun_log_message(AIRUN_LOG_INFO, "net.async", "op=%lld start target=%s socket=%lld maxBytes=%d",
            (long long)op_handle,
            target,
            (long long)op->socket_handle,
            op->max_bytes);
        native_net_async_process(op);
    } else if (strcmp(target, "sys.net.tcp.writeStart") == 0) {
        NativeNetHandleState* state;
        if (args == NULL || arg_count != 2U || args[0].type != AIVM_VAL_INT || args[1].type != AIVM_VAL_BYTES) {
            native_net_async_set_failure(op, "write_failed");
            *result = aivm_value_int(op_handle);
            return AIVM_SYSCALL_OK;
        }
        state = native_net_handle_lookup(args[0].int_value);
        if (state == NULL ||
            (state->kind != NATIVE_NET_HANDLE_KIND_TCP_STREAM &&
             state->kind != NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM) ||
            !native_net_socket_set_nonblocking(state->socket)) {
            native_net_async_set_failure(op, "write_failed");
            *result = aivm_value_int(op_handle);
            return AIVM_SYSCALL_OK;
        }
        op->kind = 3;
        op->socket_handle = args[0].int_value;
        airun_log_message(AIRUN_LOG_INFO, "net.async", "op=%lld start target=%s socket=%lld pendingBytes=%llu",
            (long long)op_handle,
            target,
            (long long)op->socket_handle,
            (unsigned long long)args[1].bytes_value.length);
        if (args[1].bytes_value.length > 0U && args[1].bytes_value.data != NULL) {
            op->pending_bytes = (uint8_t*)malloc(args[1].bytes_value.length);
            if (op->pending_bytes == NULL) {
                native_net_async_set_failure(op, "alloc_failed");
                *result = aivm_value_int(op_handle);
                return AIVM_SYSCALL_OK;
            }
            memcpy(op->pending_bytes, args[1].bytes_value.data, args[1].bytes_value.length);
            op->pending_bytes_len = args[1].bytes_value.length;
        }
        native_net_async_process(op);
    }
    *result = aivm_value_int(op_handle);
    return AIVM_SYSCALL_OK;
}
