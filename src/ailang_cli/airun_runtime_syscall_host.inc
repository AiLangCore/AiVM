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
    fflush(stdout);
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
    (void)snprintf(hello.client_name, sizeof(hello.client_name), "%s", "ailang-native");
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
#ifdef AIRUN_NATIVE_TLS_AVAILABLE
    state->tls_state = native_tls_stream_create(socket_fd, args[0].string_value);
    if (state->tls_state == NULL) {
        (void)native_net_handle_close(handle);
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    state->kind = NATIVE_NET_HANDLE_KIND_TCP_TLS_STREAM;
    if (native_tls_stream_handshake(state, NULL, 0U) != 1) {
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
    if (native_net_requested_limit_exceeded(args[1].int_value, native_net_active_runtime_limits().network_read_bytes)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
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
    if (!native_net_try_consume_read_bytes(read_count)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
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
    if (native_net_bytes_limit_exceeded(args[1].bytes_value.length, native_net_active_runtime_limits().network_write_bytes)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
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
    if (io_status >= 0 && !native_net_try_consume_write_bytes(wrote)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
    }
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
    if (native_net_bytes_limit_exceeded(args[3].bytes_value.length, native_net_active_runtime_limits().network_write_bytes)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
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
    if (wrote >= 0 && !native_net_try_consume_write_bytes((size_t)wrote)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
    }
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
    if (native_net_requested_limit_exceeded(args[1].int_value, native_net_active_runtime_limits().network_read_bytes)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
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
    if (!native_net_try_consume_read_bytes((size_t)read_count)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
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
    if (strcmp(target, "sys.net.tcp.readStart") == 0 &&
        args != NULL && arg_count == 2U &&
        args[0].type == AIVM_VAL_INT && args[1].type == AIVM_VAL_INT &&
        native_net_requested_limit_exceeded(args[1].int_value, native_net_active_runtime_limits().network_read_bytes)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
    }
    if (strcmp(target, "sys.net.tcp.writeStart") == 0 &&
        args != NULL && arg_count == 2U &&
        args[0].type == AIVM_VAL_INT && args[1].type == AIVM_VAL_BYTES &&
        native_net_bytes_limit_exceeded(args[1].bytes_value.length, native_net_active_runtime_limits().network_write_bytes)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RESOURCE_LIMIT;
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
