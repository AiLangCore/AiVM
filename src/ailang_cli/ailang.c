#ifndef _WIN32
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#ifdef _WIN32
#ifndef _CRT_RAND_S
#define _CRT_RAND_S
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <objbase.h>
#include <psapi.h>
#include <process.h>
#include <shellapi.h>
#include <sys/stat.h>
#include <wincodec.h>
#if defined(_MSC_VER)
typedef SSIZE_T ssize_t;
#endif
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#define AIVM_PATH_SEP '\\'
#define AIVM_EXE_EXT ".exe"
#else
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
/* Some test translation units include this file directly without POSIX feature
   macros, which can hide realpath(3) declaration on glibc. */
extern char* realpath(const char* path, char* resolved_path);
extern int lstat(const char* path, struct stat* buffer);
extern int kill(pid_t pid, int sig);
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define AIVM_PATH_SEP '/'
#define AIVM_EXE_EXT ""
#endif

#ifdef __APPLE__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <CFNetwork/CFHost.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ImageIO/ImageIO.h>
#include <Security/SecureTransport.h>
#include <Security/Security.h>
#include <mach/mach.h>
#pragma clang diagnostic pop
#endif

#include "aivm_c_api.h"
#include "remote/aivm_remote_channel.h"
#include "remote/aivm_remote_session.h"
#include "aivm_runtime.h"
#include "ailang_package_manager.h"
#include "airun_ui_host.h"

#ifndef AILANG_BUILD_VERSION
#define AILANG_BUILD_VERSION "local"
#endif

#ifndef AILANG_BUILD_CHANNEL
#define AILANG_BUILD_CHANNEL "local"
#endif

#ifndef AILANG_BUILD_COMMIT
#define AILANG_BUILD_COMMIT "unknown"
#endif

#ifndef AIRUN_UI_HOST_EXTERNAL
#ifndef AIRUN_ALLOW_INTERNAL_UI_FALLBACK
#error "Internal UI fallback backend is disabled by default. Define AIRUN_UI_HOST_EXTERNAL for production builds or AIRUN_ALLOW_INTERNAL_UI_FALLBACK for embedded tests."
#endif
#include "airun_ui_host_unavailable.c"
#endif

#if defined(AIRUN_MINIMAL_RUNTIME)
#if defined(_MSC_VER)
#define AIRUN_MAYBE_UNUSED
#else
#define AIRUN_MAYBE_UNUSED __attribute__((unused))
#endif
#else
#define AIRUN_MAYBE_UNUSED
#endif

static int join_path(const char* left, const char* right, char* out, size_t out_len);
static int find_executable_on_path(const char* name, char* out, size_t out_len);
static int write_text_file(const char* path, const char* text);
static int same_file_path(const char* left, const char* right);
static int remove_file_if_exists(const char* path);
static int run_native_fullstack_server(const char* www_dir);
static int ensure_directory_recursive(const char* path);
static int native_fs_dir_count_entries(const char* path, int64_t* out_count);
static int native_get_current_rss_kb(int64_t* out_kb);
static int resolve_project_dir_for_cache(const char* input, char* out_project_dir, size_t out_project_dir_len);

#ifdef _WIN32
typedef SOCKET NativeSocket;
#define NATIVE_INVALID_SOCKET INVALID_SOCKET
#define native_socket_close closesocket
#else
typedef int NativeSocket;
#define NATIVE_INVALID_SOCKET (-1)
#define native_socket_close close
#endif
static int resolve_executable_path(const char* argv0, char* out, size_t out_len);
static int dirname_of(const char* path, char* out, size_t out_len);
static int simple_resolve_path(const char* base_file, const char* import_path, char* out_path, size_t out_path_len);
static int simple_find_lock_dir_for_source(const char* source_file, char* out_project_dir, size_t out_len);
static int simple_project_config_get_string(const char* project_dir, const char* key, char* out, size_t out_len);
static int simple_resolve_import_path(const char* base_file, const char* attrs, char* out_path, size_t out_path_len);
static int simple_resolve_sdk_import_path(
    const char* base_file,
    const char* sdk_name,
    const char* import_path,
    char* out_path,
    size_t out_path_len);
static int simple_fail(const char* message);
static int simple_failf(const char* fmt, ...);
static int starts_with(const char* value, const char* prefix);
static const char* native_build_error(void);
static AivmVm* g_native_active_vm;
static char g_native_build_project_dir[PATH_MAX];
static int native_vm_append_host_node(
    AivmVm* vm,
    const char* kind,
    const char* id,
    const AivmNodeAttr* attrs,
    size_t attr_count,
    int64_t* out_handle);
static int native_bytes_to_base64(
    const uint8_t* input,
    size_t in_len,
    char* output,
    size_t output_capacity);
static int native_bytes_from_base64(
    const char* input,
    uint8_t* output,
    size_t output_capacity,
    size_t* out_len);
static int native_host_open_default(const char* target);
static AIRUN_MAYBE_UNUSED int handle_publish(int argc, char** argv);
static int native_image_decode_to_rgba_base64(
    const uint8_t* input,
    size_t input_len,
    const char* mime_type,
    AivmValue* result);
typedef enum {
    AIRUN_LOG_OFF = 0,
    AIRUN_LOG_ERROR = 1,
    AIRUN_LOG_INFO = 2,
    AIRUN_LOG_TRACE = 3
} AirunLogLevel;
typedef struct NativeDebugOptions NativeDebugOptions;
typedef struct NativeNetAsyncState NativeNetAsyncState;
static int write_native_debug_bundle(
    const NativeDebugOptions* options,
    const AivmProgram* program,
    const AivmVm* vm,
    int exit_code,
    int has_exit_code,
    const char* diagnostics_line);
#define AILANG_CLI_MAX_TARGET_OPTIONS 64

typedef struct {
    int enabled;
    int consumed;
    int x;
    int y;
} AirunInjectedClick;
typedef struct {
    struct {
        int kind;
        NativeHostUiEvent event;
        int wait_polls;
    } events[256];
    size_t count;
    size_t next_index;
} AirunInjectedEventQueue;
typedef struct {
    NativeHostUiEvent events[512];
    size_t count;
} AirunCapturedEventLog;
typedef struct {
    int enabled;
    int eof_reached;
    char pending[2048];
    size_t pending_len;
} AirunInteractState;
static int native_syscall_net_start_op(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result);
static void native_net_async_close_worker_socket(NativeNetAsyncState* op);
static void native_net_async_maybe_finalize_worker(NativeNetAsyncState* op);
#ifdef AIRUN_TEST_FAKE_OPEN_URL
static char g_native_open_url_test_scratch[1024];
#endif

#define AIRUN_NATIVE_CACHE_SCHEMA "ailang-native-cache-v1"
#define AIRUN_NATIVE_COMPILER_FINGERPRINT "native-compiler-2026-03-05-call-fixup-order-v2"

static AirunLogLevel g_airun_log_level = AIRUN_LOG_ERROR;
static FILE* g_airun_log_file = NULL;
static AirunInjectedClick g_airun_injected_click = {0, 0, 0, 0};
static AirunInjectedEventQueue g_airun_injected_events = {0};
static AirunCapturedEventLog g_airun_captured_events = {0};
static AirunInteractState g_airun_interact_state = {0, 0, {0}, 0U};
static void airun_format_value_preview(const AivmValue* value, char* buffer, size_t buffer_size);

static const char* airun_log_level_name(AirunLogLevel level)
{
    switch (level) {
        case AIRUN_LOG_ERROR: return "error";
        case AIRUN_LOG_INFO: return "info";
        case AIRUN_LOG_TRACE: return "trace";
        default: return "off";
    }
}

static AirunLogLevel airun_parse_log_level(const char* text)
{
    if (text == NULL || text[0] == '\0' || strcmp(text, "off") == 0) {
        return AIRUN_LOG_OFF;
    }
    if (strcmp(text, "error") == 0) {
        return AIRUN_LOG_ERROR;
    }
    if (strcmp(text, "info") == 0) {
        return AIRUN_LOG_INFO;
    }
    if (strcmp(text, "trace") == 0 || strcmp(text, "debug") == 0) {
        return AIRUN_LOG_TRACE;
    }
    return AIRUN_LOG_ERROR;
}

static void airun_configure_log_level(const char* cli_level)
{
    const char* env_level = getenv("AIVM_LOG_LEVEL");
    if (cli_level != NULL && cli_level[0] != '\0') {
        g_airun_log_level = airun_parse_log_level(cli_level);
        return;
    }
    g_airun_log_level = airun_parse_log_level(env_level);
}

static void airun_log_message(AirunLogLevel level, const char* category, const char* fmt, ...)
{
    va_list args;
    va_list args_copy;
    char message[512];
    if (level > g_airun_log_level || level == AIRUN_LOG_OFF) {
        return;
    }
    va_start(args, fmt);
    va_copy(args_copy, args);
    (void)vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    if (g_airun_log_file != NULL) {
        fprintf(g_airun_log_file,
            "aivm.log level=%s category=%s message=\"",
            airun_log_level_name(level),
            (category == NULL || category[0] == '\0') ? "runtime" : category);
        vfprintf(g_airun_log_file, fmt, args_copy);
        fprintf(g_airun_log_file, "\"\n");
        fflush(g_airun_log_file);
    }
    va_end(args_copy);
    fprintf(stderr,
        "aivm.log level=%s category=%s message=\"%s\"\n",
        airun_log_level_name(level),
        (category == NULL || category[0] == '\0') ? "runtime" : category,
        message);
}

static AivmSyscallBinding g_native_trace_real_bindings[128];
static size_t g_native_trace_real_binding_count = 0U;
static char g_airun_runtime_exe_path[PATH_MAX];
static const NativeDebugOptions* g_airun_live_debug_options = NULL;
static const AivmProgram* g_airun_live_debug_program = NULL;
static const AivmVm* g_airun_live_debug_vm = NULL;
static size_t g_airun_live_debug_refresh_counter = 0U;
static int g_airun_live_debug_emit_bundle = 0;
static const char* airun_value_type_name(AivmValueType type);

static void airun_refresh_live_debug_bundle(const char* diagnostics_line)
{
    if (g_airun_live_debug_options == NULL ||
        g_airun_live_debug_program == NULL ||
        g_airun_live_debug_vm == NULL ||
        !g_airun_live_debug_emit_bundle) {
        return;
    }
    (void)write_native_debug_bundle(
        g_airun_live_debug_options,
        g_airun_live_debug_program,
        g_airun_live_debug_vm,
        0,
        0,
        diagnostics_line);
}

static AivmSyscallHandler native_trace_lookup_real_handler(const char* target)
{
    size_t i;
    if (target == NULL) {
        return NULL;
    }
    for (i = 0U; i < g_native_trace_real_binding_count; i += 1U) {
        if (g_native_trace_real_bindings[i].target != NULL &&
            strcmp(g_native_trace_real_bindings[i].target, target) == 0) {
            return g_native_trace_real_bindings[i].handler;
        }
    }
    return NULL;
}

static int native_syscall_dispatch_traced(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    AivmSyscallHandler handler = native_trace_lookup_real_handler(target);
    int status;
    char arg0_preview[96];
    char arg1_preview[96];
    char arg2_preview[96];
    char arg3_preview[96];
    char arg4_preview[96];
    char arg5_preview[96];
    char result_preview[96];
    char local0_preview[96];
    char local1_preview[96];
    char local2_preview[96];
    char caller0_preview[96];
    char caller1_preview[96];
    char caller2_preview[96];
    size_t locals_base = 0U;
    size_t caller_locals_base = 0U;
    arg0_preview[0] = '\0';
    arg1_preview[0] = '\0';
    arg2_preview[0] = '\0';
    arg3_preview[0] = '\0';
    arg4_preview[0] = '\0';
    arg5_preview[0] = '\0';
    result_preview[0] = '\0';
    local0_preview[0] = '\0';
    local1_preview[0] = '\0';
    local2_preview[0] = '\0';
    caller0_preview[0] = '\0';
    caller1_preview[0] = '\0';
    caller2_preview[0] = '\0';
    if (arg_count > 0U) {
        airun_format_value_preview(&args[0], arg0_preview, sizeof(arg0_preview));
    }
    if (arg_count > 1U) {
        airun_format_value_preview(&args[1], arg1_preview, sizeof(arg1_preview));
    }
    if (arg_count > 2U) {
        airun_format_value_preview(&args[2], arg2_preview, sizeof(arg2_preview));
    }
    if (arg_count > 3U) {
        airun_format_value_preview(&args[3], arg3_preview, sizeof(arg3_preview));
    }
    if (arg_count > 4U) {
        airun_format_value_preview(&args[4], arg4_preview, sizeof(arg4_preview));
    }
    if (arg_count > 5U) {
        airun_format_value_preview(&args[5], arg5_preview, sizeof(arg5_preview));
    }
    if (g_airun_live_debug_vm != NULL && g_airun_live_debug_vm->call_frame_count > 0U) {
        locals_base = g_airun_live_debug_vm->call_frames[g_airun_live_debug_vm->call_frame_count - 1U].locals_base;
        if (locals_base < g_airun_live_debug_vm->locals_count) {
            airun_format_value_preview(&g_airun_live_debug_vm->locals[locals_base], local0_preview, sizeof(local0_preview));
        }
        if (locals_base + 1U < g_airun_live_debug_vm->locals_count) {
            airun_format_value_preview(&g_airun_live_debug_vm->locals[locals_base + 1U], local1_preview, sizeof(local1_preview));
        }
        if (locals_base + 2U < g_airun_live_debug_vm->locals_count) {
            airun_format_value_preview(&g_airun_live_debug_vm->locals[locals_base + 2U], local2_preview, sizeof(local2_preview));
        }
    }
    if (g_airun_live_debug_vm != NULL && g_airun_live_debug_vm->call_frame_count > 1U) {
        caller_locals_base = g_airun_live_debug_vm->call_frames[g_airun_live_debug_vm->call_frame_count - 2U].locals_base;
        if (caller_locals_base < g_airun_live_debug_vm->locals_count) {
            airun_format_value_preview(&g_airun_live_debug_vm->locals[caller_locals_base], caller0_preview, sizeof(caller0_preview));
        }
        if (caller_locals_base + 1U < g_airun_live_debug_vm->locals_count) {
            airun_format_value_preview(&g_airun_live_debug_vm->locals[caller_locals_base + 1U], caller1_preview, sizeof(caller1_preview));
        }
        if (caller_locals_base + 2U < g_airun_live_debug_vm->locals_count) {
            airun_format_value_preview(&g_airun_live_debug_vm->locals[caller_locals_base + 2U], caller2_preview, sizeof(caller2_preview));
        }
    }
    airun_log_message(
        AIRUN_LOG_TRACE,
        "syscall",
        "enter target=%s argCount=%llu arg0=%s arg1=%s arg2=%s arg3=%s arg4=%s arg5=%s local0=%s local1=%s local2=%s caller0=%s caller1=%s caller2=%s",
        (target == NULL) ? "<null>" : target,
        (unsigned long long)arg_count,
        arg_count > 0U ? arg0_preview : "<none>",
        arg_count > 1U ? arg1_preview : "<none>",
        arg_count > 2U ? arg2_preview : "<none>",
        arg_count > 3U ? arg3_preview : "<none>",
        arg_count > 4U ? arg4_preview : "<none>",
        arg_count > 5U ? arg5_preview : "<none>",
        local0_preview[0] == '\0' ? "<none>" : local0_preview,
        local1_preview[0] == '\0' ? "<none>" : local1_preview,
        local2_preview[0] == '\0' ? "<none>" : local2_preview,
        caller0_preview[0] == '\0' ? "<none>" : caller0_preview,
        caller1_preview[0] == '\0' ? "<none>" : caller1_preview,
        caller2_preview[0] == '\0' ? "<none>" : caller2_preview);
    if (handler == NULL) {
        if (result != NULL) {
            *result = aivm_value_void();
        }
        airun_log_message(
            AIRUN_LOG_TRACE,
            "syscall",
            "exit target=%s status=%s resultType=%s result=%s",
            (target == NULL) ? "<null>" : target,
            aivm_syscall_status_code(AIVM_SYSCALL_ERR_NOT_FOUND),
            (result == NULL) ? "null" : airun_value_type_name(result->type),
            "<none>");
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    status = handler(target, args, arg_count, result);
    if (result != NULL) {
        airun_format_value_preview(result, result_preview, sizeof(result_preview));
    }
    airun_log_message(
        AIRUN_LOG_TRACE,
        "syscall",
        "exit target=%s status=%s resultType=%s result=%s",
        (target == NULL) ? "<null>" : target,
        aivm_syscall_status_code((AivmSyscallStatus)status),
        (result == NULL) ? "null" : airun_value_type_name(result->type),
        (result == NULL) ? "<none>" : result_preview);
    g_airun_live_debug_refresh_counter += 1U;
    if ((g_airun_live_debug_refresh_counter % 128U) == 0U) {
        airun_refresh_live_debug_bundle("status=running");
    }
    return status;
}

static void native_prepare_traced_bindings(AivmSyscallBinding* bindings, size_t binding_count)
{
    size_t i;
    if (bindings == NULL) {
        g_native_trace_real_binding_count = 0U;
        return;
    }
    if (binding_count > (sizeof(g_native_trace_real_bindings) / sizeof(g_native_trace_real_bindings[0]))) {
        binding_count = sizeof(g_native_trace_real_bindings) / sizeof(g_native_trace_real_bindings[0]);
    }
    for (i = 0U; i < binding_count; i += 1U) {
        g_native_trace_real_bindings[i] = bindings[i];
        bindings[i].handler = native_syscall_dispatch_traced;
    }
    g_native_trace_real_binding_count = binding_count;
}

static void airun_format_value_preview(const AivmValue* value, char* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return;
    }
    buffer[0] = '\0';
    if (value == NULL) {
        (void)snprintf(buffer, buffer_size, "<null>");
        return;
    }
    switch (value->type) {
        case AIVM_VAL_INT:
            (void)snprintf(buffer, buffer_size, "int(%lld)", (long long)value->int_value);
            break;
        case AIVM_VAL_NUMBER:
            (void)snprintf(buffer, buffer_size, "number(%.15g)", value->number_value);
            break;
        case AIVM_VAL_BOOL:
            (void)snprintf(buffer, buffer_size, "bool(%s)", value->bool_value ? "true" : "false");
            break;
        case AIVM_VAL_NULL:
            (void)snprintf(buffer, buffer_size, "null");
            break;
        case AIVM_VAL_STRING:
            (void)snprintf(
                buffer,
                buffer_size,
                "string(\"%.48s\")",
                value->string_value == NULL ? "<null>" : value->string_value);
            break;
        case AIVM_VAL_NODE:
            (void)snprintf(buffer, buffer_size, "node(%lld)", (long long)value->node_handle);
            break;
        case AIVM_VAL_PAIR:
            (void)snprintf(buffer, buffer_size, "pair(%lld)", (long long)value->pair_handle);
            break;
        case AIVM_VAL_BYTES:
            (void)snprintf(buffer, buffer_size, "bytes(len=%llu)", (unsigned long long)value->bytes_value.length);
            break;
        case AIVM_VAL_VOID:
            (void)snprintf(buffer, buffer_size, "void");
            break;
        default:
            (void)snprintf(buffer, buffer_size, "%s", airun_value_type_name(value->type));
            break;
    }
}

static int airun_parse_click_point(const char* text, int* out_x, int* out_y)
{
    char* end_ptr = NULL;
    long parsed_x;
    long parsed_y;
    if (text == NULL || out_x == NULL || out_y == NULL) {
        return 0;
    }
    parsed_x = strtol(text, &end_ptr, 10);
    if (end_ptr == text || end_ptr == NULL || *end_ptr != ',') {
        return 0;
    }
    parsed_y = strtol(end_ptr + 1, &end_ptr, 10);
    if (end_ptr == NULL || *end_ptr != '\0') {
        return 0;
    }
    *out_x = (int)parsed_x;
    *out_y = (int)parsed_y;
    return 1;
}

static void airun_reset_injected_events(void)
{
    memset(&g_airun_injected_click, 0, sizeof(g_airun_injected_click));
    memset(&g_airun_injected_events, 0, sizeof(g_airun_injected_events));
    memset(&g_airun_captured_events, 0, sizeof(g_airun_captured_events));
    memset(&g_airun_interact_state, 0, sizeof(g_airun_interact_state));
}

static void airun_record_captured_event(const NativeHostUiEvent* event)
{
    if (event == NULL || strcmp(event->type, "none") == 0) {
        return;
    }
    if (g_airun_captured_events.count >= (sizeof(g_airun_captured_events.events) / sizeof(g_airun_captured_events.events[0]))) {
        return;
    }
    g_airun_captured_events.events[g_airun_captured_events.count] = *event;
    g_airun_captured_events.count += 1U;
}

static int airun_queue_injected_event(const NativeHostUiEvent* event)
{
    if (event == NULL) {
        return 0;
    }
    if (g_airun_injected_events.count >= (sizeof(g_airun_injected_events.events) / sizeof(g_airun_injected_events.events[0]))) {
        return 0;
    }
    g_airun_injected_events.events[g_airun_injected_events.count].kind = 1;
    g_airun_injected_events.events[g_airun_injected_events.count].event = *event;
    g_airun_injected_events.events[g_airun_injected_events.count].wait_polls = 0;
    g_airun_injected_events.count += 1U;
    return 1;
}

static int airun_queue_injected_wait(int wait_polls)
{
    if (wait_polls < 0) {
        return 0;
    }
    if (g_airun_injected_events.count >= (sizeof(g_airun_injected_events.events) / sizeof(g_airun_injected_events.events[0]))) {
        return 0;
    }
    g_airun_injected_events.events[g_airun_injected_events.count].kind = 2;
    memset(&g_airun_injected_events.events[g_airun_injected_events.count].event, 0, sizeof(NativeHostUiEvent));
    g_airun_injected_events.events[g_airun_injected_events.count].wait_polls = wait_polls;
    g_airun_injected_events.count += 1U;
    return 1;
}

static int airun_queue_injected_click(int x, int y)
{
    NativeHostUiEvent event;
    memset(&event, 0, sizeof(event));
    (void)snprintf(event.type, sizeof(event.type), "click");
    event.x = x;
    event.y = y;
    return airun_queue_injected_event(&event);
}

static int airun_queue_injected_key(const char* key_name, const char* text_value)
{
    NativeHostUiEvent event;
    memset(&event, 0, sizeof(event));
    (void)snprintf(event.type, sizeof(event.type), "key");
    event.x = -1;
    event.y = -1;
    if (key_name != NULL) {
        (void)snprintf(event.key, sizeof(event.key), "%s", key_name);
    }
    if (text_value != NULL) {
        (void)snprintf(event.text, sizeof(event.text), "%s", text_value);
    }
    return airun_queue_injected_event(&event);
}

static int airun_queue_injected_text_event(const char* text_value)
{
    NativeHostUiEvent event;
    memset(&event, 0, sizeof(event));
    (void)snprintf(event.type, sizeof(event.type), "text");
    event.x = -1;
    event.y = -1;
    if (text_value != NULL) {
        (void)snprintf(event.text, sizeof(event.text), "%s", text_value);
    }
    return airun_queue_injected_event(&event);
}

static int airun_queue_injected_close(void)
{
    NativeHostUiEvent event;
    memset(&event, 0, sizeof(event));
    (void)snprintf(event.type, sizeof(event.type), "close");
    event.x = -1;
    event.y = -1;
    return airun_queue_injected_event(&event);
}

static int airun_queue_injected_text(const char* text)
{
    if (text == NULL) {
        return 0;
    }
    if (text[0] == '\0') {
        return 1;
    }
    return airun_queue_injected_text_event(text);
}

static int airun_queue_injected_text_at(int x, int y, const char* text)
{
    if (!airun_queue_injected_click(x, y)) {
        return 0;
    }
    return airun_queue_injected_text(text);
}

static int airun_queue_injected_key_at(int x, int y, const char* key_name, const char* text_value)
{
    if (!airun_queue_injected_click(x, y)) {
        return 0;
    }
    return airun_queue_injected_key(key_name, text_value);
}

static int airun_parse_text_at_value(const char* value, int* out_x, int* out_y, const char** out_text)
{
    const char* first_comma;
    const char* second_comma;
    char x_buf[32];
    char y_buf[32];
    char* end = NULL;
    long parsed = 0L;
    if (value == NULL || out_x == NULL || out_y == NULL || out_text == NULL) {
        return 0;
    }
    first_comma = strchr(value, ',');
    if (first_comma == NULL) {
        return 0;
    }
    second_comma = strchr(first_comma + 1, ',');
    if (second_comma == NULL) {
        return 0;
    }
    if ((size_t)(first_comma - value) >= sizeof(x_buf) ||
        (size_t)(second_comma - first_comma - 1) >= sizeof(y_buf)) {
        return 0;
    }
    memcpy(x_buf, value, (size_t)(first_comma - value));
    x_buf[first_comma - value] = '\0';
    memcpy(y_buf, first_comma + 1, (size_t)(second_comma - first_comma - 1));
    y_buf[second_comma - first_comma - 1] = '\0';
    parsed = strtol(x_buf, &end, 10);
    if (end == x_buf || *end != '\0' || parsed < -1000000L || parsed > 1000000L) {
        return 0;
    }
    *out_x = (int)parsed;
    parsed = strtol(y_buf, &end, 10);
    if (end == y_buf || *end != '\0' || parsed < -1000000L || parsed > 1000000L) {
        return 0;
    }
    *out_y = (int)parsed;
    *out_text = second_comma + 1;
    return 1;
}

static int airun_parse_key_at_value(
    const char* value,
    int* out_x,
    int* out_y,
    char* out_key,
    size_t out_key_size,
    const char** out_text)
{
    const char* first_comma;
    const char* second_comma;
    const char* third_comma;
    char x_buf[32];
    char y_buf[32];
    char* end = NULL;
    long parsed = 0L;
    if (value == NULL || out_x == NULL || out_y == NULL || out_key == NULL || out_key_size == 0U || out_text == NULL) {
        return 0;
    }
    first_comma = strchr(value, ',');
    if (first_comma == NULL) {
        return 0;
    }
    second_comma = strchr(first_comma + 1, ',');
    if (second_comma == NULL) {
        return 0;
    }
    third_comma = strchr(second_comma + 1, ',');
    if ((size_t)(first_comma - value) >= sizeof(x_buf) ||
        (size_t)(second_comma - first_comma - 1) >= sizeof(y_buf)) {
        return 0;
    }
    memcpy(x_buf, value, (size_t)(first_comma - value));
    x_buf[first_comma - value] = '\0';
    memcpy(y_buf, first_comma + 1, (size_t)(second_comma - first_comma - 1));
    y_buf[second_comma - first_comma - 1] = '\0';
    parsed = strtol(x_buf, &end, 10);
    if (end == x_buf || *end != '\0' || parsed < -1000000L || parsed > 1000000L) {
        return 0;
    }
    *out_x = (int)parsed;
    parsed = strtol(y_buf, &end, 10);
    if (end == y_buf || *end != '\0' || parsed < -1000000L || parsed > 1000000L) {
        return 0;
    }
    *out_y = (int)parsed;
    if (third_comma == NULL) {
        if (strlen(second_comma + 1) >= out_key_size) {
            return 0;
        }
        (void)snprintf(out_key, out_key_size, "%s", second_comma + 1);
        *out_text = "";
        return 1;
    }
    if ((size_t)(third_comma - second_comma - 1) >= out_key_size) {
        return 0;
    }
    memcpy(out_key, second_comma + 1, (size_t)(third_comma - second_comma - 1));
    out_key[third_comma - second_comma - 1] = '\0';
    *out_text = third_comma + 1;
    return 1;
}

static int airun_pop_injected_event(NativeHostUiEvent* out_event)
{
    int kind;
    if (out_event == NULL) {
        return 0;
    }
    while (g_airun_injected_events.next_index < g_airun_injected_events.count) {
        kind = g_airun_injected_events.events[g_airun_injected_events.next_index].kind;
        if (kind == 2) {
            if (g_airun_injected_events.events[g_airun_injected_events.next_index].wait_polls > 0) {
                g_airun_injected_events.events[g_airun_injected_events.next_index].wait_polls -= 1;
                return 0;
            }
            g_airun_injected_events.next_index += 1U;
            continue;
        }
        *out_event = g_airun_injected_events.events[g_airun_injected_events.next_index].event;
        g_airun_injected_events.next_index += 1U;
        return 1;
    }
    return 0;
}

static void airun_compact_injected_events(void)
{
    size_t remaining;
    if (g_airun_injected_events.next_index == 0U) {
        return;
    }
    if (g_airun_injected_events.next_index >= g_airun_injected_events.count) {
        g_airun_injected_events.count = 0U;
        g_airun_injected_events.next_index = 0U;
        return;
    }
    remaining = g_airun_injected_events.count - g_airun_injected_events.next_index;
    memmove(
        g_airun_injected_events.events,
        &g_airun_injected_events.events[g_airun_injected_events.next_index],
        remaining * sizeof(g_airun_injected_events.events[0]));
    g_airun_injected_events.count = remaining;
    g_airun_injected_events.next_index = 0U;
}

static int airun_parse_interact_line(const char* line)
{
    int x = 0;
    int y = 0;
    if (line == NULL) {
        return 1;
    }
    while (*line == ' ' || *line == '\t') {
        line += 1;
    }
    if (*line == '\0' || *line == '#') {
        return 1;
    }
    if (starts_with(line, "click ")) {
        if (!airun_parse_click_point(line + 6, &x, &y)) {
            return 0;
        }
        return airun_queue_injected_click(x, y);
    }
    if (starts_with(line, "key ")) {
        return airun_queue_injected_key(line + 4, "");
    }
    if (starts_with(line, "text ")) {
        return airun_queue_injected_text(line + 5);
    }
    if (starts_with(line, "textat ")) {
        const char* text = NULL;
        if (!airun_parse_text_at_value(line + 7, &x, &y, &text)) {
            return 0;
        }
        return airun_queue_injected_text_at(x, y, text);
    }
    if (starts_with(line, "keyat ")) {
        char key_name[64];
        const char* text_value = NULL;
        if (!airun_parse_key_at_value(line + 6, &x, &y, key_name, sizeof(key_name), &text_value)) {
            return 0;
        }
        return airun_queue_injected_key_at(x, y, key_name, text_value);
    }
    if (starts_with(line, "wait ")) {
        char* end = NULL;
        long parsed = strtol(line + 5, &end, 10);
        if (end == line + 5 || *end != '\0' || parsed < 0L || parsed > 1000000L) {
            return 0;
        }
        return airun_queue_injected_wait((int)parsed);
    }
    if (strcmp(line, "close") == 0) {
        return airun_queue_injected_close();
    }
    if (strcmp(line, "enter") == 0) {
        return airun_queue_injected_key("enter", "");
    }
    if (strcmp(line, "backspace") == 0) {
        return airun_queue_injected_key("backspace", "");
    }
    if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
        g_airun_interact_state.eof_reached = 1;
        return 1;
    }
    return 0;
}

static void airun_interact_drain_stdin(void)
{
#ifdef _WIN32
    (void)g_airun_interact_state;
#else
    fd_set readfds;
    struct timeval timeout;
    char buffer[256];
    ssize_t read_count;
    size_t i;
    if (!g_airun_interact_state.enabled || g_airun_interact_state.eof_reached) {
        return;
    }
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) <= 0) {
            break;
        }
        read_count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (read_count <= 0) {
            g_airun_interact_state.eof_reached = 1;
            break;
        }
        for (i = 0U; i < (size_t)read_count; i += 1U) {
            char ch = buffer[i];
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                g_airun_interact_state.pending[g_airun_interact_state.pending_len] = '\0';
                (void)airun_parse_interact_line(g_airun_interact_state.pending);
                g_airun_interact_state.pending_len = 0U;
                continue;
            }
            if (g_airun_interact_state.pending_len + 1U < sizeof(g_airun_interact_state.pending)) {
                g_airun_interact_state.pending[g_airun_interact_state.pending_len++] = ch;
            }
        }
    }
#endif
    airun_compact_injected_events();
}

static int airun_queue_injected_script_file(const char* path)
{
    FILE* f;
    char line[2048];
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        while (len > 0U && (line[len - 1U] == '\n' || line[len - 1U] == '\r')) {
            line[len - 1U] = '\0';
            len -= 1U;
        }
        if (!airun_parse_interact_line(line)) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

static int ends_with(const char* value, const char* suffix)
{
    size_t value_len;
    size_t suffix_len;
    if (value == NULL || suffix == NULL) {
        return 0;
    }
    value_len = strlen(value);
    suffix_len = strlen(suffix);
    if (value_len < suffix_len) {
        return 0;
    }
    return strcmp(value + (value_len - suffix_len), suffix) == 0;
}

static int starts_with(const char* value, const char* prefix)
{
    size_t prefix_len;
    if (value == NULL || prefix == NULL) {
        return 0;
    }
    prefix_len = strlen(prefix);
    return strncmp(value, prefix, prefix_len) == 0;
}

static int native_host_open_default(const char* target)
{
    if (target == NULL ||
        (!starts_with(target, "http://") &&
         !starts_with(target, "https://"))) {
        return 0;
    }
#ifdef AIRUN_TEST_FAKE_OPEN_URL
    g_native_open_url_test_scratch[0] = '\0';
    (void)snprintf(g_native_open_url_test_scratch, sizeof(g_native_open_url_test_scratch), "%s", target);
    return 1;
#elif defined(_WIN32)
    return ((INT_PTR)ShellExecuteA(NULL, "open", target, NULL, NULL, SW_SHOWNORMAL)) > 32;
#else
    {
        pid_t pid = fork();
        if (pid < 0) {
            return 0;
        }
        if (pid == 0) {
#ifdef __APPLE__
            execl("/usr/bin/open", "open", target, (char*)NULL);
#else
            execlp("xdg-open", "xdg-open", target, (char*)NULL);
#endif
            _exit(127);
        }
    }
    return 1;
#endif
}

static int file_exists(const char* path)
{
    struct stat st;
    if (path == NULL || stat(path, &st) != 0) {
        return 0;
    }
#ifdef _WIN32
    return (st.st_mode & _S_IFREG) != 0;
#else
    return S_ISREG(st.st_mode);
#endif
}

static int directory_exists(const char* path)
{
    struct stat st;
    if (path == NULL || stat(path, &st) != 0) {
        return 0;
    }
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static int ensure_directory(const char* path)
{
    if (path == NULL) {
        return 0;
    }
    if (directory_exists(path)) {
        return 1;
    }
#ifdef _WIN32
    if (_mkdir(path) == 0) {
#else
    if (mkdir(path, 0755) == 0) {
#endif
        return 1;
    }
    return errno == EEXIST;
}

static int ensure_directory_recursive(const char* path)
{
    char tmp[PATH_MAX];
    size_t len;
    size_t i;
    if (path == NULL) {
        return 0;
    }
    len = strlen(path);
    if (len == 0U || len >= sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, path, len + 1U);
    for (i = 1U; i < len; i += 1U) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char saved = tmp[i];
            tmp[i] = '\0';
            if (tmp[0] != '\0' && !ensure_directory(tmp)) {
                tmp[i] = saved;
                return 0;
            }
            tmp[i] = saved;
        }
    }
    return ensure_directory(tmp);
}

static int copy_file(const char* src, const char* dst)
{
    FILE* in;
    FILE* out;
    unsigned char buffer[4096];
    size_t n;

    if (src == NULL || dst == NULL) {
        return 0;
    }
    if (same_file_path(src, dst)) {
        return 1;
    }

    in = fopen(src, "rb");
    if (in == NULL) {
        return 0;
    }
    out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }

    while ((n = fread(buffer, 1U, sizeof(buffer), in)) > 0U) {
        if (fwrite(buffer, 1U, n, out) != n) {
            fclose(in);
            fclose(out);
            return 0;
        }
    }

    fclose(in);
    fclose(out);
    return 1;
}

static int same_file_path(const char* left, const char* right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }
#ifdef _WIN32
    {
        char full_left[PATH_MAX];
        char full_right[PATH_MAX];
        size_t i;
        if (_fullpath(full_left, left, PATH_MAX) == NULL ||
            _fullpath(full_right, right, PATH_MAX) == NULL) {
            return 0;
        }
        for (i = 0U; full_left[i] != '\0'; i += 1U) {
            if (full_left[i] == '\\') {
                full_left[i] = '/';
            }
        }
        for (i = 0U; full_right[i] != '\0'; i += 1U) {
            if (full_right[i] == '\\') {
                full_right[i] = '/';
            }
        }
        return _stricmp(full_left, full_right) == 0;
    }
#else
    {
        struct stat left_stat;
        struct stat right_stat;
        char real_left[PATH_MAX];
        char real_right[PATH_MAX];
        if (stat(left, &left_stat) == 0 &&
            stat(right, &right_stat) == 0 &&
            left_stat.st_dev == right_stat.st_dev &&
            left_stat.st_ino == right_stat.st_ino) {
            return 1;
        }
        if (realpath(left, real_left) != NULL &&
            realpath(right, real_right) != NULL &&
            strcmp(real_left, real_right) == 0) {
            return 1;
        }
        return 0;
    }
#endif
}

static int copy_runtime_file(const char* src, const char* dst)
{
    if (!copy_file(src, dst)) {
        return 0;
    }
#ifndef _WIN32
    if (chmod(dst, 0755) != 0) {
        return 0;
    }
#endif
    return 1;
}

static int remove_file_if_exists(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (!file_exists(path)) {
        return 1;
    }
    return remove(path) == 0;
}

static volatile int g_fullstack_stop = 0;

#ifdef _WIN32
static BOOL WINAPI fullstack_ctrl_handler(DWORD ctrl_type)
{
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
        g_fullstack_stop = 1;
        return TRUE;
    }
    return FALSE;
}
#else
static void fullstack_signal_handler(int signo)
{
    (void)signo;
    g_fullstack_stop = 1;
}
#endif

static int native_send_all(NativeSocket fd, const char* bytes, size_t length)
{
    size_t sent = 0U;
    while (sent < length) {
#ifdef _WIN32
        int n = send(fd, bytes + sent, (int)(length - sent), 0);
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

static const char* fullstack_mime_type(const char* path)
{
    if (path == NULL) {
        return "application/octet-stream";
    }
    if (ends_with(path, ".html")) {
        return "text/html; charset=utf-8";
    }
    if (ends_with(path, ".js") || ends_with(path, ".mjs")) {
        return "text/javascript; charset=utf-8";
    }
    if (ends_with(path, ".wasm")) {
        return "application/wasm";
    }
    if (ends_with(path, ".aibc1")) {
        return "application/octet-stream";
    }
    return "application/octet-stream";
}

static int fullstack_send_error(NativeSocket client, int code, const char* message)
{
    char body[256];
    char header[512];
    int body_len;
    int header_len;
    if (message == NULL) {
        message = "error";
    }
    body_len = snprintf(body, sizeof(body), "%d %s\n", code, message);
    if (body_len < 0 || (size_t)body_len >= sizeof(body)) {
        return 0;
    }
    header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        code,
        message,
        body_len);
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        return 0;
    }
    return native_send_all(client, header, (size_t)header_len) &&
           native_send_all(client, body, (size_t)body_len);
}

static int fullstack_serve_request(NativeSocket client, const char* www_dir)
{
    char request[4096];
    char method[16];
    char target[1024];
    char file_path[PATH_MAX];
    char relative[1024];
    char header[512];
    struct stat st;
    FILE* file;
    size_t n;
    int request_len;
    int i;
    const char* mime;
    int header_len;
    char buffer[4096];

    if (www_dir == NULL) {
        return fullstack_send_error(client, 500, "server error");
    }

#ifdef _WIN32
    request_len = recv(client, request, (int)(sizeof(request) - 1U), 0);
#else
    request_len = (int)recv(client, request, sizeof(request) - 1U, 0);
#endif
    if (request_len <= 0) {
        return 0;
    }
    request[request_len] = '\0';

    if (sscanf(request, "%15s %1023s", method, target) != 2) {
        return fullstack_send_error(client, 400, "bad request");
    }
    if (strcmp(method, "GET") != 0) {
        return fullstack_send_error(client, 405, "method not allowed");
    }
    for (i = 0; target[i] != '\0'; i += 1) {
        if (target[i] == '?') {
            target[i] = '\0';
            break;
        }
    }
    if (strstr(target, "..") != NULL) {
        return fullstack_send_error(client, 403, "forbidden");
    }
    if (strcmp(target, "/") == 0) {
        if (snprintf(relative, sizeof(relative), "index.html") >= (int)sizeof(relative)) {
            return fullstack_send_error(client, 500, "path overflow");
        }
    } else {
        const char* rel = target;
        if (target[0] == '/') {
            rel = target + 1;
        }
        if (snprintf(relative, sizeof(relative), "%s", rel) >= (int)sizeof(relative)) {
            return fullstack_send_error(client, 500, "path overflow");
        }
    }
    if (!join_path(www_dir, relative, file_path, sizeof(file_path))) {
        return fullstack_send_error(client, 500, "path overflow");
    }
    if (stat(file_path, &st) != 0) {
        return fullstack_send_error(client, 404, "not found");
    }
#ifdef _WIN32
    if ((st.st_mode & _S_IFREG) == 0) {
#else
    if (!S_ISREG(st.st_mode)) {
#endif
        return fullstack_send_error(client, 404, "not found");
    }

    file = fopen(file_path, "rb");
    if (file == NULL) {
        return fullstack_send_error(client, 404, "not found");
    }
    mime = fullstack_mime_type(file_path);
    header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n\r\n",
        mime,
        (long long)st.st_size);
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        fclose(file);
        return fullstack_send_error(client, 500, "header overflow");
    }
    if (!native_send_all(client, header, (size_t)header_len)) {
        fclose(file);
        return 0;
    }
    while ((n = fread(buffer, 1U, sizeof(buffer), file)) > 0U) {
        if (!native_send_all(client, buffer, n)) {
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return 1;
}

static int run_native_fullstack_server(const char* www_dir)
{
    NativeSocket listener = NATIVE_INVALID_SOCKET;
    struct sockaddr_in addr;
    const char* port_env;
    int port = 8080;
    int reuse = 1;

    if (www_dir == NULL) {
        return 2;
    }
    port_env = getenv("PORT");
    if (port_env != NULL && port_env[0] != '\0') {
        int parsed = atoi(port_env);
        if (parsed > 0 && parsed <= 65535) {
            port = parsed;
        }
    }

#ifdef _WIN32
    {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            fprintf(stderr, "Err#err1(code=RUN001 message=\"WSAStartup failed.\" nodeId=publish)\n");
            return 2;
        }
    }
    SetConsoleCtrlHandler(fullstack_ctrl_handler, TRUE);
#else
    signal(SIGINT, fullstack_signal_handler);
    signal(SIGTERM, fullstack_signal_handler);
#endif

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == NATIVE_INVALID_SOCKET) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Failed to create fullstack listener socket.\" nodeId=publish)\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 2;
    }
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(listener, 16) != 0) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Failed to bind/listen fullstack server socket.\" nodeId=publish)\n");
        native_socket_close(listener);
#ifdef _WIN32
        WSACleanup();
#endif
        return 2;
    }

    printf("[fullstack] serving static client from %s at http://localhost:%d\n", www_dir, port);
    printf("[fullstack] press Ctrl+C to stop\n");
    fflush(stdout);

    g_fullstack_stop = 0;
    while (!g_fullstack_stop) {
        fd_set read_fds;
        struct timeval timeout;
        int ready;
        NativeSocket client;
        FD_ZERO(&read_fds);
        FD_SET(listener, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;
#ifdef _WIN32
        ready = select(0, &read_fds, NULL, NULL, &timeout);
#else
        ready = select((int)listener + 1, &read_fds, NULL, NULL, &timeout);
#endif
        if (ready < 0) {
            if (g_fullstack_stop) {
                break;
            }
            continue;
        }
        if (ready == 0) {
            continue;
        }
        client = accept(listener, NULL, NULL);
        if (client == NATIVE_INVALID_SOCKET) {
            continue;
        }
        (void)fullstack_serve_request(client, www_dir);
        native_socket_close(client);
    }

    native_socket_close(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

static int read_binary_file(const char* path, unsigned char** out_bytes, size_t* out_size)
{
    FILE* f;
    long length;
    unsigned char* bytes;
    size_t read_count;

    if (path == NULL || out_bytes == NULL || out_size == NULL) {
        return 0;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    length = ftell(f);
    if (length < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    bytes = (unsigned char*)malloc((size_t)length);
    if (bytes == NULL && length > 0) {
        fclose(f);
        return 0;
    }

    read_count = fread(bytes, 1U, (size_t)length, f);
    fclose(f);

    if (read_count != (size_t)length) {
        free(bytes);
        return 0;
    }

    *out_bytes = bytes;
    *out_size = (size_t)length;
    return 1;
}

static int is_supported_aibc1_file(const char* path)
{
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    AivmProgram program;
    AivmProgramLoadResult load_result;
    if (path == NULL || !file_exists(path)) {
        return 0;
    }
    if (!read_binary_file(path, &bytes, &byte_count)) {
        return 0;
    }
    aivm_program_init(&program, NULL, 0U);
    load_result = aivm_program_load_aibc1(bytes, byte_count, &program);
    free(bytes);
    aivm_program_clear(&program);
    return load_result.status == AIVM_PROGRAM_OK;
}

static int read_text_file(const char* path, char* out, size_t out_len)
{
    FILE* f;
    size_t n;

    if (path == NULL || out == NULL || out_len == 0U) {
        return 0;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }

    n = fread(out, 1U, out_len - 1U, f);
    fclose(f);
    out[n] = '\0';
    return 1;
}

static int read_text_file_alloc(const char* path, char** out_text)
{
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    char* text;
    if (path == NULL || out_text == NULL) {
        return 0;
    }
    *out_text = NULL;
    if (!read_binary_file(path, &bytes, &byte_count)) {
        return 0;
    }
    text = (char*)malloc(byte_count + 1U);
    if (text == NULL) {
        free(bytes);
        return 0;
    }
    if (byte_count > 0U) {
        memcpy(text, bytes, byte_count);
    }
    text[byte_count] = '\0';
    free(bytes);
    *out_text = text;
    return 1;
}

static int write_text_file(const char* path, const char* text)
{
    FILE* f;
    size_t len;
    if (path == NULL || text == NULL) {
        return 0;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    len = strlen(text);
    if (len > 0U && fwrite(text, 1U, len, f) != len) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static int join_path(const char* left, const char* right, char* out, size_t out_len)
{
    int n;
    if (left == NULL || right == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    n = snprintf(out, out_len, "%s%c%s", left, AIVM_PATH_SEP, right);
    return n >= 0 && (size_t)n < out_len;
}

static int canonical_existing_path(const char* path, char* out, size_t out_len)
{
    char resolved[PATH_MAX];
    int n;
    if (path == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (realpath(path, resolved) != NULL) {
        n = snprintf(out, out_len, "%s", resolved);
        return n >= 0 && (size_t)n < out_len;
    }
    n = snprintf(out, out_len, "%s", path);
    return n >= 0 && (size_t)n < out_len;
}

static int replace_ext(const char* path, const char* from_ext, const char* to_ext, char* out, size_t out_len)
{
    size_t path_len;
    size_t from_len;
    size_t to_len;

    if (path == NULL || from_ext == NULL || to_ext == NULL || out == NULL) {
        return 0;
    }

    path_len = strlen(path);
    from_len = strlen(from_ext);
    to_len = strlen(to_ext);

    if (path_len < from_len || strcmp(path + (path_len - from_len), from_ext) != 0) {
        return 0;
    }
    if (path_len - from_len + to_len + 1U > out_len) {
        return 0;
    }

    memcpy(out, path, path_len - from_len);
    memcpy(out + (path_len - from_len), to_ext, to_len);
    out[path_len - from_len + to_len] = '\0';
    return 1;
}

static int dirname_of(const char* path, char* out, size_t out_len)
{
    const char* slash;
    const char* bslash;
    size_t len;

    if (path == NULL || out == NULL || out_len == 0U) {
        return 0;
    }

    slash = strrchr(path, '/');
    bslash = strrchr(path, '\\');
    if (slash == NULL || (bslash != NULL && bslash > slash)) {
        slash = bslash;
    }

    if (slash == NULL) {
        if (out_len < 2U) {
            return 0;
        }
        out[0] = '.';
        out[1] = '\0';
        return 1;
    }

    len = (size_t)(slash - path);
    if (len == 0U) {
        if (out_len < 2U) {
            return 0;
        }
        out[0] = '.';
        out[1] = '\0';
        return 1;
    }

    if (len + 1U > out_len) {
        return 0;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return 1;
}

static int resolve_executable_path(const char* argv0, char* out, size_t out_len)
{
#ifdef _WIN32
    (void)argv0;
    if (out == NULL || out_len == 0U) {
        return 0;
    }
    return GetModuleFileNameA(NULL, out, (DWORD)out_len) > 0U;
#else
    if (out != NULL && out_len > 0U) {
        out[0] = '\0';
    }
    if (argv0 == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (realpath(argv0, out) != NULL) {
        return 1;
    }
    if (strchr(argv0, '/') == NULL && strchr(argv0, '\\') == NULL) {
        if (find_executable_on_path(argv0, out, out_len)) {
            return 1;
        }
    }
    return 0;
#endif
}

static int find_executable_on_path(const char* name, char* out, size_t out_len)
{
#ifdef _WIN32
    char full[PATH_MAX];
    DWORD found;
    if (name == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    found = SearchPathA(NULL, name, NULL, (DWORD)sizeof(full), full, NULL);
    if (found == 0U || found >= (DWORD)sizeof(full)) {
        return 0;
    }
    return snprintf(out, out_len, "%s", full) < (int)out_len;
#else
    const char* path_env;
    const char* segment;
    const char* end;

    if (name == NULL || out == NULL || out_len == 0U) {
        return 0;
    }

    path_env = getenv("PATH");
    if (path_env == NULL || path_env[0] == '\0') {
        return 0;
    }

    segment = path_env;
    while (segment != NULL && segment[0] != '\0') {
        char candidate[PATH_MAX];
        size_t segment_len;
        end = strchr(segment, ':');
        segment_len = (end == NULL) ? strlen(segment) : (size_t)(end - segment);
        if (segment_len > 0U && segment_len < sizeof(candidate)) {
            if (segment_len + 1U + strlen(name) + 1U < sizeof(candidate)) {
                int written;
                memcpy(candidate, segment, segment_len);
                candidate[segment_len] = '\0';
                written = snprintf(candidate + segment_len, sizeof(candidate) - segment_len, "%c%s", AIVM_PATH_SEP, name);
                if (written > 0 && (size_t)written < (sizeof(candidate) - segment_len) &&
                    realpath(candidate, out) != NULL) {
                    return 1;
                }
            }
        }
        if (end == NULL) {
            break;
        }
        segment = end + 1;
    }
    return 0;
#endif
}

static int extract_manifest_attr(const char* text, const char* key, char* out, size_t out_len)
{
    char needle[64];
    const char* pos;
    const char* value_start;
    const char* value_end;

    if (text == NULL || key == NULL || out == NULL || out_len == 0U) {
        return 0;
    }

    if (snprintf(needle, sizeof(needle), "%s=\"", key) >= (int)sizeof(needle)) {
        return 0;
    }

    pos = strstr(text, needle);
    if (pos == NULL) {
        return 0;
    }

    value_start = pos + strlen(needle);
    value_end = strchr(value_start, '"');
    if (value_end == NULL) {
        return 0;
    }

    if ((size_t)(value_end - value_start) + 1U > out_len) {
        return 0;
    }

    memcpy(out, value_start, (size_t)(value_end - value_start));
    out[value_end - value_start] = '\0';
    return 1;
}

static int resolve_input_to_aibc1(const char* input, char* out_path, size_t out_len)
{
    char manifest_path[PATH_MAX];
    char manifest_text[8192];
    char project_dir[PATH_MAX];
    char project_app_aibc1[PATH_MAX];
    char entry_file[PATH_MAX];
    char entry_aos[PATH_MAX];

    if (input == NULL || out_path == NULL || out_len == 0U) {
        return 0;
    }

    if (ends_with(input, ".aibc1") && file_exists(input)) {
        if (snprintf(out_path, out_len, "%s", input) >= (int)out_len) {
            return 0;
        }
        return 1;
    }

    if (ends_with(input, ".aos")) {
        if (!replace_ext(input, ".aos", ".aibc1", out_path, out_len)) {
            return 0;
        }
        return file_exists(out_path);
    }

    if (ends_with(input, "project.aiproj")) {
        if (snprintf(manifest_path, sizeof(manifest_path), "%s", input) >= (int)sizeof(manifest_path)) {
            return 0;
        }
        if (!dirname_of(input, project_dir, sizeof(project_dir))) {
            return 0;
        }
    } else if (directory_exists(input)) {
        if (!join_path(input, "project.aiproj", manifest_path, sizeof(manifest_path))) {
            return 0;
        }
        if (snprintf(project_dir, sizeof(project_dir), "%s", input) >= (int)sizeof(project_dir)) {
            return 0;
        }
    } else {
        return 0;
    }

    if (!file_exists(manifest_path) || !read_text_file(manifest_path, manifest_text, sizeof(manifest_text))) {
        return 0;
    }

    if (join_path(project_dir, "app.aibc1", project_app_aibc1, sizeof(project_app_aibc1)) &&
        file_exists(project_app_aibc1)) {
        return snprintf(out_path, out_len, "%s", project_app_aibc1) < (int)out_len;
    }

    if (!extract_manifest_attr(manifest_text, "entryFile", entry_file, sizeof(entry_file))) {
        return 0;
    }
    if (!join_path(project_dir, entry_file, entry_aos, sizeof(entry_aos))) {
        return 0;
    }
    if (!replace_ext(entry_aos, ".aos", ".aibc1", out_path, out_len)) {
        return 0;
    }
    return file_exists(out_path);
}

static int resolve_input_to_aos(const char* input, char* out_path, size_t out_len)
{
    char manifest_path[PATH_MAX];
    char manifest_text[8192];
    char project_dir[PATH_MAX];
    char entry_file[PATH_MAX];

    if (input == NULL || out_path == NULL || out_len == 0U) {
        return 0;
    }

    if (ends_with(input, ".aos") && file_exists(input)) {
        if (snprintf(out_path, out_len, "%s", input) >= (int)out_len) {
            return 0;
        }
        return 1;
    }

    if (ends_with(input, "project.aiproj")) {
        if (snprintf(manifest_path, sizeof(manifest_path), "%s", input) >= (int)sizeof(manifest_path)) {
            return 0;
        }
        if (!dirname_of(input, project_dir, sizeof(project_dir))) {
            return 0;
        }
    } else if (directory_exists(input)) {
        if (!join_path(input, "project.aiproj", manifest_path, sizeof(manifest_path))) {
            return 0;
        }
        if (snprintf(project_dir, sizeof(project_dir), "%s", input) >= (int)sizeof(project_dir)) {
            return 0;
        }
    } else {
        return 0;
    }

    if (!file_exists(manifest_path) || !read_text_file(manifest_path, manifest_text, sizeof(manifest_text))) {
        return 0;
    }
    if (!extract_manifest_attr(manifest_text, "entryFile", entry_file, sizeof(entry_file))) {
        return 0;
    }
    if (!join_path(project_dir, entry_file, out_path, out_len)) {
        return 0;
    }
    return file_exists(out_path);
}

static const char* path_basename_ptr(const char* path)
{
    const char* slash;
    const char* bslash;
    if (path == NULL) {
        return NULL;
    }
    slash = strrchr(path, '/');
    bslash = strrchr(path, '\\');
    if (slash == NULL || (bslash != NULL && bslash > slash)) {
        slash = bslash;
    }
    return (slash == NULL) ? path : (slash + 1);
}

static int strip_known_extension(const char* name, char* out, size_t out_len)
{
    size_t n;
    if (name == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    n = strlen(name);
    if (n >= 4U && memcmp(name + (n - 4U), ".aos", 4U) == 0) {
        n -= 4U;
    } else if (n >= 6U && memcmp(name + (n - 6U), ".aibc1", 6U) == 0) {
        n -= 6U;
    } else if (n >= 7U && memcmp(name + (n - 7U), ".aiproj", 7U) == 0) {
        n -= 7U;
    }
    if (n >= 3U && memcmp(name + (n - 3U), ".in", 3U) == 0) {
        n -= 3U;
    } else if (n >= 4U && memcmp(name + (n - 4U), ".out", 4U) == 0) {
        n -= 4U;
    }
    if (n == 0U || n + 1U > out_len) {
        return 0;
    }
    memcpy(out, name, n);
    out[n] = '\0';
    return 1;
}

static int sanitize_app_name(const char* input, char* out, size_t out_len)
{
    size_t i;
    size_t w = 0U;
    if (input == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    for (i = 0U; input[i] != '\0'; i += 1U) {
        char c = input[i];
        int keep = (c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') ||
                   c == '_' || c == '-';
        if (!keep) {
            continue;
        }
        if (w + 1U >= out_len) {
            return 0;
        }
        out[w++] = c;
    }
    if (w == 0U) {
        if (out_len < 4U) {
            return 0;
        }
        memcpy(out, "app", 4U);
        return 1;
    }
    out[w] = '\0';
    return 1;
}

static int derive_publish_app_name(const char* program_input, char* out_name, size_t out_len)
{
    char manifest_path[PATH_MAX];
    char manifest_text[8192];
    char raw_name[PATH_MAX];
    const char* base;

    if (program_input == NULL || out_name == NULL || out_len == 0U) {
        return 0;
    }
    if (ends_with(program_input, "project.aiproj")) {
        if (read_text_file(program_input, manifest_text, sizeof(manifest_text)) &&
            extract_manifest_attr(manifest_text, "name", raw_name, sizeof(raw_name))) {
            return sanitize_app_name(raw_name, out_name, out_len);
        }
    } else if (directory_exists(program_input)) {
        if (!join_path(program_input, "project.aiproj", manifest_path, sizeof(manifest_path))) {
            return 0;
        }
        if (read_text_file(manifest_path, manifest_text, sizeof(manifest_text)) &&
            extract_manifest_attr(manifest_text, "name", raw_name, sizeof(raw_name))) {
            return sanitize_app_name(raw_name, out_name, out_len);
        }
    }
    base = path_basename_ptr(program_input);
    if (base == NULL || !strip_known_extension(base, raw_name, sizeof(raw_name))) {
        return sanitize_app_name("app", out_name, out_len);
    }
    return sanitize_app_name(raw_name, out_name, out_len);
}

static int resolve_manifest_path_for_input(const char* input, char* out_manifest, size_t out_len)
{
    if (input == NULL || out_manifest == NULL || out_len == 0U) {
        return 0;
    }
    if (ends_with(input, "project.aiproj") && file_exists(input)) {
        return snprintf(out_manifest, out_len, "%s", input) < (int)out_len;
    }
    if (directory_exists(input)) {
        if (!join_path(input, "project.aiproj", out_manifest, out_len)) {
            return 0;
        }
        return file_exists(out_manifest);
    }
    return 0;
}

static int pick_single_target_from_list(const char* csv, char* out_target, size_t out_len)
{
    const char* p;
    const char* first_start;
    const char* first_end;
    const char* second_start;
    size_t n;
    if (csv == NULL || out_target == NULL || out_len == 0U) {
        return 0;
    }

    p = csv;
    while (*p == ' ' || *p == '\t') {
        p += 1;
    }
    first_start = p;
    while (*p != '\0' && *p != ',') {
        p += 1;
    }
    first_end = p;
    while (first_end > first_start && (first_end[-1] == ' ' || first_end[-1] == '\t')) {
        first_end -= 1;
    }
    if (first_end <= first_start) {
        return 0;
    }
    if (*p == ',') {
        p += 1;
        while (*p == ' ' || *p == '\t') {
            p += 1;
        }
        second_start = p;
        if (*second_start != '\0') {
            return -1;
        }
    }

    n = (size_t)(first_end - first_start);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out_target, first_start, n);
    out_target[n] = '\0';
    return 1;
}

static int resolve_publish_target_from_manifest(const char* program_input, char* out_target, size_t out_len)
{
    char manifest_path[PATH_MAX];
    char manifest_text[8192];
    char target_value[128];
    int pick_rc;

    if (program_input == NULL || out_target == NULL || out_len == 0U) {
        return 0;
    }
    if (!resolve_manifest_path_for_input(program_input, manifest_path, sizeof(manifest_path))) {
        return 0;
    }
    if (!read_text_file(manifest_path, manifest_text, sizeof(manifest_text))) {
        return 0;
    }

    if (extract_manifest_attr(manifest_text, "publishTarget", target_value, sizeof(target_value))) {
        if (snprintf(out_target, out_len, "%s", target_value) >= (int)out_len) {
            return 0;
        }
        return 1;
    }
    if (extract_manifest_attr(manifest_text, "publishTargets", target_value, sizeof(target_value))) {
        pick_rc = pick_single_target_from_list(target_value, out_target, out_len);
        if (pick_rc == -1) {
            return -1;
        }
        return pick_rc;
    }

    return 0;
}

static int resolve_publish_wasm_fullstack_host_target_from_manifest(const char* program_input, char* out_target, size_t out_len)
{
    char manifest_path[PATH_MAX];
    char manifest_text[8192];
    if (program_input == NULL || out_target == NULL || out_len == 0U) {
        return 0;
    }
    if (!resolve_manifest_path_for_input(program_input, manifest_path, sizeof(manifest_path))) {
        return 0;
    }
    if (!read_text_file(manifest_path, manifest_text, sizeof(manifest_text))) {
        return 0;
    }
    if (!extract_manifest_attr(manifest_text, "publishWasmFullstackHostTarget", out_target, out_len)) {
        return 0;
    }
    return 1;
}

static const char* host_rid(void)
{
#ifdef _WIN32
#if defined(_M_ARM64) || defined(__aarch64__)
    return "windows-arm64";
#else
    return "windows-x64";
#endif
#elif defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
    return "osx-arm64";
#else
    return "osx-x64";
#endif
#else
#if defined(__aarch64__) || defined(__arm64__)
    return "linux-arm64";
#else
    return "linux-x64";
#endif
#endif
}

static int parse_target_to_artifact(const char* rid, char* out_dir, size_t out_dir_len, char* out_bin, size_t out_bin_len)
{
    const char* platform;
    const char* arch;
    int n;

    if (rid == NULL || out_dir == NULL || out_bin == NULL) {
        return 0;
    }

    if (starts_with(rid, "osx-")) {
        platform = "osx";
        arch = rid + 4;
        if (strcmp(arch, "x64") != 0 && strcmp(arch, "arm64") != 0) {
            return 0;
        }
        n = snprintf(out_bin, out_bin_len, "aivm-runtime");
    } else if (starts_with(rid, "linux-")) {
        platform = "linux";
        arch = rid + 6;
        if (strcmp(arch, "x64") != 0 && strcmp(arch, "arm64") != 0) {
            return 0;
        }
        n = snprintf(out_bin, out_bin_len, "aivm-runtime");
    } else if (starts_with(rid, "windows-")) {
        platform = "windows";
        arch = rid + 8;
        if (strcmp(arch, "x64") != 0 && strcmp(arch, "arm64") != 0) {
            return 0;
        }
        n = snprintf(out_bin, out_bin_len, "aivm-runtime.exe");
    } else if (strcmp(rid, "wasm32") == 0) {
        n = snprintf(out_bin, out_bin_len, "aivm-runtime-wasm32.wasm");
        if (n < 0 || (size_t)n >= out_bin_len) {
            return 0;
        }
        n = snprintf(out_dir, out_dir_len, ".artifacts/aivm-wasm32");
        return n >= 0 && (size_t)n < out_dir_len;
    } else {
        return 0;
    }

    if (n < 0 || (size_t)n >= out_bin_len) {
        return 0;
    }

    n = snprintf(out_dir, out_dir_len, ".artifacts/ailang-%s-%s", platform, arch);
    return n >= 0 && (size_t)n < out_dir_len;
}

static int resolve_runtime_artifact_dir(const char* artifact_dir, char* out_dir, size_t out_dir_len)
{
    char exe_dir[PATH_MAX];
    char sdk_root[PATH_MAX];
    char sdk_artifact_dir[PATH_MAX];

    if (artifact_dir == NULL || out_dir == NULL || out_dir_len == 0U) {
        return 0;
    }
    if (directory_exists(artifact_dir)) {
        return 1;
    }
    if (g_airun_runtime_exe_path[0] == '\0' ||
        !dirname_of(g_airun_runtime_exe_path, exe_dir, sizeof(exe_dir)) ||
        !dirname_of(exe_dir, sdk_root, sizeof(sdk_root)) ||
        !join_path(sdk_root, artifact_dir, sdk_artifact_dir, sizeof(sdk_artifact_dir))) {
        return 1;
    }
    if (!directory_exists(sdk_artifact_dir)) {
        return 1;
    }
    return snprintf(out_dir, out_dir_len, "%s", sdk_artifact_dir) >= 0 &&
           strlen(sdk_artifact_dir) < out_dir_len;
}

static void print_usage(void)
{
#ifdef AIRUN_MINIMAL_RUNTIME
    fprintf(stderr,
        "Usage: aivm-runtime <command> [options]\n"
        "\n"
        "Commands:\n"
        "  run <program(.aibc1|.aos|project-dir|project.aiproj)> [--target <rid>] [--platform <console|desktop|web|server|tablet|phone|watch>] [--wasm-profile <cli|spa|fullstack>] [--out <dir>] [--port <n>] [--no-open] [--vm=<selector>] [--no-cache] [--] [app-args...]\n"
        "  version | --version\n"
        "\n"
        "VM selectors:\n"
        "  c      current stable C VM (default)\n"
        "  cvN    reserved future C VM profile/version selector (currently maps to c)\n"
        "\n"
        "Target/platform:\n"
        "  --target selects runtime output and derives the default UI platform.\n"
        "  --platform is an advanced layout/input-profile override.\n");
#else
    fprintf(stderr,
        "Usage: ailang <command> [options]\n"
        "\n"
        "Commands:\n"
        "  run <program(.aibc1|.aos|project-dir|project.aiproj)> [--target <rid>] [--platform <console|desktop|web|server|tablet|phone|watch>] [--wasm-profile <cli|spa|fullstack>] [--out <dir>] [--port <n>] [--no-open] [--vm=<selector>] [--no-cache] [--] [app-args...]\n"
        "  build <program(.aibc1|.aos|project-dir|project.aiproj)> [--out <dir>] [--no-cache]\n"
        "  init <project-dir> [--template <cli|cli-args>] [--agent <codex|claude|cursor|gemini|copilot|windsurf>] [--agents <list|all>] [--force]\n"
        "  template list [projects|files] [project-dir]\n"
        "  package <list|restore> [project-dir]\n"
        "  package add <name[@version]> [project-dir]\n"
        "  package remove <name> [project-dir]\n"
        "  test [project-dir] [--no-cache]\n"
        "  clean [program(.aibc1|.aos|project-dir|project.aiproj)]\n"
        "  repl\n"
        "  bench [--iterations <n>] [--human]\n"
        "  debug run <program(.aibc1|.aos|project-dir|project.aiproj)> [--vm=<selector>] [--out <dir>] [--log-level <off|error|info|trace>] [--inject-click <x,y>] [--inject-key <name>] [--inject-key-at <x,y,key[,text]>] [--inject-text <text>] [--inject-text-at <x,y,text>] [--inject-wait <polls>] [--inject-close] [--inject-script <path>] [--] [app-args...]\n"
        "  debug trace run <program(.aibc1|.aos|project-dir|project.aiproj)> [--vm=<selector>] [--out <dir>] [--log-level <off|error|info|trace>] [--inject-click <x,y>] [--inject-key <name>] [--inject-key-at <x,y,key[,text]>] [--inject-text <text>] [--inject-text-at <x,y,text>] [--inject-wait <polls>] [--inject-close] [--inject-script <path>] [--] [app-args...]\n"
        "  debug capture run <program(.aibc1|.aos|project-dir|project.aiproj)> [--vm=<selector>] [--out <dir>] [--log-level <off|error|info|trace>] [--inject-click <x,y>] [--inject-key <name>] [--inject-key-at <x,y,key[,text]>] [--inject-text <text>] [--inject-text-at <x,y,text>] [--inject-wait <polls>] [--inject-close] [--inject-script <path>] [--] [app-args...]\n"
        "  debug interact run <program(.aibc1|.aos|project-dir|project.aiproj)> [--vm=<selector>] [--out <dir>] [--log-level <off|error|info|trace>] [--] [app-args...]\n"
        "  debug profile <program(.aibc1|.aos|project-dir|project.aiproj)> [--iterations <n>] [--max-growth-kb <kb>] [--out <file>] [--vm=<selector>] [--no-cache] [--] [app-args...]\n"
        "  debug dns <host> [port]\n"
        "  debug disasm <program(.aibc1|.aos|project-dir|project.aiproj)> [start] [end]\n"
        "  publish <program(.aibc1|.aos|project-dir|project.aiproj)> [--target <rid>] [--wasm-profile <cli|spa|fullstack>] [--wasm-fullstack-host-target <rid>] [--out <dir>]\n"
        "  version | --version\n"
        "\n"
        "VM selectors:\n"
        "  c      current stable C VM (default)\n"
        "  cvN    reserved future C VM profile/version selector (currently maps to c)\n"
        "\n"
        "Target/platform:\n"
        "  --target selects runtime output and derives the default UI platform.\n"
        "  --platform is an advanced layout/input-profile override.\n");
#endif
}

static int print_unsupported_vm_mode(const char* mode)
{
    if (mode != NULL && strcmp(mode, "ast") == 0) {
        fprintf(stderr,
            "Err#err1(code=DEV008 message=\"AST VM mode is debug-only and not supported by native runtime.\" nodeId=vmMode)\n");
        return 2;
    }
    fprintf(stderr,
        "Err#err1(code=DEV008 message=\"Unsupported VM mode for native C runtime: %s\" nodeId=vmMode)\n",
        mode != NULL ? mode : "(null)");
    return 2;
}

static int parse_int(const char* text, int* out);
static int parse_nonnegative_int(const char* text, int* out);

typedef struct {
    const char* name;
    const char* description;
} AirunInitTemplate;

enum {
    AIRUN_AGENT_CODEX = 1 << 0,
    AIRUN_AGENT_CLAUDE = 1 << 1,
    AIRUN_AGENT_CURSOR = 1 << 2,
    AIRUN_AGENT_GEMINI = 1 << 3,
    AIRUN_AGENT_COPILOT = 1 << 4,
    AIRUN_AGENT_WINDSURF = 1 << 5,
    AIRUN_AGENT_ALL = AIRUN_AGENT_CODEX |
        AIRUN_AGENT_CLAUDE |
        AIRUN_AGENT_CURSOR |
        AIRUN_AGENT_GEMINI |
        AIRUN_AGENT_COPILOT |
        AIRUN_AGENT_WINDSURF
};

static const AirunInitTemplate g_airun_init_templates[] = {
    { "cli", "Basic CLI app that prints a greeting." },
    { "cli-args", "CLI app that prints the first app argument." }
};

static int sanitize_project_name(const char* raw, char* out, size_t out_len)
{
    size_t i = 0U;
    size_t w = 0U;
    int previous_dash = 0;
    if (raw == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    while (raw[i] != '\0') {
        unsigned char ch = (unsigned char)raw[i];
        if (isalnum(ch)) {
            if (w + 1U >= out_len) {
                return 0;
            }
            out[w++] = (char)tolower(ch);
            previous_dash = 0;
        } else if ((ch == '-' || ch == '_' || isspace(ch)) && !previous_dash) {
            if (w + 1U >= out_len) {
                return 0;
            }
            out[w++] = '-';
            previous_dash = 1;
        }
        i += 1U;
    }
    while (w > 0U && out[w - 1U] == '-') {
        w -= 1U;
    }
    if (w == 0U) {
        if (out_len < 4U) {
            return 0;
        }
        memcpy(out, "app", 4U);
        return 1;
    }
    out[w] = '\0';
    return 1;
}

static const AirunInitTemplate* find_init_template(const char* name)
{
    size_t i;
    const char* wanted = (name == NULL || name[0] == '\0') ? "cli" : name;
    for (i = 0U; i < (sizeof(g_airun_init_templates) / sizeof(g_airun_init_templates[0])); i += 1U) {
        if (strcmp(g_airun_init_templates[i].name, wanted) == 0) {
            return &g_airun_init_templates[i];
        }
    }
    return NULL;
}

static int init_agent_flag_for_name(const char* name, size_t len, int* out_flag)
{
    if (name == NULL || out_flag == NULL) {
        return 0;
    }
    if (len == 3U && strncmp(name, "all", len) == 0) {
        *out_flag = AIRUN_AGENT_ALL;
        return 1;
    }
    if (len == 5U && strncmp(name, "codex", len) == 0) {
        *out_flag = AIRUN_AGENT_CODEX;
        return 1;
    }
    if (len == 6U && strncmp(name, "claude", len) == 0) {
        *out_flag = AIRUN_AGENT_CLAUDE;
        return 1;
    }
    if (len == 6U && strncmp(name, "cursor", len) == 0) {
        *out_flag = AIRUN_AGENT_CURSOR;
        return 1;
    }
    if (len == 6U && strncmp(name, "gemini", len) == 0) {
        *out_flag = AIRUN_AGENT_GEMINI;
        return 1;
    }
    if (len == 7U && strncmp(name, "copilot", len) == 0) {
        *out_flag = AIRUN_AGENT_COPILOT;
        return 1;
    }
    if (len == 8U && strncmp(name, "windsurf", len) == 0) {
        *out_flag = AIRUN_AGENT_WINDSURF;
        return 1;
    }
    return 0;
}

static int parse_init_agent_list(const char* text, int* flags)
{
    const char* start;
    const char* p;
    int flag;
    if (text == NULL || flags == NULL || text[0] == '\0') {
        return 0;
    }
    start = text;
    p = text;
    while (1) {
        if (*p == ',' || *p == '\0') {
            size_t len = (size_t)(p - start);
            while (len > 0U && isspace((unsigned char)*start)) {
                start += 1;
                len -= 1U;
            }
            while (len > 0U && isspace((unsigned char)start[len - 1U])) {
                len -= 1U;
            }
            if (len == 0U || !init_agent_flag_for_name(start, len, &flag)) {
                return 0;
            }
            *flags |= flag;
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
        p += 1;
    }
    return 1;
}

static int render_init_project_file(const char* project_name, char* out, size_t out_len)
{
    int n;
    if (project_name == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    n = snprintf(
        out,
        out_len,
        "Program#p1 {\n"
        "  Project#proj1(\n"
        "    name=\"%s\"\n"
        "    entryFile=\"src/app.aos\"\n"
        "    entryExport=\"start\"\n"
        "  )\n"
        "}\n",
        project_name);
    return n >= 0 && (size_t)n < out_len;
}

static int render_init_app_file(const char* template_name, const char* project_name, char* out, size_t out_len)
{
    int n;
    if (template_name == NULL || project_name == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (strcmp(template_name, "cli-args") == 0) {
        n = snprintf(
            out,
            out_len,
            "Program#p1 {\n"
            "  Export#e1(name=start)\n"
            "\n"
            "  Let#l1(name=start) {\n"
            "    Fn#f1(params=args) {\n"
            "      Block#b1 {\n"
            "        Let#l2(name=count) { ChildCount#cc1 { Var#v1(name=args) } }\n"
            "        If#i1 {\n"
            "          Eq#eq1 { Var#v2(name=count) Lit#i1(value=0) }\n"
            "          Block#b2 {\n"
            "            Call#c1(target=sys.stdout.writeLine) { Lit#s1(value=\"%s: no app args\") }\n"
            "            Return#r1 { Lit#i2(value=0) }\n"
            "          }\n"
            "          Block#b3 {\n"
            "            Call#c2(target=sys.stdout.writeLine) {\n"
            "              AttrValueString#a1 {\n"
            "                ChildAt#ca1 { Var#v3(name=args) Lit#i3(value=0) }\n"
            "                Lit#i4(value=0)\n"
            "              }\n"
            "            }\n"
            "            Return#r2 { Lit#i5(value=0) }\n"
            "          }\n"
            "        }\n"
            "      }\n"
            "    }\n"
            "  }\n"
            "}\n",
            project_name);
        return n >= 0 && (size_t)n < out_len;
    }

    n = snprintf(
        out,
        out_len,
        "Program#p1 {\n"
        "  Export#e1(name=start)\n"
        "\n"
        "  Let#l1(name=start) {\n"
        "    Fn#f1(params=args) {\n"
        "      Block#b1 {\n"
        "        Call#c1(target=sys.stdout.writeLine) { Lit#s1(value=\"Hello from %s\") }\n"
        "        Return#r1 { Lit#i1(value=0) }\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n",
        project_name);
    return n >= 0 && (size_t)n < out_len;
}

static const char* init_agents_doc_text(void)
{
    return
        "# Agent Rules\n"
        "\n"
        "- Prefer AiLang built-in tooling.\n"
        "- Keep behavior deterministic and output reviewable.\n"
        "- Entry function is `Export start(args)`.\n"
        "- Return an integer exit code from command-line apps.\n"
        "- Project layout: `project.aiproj`, `src/app.aos`, and optional `Assets/` data.\n"
        "- Use `.aos` for executable code and `.toml` for project data.\n";
}

static int write_agent_pointer_file(const char* path, const char* agent_name)
{
    char text[512];
    int n;
    if (path == NULL || agent_name == NULL) {
        return 0;
    }
    n = snprintf(
        text,
        sizeof(text),
        "# %s Instructions\n"
        "\n"
        "Use `AGENTS.md` in this project root as the canonical AiLang agent instruction file.\n",
        agent_name);
    return n >= 0 && (size_t)n < sizeof(text) && write_text_file(path, text);
}

static int write_cursor_rule_file(const char* path)
{
    return write_text_file(
        path,
        "---\n"
        "description: AiLang project agent rules\n"
        "globs: **/*\n"
        "alwaysApply: true\n"
        "---\n"
        "\n"
        "Use `AGENTS.md` in this project root as the canonical AiLang agent instruction file.\n");
}

static AIRUN_MAYBE_UNUSED int handle_init(int argc, char** argv)
{
    const char* project_dir = NULL;
    const char* template_name = "cli";
    const AirunInitTemplate* template_def;
    int agent_flags = AIRUN_AGENT_CODEX;
    char project_name[128];
    char project_file[PATH_MAX];
    char agents_file[PATH_MAX];
    char claude_file[PATH_MAX];
    char gemini_file[PATH_MAX];
    char windsurf_file[PATH_MAX];
    char cursor_dir[PATH_MAX];
    char cursor_rules_dir[PATH_MAX];
    char cursor_file[PATH_MAX];
    char github_dir[PATH_MAX];
    char copilot_file[PATH_MAX];
    char gitignore_file[PATH_MAX];
    char src_dir[PATH_MAX];
    char app_file[PATH_MAX];
    char project_text[1024];
    char app_text[4096];
    int force = 0;
    int64_t existing_entries = 0;
    int i;

    for (i = 2; i < argc; i += 1) {
        if (strcmp(argv[i], "--template") == 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --template value.\" nodeId=argv)\n");
                return 2;
            }
            template_name = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--agent") == 0 || strcmp(argv[i], "--agents") == 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --agent value.\" nodeId=argv)\n");
                return 2;
            }
            if (!parse_init_agent_list(argv[++i], &agent_flags)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Unknown init agent.\" nodeId=agent)\n");
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "--force") == 0) {
            force = 1;
            continue;
        }
        if (project_dir == NULL && argv[i][0] != '-') {
            project_dir = argv[i];
            continue;
        }
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Unsupported init argument.\" nodeId=argv)\n");
        return 2;
    }

    if (project_dir == NULL) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Missing init project directory.\" nodeId=argv)\n");
        return 2;
    }

    template_def = find_init_template(template_name);
    if (template_def == NULL) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Unknown init template.\" nodeId=template)\n");
        return 2;
    }

    if (!sanitize_project_name(path_basename_ptr(project_dir), project_name, sizeof(project_name))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to derive project name.\" nodeId=project)\n");
        return 2;
    }

    if (directory_exists(project_dir)) {
        if (!native_fs_dir_count_entries(project_dir, &existing_entries)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Failed to inspect init target directory.\" nodeId=project)\n");
            return 2;
        }
        if (existing_entries > 0 && !force) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Init target directory is not empty. Use --force to scaffold into an existing directory.\" nodeId=project)\n");
            return 2;
        }
    } else if (!ensure_directory_recursive(project_dir)) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to create init project directory.\" nodeId=project)\n");
        return 2;
    }

    if (!join_path(project_dir, "src", src_dir, sizeof(src_dir)) ||
        !ensure_directory_recursive(src_dir) ||
        !join_path(project_dir, "project.aiproj", project_file, sizeof(project_file)) ||
        !join_path(project_dir, "AGENTS.md", agents_file, sizeof(agents_file)) ||
        !join_path(project_dir, "CLAUDE.md", claude_file, sizeof(claude_file)) ||
        !join_path(project_dir, "GEMINI.md", gemini_file, sizeof(gemini_file)) ||
        !join_path(project_dir, ".windsurfrules", windsurf_file, sizeof(windsurf_file)) ||
        !join_path(project_dir, ".cursor", cursor_dir, sizeof(cursor_dir)) ||
        !join_path(cursor_dir, "rules", cursor_rules_dir, sizeof(cursor_rules_dir)) ||
        !join_path(cursor_rules_dir, "ailang.mdc", cursor_file, sizeof(cursor_file)) ||
        !join_path(project_dir, ".github", github_dir, sizeof(github_dir)) ||
        !join_path(github_dir, "copilot-instructions.md", copilot_file, sizeof(copilot_file)) ||
        !join_path(project_dir, ".gitignore", gitignore_file, sizeof(gitignore_file)) ||
        !join_path(src_dir, "app.aos", app_file, sizeof(app_file))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Init path overflow or directory creation failed.\" nodeId=project)\n");
        return 2;
    }

    if (!render_init_project_file(project_name, project_text, sizeof(project_text)) ||
        !render_init_app_file(template_def->name, project_name, app_text, sizeof(app_text)) ||
        !write_text_file(project_file, project_text) ||
        !write_text_file(agents_file, init_agents_doc_text()) ||
        !write_text_file(
            gitignore_file,
            ".toolchain/\n"
            "dist/\n"
            ".artifacts/\n"
            "config.local.toml\n") ||
        !write_text_file(app_file, app_text)) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to emit init template files.\" nodeId=project)\n");
        return 2;
    }

    if (((agent_flags & AIRUN_AGENT_CLAUDE) != 0 &&
            !write_agent_pointer_file(claude_file, "Claude")) ||
        ((agent_flags & AIRUN_AGENT_GEMINI) != 0 &&
            !write_agent_pointer_file(gemini_file, "Gemini")) ||
        ((agent_flags & AIRUN_AGENT_WINDSURF) != 0 &&
            !write_agent_pointer_file(windsurf_file, "Windsurf")) ||
        ((agent_flags & AIRUN_AGENT_CURSOR) != 0 &&
            (!ensure_directory_recursive(cursor_rules_dir) || !write_cursor_rule_file(cursor_file))) ||
        ((agent_flags & AIRUN_AGENT_COPILOT) != 0 &&
            (!ensure_directory_recursive(github_dir) || !write_agent_pointer_file(copilot_file, "GitHub Copilot")))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to emit init agent files.\" nodeId=agent)\n");
        return 2;
    }

    printf("Ok#ok1(type=string value=\"%s\")\n", project_dir);
    return 0;
}

static int is_reserved_cv_selector(const char* mode)
{
    size_t i;
    if (mode == NULL || mode[0] != 'c' || mode[1] != 'v' || mode[2] == '\0') {
        return 0;
    }
    for (i = 2U; mode[i] != '\0'; i += 1U) {
        if (!isdigit((unsigned char)mode[i])) {
            return 0;
        }
    }
    return 1;
}

static int wasm_profile_is_valid(const char* profile)
{
    if (profile == NULL) {
        return 0;
    }
    return strcmp(profile, "cli") == 0 ||
           strcmp(profile, "spa") == 0 ||
           strcmp(profile, "web") == 0 ||
           strcmp(profile, "fullstack") == 0;
}

static const char* wasm_profile_normalize(const char* profile)
{
    if (profile == NULL || strcmp(profile, "web") == 0) {
        return "spa";
    }
    return profile;
}

static int wasm_browser_net_target_is_supported(const char* target)
{
    return target != NULL &&
        (strcmp(target, "sys.net.tcp.connectStart") == 0 ||
         strcmp(target, "sys.net.tcp.connectTlsStart") == 0 ||
         strcmp(target, "sys.net.tcp.readStart") == 0 ||
         strcmp(target, "sys.net.tcp.writeStart") == 0 ||
         strcmp(target, "sys.net.tcp.close") == 0 ||
         strcmp(target, "sys.net.async.poll") == 0 ||
         strcmp(target, "sys.net.async.await") == 0 ||
         strcmp(target, "sys.net.async.cancel") == 0 ||
         strcmp(target, "sys.net.asyncCancel") == 0 ||
         strcmp(target, "sys.net.async.resultInt") == 0 ||
         strcmp(target, "sys.net.async.resultBytes") == 0 ||
         strcmp(target, "sys.net.async.error") == 0);
}

static int wasm_syscall_unavailable_for_profile(const char* profile, const char* target)
{
    if (profile == NULL || target == NULL) {
        return 0;
    }
    if (strcmp(profile, "cli") == 0) {
        return strcmp(target, "sys.process.spawn") == 0 ||
               strcmp(target, "sys.process.wait") == 0 ||
               strcmp(target, "sys.process.kill") == 0 ||
               strcmp(target, "sys.process.stdout.read") == 0 ||
               strcmp(target, "sys.process.stderr.read") == 0 ||
               strcmp(target, "sys.process.poll") == 0 ||
               strcmp(target, "sys.image.decodeToRgbaBase64") == 0 ||
               strncmp(target, "sys.ui.", 7U) == 0 ||
               strncmp(target, "sys.ui_", 7U) == 0;
    }
    if (strcmp(profile, "spa") == 0 || strcmp(profile, "fullstack") == 0) {
        return strcmp(target, "sys.process.spawn") == 0 ||
               strcmp(target, "sys.process.wait") == 0 ||
               strcmp(target, "sys.process.kill") == 0 ||
               strcmp(target, "sys.process.stdout.read") == 0 ||
               strcmp(target, "sys.process.stderr.read") == 0 ||
               strcmp(target, "sys.process.poll") == 0 ||
               strcmp(target, "sys.image.decodeToRgbaBase64") == 0 ||
               (strncmp(target, "sys.net.", 8U) == 0 && !wasm_browser_net_target_is_supported(target)) ||
               strncmp(target, "sys.fs.", 7U) == 0;
    }
    return 0;
}

static int syscall_target_already_reported(const AivmProgram* program, size_t index, const char* target)
{
    size_t j;
    if (program == NULL || target == NULL) {
        return 0;
    }
    for (j = 0U; j < index; j += 1U) {
        const char* prior_target;
        if (program->constants[j].type != AIVM_VAL_STRING || program->constants[j].string_value == NULL) {
            continue;
        }
        prior_target = program->constants[j].string_value;
        if (strncmp(prior_target, "sys.", 4U) != 0) {
            continue;
        }
        if (strcmp(prior_target, target) == 0) {
            return 1;
        }
    }
    return 0;
}

static void emit_wasm_profile_warnings(const char* profile, const AivmProgram* program)
{
    size_t i;
    if (profile == NULL || program == NULL) {
        return;
    }
    for (i = 0U; i < program->constant_count; i += 1U) {
        const char* target = NULL;
        if (program->constants[i].type != AIVM_VAL_STRING || program->constants[i].string_value == NULL) {
            continue;
        }
        target = program->constants[i].string_value;
        if (strncmp(target, "sys.", 4U) != 0) {
            continue;
        }
        if (!wasm_syscall_unavailable_for_profile(profile, target)) {
            continue;
        }
        if (!syscall_target_already_reported(program, i, target)) {
            fprintf(
                stderr,
                "Warn#warn1(code=WASM001 message=\"%s is not available on wasm profile '%s'; runtime will raise RUN101 if executed.\" nodeId=publish)\n",
                target,
                profile);
        }
    }
}

static int native_syscall_requires_host_ui_capability(const char* syscall_target)
{
    if (syscall_target == NULL) {
        return 0;
    }
    return strncmp(syscall_target, "sys.ui.", 7U) == 0 ||
           strncmp(syscall_target, "sys.ui_", 7U) == 0;
}

static int native_syscall_requires_host_process_capability(const char* syscall_target)
{
    if (syscall_target == NULL) {
        return 0;
    }
    return strcmp(syscall_target, "sys.process.spawn") == 0 ||
           strcmp(syscall_target, "sys.process.wait") == 0 ||
           strcmp(syscall_target, "sys.process.kill") == 0 ||
           strcmp(syscall_target, "sys.process.stdout.read") == 0 ||
           strcmp(syscall_target, "sys.process.stderr.read") == 0 ||
           strcmp(syscall_target, "sys.process.poll") == 0;
}

static int native_syscall_requires_host_fs_capability(const char* syscall_target)
{
    if (syscall_target == NULL) {
        return 0;
    }
    return strncmp(syscall_target, "sys.fs.", 7U) == 0 ||
           strncmp(syscall_target, "sys.fs_", 7U) == 0;
}

static int native_syscall_requires_host_net_capability(const char* syscall_target)
{
    if (syscall_target == NULL) {
        return 0;
    }
    return strncmp(syscall_target, "sys.net.", 8U) == 0 ||
           strncmp(syscall_target, "sys.net_", 8U) == 0;
}

static void emit_native_target_capability_warnings(const char* target_rid, const AivmProgram* program)
{
    size_t i;
    if (target_rid == NULL || program == NULL) {
        return;
    }
    for (i = 0U; i < program->constant_count; i += 1U) {
        const char* target = NULL;
        if (program->constants[i].type != AIVM_VAL_STRING || program->constants[i].string_value == NULL) {
            continue;
        }
        target = program->constants[i].string_value;
        if (syscall_target_already_reported(program, i, target)) {
            continue;
        }
        if (native_syscall_requires_host_ui_capability(target)) {
            fprintf(
                stderr,
                "Warn#warn1(code=CAP001 message=\"%s requires host UI capability on target '%s'; runtime will raise AIVMS001 if unavailable.\" nodeId=publish)\n",
                target,
                target_rid);
            continue;
        }
        if (native_syscall_requires_host_process_capability(target)) {
            fprintf(
                stderr,
                "Warn#warn1(code=CAP002 message=\"%s requires host process capability on target '%s'; runtime will raise AIVMS001 if unavailable.\" nodeId=publish)\n",
                target,
                target_rid);
            continue;
        }
        if (native_syscall_requires_host_fs_capability(target)) {
            fprintf(
                stderr,
                "Warn#warn1(code=CAP003 message=\"%s requires host filesystem capability on target '%s'; runtime will raise AIVMS001 if unavailable.\" nodeId=publish)\n",
                target,
                target_rid);
            continue;
        }
        if (native_syscall_requires_host_net_capability(target)) {
            fprintf(
                stderr,
                "Warn#warn1(code=CAP004 message=\"%s requires host network capability on target '%s'; runtime will raise AIVMS001 if unavailable.\" nodeId=publish)\n",
                target,
                target_rid);
        }
    }
}

static int emit_wasm_cli_launchers(const char* out_dir, const char* runtime_name, const char* app_name)
{
    char run_sh_path[PATH_MAX];
    char run_ps1_path[PATH_MAX];
    char run_sh[1024];
    char run_ps1[1024];
    if (out_dir == NULL || runtime_name == NULL || app_name == NULL) {
        return 0;
    }
    if (!join_path(out_dir, "run.sh", run_sh_path, sizeof(run_sh_path)) ||
        !join_path(out_dir, "run.ps1", run_ps1_path, sizeof(run_ps1_path))) {
        return 0;
    }
    if (snprintf(
            run_sh,
            sizeof(run_sh),
            "#!/usr/bin/env bash\nset -euo pipefail\nDIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\nexec wasmtime run -C cache=n \"${DIR}/%s\" aibc-stdin < \"${DIR}/app.aibc1\"\n",
            runtime_name) >= (int)sizeof(run_sh)) {
        return 0;
    }
    if (snprintf(
            run_ps1,
            sizeof(run_ps1),
            "$ErrorActionPreference = 'Stop'\n$dir = Split-Path -Parent $MyInvocation.MyCommand.Path\nwasmtime run -C cache=n \"$dir/%s\" aibc-stdin < \"$dir/app.aibc1\"\n",
            runtime_name) >= (int)sizeof(run_ps1)) {
        return 0;
    }
    if (!write_text_file(run_sh_path, run_sh) || !write_text_file(run_ps1_path, run_ps1)) {
        return 0;
    }
#ifndef _WIN32
    if (chmod(run_sh_path, 0755) != 0) {
        return 0;
    }
#endif
    (void)app_name;
    return 1;
}

static int emit_wasm_spa_files(const char* out_dir)
{
    char index_path[PATH_MAX];
    char main_path[PATH_MAX];
    char remote_client_path[PATH_MAX];
    char index_html[2048];
    char main_js[32768];
    char remote_client_js[8192];
    if (out_dir == NULL) {
        return 0;
    }
    if (!join_path(out_dir, "index.html", index_path, sizeof(index_path)) ||
        !join_path(out_dir, "main.js", main_path, sizeof(main_path)) ||
        !join_path(out_dir, "remote-client.js", remote_client_path, sizeof(remote_client_path))) {
        return 0;
    }
    if (snprintf(
            index_html,
            sizeof(index_html),
            "<!doctype html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"utf-8\">\n  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">\n  <title>AiLang wasm app</title>\n  <style>\n    html, body { width: 100%%; height: 100%%; margin: 0; overflow: hidden; }\n    body { background: #0f172a; }\n    #output:empty { display: none; }\n    [data-aivm-window-id] { width: 100%%; height: 100%%; overflow: hidden; }\n    [data-aivm-window-id] > svg { display: block; width: 100%%; height: 100%%; border: 0; outline: none; touch-action: none; }\n  </style>\n</head>\n<body>\n  <pre id=\"output\"></pre>\n  <script type=\"module\" src=\"./main.js\"></script>\n</body>\n</html>\n") >= (int)sizeof(index_html)) {
        return 0;
    }
    {
        const char* main_js_head =
            "import createRuntime from './aivm-runtime-wasm32-web.mjs';\n"
            "import { createAivmRemoteClient } from './remote-client.js';\n"
            "const output = document.getElementById('output');\n"
            "const endpoint = globalThis.AIVM_REMOTE_WS_ENDPOINT || (`ws://${location.hostname}:8765`);\n"
            "const aiLangRoot = globalThis.AiLang || (globalThis.AiLang = {});\n"
            "const remoteMode = globalThis.AIVM_REMOTE_MODE || 'ws';\n"
            "if (remoteMode !== 'ws' && remoteMode !== 'js') {\n"
            "  throw new Error(`RUN101: unsupported AIVM_REMOTE_MODE '${remoteMode}' (expected 'ws' or 'js')`);\n"
            "}\n"
            "const stdinQueue = [];\n"
            "let stdinClosed = false;\n"
            "const uiState = { windows: new Map() };\n"
            "const uiMeasureCanvas = (typeof document !== 'undefined' && typeof document.createElement === 'function') ? document.createElement('canvas') : null;\n"
            "const uiMeasureContext = uiMeasureCanvas ? uiMeasureCanvas.getContext('2d') : null;\n"
            "function xmlEscape(text) {\n"
            "  return String(text ?? '').replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('\"', '&quot;').replaceAll(\"'\", '&apos;');\n"
            "}\n";
        const char* main_js_head2 =
            "function uiEventModifiers(ev) {\n"
            "  const mods = [];\n"
            "  if (ev?.altKey) mods.push('alt');\n"
            "  if (ev?.ctrlKey) mods.push('ctrl');\n"
            "  if (ev?.metaKey) mods.push('meta');\n"
            "  if (ev?.shiftKey) mods.push('shift');\n"
            "  return mods.join(',');\n"
            "}\n"
            "function uiEventKey(key) {\n"
            "  const raw = String(key ?? '');\n"
            "  if (raw.length === 1) return raw;\n"
            "  if (raw === 'Backspace') return 'backspace';\n"
            "  if (raw === 'Delete') return 'delete';\n"
            "  if (raw === 'Enter') return 'enter';\n"
            "  if (raw === 'Escape') return 'escape';\n"
            "  if (raw === 'Tab') return 'tab';\n"
            "  if (raw === 'ArrowLeft') return 'left';\n"
            "  if (raw === 'ArrowRight') return 'right';\n"
            "  if (raw === 'ArrowUp') return 'up';\n"
            "  if (raw === 'ArrowDown') return 'down';\n"
            "  return raw.toLowerCase();\n"
            "}\n"
            "function uiEventText(ev) {\n"
            "  const key = String(ev?.key ?? '');\n"
            "  if (key.length === 1 && !ev?.ctrlKey && !ev?.metaKey) return key;\n"
            "  return '';\n"
            "}\n"
            "function uiEventTargetId(ev) {\n"
            "  const target = ev?.target;\n"
            "  if (target && typeof target.getAttribute === 'function') {\n"
            "    return String(target.getAttribute('data-aivm-id') ?? '');\n"
            "  }\n"
            "  return '';\n"
            "}\n";
        const char* main_js_head3 =
            "function ensureUiWindow(windowId, title, width, height) {\n"
            "  if (typeof document === 'undefined' || !document || !document.createElement) return null;\n"
            "  const existing = uiState.windows.get(windowId);\n"
            "  if (existing) return existing;\n"
            "  const host = document.createElement('div');\n"
            "  host.setAttribute('data-aivm-window-id', String(windowId));\n"
            "  host.style.width = '100%';\n"
            "  host.style.height = '100%';\n"
            "  const label = document.createElement('div');\n"
            "  label.textContent = String(title ?? 'AiLang');\n"
            "  label.style.display = 'none';\n"
            "  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');\n"
            "  svg.setAttribute('width', '100%');\n"
            "  svg.setAttribute('height', '100%');\n"
            "  svg.setAttribute('viewBox', `0 0 ${width} ${height}`);\n"
            "  svg.setAttribute('preserveAspectRatio', 'none');\n"
            "  svg.setAttribute('aria-label', String(title ?? 'AiLang'));\n"
            "  svg.setAttribute('tabindex', '0');\n"
            "  svg.style.display = 'block';\n"
            "  svg.style.width = '100%';\n"
            "  svg.style.height = '100%';\n"
            "  svg.style.border = '0';\n"
            "  svg.style.outline = 'none';\n"
            "  svg.style.touchAction = 'none';\n"
            "  host.appendChild(label);\n"
            "  host.appendChild(svg);\n"
            "  (document.body || document.documentElement).appendChild(host);\n"
            "  const minLogicalWidth = Math.min(640, Math.max(1, width | 0));\n"
            "  const state = { host, svg, width, height, frameParts: [], nextElementId: 1, focusedTargetId: '', closed: false, closeConsumed: false, resizeHandler: null, pointerHandler: null, pointerMoveHandler: null, pointerUpHandler: null, wheelHandler: null, clickHandler: null, touchHandler: null, keyDownHandler: null, blurHandler: null, pointerDown: false, lastPointer: null, eventQueue: [], lastPolledEvent: { type: 'none', targetId: '', x: -1, y: -1, dx: 0, dy: 0, key: '', text: '', modifiers: '', repeat: false } };\n"
            "  const pushEvent = (evt) => { state.eventQueue.push(evt); if (state.eventQueue.length > 64) state.eventQueue.shift(); };\n"
            "  const clampToWindow = (x, y) => {\n"
            "    const maxX = Math.max(0, (state.width | 0) - 1);\n"
            "    const maxY = Math.max(0, (state.height | 0) - 1);\n"
            "    const nx = Math.min(maxX, Math.max(0, x | 0));\n"
            "    const ny = Math.min(maxY, Math.max(0, y | 0));\n"
            "    return { x: nx, y: ny };\n"
            "  };\n"
            "  const eventToLocal = (ev) => {\n"
            "    const rect = svg.getBoundingClientRect();\n"
            "    const cx = Number(ev?.clientX);\n"
            "    const cy = Number(ev?.clientY);\n"
            "    const scaleX = rect.width > 0 ? state.width / rect.width : 1;\n"
            "    const scaleY = rect.height > 0 ? state.height / rect.height : 1;\n"
            "    return {\n"
            "      x: Number.isFinite(cx) ? (cx - rect.left) * scaleX : 0,\n"
            "      y: Number.isFinite(cy) ? (cy - rect.top) * scaleY : 0,\n"
            "    };\n"
            "  };\n"
            "  const syncWindowSize = () => {\n"
            "    const rect = host.getBoundingClientRect();\n"
            "    const physicalW = Math.max(1, rect.width | 0);\n"
            "    const physicalH = Math.max(1, rect.height | 0);\n"
            "    const nextW = Math.max(minLogicalWidth, physicalW);\n"
            "    const scale = physicalW / nextW;\n"
            "    const nextH = Math.max(1, Math.round(physicalH / scale));\n"
            "    state.width = nextW; state.height = nextH;\n"
            "    svg.setAttribute('viewBox', `0 0 ${nextW} ${nextH}`);\n"
            "  };\n";
        const char* main_js_head3b =
            "  const emitClickEvent = (sourceEv, pointEv) => {\n"
            "    svg.focus();\n"
            "    const targetId = uiEventTargetId(sourceEv);\n"
            "    const p = eventToLocal(pointEv ?? sourceEv);\n"
            "    const clamped = clampToWindow(p.x, p.y);\n"
            "    state.focusedTargetId = targetId;\n"
            "    pushEvent({ type: 'click', targetId, x: clamped.x, y: clamped.y, dx: 0, dy: 0, key: '', text: '', modifiers: uiEventModifiers(sourceEv), repeat: false });\n"
            "  };\n"
            "  if (typeof window !== 'undefined' && 'PointerEvent' in window) {\n"
            "    const onPointerDown = (ev) => { state.pointerDown = true; state.lastPointer = eventToLocal(ev); emitClickEvent(ev, ev); };\n"
            "    const onPointerMove = (ev) => {\n"
            "      if (!state.pointerDown) return;\n"
            "      const targetId = uiEventTargetId(ev);\n"
            "      const p = eventToLocal(ev);\n"
            "      const last = state.lastPointer || p;\n"
            "      const clamped = clampToWindow(p.x, p.y);\n"
            "      pushEvent({ type: 'drag', targetId, x: clamped.x, y: clamped.y, dx: Math.round(p.x - last.x), dy: Math.round(p.y - last.y), key: '', text: '', modifiers: uiEventModifiers(ev), repeat: false });\n"
            "      state.lastPointer = p;\n"
            "    };\n"
            "    const onPointerUp = () => { state.pointerDown = false; state.lastPointer = null; };\n"
            "    state.pointerHandler = onPointerDown;\n"
            "    state.pointerMoveHandler = onPointerMove;\n"
            "    state.pointerUpHandler = onPointerUp;\n"
            "    svg.addEventListener('pointerdown', onPointerDown);\n"
            "    svg.addEventListener('pointermove', onPointerMove);\n"
            "    svg.addEventListener('pointerup', onPointerUp);\n"
            "    svg.addEventListener('pointercancel', onPointerUp);\n"
            "  } else {\n"
            "    const onClick = (ev) => { emitClickEvent(ev, ev); };\n"
            "    const onTouchStart = (ev) => {\n"
            "      if (typeof ev?.preventDefault === 'function') ev.preventDefault();\n"
            "      const touch = (ev?.touches && ev.touches.length > 0) ? ev.touches[0] : null;\n"
            "      emitClickEvent(ev, touch);\n"
            "    };\n"
            "    state.clickHandler = onClick;\n"
            "    state.touchHandler = onTouchStart;\n"
            "    svg.addEventListener('click', onClick);\n"
            "    svg.addEventListener('touchstart', onTouchStart, { passive: false });\n"
            "  }\n"
            "  const onWheel = (ev) => {\n"
            "    if (typeof ev?.preventDefault === 'function') ev.preventDefault();\n"
            "    const targetId = uiEventTargetId(ev);\n"
            "    const p = eventToLocal(ev);\n"
            "    const clamped = clampToWindow(p.x, p.y);\n"
            "    pushEvent({ type: 'wheel', targetId, x: clamped.x, y: clamped.y, dx: Math.round(Number(ev?.deltaX) || 0), dy: Math.round(Number(ev?.deltaY) || 0), key: '', text: '', modifiers: uiEventModifiers(ev), repeat: false });\n"
            "  };\n"
            "  state.wheelHandler = onWheel;\n"
            "  svg.addEventListener('wheel', onWheel, { passive: false });\n"
            "  const emitKeyEvent = (ev) => {\n"
            "    pushEvent({ type: 'key', targetId: String(state.focusedTargetId ?? ''), x: -1, y: -1, dx: 0, dy: 0, key: uiEventKey(ev?.key), text: uiEventText(ev), modifiers: uiEventModifiers(ev), repeat: !!ev?.repeat });\n"
            "  };\n"
            "  const onKeyDown = (ev) => { emitKeyEvent(ev); };\n"
            "  state.keyDownHandler = onKeyDown;\n"
            "  svg.addEventListener('keydown', onKeyDown);\n"
            "  const onBlur = () => { state.focusedTargetId = ''; };\n"
            "  state.blurHandler = onBlur;\n"
            "  svg.addEventListener('blur', onBlur);\n"
            "  if (typeof window !== 'undefined' && window?.addEventListener) {\n"
            "    const onResize = () => { if (!state.closed) syncWindowSize(); };\n"
            "    state.resizeHandler = onResize;\n"
            "    window.addEventListener('resize', onResize);\n"
            "  }\n"
            "  syncWindowSize();\n"
            "  uiState.windows.set(windowId, state);\n"
            "  return state;\n"
            "}\n";
        const char* main_js_head4 =
            "globalThis.__aivmUiCreateWindow = (windowId, title, width, height) => {\n"
            "  if (!Number.isInteger(windowId) || windowId <= 0 || !Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0) return -1;\n"
            "  return ensureUiWindow(windowId, title, width, height) ? 0 : -1;\n"
            "};\n"
            "globalThis.__aivmUiBeginFrame = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  win.frameParts = [];\n"
            "  win.nextElementId = 1;\n"
            "  win.nextClipId = 1;\n"
            "  win.clipStack = [];\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiDrawRect = (windowId, x, y, w, h, color) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  const id = `n${win.nextElementId++}`;\n"
            "  const clip = win.clipStack?.length ? ` clip-path=\"url(#${win.clipStack[win.clipStack.length - 1]})\"` : '';\n"
            "  win.frameParts.push(`<rect data-aivm-id=\"${id}\"${clip} x=\"${x|0}\" y=\"${y|0}\" width=\"${w|0}\" height=\"${h|0}\" fill=\"${xmlEscape(color)}\"/>`);\n"
            "  return 0;\n"
            "};\n";
        const char* main_js_mid =
            "globalThis.__aivmUiDrawText = (windowId, x, y, text, color, size) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  const id = `n${win.nextElementId++}`;\n"
            "  const textEscaped = xmlEscape(text);\n"
            "  const clip = win.clipStack?.length ? ` clip-path=\"url(#${win.clipStack[win.clipStack.length - 1]})\"` : '';\n"
            "  win.frameParts.push(`<text data-aivm-id=\"${id}\"${clip} x=\"${x|0}\" y=\"${y|0}\" fill=\"${xmlEscape(color)}\" font-size=\"${size|0}\" font-family=\"system-ui, sans-serif\" dominant-baseline=\"text-before-edge\">${textEscaped}</text>`);\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiMeasureText = (windowId, text, size) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed || !uiMeasureContext || !Number.isInteger(size) || size <= 0) return -1;\n"
            "  uiMeasureContext.font = `${size}px system-ui, sans-serif`;\n"
            "  return Math.ceil(uiMeasureContext.measureText(String(text ?? '')).width);\n"
            "};\n"
            "globalThis.__aivmUiDrawLine = (windowId, x1, y1, x2, y2, color, width) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  const id = `n${win.nextElementId++}`;\n"
            "  const clip = win.clipStack?.length ? ` clip-path=\"url(#${win.clipStack[win.clipStack.length - 1]})\"` : '';\n"
            "  win.frameParts.push(`<line data-aivm-id=\"${id}\"${clip} x1=\"${x1|0}\" y1=\"${y1|0}\" x2=\"${x2|0}\" y2=\"${y2|0}\" stroke=\"${xmlEscape(color)}\" stroke-width=\"${width|0}\"/>`);\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiDrawEllipse = (windowId, x, y, w, h, color) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  const id = `n${win.nextElementId++}`;\n"
            "  const rx = (w|0) / 2;\n"
            "  const ry = (h|0) / 2;\n"
            "  const cx = (x|0) + rx;\n"
            "  const cy = (y|0) + ry;\n"
            "  const clip = win.clipStack?.length ? ` clip-path=\"url(#${win.clipStack[win.clipStack.length - 1]})\"` : '';\n"
            "  win.frameParts.push(`<ellipse data-aivm-id=\"${id}\"${clip} cx=\"${cx}\" cy=\"${cy}\" rx=\"${rx}\" ry=\"${ry}\" fill=\"${xmlEscape(color)}\"/>`);\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiDrawPath = (windowId, path, fillColor, strokeColor, strokeWidth) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  const id = `n${win.nextElementId++}`;\n"
            "  const sw = strokeWidth|0;\n"
            "  const safePath = xmlEscape(path);\n"
            "  const fill = String(fillColor ?? '');\n"
            "  const stroke = String(strokeColor ?? '');\n"
            "  const fillAttr = fill.length > 0 && fill !== 'none' ? xmlEscape(fill) : 'none';\n"
            "  const strokeAttr = sw > 0 && stroke.length > 0 && stroke !== 'none' ? ` stroke=\"${xmlEscape(stroke)}\" stroke-width=\"${sw}\"` : '';\n"
            "  const clip = win.clipStack?.length ? ` clip-path=\"url(#${win.clipStack[win.clipStack.length - 1]})\"` : '';\n"
            "  win.frameParts.push(`<path data-aivm-id=\"${id}\"${clip} d=\"${safePath}\" fill=\"${fillAttr}\"${strokeAttr}/>`);\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiPushClipPath = (windowId, path) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  const id = `clip${windowId}_${win.nextClipId++}`;\n"
            "  win.frameParts.push(`<defs><clipPath id=\"${id}\"><path d=\"${xmlEscape(path)}\"/></clipPath></defs>`);\n"
            "  if (!Array.isArray(win.clipStack)) win.clipStack = [];\n"
            "  win.clipStack.push(id);\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiPopClipPath = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed || !Array.isArray(win.clipStack) || win.clipStack.length < 1) return -1;\n"
            "  win.clipStack.pop();\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiDrawImage = (windowId, x, y, w, h, src) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  const id = `n${win.nextElementId++}`;\n"
            "  const clip = win.clipStack?.length ? ` clip-path=\"url(#${win.clipStack[win.clipStack.length - 1]})\"` : '';\n"
            "  win.frameParts.push(`<image data-aivm-id=\"${id}\"${clip} x=\"${x|0}\" y=\"${y|0}\" width=\"${w|0}\" height=\"${h|0}\" href=\"${xmlEscape(src)}\"/>`);\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiEndFrame = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  return (win && !win.closed) ? 0 : -1;\n"
            "};\n";
        const char* main_js_tail =
            "globalThis.__aivmUiPresent = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  win.svg.innerHTML = win.frameParts.join('');\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiCloseWindow = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return -1;\n"
            "  if (win.closed) return 0;\n"
            "  win.closed = true;\n"
            "  win.focusedTargetId = '';\n"
            "  if (typeof window !== 'undefined' && window?.removeEventListener && typeof win.resizeHandler === 'function') {\n"
            "    window.removeEventListener('resize', win.resizeHandler);\n"
            "    win.resizeHandler = null;\n"
            "  }\n"
            "  if (typeof win.pointerHandler === 'function') {\n"
            "    win.svg.removeEventListener('pointerdown', win.pointerHandler);\n"
            "    win.pointerHandler = null;\n"
            "  }\n"
            "  if (typeof win.pointerMoveHandler === 'function') {\n"
            "    win.svg.removeEventListener('pointermove', win.pointerMoveHandler);\n"
            "    win.pointerMoveHandler = null;\n"
            "  }\n"
            "  if (typeof win.pointerUpHandler === 'function') {\n"
            "    win.svg.removeEventListener('pointerup', win.pointerUpHandler);\n"
            "    win.svg.removeEventListener('pointercancel', win.pointerUpHandler);\n"
            "    win.pointerUpHandler = null;\n"
            "  }\n"
            "  if (typeof win.wheelHandler === 'function') {\n"
            "    win.svg.removeEventListener('wheel', win.wheelHandler, { passive: false });\n"
            "    win.wheelHandler = null;\n"
            "  }\n"
            "  if (typeof win.clickHandler === 'function') {\n"
            "    win.svg.removeEventListener('click', win.clickHandler);\n"
            "    win.clickHandler = null;\n"
            "  }\n"
            "  if (typeof win.touchHandler === 'function') {\n"
            "    win.svg.removeEventListener('touchstart', win.touchHandler, { passive: false });\n"
            "    win.touchHandler = null;\n"
            "  }\n"
            "  if (typeof win.keyDownHandler === 'function') {\n"
            "    win.svg.removeEventListener('keydown', win.keyDownHandler);\n"
            "    win.keyDownHandler = null;\n"
            "  }\n"
            "  if (typeof win.blurHandler === 'function') {\n"
            "    win.svg.removeEventListener('blur', win.blurHandler);\n"
            "    win.blurHandler = null;\n"
            "  }\n"
            "  win.frameParts = [];\n"
            "  win.lastPolledEvent = { type: 'none', targetId: '', x: -1, y: -1, dx: 0, dy: 0, key: '', text: '', modifiers: '', repeat: false };\n"
            "  win.eventQueue.push({ type: 'closed', targetId: '', x: -1, y: -1, dx: 0, dy: 0, key: '', text: '', modifiers: '', repeat: false });\n"
            "  if (win.eventQueue.length > 64) win.eventQueue.shift();\n"
            "  win.host.remove();\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiWaitFrame = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  return 0;\n"
            "};\n";
        const char* main_js_tailb =
            "globalThis.__aivmUiGetWindowWidth = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  return (win.width | 0) || (win.svg?.viewBox?.baseVal?.width | 0) || -1;\n"
            "};\n"
            "globalThis.__aivmUiGetWindowHeight = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win || win.closed) return -1;\n"
            "  return (win.height | 0) || (win.svg?.viewBox?.baseVal?.height | 0) || -1;\n"
            "};\n"
            "globalThis.__aivmUiPollEventType = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return -1;\n"
            "  const evt = win.eventQueue.length > 0 ? win.eventQueue.shift() : { type: 'none', targetId: '', x: -1, y: -1, dx: 0, dy: 0, key: '', text: '', modifiers: '', repeat: false };\n"
            "  win.lastPolledEvent = evt;\n"
            "  win.closeConsumed = (evt.type === 'closed');\n"
            "  if (evt.type === 'closed') return 1;\n"
            "  if (evt.type === 'click') return 2;\n"
            "  if (evt.type === 'key') return 3;\n"
            "  if (evt.type === 'wheel') return 4;\n"
            "  if (evt.type === 'drag') return 5;\n"
            "  return 0;\n"
            "};\n"
            "globalThis.__aivmUiPollEventX = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return -1;\n"
            "  return (win.lastPolledEvent?.x | 0);\n"
            "};\n"
            "globalThis.__aivmUiPollEventY = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return -1;\n"
            "  return (win.lastPolledEvent?.y | 0);\n"
            "};\n"
            "globalThis.__aivmUiPollEventDx = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return 0;\n"
            "  return (win.lastPolledEvent?.dx | 0);\n"
            "};\n"
            "globalThis.__aivmUiPollEventDy = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return 0;\n"
            "  return (win.lastPolledEvent?.dy | 0);\n"
            "};\n"
            "globalThis.__aivmUiPollEventTargetId = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return '';\n"
            "  return String(win.lastPolledEvent?.targetId ?? '');\n"
            "};\n"
            "globalThis.__aivmUiPollEventKey = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return '';\n"
            "  return String(win.lastPolledEvent?.key ?? '');\n"
            "};\n"
            "globalThis.__aivmUiPollEventText = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return '';\n"
            "  return String(win.lastPolledEvent?.text ?? '');\n"
            "};\n"
            "globalThis.__aivmUiPollEventModifiers = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return '';\n"
            "  return String(win.lastPolledEvent?.modifiers ?? '');\n"
            "};\n"
            "globalThis.__aivmUiPollEventRepeat = (windowId) => {\n"
            "  const win = uiState.windows.get(windowId);\n"
            "  if (!win) return -1;\n"
            "  const repeatValue = win.lastPolledEvent?.repeat ? 1 : 0;\n"
            "  if (win.closeConsumed) {\n"
            "    uiState.windows.delete(windowId);\n"
            "  }\n"
            "  return repeatValue;\n"
            "};\n"
            "function readHostStdin() {\n"
            "  if (typeof globalThis.AIVM_HOST_STDIN_READ !== 'function') return undefined;\n"
            "  return globalThis.AIVM_HOST_STDIN_READ();\n"
            "}\n";
        const char* main_js_tail2 =
            "aiLangRoot.stdin = {\n"
            "  push(text) { stdinQueue.push(String(text ?? '')); },\n"
            "  close() { stdinClosed = true; },\n"
            "  _drain() {\n"
            "    if (stdinQueue.length > 0) return stdinQueue.shift();\n"
            "    const hostValue = readHostStdin();\n"
            "    if (hostValue === null) return null;\n"
            "    if (hostValue !== undefined) return String(hostValue ?? '');\n"
            "    return stdinClosed ? null : '';\n"
            "  }\n"
            "};\n"
            "aiLangRoot.remote = aiLangRoot.remote || {};\n"
            "let remoteCallImpl = null;\n"
            "if (remoteMode === 'js' && typeof aiLangRoot.remote.call === 'function') {\n"
            "  remoteCallImpl = (cap, op, value) => aiLangRoot.remote.call(cap, op, value);\n"
            "} else if (remoteMode === 'js') {\n"
            "  remoteCallImpl = () => Promise.reject(new Error('AIVM_REMOTE_MODE=js requires AiLang.remote.call adapter'));\n"
            "} else {\n"
            "  const remoteClient = createAivmRemoteClient({ endpoint });\n"
            "  remoteCallImpl = (cap, op, value) => remoteClient.call(cap, op, value);\n"
            "}\n"
            "if (remoteMode !== 'js' || typeof aiLangRoot.remote.call !== 'function') {\n"
            "  aiLangRoot.remote.call = remoteCallImpl;\n"
            "}\n"
            "globalThis.__aivmRemoteCall = (cap, op, value) => remoteCallImpl(cap, op, value);\n"
            "globalThis.__aivmStdinRead = () => aiLangRoot.stdin._drain();\n"
            "const runtime = await createRuntime();\n"
            "const bytes = await (await fetch('./app.aibc1')).arrayBuffer();\n"
            "runtime.FS.writeFile('/app.aibc1', new Uint8Array(bytes));\n"
            "const logs = [];\n"
            "runtime.print = (line) => { const s = String(line); logs.push(s); console.log(s); };\n"
            "runtime.printErr = (line) => { const s = String(line); logs.push(s); console.error(s); };\n"
            "const runPromise = runtime.ccall('aivm_web_run_app', 'number', [], [], { async: true });\n"
            "Promise.resolve(runPromise).then(() => {\n"
            "  if (output) output.textContent = logs.join('\\n');\n"
            "}).catch((error) => {\n"
            "  console.error(error);\n"
            "  if (output) output.textContent = logs.join('\\n');\n"
            "});\n";
        if (snprintf(main_js, sizeof(main_js), "%s%s%s%s%s%s%s%s%s", main_js_head, main_js_head2, main_js_head3, main_js_head3b, main_js_head4, main_js_mid, main_js_tail, main_js_tailb, main_js_tail2) >= (int)sizeof(main_js)) {
            return 0;
        }
    }
    {
        const char* remote_client_js_head =
            "function encodeText(text) { return new TextEncoder().encode(text); }\n"
            "function decodeText(bytes) { return new TextDecoder().decode(bytes); }\n"
            "function writeU16LE(arr, off, v) { arr[off]=v&255; arr[off+1]=(v>>8)&255; }\n"
            "function writeU32LE(arr, off, v) { arr[off]=v&255; arr[off+1]=(v>>8)&255; arr[off+2]=(v>>16)&255; arr[off+3]=(v>>24)&255; }\n"
            "function readU16LE(arr, off) { return arr[off] | (arr[off+1] << 8); }\n"
            "function readU32LE(arr, off) { return (arr[off] | (arr[off+1] << 8) | (arr[off+2] << 16) | (arr[off+3] << 24)) >>> 0; }\n"
            "function encodeStr(s) { const b = encodeText(s); const out = new Uint8Array(2 + b.length); writeU16LE(out, 0, b.length); out.set(b, 2); return out; }\n"
            "function decodeStr(arr, off) { if (off + 2 > arr.length) throw new Error('bad string header'); const n = readU16LE(arr, off); const start = off + 2; if (start + n > arr.length) throw new Error('bad string length'); return { value: decodeText(arr.slice(start, start + n)), next: start + n }; }\n"
            "function join(parts) { const len = parts.reduce((a,b)=>a+b.length,0); const out = new Uint8Array(len); let i=0; for (const p of parts) { out.set(p, i); i += p.length; } return out; }\n"
            "function encodeCall(id, cap, op, value) {\n"
            "  const capB = encodeStr(cap); const opB = encodeStr(op);\n"
            "  const payload = new Uint8Array(capB.length + opB.length + 8);\n"
            "  payload.set(capB, 0); payload.set(opB, capB.length);\n"
            "  const dv = new DataView(payload.buffer); dv.setBigInt64(capB.length + opB.length, BigInt(value), true);\n"
            "  const frame = new Uint8Array(9 + payload.length); frame[0] = 0x10; writeU32LE(frame, 1, id); writeU32LE(frame, 5, payload.length); frame.set(payload, 9); return frame;\n"
            "}\n"
            "function decodeFrame(arr) {\n"
            "  if (!(arr instanceof Uint8Array) || arr.length < 9) throw new Error('short frame');\n"
            "  const payloadLen = readU32LE(arr, 5);\n"
            "  if (arr.length < 9 + payloadLen) throw new Error('truncated frame');\n"
            "  return { type: arr[0], id: readU32LE(arr,1), payload: arr.slice(9, 9 + payloadLen) };\n"
            "}\n"
            "function decodeResult(payload) { if (!(payload instanceof Uint8Array) || payload.length < 8) throw new Error('bad result payload'); const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength); return Number(dv.getBigInt64(0, true)); }\n"
            "function decodeError(payload) { if (!(payload instanceof Uint8Array) || payload.length < 6) throw new Error('bad error payload'); const code = readU32LE(payload, 0); const msg = decodeStr(payload, 4).value; return { code, message: msg }; }\n"
            "function encodeHello(id, caps) {\n"
            "  const clientName = encodeStr('aivm-web-client');\n"
            "  const capParts = caps.map((c)=>encodeStr(c));\n"
            "  const header = new Uint8Array(2 + 4); writeU16LE(header, 0, 1); writeU32LE(header, 2, caps.length);\n"
            "  const payload = join([clientName, header, ...capParts]);\n"
            "  const frame = new Uint8Array(9 + payload.length); frame[0]=0x01; writeU32LE(frame,1,id); writeU32LE(frame,5,payload.length); frame.set(payload,9); return frame;\n"
            "}\n";
        const char* remote_client_js_tail =
            "export function createAivmRemoteClient({ endpoint }) {\n"
            "  let ws = null; let nextId = 2; const pending = new Map(); let ready = null;\n"
            "  function failPending(reason) {\n"
            "    for (const [, p] of pending) { p.reject(new Error(reason)); }\n"
            "    pending.clear();\n"
            "  }\n"
            "  function ensureSocket() {\n"
            "    if (ws && ws.readyState === WebSocket.OPEN) return Promise.resolve();\n"
            "    if (ready) return ready;\n"
            "    ready = new Promise((resolve, reject) => {\n"
            "      let settled = false;\n"
            "      const resolveReady = () => { if (settled) return; settled = true; resolve(); };\n"
            "      const rejectReady = (message) => { if (settled) return; settled = true; reject(new Error(message)); };\n"
            "      ws = new WebSocket(endpoint); ws.binaryType = 'arraybuffer';\n"
            "      ws.onopen = () => { ws.send(encodeHello(1, ['cap.remote'])); };\n"
            "      ws.onmessage = (ev) => {\n"
            "        let frame = null;\n"
            "        if (!(ev.data instanceof ArrayBuffer)) { rejectReady('remote invalid websocket frame payload'); ready = null; ws = null; failPending('remote invalid websocket frame payload'); return; }\n"
            "        try { const arr = new Uint8Array(ev.data); frame = decodeFrame(arr); } catch (_) { rejectReady('remote invalid websocket frame payload'); ready = null; ws = null; failPending('remote invalid websocket frame payload'); return; }\n"
            "        if (!settled && frame.id !== 1) { rejectReady(`remote unexpected handshake frame id ${frame.id}`); ready = null; ws = null; return; }\n"
            "        if (frame.type === 0x02 && frame.id === 1) { resolveReady(); return; }\n"
            "        if (frame.type === 0x03 && frame.id === 1) { let err = null; try { err = decodeError(frame.payload); } catch (_) { rejectReady('remote invalid websocket frame payload'); ready = null; ws = null; failPending('remote invalid websocket frame payload'); return; } rejectReady(`remote handshake denied ${err.code}: ${err.message}`); ready = null; ws = null; return; }\n"
            "        if (frame.id === 1) { rejectReady(`remote unexpected handshake frame type ${frame.type}`); ready = null; ws = null; return; }\n"
            "        const p = pending.get(frame.id); if (!p) return;\n"
            "        if (frame.type === 0x11) { let resultValue = 0; try { resultValue = decodeResult(frame.payload); } catch (_) { pending.delete(frame.id); p.reject(new Error('remote invalid websocket frame payload')); ready = null; ws = null; failPending('remote invalid websocket frame payload'); return; } pending.delete(frame.id); p.resolve(resultValue); return; }\n"
            "        if (frame.type === 0x12) { let err = null; try { err = decodeError(frame.payload); } catch (_) { pending.delete(frame.id); p.reject(new Error('remote invalid websocket frame payload')); ready = null; ws = null; failPending('remote invalid websocket frame payload'); return; } pending.delete(frame.id); p.reject(new Error(`remote ${err.code}: ${err.message}`)); return; }\n"
            "        pending.delete(frame.id); p.reject(new Error(`remote unexpected frame type ${frame.type}`));\n"
            "      };\n"
            "      ws.onerror = () => { rejectReady('remote websocket error'); ready = null; ws = null; failPending('remote websocket error'); };\n"
            "      ws.onclose = () => { rejectReady('remote websocket closed'); ready = null; ws = null; failPending('remote websocket closed'); };\n"
            "    });\n"
            "    return ready;\n"
            "  }\n"
            "  return {\n"
            "    async call(cap, op, value) {\n"
            "      await ensureSocket();\n"
            "      const id = nextId++;\n"
            "      return new Promise((resolve, reject) => { pending.set(id, { resolve, reject }); ws.send(encodeCall(id, cap, op, value)); });\n"
            "    }\n"
            "  };\n"
            "}\n";
        if (snprintf(remote_client_js, sizeof(remote_client_js), "%s%s", remote_client_js_head, remote_client_js_tail) >= (int)sizeof(remote_client_js)) {
            return 0;
        }
    }
    return write_text_file(index_path, index_html) &&
           write_text_file(main_path, main_js) &&
           write_text_file(remote_client_path, remote_client_js);
}

static int emit_wasm_fullstack_layout(const char* out_dir)
{
    char www_dir[PATH_MAX];
    char readme_path[PATH_MAX];
    char readme[1024];
    if (out_dir == NULL) {
        return 0;
    }
    if (!join_path(out_dir, "www", www_dir, sizeof(www_dir))) {
        return 0;
    }
    if (!ensure_directory(www_dir)) {
        return 0;
    }
    if (!emit_wasm_spa_files(www_dir)) {
        return 0;
    }
    if (snprintf(
            readme,
            sizeof(readme),
            "# AiLang wasm fullstack package\n\n"
            "This package contains a self-contained app binary and `www/` client assets.\n"
            "The app binary is responsible for serving `www/` and websocket endpoint `ws://<host>:8765`.\n"
            "Set `AIVM_REMOTE_WS_ENDPOINT` in browser page to override endpoint.\n") >= (int)sizeof(readme)) {
        return 0;
    }
    if (!join_path(out_dir, "README.md", readme_path, sizeof(readme_path))) {
        return 0;
    }
    if (!write_text_file(readme_path, readme)) {
        return 0;
    }
    return 1;
}

#define NATIVE_PROCESS_CAPACITY 32U
#define NATIVE_PROCESS_READ_CHUNK 4096U
#define NATIVE_WORKER_CAPACITY 64U
#define NATIVE_WORKER_TEXT_CAPACITY 512U
#define NATIVE_NET_HANDLE_CAPACITY 64U
#define NATIVE_NET_ASYNC_CAPACITY 128U
#define NATIVE_NET_BYTES_CHUNK 65536U

typedef struct NativeProcessState
{
    int used;
    int finished;
    int exit_code;
    int stdout_closed;
    int stderr_closed;
    uint8_t* stdout_buffer;
    size_t stdout_buffer_len;
    size_t stdout_buffer_pos;
    uint8_t* stderr_buffer;
    size_t stderr_buffer_len;
    size_t stderr_buffer_pos;
#ifdef _WIN32
    HANDLE process_handle;
    HANDLE stdout_read;
    HANDLE stderr_read;
#else
    pid_t pid;
    int stdout_fd;
    int stderr_fd;
#endif
} NativeProcessState;

static NativeProcessState g_native_processes[NATIVE_PROCESS_CAPACITY];
static uint8_t g_native_process_read_scratch[NATIVE_PROCESS_READ_CHUNK];

#include "airun_net_host.inc"
#include "airun_fs_host.inc"
#include "airun_storage_host.inc"
#include "airun_time_host.inc"
#include "airun_process_host.inc"
#include "airun_worker_host.inc"

#include "airun_ui_runtime_host.inc"

static int native_base64_decode_char(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (int)(ch - 'A');
    }
    if (ch >= 'a' && ch <= 'z') {
        return (int)(ch - 'a') + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return (int)(ch - '0') + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

#define NATIVE_BYTES_SCRATCH_CAPACITY 131072U
static uint8_t g_native_bytes_scratch[NATIVE_BYTES_SCRATCH_CAPACITY];
#if defined(_WIN32) || defined(__APPLE__)
static char* g_native_string_scratch = NULL;
static size_t g_native_string_scratch_capacity = 0U;

static int native_string_scratch_ensure_capacity(size_t required_capacity)
{
    char* grown;
    size_t new_capacity;
    if (required_capacity == 0U) {
        required_capacity = 1U;
    }
    if (required_capacity <= g_native_string_scratch_capacity && g_native_string_scratch != NULL) {
        return 1;
    }
    new_capacity = (g_native_string_scratch_capacity == 0U) ? 256U : g_native_string_scratch_capacity;
    while (new_capacity < required_capacity) {
        if (new_capacity > (SIZE_MAX / 2U)) {
            new_capacity = required_capacity;
            break;
        }
        new_capacity *= 2U;
    }
    grown = (char*)realloc(g_native_string_scratch, new_capacity);
    if (grown == NULL) {
        return 0;
    }
    g_native_string_scratch = grown;
    g_native_string_scratch_capacity = new_capacity;
    return 1;
}
#endif

static int native_bytes_from_base64(
    const char* input,
    uint8_t* out_bytes,
    size_t out_capacity,
    size_t* out_length)
{
    size_t input_len;
    size_t i;
    size_t out_index = 0U;
    if (out_length == NULL) {
        return 0;
    }
    *out_length = 0U;
    if (input == NULL) {
        return 0;
    }
    input_len = strlen(input);
    if (input_len == 0U) {
        return 1;
    }
    if ((input_len % 4U) != 0U) {
        return 0;
    }

    for (i = 0U; i < input_len; i += 4U) {
        int c0 = native_base64_decode_char(input[i]);
        int c1 = native_base64_decode_char(input[i + 1U]);
        int c2;
        int c3;
        uint32_t chunk;
        int pad = 0;
        if (c0 < 0 || c1 < 0) {
            return 0;
        }
        if (input[i + 2U] == '=') {
            c2 = 0;
            pad += 1;
            if (input[i + 3U] != '=') {
                return 0;
            }
            c3 = 0;
            pad += 1;
        } else {
            c2 = native_base64_decode_char(input[i + 2U]);
            if (c2 < 0) {
                return 0;
            }
            if (input[i + 3U] == '=') {
                c3 = 0;
                pad += 1;
            } else {
                c3 = native_base64_decode_char(input[i + 3U]);
                if (c3 < 0) {
                    return 0;
                }
            }
        }
        if (pad > 0 && i + 4U != input_len) {
            return 0;
        }
        chunk = ((uint32_t)c0 << 18U) |
                ((uint32_t)c1 << 12U) |
                ((uint32_t)c2 << 6U) |
                (uint32_t)c3;

        if (out_bytes != NULL && out_index < out_capacity) {
            out_bytes[out_index] = (uint8_t)((chunk >> 16U) & 0xffU);
        }
        out_index += 1U;
        if (pad < 2) {
            if (out_bytes != NULL && out_index < out_capacity) {
                out_bytes[out_index] = (uint8_t)((chunk >> 8U) & 0xffU);
            }
            out_index += 1U;
        }
        if (pad == 0) {
            if (out_bytes != NULL && out_index < out_capacity) {
                out_bytes[out_index] = (uint8_t)(chunk & 0xffU);
            }
            out_index += 1U;
        }
    }

    if (out_bytes != NULL && out_index > out_capacity) {
        return 0;
    }
    *out_length = out_index;
    return 1;
}

static int native_bytes_to_base64(
    const uint8_t* input,
    size_t input_len,
    char* out_text,
    size_t out_capacity)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0U;
    size_t out_index = 0U;

    if (out_capacity == 0U) {
        return 0;
    }
    if (input_len == 0U) {
        if (out_capacity < 1U) {
            return 0;
        }
        out_text[0] = '\0';
        return 1;
    }
    if (input == NULL) {
        return 0;
    }

    while (i < input_len) {
        uint32_t chunk = 0U;
        size_t remain = input_len - i;
        size_t bytes_in_chunk = remain >= 3U ? 3U : remain;
        chunk |= (uint32_t)input[i] << 16U;
        if (bytes_in_chunk > 1U) {
            chunk |= (uint32_t)input[i + 1U] << 8U;
        }
        if (bytes_in_chunk > 2U) {
            chunk |= (uint32_t)input[i + 2U];
        }
        if (out_index + 4U >= out_capacity) {
            return 0;
        }
        out_text[out_index++] = alphabet[(chunk >> 18U) & 0x3fU];
        out_text[out_index++] = alphabet[(chunk >> 12U) & 0x3fU];
        out_text[out_index++] = (bytes_in_chunk > 1U) ? alphabet[(chunk >> 6U) & 0x3fU] : '=';
        out_text[out_index++] = (bytes_in_chunk > 2U) ? alphabet[chunk & 0x3fU] : '=';
        i += bytes_in_chunk;
    }
    out_text[out_index] = '\0';
    return 1;
}

#if defined(_WIN32) || defined(__APPLE__)
static int native_rgba_base64_capacity(size_t rgba_length, size_t* out_capacity)
{
    size_t capacity;
    if (out_capacity == NULL) {
        return 0;
    }
    if (rgba_length > ((SIZE_MAX - 3U) / 4U) * 3U) {
        return 0;
    }
    capacity = ((rgba_length + 2U) / 3U) * 4U;
    if (capacity >= SIZE_MAX) {
        return 0;
    }
    *out_capacity = capacity + 1U;
    return 1;
}
#endif

static int native_image_decode_to_rgba_base64(
    const uint8_t* input,
    size_t input_len,
    const char* mime_type,
    AivmValue* result)
{
    (void)mime_type;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (input == NULL || input_len == 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
#ifdef _WIN32
    {
        HRESULT init_hr;
        int should_uninit = 0;
        IWICImagingFactory* factory = NULL;
        IWICStream* stream = NULL;
        IWICBitmapDecoder* decoder = NULL;
        IWICBitmapFrameDecode* frame = NULL;
        IWICFormatConverter* converter = NULL;
        UINT width = 0U;
        UINT height = 0U;
        uint8_t* rgba = NULL;
        size_t rgba_length;
        size_t base64_capacity;
        int status = AIVM_SYSCALL_ERR_INVALID;

        if (input_len > (size_t)UINT_MAX) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        init_hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(init_hr)) {
            should_uninit = 1;
        } else if (init_hr != RPC_E_CHANGED_MODE) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        if (FAILED(CoCreateInstance(
                &CLSID_WICImagingFactory,
                NULL,
                CLSCTX_INPROC_SERVER,
                &IID_IWICImagingFactory,
                (LPVOID*)&factory))) {
            goto cleanup_windows;
        }
        if (FAILED(factory->lpVtbl->CreateStream(factory, &stream))) {
            goto cleanup_windows;
        }
        if (FAILED(stream->lpVtbl->InitializeFromMemory(stream, (WICInProcPointer)input, (DWORD)input_len))) {
            goto cleanup_windows;
        }
        if (FAILED(factory->lpVtbl->CreateDecoderFromStream(
                factory,
                (IStream*)stream,
                NULL,
                WICDecodeMetadataCacheOnLoad,
                &decoder))) {
            goto cleanup_windows;
        }
        if (FAILED(decoder->lpVtbl->GetFrame(decoder, 0U, &frame))) {
            goto cleanup_windows;
        }
        if (FAILED(factory->lpVtbl->CreateFormatConverter(factory, &converter))) {
            goto cleanup_windows;
        }
        if (FAILED(converter->lpVtbl->Initialize(
                converter,
                (IWICBitmapSource*)frame,
                &GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                NULL,
                0.0,
                WICBitmapPaletteTypeCustom))) {
            goto cleanup_windows;
        }
        if (FAILED(converter->lpVtbl->GetSize(converter, &width, &height))) {
            goto cleanup_windows;
        }
        if (width == 0U || height == 0U) {
            goto cleanup_windows;
        }
        if ((size_t)width > (SIZE_MAX / 4U) || (size_t)height > (SIZE_MAX / ((size_t)width * 4U))) {
            goto cleanup_windows;
        }
        rgba_length = (size_t)width * (size_t)height * 4U;
        rgba = (uint8_t*)malloc(rgba_length);
        if (rgba == NULL) {
            goto cleanup_windows;
        }
        if (FAILED(converter->lpVtbl->CopyPixels(
                converter,
                NULL,
                width * 4U,
                (UINT)rgba_length,
                rgba))) {
            goto cleanup_windows;
        }
        if (!native_rgba_base64_capacity(rgba_length, &base64_capacity)) {
            goto cleanup_windows;
        }
        if (!native_string_scratch_ensure_capacity(base64_capacity)) {
            goto cleanup_windows;
        }
        if (!native_bytes_to_base64(rgba, rgba_length, g_native_string_scratch, base64_capacity)) {
            goto cleanup_windows;
        }
        *result = aivm_value_string(g_native_string_scratch);
        status = AIVM_SYSCALL_OK;

cleanup_windows:
        if (converter != NULL) {
            converter->lpVtbl->Release(converter);
        }
        if (frame != NULL) {
            frame->lpVtbl->Release(frame);
        }
        if (decoder != NULL) {
            decoder->lpVtbl->Release(decoder);
        }
        if (stream != NULL) {
            stream->lpVtbl->Release(stream);
        }
        if (factory != NULL) {
            factory->lpVtbl->Release(factory);
        }
        free(rgba);
        if (should_uninit != 0) {
            CoUninitialize();
        }
        if (status != AIVM_SYSCALL_OK) {
            result->type = AIVM_VAL_VOID;
        }
        return status;
    }
#elif defined(__APPLE__)
    CFDataRef data = NULL;
    CGImageSourceRef source = NULL;
    CGImageRef image = NULL;
    CGColorSpaceRef color_space = NULL;
    CGContextRef context = NULL;
    uint8_t* rgba = NULL;
    size_t width;
    size_t height;
    size_t rgba_length;
    size_t base64_capacity;
    int status = AIVM_SYSCALL_ERR_INVALID;

    data = CFDataCreate(kCFAllocatorDefault, input, (CFIndex)input_len);
    if (data == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    source = CGImageSourceCreateWithData(data, NULL);
    if (source == NULL) {
        goto cleanup;
    }
    image = CGImageSourceCreateImageAtIndex(source, 0U, NULL);
    if (image == NULL) {
        goto cleanup;
    }
    width = (size_t)CGImageGetWidth(image);
    height = (size_t)CGImageGetHeight(image);
    if (width == 0U || height == 0U) {
        goto cleanup;
    }
    if (width > (SIZE_MAX / 4U) || height > (SIZE_MAX / (width * 4U))) {
        goto cleanup;
    }
    rgba_length = width * height * 4U;
    rgba = (uint8_t*)malloc(rgba_length);
    if (rgba == NULL) {
        goto cleanup;
    }
    memset(rgba, 0, rgba_length);
    color_space = CGColorSpaceCreateDeviceRGB();
    if (color_space == NULL) {
        goto cleanup;
    }
    context = CGBitmapContextCreate(
        rgba,
        width,
        height,
        8U,
        width * 4U,
        color_space,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (context == NULL) {
        goto cleanup;
    }
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context, CGRectMake(0.0, 0.0, (CGFloat)width, (CGFloat)height), image);
    if (!native_rgba_base64_capacity(rgba_length, &base64_capacity)) {
        goto cleanup;
    }
    if (!native_string_scratch_ensure_capacity(base64_capacity)) {
        goto cleanup;
    }
    if (!native_bytes_to_base64(rgba, rgba_length, g_native_string_scratch, base64_capacity)) {
        goto cleanup;
    }
    *result = aivm_value_string(g_native_string_scratch);
    status = AIVM_SYSCALL_OK;

cleanup:
    if (context != NULL) {
        CGContextRelease(context);
    }
    if (color_space != NULL) {
        CGColorSpaceRelease(color_space);
    }
    if (image != NULL) {
        CGImageRelease(image);
    }
    if (source != NULL) {
        CFRelease(source);
    }
    if (data != NULL) {
        CFRelease(data);
    }
    free(rgba);
    if (status != AIVM_SYSCALL_OK) {
        result->type = AIVM_VAL_VOID;
    }
    return status;
#else
    result->type = AIVM_VAL_VOID;
    return AIVM_SYSCALL_ERR_INVALID;
#endif
}

#include "airun_crypto_host.inc"

#include "airun_text_host.inc"

struct NativeDebugOptions {
    int emit_bundle;
    const char* out_dir;
    const char* input_path;
    const char* debug_mode;
};

static const char* aivm_opcode_name(AivmOpcode opcode)
{
    switch (opcode) {
        case AIVM_OP_NOP: return "NOP";
        case AIVM_OP_HALT: return "HALT";
        case AIVM_OP_STUB: return "STUB";
        case AIVM_OP_PUSH_INT: return "PUSH_INT";
        case AIVM_OP_POP: return "POP";
        case AIVM_OP_STORE_LOCAL: return "STORE_LOCAL";
        case AIVM_OP_LOAD_LOCAL: return "LOAD_LOCAL";
        case AIVM_OP_ADD_INT: return "ADD_INT";
        case AIVM_OP_JUMP: return "JUMP";
        case AIVM_OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
        case AIVM_OP_PUSH_BOOL: return "PUSH_BOOL";
        case AIVM_OP_CALL: return "CALL";
        case AIVM_OP_RET: return "RET";
        case AIVM_OP_EQ_INT: return "EQ_INT";
        case AIVM_OP_EQ: return "EQ";
        case AIVM_OP_CONST: return "CONST";
        case AIVM_OP_STR_CONCAT: return "STR_CONCAT";
        case AIVM_OP_TO_STRING: return "TO_STRING";
        case AIVM_OP_STR_ESCAPE: return "STR_ESCAPE";
        case AIVM_OP_RETURN: return "RETURN";
        case AIVM_OP_STR_SUBSTRING: return "STR_SUBSTRING";
        case AIVM_OP_STR_REMOVE: return "STR_REMOVE";
        case AIVM_OP_CALL_SYS: return "CALL_SYS";
        case AIVM_OP_ASYNC_CALL: return "ASYNC_CALL";
        case AIVM_OP_ASYNC_CALL_SYS: return "ASYNC_CALL_SYS";
        case AIVM_OP_AWAIT: return "AWAIT";
        case AIVM_OP_PAR_BEGIN: return "PAR_BEGIN";
        case AIVM_OP_PAR_FORK: return "PAR_FORK";
        case AIVM_OP_PAR_JOIN: return "PAR_JOIN";
        case AIVM_OP_PAR_CANCEL: return "PAR_CANCEL";
        case AIVM_OP_STR_UTF8_BYTE_COUNT: return "STR_UTF8_BYTE_COUNT";
        case AIVM_OP_NODE_KIND: return "NODE_KIND";
        case AIVM_OP_NODE_ID: return "NODE_ID";
        case AIVM_OP_ATTR_COUNT: return "ATTR_COUNT";
        case AIVM_OP_ATTR_KEY: return "ATTR_KEY";
        case AIVM_OP_ATTR_VALUE_KIND: return "ATTR_VALUE_KIND";
        case AIVM_OP_ATTR_VALUE_STRING: return "ATTR_VALUE_STRING";
        case AIVM_OP_ATTR_VALUE_INT: return "ATTR_VALUE_INT";
        case AIVM_OP_ATTR_VALUE_BOOL: return "ATTR_VALUE_BOOL";
        case AIVM_OP_CHILD_COUNT: return "CHILD_COUNT";
        case AIVM_OP_CHILD_AT: return "CHILD_AT";
        case AIVM_OP_MAKE_BLOCK: return "MAKE_BLOCK";
        case AIVM_OP_APPEND_CHILD: return "APPEND_CHILD";
        case AIVM_OP_MAKE_ERR: return "MAKE_ERR";
        case AIVM_OP_MAKE_LIT_STRING: return "MAKE_LIT_STRING";
        case AIVM_OP_MAKE_LIT_INT: return "MAKE_LIT_INT";
        case AIVM_OP_MAKE_LIT_BOOL: return "MAKE_LIT_BOOL";
        case AIVM_OP_MAKE_NODE: return "MAKE_NODE";
        case AIVM_OP_MAKE_FIELD_STRING: return "MAKE_FIELD_STRING";
        case AIVM_OP_MAKE_MAP: return "MAKE_MAP";
        case AIVM_OP_MAKE_NODE_EMPTY: return "MAKE_NODE_EMPTY";
        case AIVM_OP_APPEND_ATTR: return "APPEND_ATTR";
        case AIVM_OP_STR_FIND: return "STR_FIND";
        case AIVM_OP_STR_FROM_CODEPOINT: return "STR_FROM_CODEPOINT";
        case AIVM_OP_STR_DECODE_UNICODE_HEX4: return "STR_DECODE_UNICODE_HEX4";
        case AIVM_OP_STR_DECODE_UNICODE_SURROGATE_PAIR_HEX4: return "STR_DECODE_UNICODE_SURROGATE_PAIR_HEX4";
        case AIVM_OP_BYTES_LENGTH: return "BYTES_LENGTH";
        case AIVM_OP_BYTES_AT: return "BYTES_AT";
        case AIVM_OP_BYTES_SLICE: return "BYTES_SLICE";
        case AIVM_OP_BYTES_CONCAT: return "BYTES_CONCAT";
        case AIVM_OP_BYTES_FROM_UTF8_STRING: return "BYTES_FROM_UTF8_STRING";
        case AIVM_OP_BYTES_TO_UTF8_STRING: return "BYTES_TO_UTF8_STRING";
        case AIVM_OP_BYTES_FROM_BASE64: return "BYTES_FROM_BASE64";
        case AIVM_OP_BYTES_TO_BASE64: return "BYTES_TO_BASE64";
        case AIVM_OP_MAKE_PAIR: return "MAKE_PAIR";
        case AIVM_OP_PAIR_FIRST: return "PAIR_FIRST";
        case AIVM_OP_PAIR_SECOND: return "PAIR_SECOND";
        case AIVM_OP_SUB_NUM: return "SUB_NUM";
        case AIVM_OP_MUL_NUM: return "MUL_NUM";
        case AIVM_OP_DIV_NUM: return "DIV_NUM";
        case AIVM_OP_MOD_NUM: return "MOD_NUM";
        case AIVM_OP_POW_NUM: return "POW_NUM";
        case AIVM_OP_LT_NUM: return "LT_NUM";
        default: return "UNKNOWN";
    }
}

static const char* airun_value_type_name(AivmValueType type)
{
    switch (type) {
        case AIVM_VAL_VOID: return "void";
        case AIVM_VAL_INT: return "int";
        case AIVM_VAL_NUMBER: return "number";
        case AIVM_VAL_BOOL: return "bool";
        case AIVM_VAL_NULL: return "null";
        case AIVM_VAL_STRING: return "string";
        case AIVM_VAL_BYTES: return "bytes";
        case AIVM_VAL_NODE: return "node";
        case AIVM_VAL_PAIR: return "pair";
        default: return "unknown";
    }
}

static const char* infer_call_target(const AivmProgram* program, const AivmVm* vm)
{
    size_t pc;
    const AivmInstruction* inst;
    if (program == NULL || vm == NULL || program->instructions == NULL) {
        return NULL;
    }
    pc = vm->instruction_pointer;
    if (pc >= program->instruction_count) {
        return NULL;
    }
    inst = &program->instructions[pc];
    if (inst->opcode != AIVM_OP_CALL_SYS && inst->opcode != AIVM_OP_ASYNC_CALL_SYS) {
        return NULL;
    }
    if (pc == 0U) {
        return NULL;
    }
    {
        const AivmInstruction* prev = &program->instructions[pc - 1U];
        if (prev->opcode == AIVM_OP_CONST &&
            prev->operand_int >= 0 &&
            (size_t)prev->operand_int < program->constant_count) {
            AivmValue target_value = program->constants[prev->operand_int];
            if (target_value.type == AIVM_VAL_STRING && target_value.string_value != NULL) {
                return target_value.string_value;
            }
        }
    }
    return NULL;
}

#include "airun_debug_host.inc"

static int emit_vm_error_with_context(const AivmProgram* program, const AivmVm* vm, const char* vm_error_message)
{
    const char* phase = "exec";
    const char* function_name = "main";
    const char* node_id = "unknown";
    const char* opcode_name = "UNKNOWN";
    const char* call_target = NULL;
    const char* vm_code = "VM000";
    const char* vm_msg = "Unknown VM error.";
    const char* detail = NULL;
    const char* detail_target = NULL;
    char detail_target_storage[128];
    const char* stack_top0 = "empty";
    const char* stack_top1 = "empty";
    const char* stack_top2 = "empty";
    size_t pc = 0U;
    size_t display_pc = 0U;
    const AivmInstruction* inst = NULL;
    char typed_code[32];
    const char* detail_colon;
    const char* detail_slash;
    size_t code_len;
    if (vm == NULL || program == NULL) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"%s phase=exec function=main pc=0 nodeId=unknown opcode=UNKNOWN\" nodeId=vm)\n",
            (vm_error_message == NULL) ? "VM execution failed." : vm_error_message);
        return 3;
    }
    detail = aivm_vm_error_detail(vm);
    vm_code = aivm_vm_error_code(vm->error);
    vm_msg = aivm_vm_error_message(vm->error);
    pc = vm->instruction_pointer;
    display_pc = pc;
    if (program->instruction_count > 0U && display_pc >= program->instruction_count) {
        display_pc = program->instruction_count - 1U;
    }
    if (vm->error == AIVM_VM_ERR_SYSCALL) {
        phase = "syscall";
        if (vm->stack_count > 0U) {
            stack_top0 = airun_value_type_name(vm->stack[vm->stack_count - 1U].type);
        }
        if (vm->stack_count > 1U) {
            stack_top1 = airun_value_type_name(vm->stack[vm->stack_count - 2U].type);
        }
        if (vm->stack_count > 2U) {
            stack_top2 = airun_value_type_name(vm->stack[vm->stack_count - 3U].type);
        }
    }
    if (program->instructions != NULL && display_pc < program->instruction_count) {
        inst = &program->instructions[display_pc];
        opcode_name = aivm_opcode_name(inst->opcode);
        {
            AivmVm vm_for_target = *vm;
            vm_for_target.instruction_pointer = display_pc;
            call_target = infer_call_target(program, &vm_for_target);
        }
    }
    if (vm->error == AIVM_VM_ERR_SYSCALL &&
        inst != NULL &&
        (inst->opcode == AIVM_OP_CALL_SYS || inst->opcode == AIVM_OP_ASYNC_CALL_SYS)) {
        display_pc = pc;
        opcode_name = aivm_opcode_name(inst->opcode);
    }

    if (vm->error == AIVM_VM_ERR_SYSCALL && detail != NULL && detail[0] != '\0') {
        {
            const char* target_marker = strstr(detail, "target=");
            if (target_marker != NULL) {
                size_t target_len = 0U;
                target_marker += strlen("target=");
                while (target_marker[target_len] != '\0' &&
                       !isspace((unsigned char)target_marker[target_len]) &&
                       target_marker[target_len] != '"') {
                    target_len += 1U;
                }
                if (target_len > 0U) {
                    if (target_len >= sizeof(detail_target_storage)) {
                        target_len = sizeof(detail_target_storage) - 1U;
                    }
                    memcpy(detail_target_storage, target_marker, target_len);
                    detail_target_storage[target_len] = '\0';
                    detail_target = detail_target_storage;
                }
            }
        }
        memset(typed_code, 0, sizeof(typed_code));
        detail_colon = strchr(detail, ':');
        detail_slash = strchr(detail, '/');
        if (detail_colon == NULL) {
            detail_colon = detail + strlen(detail);
        }
        if (detail_slash != NULL && detail_slash < detail_colon) {
            detail_colon = detail_slash;
        }
        code_len = (size_t)(detail_colon - detail);
        if (code_len > 0U && code_len < sizeof(typed_code)) {
            memcpy(typed_code, detail, code_len);
            typed_code[code_len] = '\0';
            if (detail_target != NULL &&
                call_target != NULL &&
                strcmp(detail_target, call_target) != 0) {
                fprintf(stderr,
                    "Err#err1(code=%s message=\"phase=%s function=%s pc=%llu nodeId=%s opcode=%s callTarget=%s bytecodeTarget=%s stackTop0=%s stackTop1=%s stackTop2=%s vmCode=%s vmMessage=%s detail=%s\" nodeId=vm)\n",
                    typed_code,
                    phase,
                    function_name,
                    (unsigned long long)display_pc,
                    node_id,
                    opcode_name,
                    detail_target,
                    call_target,
                    stack_top0,
                    stack_top1,
                    stack_top2,
                    vm_code,
                    vm_msg,
                    detail);
            } else {
                fprintf(stderr,
                    "Err#err1(code=%s message=\"phase=%s function=%s pc=%llu nodeId=%s opcode=%s callTarget=%s vmCode=%s vmMessage=%s detail=%s\" nodeId=vm)\n",
                    typed_code,
                    phase,
                    function_name,
                    (unsigned long long)display_pc,
                    node_id,
                    opcode_name,
                    (call_target == NULL) ? "unknown" : call_target,
                    vm_code,
                    vm_msg,
                    detail);
            }
            return 3;
        }
    }

    fprintf(stderr,
        "Err#err1(code=RUN001 message=\"%s phase=%s function=%s pc=%llu nodeId=%s opcode=%s callTarget=%s vmCode=%s vmMessage=%s detail=%s\" nodeId=vm)\n",
        (vm_error_message == NULL) ? "VM execution failed." : vm_error_message,
        phase,
        function_name,
        (unsigned long long)display_pc,
        node_id,
        opcode_name,
        (call_target == NULL) ? "unknown" : call_target,
        vm_code,
        vm_msg,
        (detail == NULL) ? "none" : detail);
    return 3;
}

static int run_native_compiled_program(
    const AivmProgram* program,
    const char* vm_error_message,
    const char* const* process_argv,
    size_t process_argv_count,
    const NativeDebugOptions* debug_options)
{
    AivmSyscallBinding bindings[128] = { 0 };
    size_t binding_count;
    static AivmVm vm;
    int ok;
    int exit_code = 0;
    int has_exit_code = 0;
    char diagnostics_line[768];

    if (program == NULL) {
        (void)write_native_debug_bundle(debug_options, NULL, NULL, 0, 0, "program-null");
        return 2;
    }
    g_native_active_vm = &vm;
    g_airun_live_debug_options = debug_options;
    g_airun_live_debug_program = program;
    g_airun_live_debug_vm = &vm;
    g_airun_live_debug_refresh_counter = 0U;
    g_airun_live_debug_emit_bundle = (debug_options == NULL) ? 0 : debug_options->emit_bundle;
    (void)snprintf(g_native_ui_event_type, sizeof(g_native_ui_event_type), "none");
    g_native_ui_event_key[0] = '\0';
    g_native_ui_event_text[0] = '\0';
    g_native_ui_event_target_id[0] = '\0';
    native_scene_capture_configure(
        (debug_options == NULL) ? "off" : debug_options->debug_mode,
        (debug_options == NULL) ? NULL : debug_options->out_dir);
    airun_log_capture_configure(
        (debug_options == NULL) ? 0 : debug_options->emit_bundle,
        (debug_options == NULL) ? NULL : debug_options->out_dir);
    airun_log_message(
        AIRUN_LOG_INFO,
        "runtime",
        "runtime-identity name=ailang-native-c abi=%u exe=%s",
        aivm_c_abi_version(),
        g_airun_runtime_exe_path[0] == '\0' ? "" : g_airun_runtime_exe_path);
    (void)write_native_debug_bundle(
        debug_options,
        program,
        NULL,
        0,
        0,
        "status=running");
    airun_log_message(
        AIRUN_LOG_TRACE,
        "runtime",
        "compiled-program argv-count=%llu arg0=%s arg1=%s arg2=%s",
        (unsigned long long)process_argv_count,
        (process_argv_count > 0U && process_argv != NULL && process_argv[0] != NULL) ? process_argv[0] : "",
        (process_argv_count > 1U && process_argv != NULL && process_argv[1] != NULL) ? process_argv[1] : "",
        (process_argv_count > 2U && process_argv != NULL && process_argv[2] != NULL) ? process_argv[2] : "");
    native_ui_runtime_reset_handles();
    native_host_ui_reset();
    native_net_reset();
    native_fs_file_handles_reset();
    native_worker_reset_all();

    bindings[0].target = "sys.stdout.writeLine";
    bindings[0].handler = native_syscall_stdout_write_line;
    bindings[1].target = "io.print";
    bindings[1].handler = native_syscall_stdout_write_line;
    bindings[2].target = "io.write";
    bindings[2].handler = native_syscall_stdout_write_line;
    bindings[3].target = "sys.process.args";
    bindings[3].handler = native_syscall_process_argv;
    bindings[10].target = "sys.fs.file.delete";
    bindings[10].handler = native_syscall_fs_file_delete;
    bindings[11].target = "sys.fs.dir.delete";
    bindings[11].handler = native_syscall_fs_dir_delete;
    bindings[12].target = "sys.process.spawn";
    bindings[12].handler = native_syscall_process_spawn;
    bindings[13].target = "sys.process.wait";
    bindings[13].handler = native_syscall_process_wait;
    bindings[14].target = "sys.process.kill";
    bindings[14].handler = native_syscall_process_kill;
    bindings[15].target = "sys.process.stdout.read";
    bindings[15].handler = native_syscall_process_stdout_read;
    bindings[16].target = "sys.process.stderr.read";
    bindings[16].handler = native_syscall_process_stderr_read;
    bindings[17].target = "sys.process.poll";
    bindings[17].handler = native_syscall_process_poll;
    bindings[24].target = "sys.ui.measureText";
    bindings[24].handler = native_syscall_ui_measure_text;
    bindings[25].target = "sys.ui.createWindow";
    bindings[25].handler = native_syscall_ui_create_window;
    bindings[26].target = "sys.ui.beginFrame";
    bindings[26].handler = native_syscall_ui_void_1;
    bindings[27].target = "sys.ui.drawRect";
    bindings[27].handler = native_syscall_ui_draw_rect;
    bindings[28].target = "sys.ui.drawText";
    bindings[28].handler = native_syscall_ui_draw_text;
    bindings[29].target = "sys.ui.endFrame";
    bindings[29].handler = native_syscall_ui_void_1;
    bindings[30].target = "sys.ui.pollEvent";
    bindings[30].handler = native_syscall_ui_poll_event;
    bindings[31].target = "sys.ui.present";
    bindings[31].handler = native_syscall_ui_void_1;
    bindings[32].target = "sys.ui.closeWindow";
    bindings[32].handler = native_syscall_ui_void_1;
    bindings[33].target = "sys.ui.drawLine";
    bindings[33].handler = native_syscall_ui_draw_line;
    bindings[34].target = "sys.ui.drawEllipse";
    bindings[34].handler = native_syscall_ui_draw_ellipse;
    bindings[35].target = "sys.ui.drawPath";
    bindings[35].handler = native_syscall_ui_draw_path;
    bindings[36].target = "sys.ui.drawImage";
    bindings[36].handler = native_syscall_ui_draw_image;
    bindings[37].target = "sys.ui.getWindowSize";
    bindings[37].handler = native_syscall_ui_get_window_size;
    bindings[38].target = "sys.ui.waitFrame";
    bindings[38].handler = native_syscall_ui_void_1;
    bindings[39].target = "sys.worker.start";
    bindings[39].handler = native_syscall_worker_start;
    bindings[40].target = "sys.worker.poll";
    bindings[40].handler = native_syscall_worker_poll;
    bindings[41].target = "sys.worker.result";
    bindings[41].handler = native_syscall_worker_result;
    bindings[42].target = "sys.worker.error";
    bindings[42].handler = native_syscall_worker_error;
    bindings[43].target = "sys.worker.cancel";
    bindings[43].handler = native_syscall_worker_cancel;
    bindings[44].target = "sys.remote.call";
    bindings[44].handler = native_syscall_remote_call;
    /* Legacy aliases retained for pre-release AiBC1 samples still using underscore style names. */
    bindings[45].target = "sys.stdout_writeLine";
    bindings[45].handler = native_syscall_stdout_write_line;
    bindings[46].target = "sys.process_argv";
    bindings[46].handler = native_syscall_process_argv;
    bindings[47].target = "sys.process.cwd";
    bindings[47].handler = native_syscall_process_cwd;
    bindings[48].target = "sys.process.env.get";
    bindings[48].handler = native_syscall_process_env_get;
    bindings[49].target = "sys.platform";
    bindings[49].handler = native_syscall_identity_0;
    bindings[50].target = "sys.arch";
    bindings[50].handler = native_syscall_identity_0;
    bindings[51].target = "sys.os.version";
    bindings[51].handler = native_syscall_identity_0;
    bindings[52].target = "sys.runtime";
    bindings[52].handler = native_syscall_identity_0;
    bindings[63].target = "sys.runtime.platform";
    bindings[63].handler = native_syscall_identity_0;
    bindings[108].target = "sys.runtime.target";
    bindings[108].handler = native_syscall_identity_0;
    bindings[53].target = "sys.time.nowUnixMs";
    bindings[53].handler = native_syscall_time_now_unix_ms;
    bindings[54].target = "sys.time.monotonicMs";
    bindings[54].handler = native_syscall_time_monotonic_ms;
    bindings[55].target = "sys.time.sleepMs";
    bindings[55].handler = native_syscall_time_sleep_ms;
    bindings[56].target = "sys.time.timeZoneId";
    bindings[56].handler = native_syscall_time_zone_id;
    bindings[57].target = "sys.time.timeZoneOffsetMinutesAt";
    bindings[57].handler = native_syscall_time_zone_offset_minutes_at;
    bindings[58].target = "sys.fs.file.read";
    bindings[58].handler = native_syscall_fs_file_read;
    bindings[59].target = "sys.fs.file.exists";
    bindings[59].handler = native_syscall_fs_file_exists;
    bindings[60].target = "sys.fs.path.exists";
    bindings[60].handler = native_syscall_fs_path_exists;
    bindings[61].target = "sys.fs.file.write";
    bindings[61].handler = native_syscall_fs_file_write;
    bindings[62].target = "sys.fs.dir.create";
    bindings[62].handler = native_syscall_fs_dir_create;
    bindings[64].target = "sys.console.write";
    bindings[64].handler = native_syscall_console_write;
    bindings[65].target = "sys.console.writeLine";
    bindings[65].handler = native_syscall_console_write_line;
    bindings[66].target = "sys.console.writeErrLine";
    bindings[66].handler = native_syscall_console_write_err_line;
    bindings[67].target = "sys.console.readLine";
    bindings[67].handler = native_syscall_console_read_line;
    bindings[68].target = "sys.console.readAllStdin";
    bindings[68].handler = native_syscall_console_read_all_stdin;
    bindings[69].target = "sys.process.exit";
    bindings[69].handler = native_syscall_process_exit;
    bindings[70].target = "sys.fs.dir.list";
    bindings[70].handler = native_syscall_fs_dir_list;
    bindings[71].target = "sys.fs.path.stat";
    bindings[71].handler = native_syscall_fs_path_stat;
    bindings[72].target = "sys.net.tcp.close";
    bindings[72].handler = native_syscall_net_tcp_close;
    bindings[73].target = "sys.net.tcp.connect";
    bindings[73].handler = native_syscall_net_tcp_connect;
    bindings[74].target = "sys.net.tcp.listen";
    bindings[74].handler = native_syscall_net_tcp_listen;
    bindings[75].target = "sys.net.tcp.listenTls";
    bindings[75].handler = native_syscall_net_tcp_listen_tls;
    bindings[76].target = "sys.net.tcp.accept";
    bindings[76].handler = native_syscall_net_tcp_accept;
    bindings[77].target = "sys.net.tcp.read";
    bindings[77].handler = native_syscall_net_tcp_read;
    bindings[78].target = "sys.net.tcp.write";
    bindings[78].handler = native_syscall_net_tcp_write;
    bindings[79].target = "sys.net.tcp.connectTls";
    bindings[79].handler = native_syscall_net_tcp_connect_tls;
    bindings[80].target = "sys.net.tcp.connectStart";
    bindings[80].handler = native_syscall_net_start_op;
    bindings[81].target = "sys.net.tcp.connectTlsStart";
    bindings[81].handler = native_syscall_net_tcp_connect_tls_start;
    bindings[82].target = "sys.net.tcp.readStart";
    bindings[82].handler = native_syscall_net_start_op;
    bindings[83].target = "sys.net.tcp.writeStart";
    bindings[83].handler = native_syscall_net_start_op;
    bindings[84].target = "sys.net.async.poll";
    bindings[84].handler = native_syscall_net_async_poll;
    bindings[85].target = "sys.net.async.cancel";
    bindings[85].handler = native_syscall_net_async_cancel;
    bindings[86].target = "sys.net.async.await";
    bindings[86].handler = native_syscall_net_async_await;
    bindings[87].target = "sys.net.async.resultInt";
    bindings[87].handler = native_syscall_net_async_result_int;
    bindings[88].target = "sys.net.async.resultBytes";
    bindings[88].handler = native_syscall_net_async_result_bytes;
    bindings[89].target = "sys.net.async.error";
    bindings[89].handler = native_syscall_net_async_error;
    bindings[90].target = "sys.net.udp.bind";
    bindings[90].handler = native_syscall_net_udp_bind;
    bindings[91].target = "sys.net.udp.recv";
    bindings[91].handler = native_syscall_net_udp_recv;
    bindings[92].target = "sys.net.udp.send";
    bindings[92].handler = native_syscall_net_udp_send;
    bindings[93].target = "sys.net.dns.lookup";
    bindings[93].handler = native_syscall_net_dns_lookup;
    bindings[99].target = "sys.crypto.randomBytes";
    bindings[99].handler = native_syscall_crypto_random_bytes;
    bindings[100].target = "sys.host.openDefault";
    bindings[100].handler = native_syscall_host_open_default;
    bindings[101].target = "sys.image.decodeToRgbaBase64";
    bindings[101].handler = native_syscall_image_decode_to_rgba_base64;
    bindings[103].target = "sys.fs.file.openRead";
    bindings[103].handler = native_syscall_fs_file_open_read;
    bindings[104].target = "sys.fs.file.readChunk";
    bindings[104].handler = native_syscall_fs_file_read_chunk;
    bindings[105].target = "sys.fs.file.close";
    bindings[105].handler = native_syscall_fs_file_close;
    bindings[106].target = "sys.fs.file.openWrite";
    bindings[106].handler = native_syscall_fs_file_open_write;
    bindings[107].target = "sys.fs.file.writeChunk";
    bindings[107].handler = native_syscall_fs_file_write_chunk;
    bindings[109].target = "sys.storage.local.available";
    bindings[109].handler = native_syscall_storage_local_available;
    bindings[110].target = "sys.storage.local.get";
    bindings[110].handler = native_syscall_storage_local_get;
    bindings[111].target = "sys.storage.local.set";
    bindings[111].handler = native_syscall_storage_local_set;
    bindings[112].target = "sys.storage.local.delete";
    bindings[112].handler = native_syscall_storage_local_delete;
    bindings[113].target = "sys.storage.local.exists";
    bindings[113].handler = native_syscall_storage_local_exists;
    bindings[114].target = "sys.storage.secure.available";
    bindings[114].handler = native_syscall_storage_secure_available;
    bindings[115].target = "sys.storage.secure.get";
    bindings[115].handler = native_syscall_storage_secure_get;
    bindings[116].target = "sys.storage.secure.set";
    bindings[116].handler = native_syscall_storage_secure_bool_false;
    bindings[117].target = "sys.storage.secure.delete";
    bindings[117].handler = native_syscall_storage_secure_bool_false;
    bindings[118].target = "sys.storage.secure.exists";
    bindings[118].handler = native_syscall_storage_secure_bool_false;
    bindings[119].target = "sys.ui.pushClipPath";
    bindings[119].handler = native_syscall_ui_push_clip_path;
    bindings[120].target = "sys.ui.popClipPath";
    bindings[120].handler = native_syscall_ui_void_1;
    binding_count = 121U;
    if (debug_options != NULL) {
        bindings[binding_count].target = "sys.debug.mode";
        bindings[binding_count].handler = native_syscall_debug_mode;
        binding_count += 1U;
        bindings[binding_count].target = "sys.debug.captureFrameBegin";
        bindings[binding_count].handler = native_syscall_debug_capture_frame_begin;
        binding_count += 1U;
        bindings[binding_count].target = "sys.debug.captureDraw";
        bindings[binding_count].handler = native_syscall_debug_capture_draw;
        binding_count += 1U;
        bindings[binding_count].target = "sys.debug.captureFrameEnd";
        bindings[binding_count].handler = native_syscall_debug_capture_frame_end;
        binding_count += 1U;
    }
    if (g_airun_log_level >= AIRUN_LOG_TRACE) {
        native_prepare_traced_bindings(bindings, binding_count);
    } else {
        g_native_trace_real_binding_count = 0U;
    }
    ok = aivm_execute_program_with_syscalls_and_argv(
        program,
        bindings,
        binding_count,
        process_argv,
        process_argv_count,
        &vm);
    if (!ok || vm.status == AIVM_VM_STATUS_ERROR) {
        const char* detail = aivm_vm_error_detail(&vm);
        (void)snprintf(
            diagnostics_line,
            sizeof(diagnostics_line),
            "status=error vm_code=%s detail=%s pc=%llu",
            aivm_vm_error_code(vm.error),
            (detail == NULL) ? "none" : detail,
            (unsigned long long)vm.instruction_pointer);
        (void)write_native_debug_bundle(debug_options, program, &vm, 0, 0, diagnostics_line);
        native_net_reset();
        native_fs_file_handles_reset();
        native_worker_reset_all();
        native_host_ui_shutdown();
        native_scene_capture_reset();
        airun_log_capture_close();
        g_airun_live_debug_options = NULL;
        g_airun_live_debug_program = NULL;
        g_airun_live_debug_vm = NULL;
        g_airun_live_debug_refresh_counter = 0U;
        g_airun_live_debug_emit_bundle = 0;
        g_native_active_vm = NULL;
        return emit_vm_error_with_context(program, &vm, vm_error_message);
    }
    if (vm.status == AIVM_VM_STATUS_HALTED && vm.stack_count > 0U) {
        const AivmValue* top = &vm.stack[vm.stack_count - 1U];
        if (top->type == AIVM_VAL_INT) {
            has_exit_code = 1;
            exit_code = (int)top->int_value;
        }
    }
    (void)snprintf(
        diagnostics_line,
        sizeof(diagnostics_line),
        "status=ok vm_code=%s pc=%llu",
        aivm_vm_error_code(vm.error),
        (unsigned long long)vm.instruction_pointer);
    (void)write_native_debug_bundle(debug_options, program, &vm, exit_code, has_exit_code, diagnostics_line);
    native_net_reset();
    native_fs_file_handles_reset();
    native_worker_reset_all();
    native_host_ui_shutdown();
    native_scene_capture_reset();
    airun_log_capture_close();
    g_airun_live_debug_options = NULL;
    g_airun_live_debug_program = NULL;
    g_airun_live_debug_vm = NULL;
    g_airun_live_debug_refresh_counter = 0U;
    g_airun_live_debug_emit_bundle = 0;
    g_native_active_vm = NULL;
    if (has_exit_code) {
        printf("Ok#ok1(type=int value=%d)\n", exit_code);
        return exit_code;
    }
    return 0;
}

static int run_native_aibc1(
    const char* path,
    const char* const* process_argv,
    size_t process_argv_count,
    const NativeDebugOptions* debug_options)
{
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    AivmProgram program;
    AivmProgramLoadResult load_result;

    if (!read_binary_file(path, &bytes, &byte_count)) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to read AiBC1 file. phase=load function=main pc=0 nodeId=unknown opcode=UNKNOWN callTarget=unknown\" nodeId=program)\n");
        return 2;
    }

    load_result = aivm_program_load_aibc1(bytes, byte_count, &program);
    free(bytes);

    if (load_result.status != AIVM_PROGRAM_OK) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to load AiBC1 program. phase=load function=main pc=%llu nodeId=unknown opcode=UNKNOWN callTarget=unknown loadCode=%s\" nodeId=program)\n",
            (unsigned long long)load_result.error_offset,
            aivm_program_status_code(load_result.status));
        return 2;
    }
    return run_native_compiled_program(
        &program,
        "AiBC1 execution failed.",
        process_argv,
        process_argv_count,
        debug_options);
}

static int parse_attr_value_start(const char* attrs, const char* key, const char** out_value_start)
{
    const char* p;
    size_t key_len;
    if (attrs == NULL || key == NULL || out_value_start == NULL) {
        return 0;
    }
    key_len = strlen(key);
    if (key_len == 0U) {
        return 0;
    }
    p = attrs;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') {
            p += 1;
        }
        if (strncmp(p, key, key_len) == 0 &&
            (p[key_len] == '=' || p[key_len] == ' ' || p[key_len] == '\t' || p[key_len] == '\r' ||
             p[key_len] == '\n')) {
            const char* q = p + key_len;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
                q += 1;
            }
            if (*q == '=') {
                q += 1;
                while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
                    q += 1;
                }
                *out_value_start = q;
                return 1;
            }
        }
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != ',') {
            if (*p == '"') {
                int escaped = 0;
                p += 1;
                while (*p != '\0') {
                    if (!escaped && *p == '"') {
                        p += 1;
                        break;
                    }
                    if (!escaped && *p == '\\') {
                        escaped = 1;
                    } else {
                        escaped = 0;
                    }
                    p += 1;
                }
            } else {
                p += 1;
            }
        }
    }
    return 0;
}

static int parse_attr_span(const char* attrs, const char* key, char* out, size_t out_len)
{
    const char* vstart;
    const char* vend;
    size_t n;

    if (attrs == NULL || key == NULL || out == NULL || out_len == 0U ||
        !parse_attr_value_start(attrs, key, &vstart)) {
        return 0;
    }
    if (*vstart == '"') {
        int escaped = 0;
        vstart += 1;
        vend = vstart;
        while (*vend != '\0') {
            if (!escaped && *vend == '"') {
                break;
            }
            if (!escaped && *vend == '\\') {
                escaped = 1;
            } else {
                escaped = 0;
            }
            vend += 1;
        }
    } else {
        vend = vstart;
        while (*vend != '\0' && *vend != ' ' && *vend != '\t' && *vend != '\r' && *vend != '\n') {
            vend += 1;
        }
    }
    if (vend == NULL || vend < vstart) {
        return 0;
    }
    n = (size_t)(vend - vstart);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out, vstart, n);
    out[n] = '\0';
    return 1;
}

static int parse_attr_value_is_quoted(const char* attrs, const char* key)
{
    const char* vstart;
    if (!parse_attr_value_start(attrs, key, &vstart)) {
        return 0;
    }
    return *vstart == '"';
}

static int has_attr_key(const char* attrs, const char* key)
{
    const char* vstart;
    return parse_attr_value_start(attrs, key, &vstart);
}

static int parse_attr_int64(const char* attrs, const char* key, int64_t* out_value)
{
    char text[64];
    char* end = NULL;
    long long v;
    if (out_value == NULL || !parse_attr_span(attrs, key, text, sizeof(text))) {
        return 0;
    }
    v = strtoll(text, &end, 10);
    if (end == text || *end != '\0') {
        return 0;
    }
    *out_value = (int64_t)v;
    return 1;
}

static int parse_attr_double(const char* attrs, const char* key, double* out_value)
{
    char text[128];
    char* end = NULL;
    double v;
    if (out_value == NULL || !parse_attr_span(attrs, key, text, sizeof(text))) {
        return 0;
    }
    v = strtod(text, &end);
    if (end == text || *end != '\0') {
        return 0;
    }
    *out_value = v;
    return 1;
}

static int parse_attr_bool(const char* attrs, const char* key, int* out_value)
{
    char text[16];
    if (out_value == NULL || !parse_attr_span(attrs, key, text, sizeof(text))) {
        return 0;
    }
    if (strcmp(text, "true") == 0) {
        *out_value = 1;
        return 1;
    }
    if (strcmp(text, "false") == 0) {
        *out_value = 0;
        return 1;
    }
    return 0;
}

static int unescape_string(const char* input, char* out, size_t out_len)
{
    size_t i = 0U;
    size_t w = 0U;
    if (input == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    while (input[i] != '\0') {
        char c = input[i];
        if (c == '\\') {
            char e = input[i + 1U];
            if (e == '\0') {
                return 0;
            }
            if (w + 1U >= out_len) {
                return 0;
            }
            if (e == 'n') {
                out[w++] = '\n';
            } else if (e == 'r') {
                out[w++] = '\r';
            } else if (e == 't') {
                out[w++] = '\t';
            } else if (e == '"' || e == '\\') {
                out[w++] = e;
            } else {
                return 0;
            }
            i += 2U;
            continue;
        }
        if (w + 1U >= out_len) {
            return 0;
        }
        out[w++] = c;
        i += 1U;
    }
    out[w] = '\0';
    return 1;
}

static int opcode_from_text(const char* op_text, AivmOpcode* out_opcode)
{
    if (op_text == NULL || out_opcode == NULL) {
        return 0;
    }
#define MAP_OP(name) if (strcmp(op_text, #name) == 0) { *out_opcode = AIVM_OP_##name; return 1; }
    MAP_OP(NOP)
    MAP_OP(HALT)
    MAP_OP(STUB)
    MAP_OP(PUSH_INT)
    MAP_OP(POP)
    MAP_OP(STORE_LOCAL)
    MAP_OP(LOAD_LOCAL)
    MAP_OP(ADD_INT)
    MAP_OP(JUMP)
    MAP_OP(JUMP_IF_FALSE)
    MAP_OP(PUSH_BOOL)
    MAP_OP(CALL)
    MAP_OP(RET)
    MAP_OP(EQ_INT)
    MAP_OP(EQ)
    MAP_OP(CONST)
    MAP_OP(STR_CONCAT)
    MAP_OP(TO_STRING)
    MAP_OP(STR_ESCAPE)
    MAP_OP(RETURN)
    MAP_OP(STR_SUBSTRING)
    MAP_OP(STR_REMOVE)
    MAP_OP(CALL_SYS)
    MAP_OP(ASYNC_CALL)
    MAP_OP(ASYNC_CALL_SYS)
    MAP_OP(AWAIT)
    MAP_OP(PAR_BEGIN)
    MAP_OP(PAR_FORK)
    MAP_OP(PAR_JOIN)
    MAP_OP(PAR_CANCEL)
    MAP_OP(STR_UTF8_BYTE_COUNT)
    MAP_OP(NODE_KIND)
    MAP_OP(NODE_ID)
    MAP_OP(ATTR_COUNT)
    MAP_OP(ATTR_KEY)
    MAP_OP(ATTR_VALUE_KIND)
    MAP_OP(ATTR_VALUE_STRING)
    MAP_OP(ATTR_VALUE_INT)
    MAP_OP(ATTR_VALUE_BOOL)
    MAP_OP(CHILD_COUNT)
    MAP_OP(CHILD_AT)
    MAP_OP(MAKE_BLOCK)
    MAP_OP(APPEND_CHILD)
    MAP_OP(MAKE_ERR)
    MAP_OP(MAKE_LIT_STRING)
    MAP_OP(MAKE_LIT_INT)
    MAP_OP(MAKE_NODE)
    MAP_OP(MAKE_FIELD_STRING)
    MAP_OP(MAKE_MAP)
    MAP_OP(MAKE_NODE_EMPTY)
    MAP_OP(APPEND_ATTR)
    MAP_OP(BYTES_FROM_UTF8_STRING)
    MAP_OP(BYTES_TO_UTF8_STRING)
    MAP_OP(BYTES_FROM_BASE64)
    MAP_OP(BYTES_TO_BASE64)
    MAP_OP(MAKE_PAIR)
    MAP_OP(PAIR_FIRST)
    MAP_OP(PAIR_SECOND)
    MAP_OP(SUB_NUM)
    MAP_OP(MUL_NUM)
    MAP_OP(DIV_NUM)
    MAP_OP(MOD_NUM)
    MAP_OP(POW_NUM)
    MAP_OP(LT_NUM)
#undef MAP_OP
    return 0;
}

static int bytecode_add_string_const(AivmProgram* program, const char* value, int64_t* out_idx)
{
    size_t i;
    size_t len;
    size_t base;
    if (program == NULL || value == NULL || out_idx == NULL) {
        return 0;
    }
    for (i = 0U; i < program->constant_count; i += 1U) {
        if (program->constant_storage[i].type == AIVM_VAL_STRING &&
            program->constant_storage[i].string_value != NULL &&
            strcmp(program->constant_storage[i].string_value, value) == 0) {
            *out_idx = (int64_t)i;
            return 1;
        }
    }
    if (program->constant_count >= AIVM_PROGRAM_MAX_CONSTANTS) {
        return 0;
    }
    len = strlen(value);
    base = program->string_storage_used;
    if (base + len + 1U > AIVM_PROGRAM_MAX_STRING_BYTES) {
        return 0;
    }
    memcpy(&program->string_storage[base], value, len + 1U);
    program->string_storage_used += (len + 1U);
    program->constant_storage[program->constant_count] = aivm_value_string(&program->string_storage[base]);
    *out_idx = (int64_t)program->constant_count;
    program->constant_count += 1U;
    return 1;
}

static int parse_bytecode_aos_to_program_text(
    const char* source,
    AivmProgram* out_program,
    int allow_legacy_zero_b)
{
    const char* p;

    if (source == NULL || out_program == NULL) {
        return 0;
    }
    if (strstr(source, "Bytecode#") == NULL) {
        return 0;
    }

    aivm_program_clear(out_program);
    out_program->instructions = out_program->instruction_storage;
    out_program->constants = out_program->constant_storage;
    out_program->format_version = 2U;
    out_program->format_flags = 0U;

    p = source;
    while ((p = strstr(p, "Const#")) != NULL) {
        const char* lparen = strchr(p, '(');
        const char* rparen;
        char attrs[512];
        char kind[32];
        size_t n;
        if (out_program->constant_count >= AIVM_PROGRAM_MAX_CONSTANTS || lparen == NULL) {
            return 0;
        }
        rparen = strchr(lparen, ')');
        if (rparen == NULL || rparen <= lparen) {
            return 0;
        }
        n = (size_t)(rparen - (lparen + 1));
        if (n + 1U > sizeof(attrs)) {
            return 0;
        }
        memcpy(attrs, lparen + 1, n);
        attrs[n] = '\0';
        if (!parse_attr_span(attrs, "kind", kind, sizeof(kind))) {
            return 0;
        }
        if (strcmp(kind, "int") == 0 || strcmp(kind, "number") == 0) {
            int64_t v = 0;
            double number_value = 0.0;
            if (parse_attr_int64(attrs, "value", &v)) {
                out_program->constant_storage[out_program->constant_count] = aivm_value_int(v);
            } else if (strcmp(kind, "number") == 0 && parse_attr_double(attrs, "value", &number_value)) {
                out_program->constant_storage[out_program->constant_count] = aivm_value_number(number_value);
            } else {
                return 0;
            }
        } else if (strcmp(kind, "bool") == 0) {
            int b = 0;
            if (!parse_attr_bool(attrs, "value", &b)) {
                return 0;
            }
            out_program->constant_storage[out_program->constant_count] = aivm_value_bool(b);
        } else if (strcmp(kind, "string") == 0) {
            char raw[1024];
            char unescaped[1024];
            size_t len;
            size_t base;
            if (!parse_attr_span(attrs, "value", raw, sizeof(raw))) {
                return 0;
            }
            if (!unescape_string(raw, unescaped, sizeof(unescaped))) {
                return 0;
            }
            len = strlen(unescaped);
            base = out_program->string_storage_used;
            if (base + len + 1U > AIVM_PROGRAM_MAX_STRING_BYTES) {
                return 0;
            }
            memcpy(&out_program->string_storage[base], unescaped, len + 1U);
            out_program->string_storage_used += (len + 1U);
            out_program->constant_storage[out_program->constant_count] = aivm_value_string(&out_program->string_storage[base]);
        } else if (strcmp(kind, "node") == 0) {
            /* Legacy bytecode AOS may carry opaque node constants (for example value="Node#n1").
               The native publish path preserves the value family as a deterministic node handle. */
            out_program->constant_storage[out_program->constant_count] =
                aivm_value_node((int64_t)(out_program->constant_count + 1U));
        } else if (strcmp(kind, "null") == 0) {
            out_program->constant_storage[out_program->constant_count] = aivm_value_null();
        } else if (strcmp(kind, "void") == 0) {
            out_program->constant_storage[out_program->constant_count] = aivm_value_void();
        } else {
            return 0;
        }
        out_program->constant_count += 1U;
        p = rparen + 1;
    }

    p = source;
    while ((p = strstr(p, "Inst#")) != NULL) {
        const char* lparen = strchr(p, '(');
        const char* rparen;
        char attrs[512];
        char op[64];
        char syscall_target[512];
        int64_t a = 0;
        AivmOpcode opcode;
        size_t n;

        if (out_program->instruction_count >= AIVM_PROGRAM_MAX_INSTRUCTIONS || lparen == NULL) {
            return 0;
        }
        rparen = strchr(lparen, ')');
        if (rparen == NULL || rparen <= lparen) {
            return 0;
        }
        n = (size_t)(rparen - (lparen + 1));
        if (n + 1U > sizeof(attrs)) {
            return 0;
        }
        memcpy(attrs, lparen + 1, n);
        attrs[n] = '\0';

        if (!parse_attr_span(attrs, "op", op, sizeof(op)) || !opcode_from_text(op, &opcode)) {
            return 0;
        }
        if (parse_attr_span(attrs, "s", syscall_target, sizeof(syscall_target))) {
            int64_t target_const_idx = 0;
            if (!allow_legacy_zero_b ||
                (opcode != AIVM_OP_CALL_SYS && opcode != AIVM_OP_ASYNC_CALL_SYS) ||
                !bytecode_add_string_const(out_program, syscall_target, &target_const_idx) ||
                out_program->instruction_count >= AIVM_PROGRAM_MAX_INSTRUCTIONS) {
                return 0;
            }
            out_program->instruction_storage[out_program->instruction_count].opcode = AIVM_OP_CONST;
            out_program->instruction_storage[out_program->instruction_count].operand_int = target_const_idx;
            out_program->instruction_count += 1U;
        }
        if (has_attr_key(attrs, "b")) {
            int64_t b = 0;
            if (!allow_legacy_zero_b || !parse_attr_int64(attrs, "b", &b) || b != 0) {
                return 0;
            }
        }
        (void)parse_attr_int64(attrs, "a", &a);

        out_program->instruction_storage[out_program->instruction_count].opcode = opcode;
        out_program->instruction_storage[out_program->instruction_count].operand_int = a;
        out_program->instruction_count += 1U;
        p = rparen + 1;
    }

    return out_program->instruction_count > 0U;
}

static int parse_bytecode_aos_to_program_file(
    const char* aos_path,
    AivmProgram* out_program,
    int allow_legacy_zero_b)
{
    char* source = NULL;
    int ok;
    if (aos_path == NULL || out_program == NULL) {
        return simple_fail("graph compile: invalid args");
    }
    if (!read_text_file_alloc(aos_path, &source)) {
        return 0;
    }
    ok = parse_bytecode_aos_to_program_text(source, out_program, allow_legacy_zero_b);
    free(source);
    return ok;
}

static int source_file_looks_like_bytecode_aos(const char* aos_path)
{
    char source[4096];
    if (aos_path == NULL) {
        return 0;
    }
    if (!read_text_file(aos_path, source, sizeof(source))) {
        return 0;
    }
    return strstr(source, "Bytecode#") != NULL;
}

typedef struct {
    char kind[32];
    char attrs[512];
    const char* body_start;
    const char* body_end;
    const char* next;
} SimpleNodeView;

static char g_simple_last_error[256];

static int simple_find_matching_brace(const char* open_brace, const char* end, const char** out_close)
{
    int depth = 0;
    int in_string = 0;
    const char* q;
    if (open_brace == NULL || end == NULL || out_close == NULL || *open_brace != '{') {
        return 0;
    }
    q = open_brace;
    while (q < end && *q != '\0') {
        char c = *q;
        if (in_string) {
            if (c == '\\' && (q + 1) < end) {
                q += 2;
                continue;
            }
            if (c == '"') {
                in_string = 0;
            }
            q += 1;
            continue;
        }
        if (c == '"') {
            in_string = 1;
            q += 1;
            continue;
        }
        if (c == '{') {
            depth += 1;
        } else if (c == '}') {
            depth -= 1;
            if (depth == 0) {
                *out_close = q;
                return 1;
            }
        }
        q += 1;
    }
    return 0;
}

static int simple_find_matching_paren(const char* open_paren, const char* end, const char** out_close)
{
    int depth = 0;
    int in_string = 0;
    const char* q;
    if (open_paren == NULL || end == NULL || out_close == NULL || *open_paren != '(') {
        return 0;
    }
    q = open_paren;
    while (q < end && *q != '\0') {
        char c = *q;
        if (in_string) {
            if (c == '\\' && (q + 1) < end) {
                q += 2;
                continue;
            }
            if (c == '"') {
                in_string = 0;
            }
            q += 1;
            continue;
        }
        if (c == '"') {
            in_string = 1;
            q += 1;
            continue;
        }
        if (c == '(') {
            depth += 1;
        } else if (c == ')') {
            depth -= 1;
            if (depth == 0) {
                *out_close = q;
                return 1;
            }
        }
        q += 1;
    }
    return 0;
}

static int simple_parse_next_node(const char* cursor, const char* end, SimpleNodeView* out_node)
{
    const char* kstart;
    const char* kdelim;
    const char* kid_end;
    const char* lparen;
    const char* rparen;
    const char* open_brace;
    const char* close_brace;
    size_t kind_len;
    size_t attrs_len;

    if (cursor == NULL || end == NULL || out_node == NULL) {
        return 0;
    }
    while (cursor < end && *cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor += 1;
    }
    if (cursor >= end || *cursor == '\0') {
        return 0;
    }
    kstart = cursor;
    kdelim = kstart;
    while (kdelim < end && *kdelim != '\0' &&
           !isspace((unsigned char)*kdelim) &&
           *kdelim != '(' && *kdelim != '{' && *kdelim != '#') {
        kdelim += 1;
    }
    if (kdelim <= kstart || kdelim >= end) {
        return 0;
    }
    kind_len = (size_t)(kdelim - kstart);
    if (kind_len == 0U || kind_len + 1U > sizeof(out_node->kind)) {
        return 0;
    }
    memcpy(out_node->kind, kstart, kind_len);
    out_node->kind[kind_len] = '\0';

    kid_end = kdelim;
    if (*kid_end == '#') {
        kid_end += 1;
        while (kid_end < end && *kid_end != '\0' &&
               !isspace((unsigned char)*kid_end) &&
               *kid_end != '(' && *kid_end != '{') {
            kid_end += 1;
        }
    }

    lparen = NULL;
    if (kid_end < end && *kid_end == '(') {
        lparen = kid_end;
    }
    if (lparen != NULL) {
        if (!simple_find_matching_paren(lparen, end, &rparen) || rparen == NULL || rparen >= end) {
            return 0;
        }
        attrs_len = (size_t)(rparen - (lparen + 1));
        if (attrs_len + 1U > sizeof(out_node->attrs)) {
            return 0;
        }
        memcpy(out_node->attrs, lparen + 1, attrs_len);
        out_node->attrs[attrs_len] = '\0';
        open_brace = rparen + 1;
    } else {
        out_node->attrs[0] = '\0';
        open_brace = kid_end;
    }
    while (open_brace < end && *open_brace != '\0' && isspace((unsigned char)*open_brace)) {
        open_brace += 1;
    }
    if (open_brace < end && *open_brace == '{') {
        if (!simple_find_matching_brace(open_brace, end, &close_brace)) {
            return 0;
        }
        out_node->body_start = open_brace + 1;
        out_node->body_end = close_brace;
        out_node->next = close_brace + 1;
    } else {
        out_node->body_start = open_brace;
        out_node->body_end = open_brace;
        out_node->next = open_brace;
    }
    return 1;
}

static int simple_fail(const char* message)
{
    if (message == NULL) {
        message = "unknown";
    }
    (void)snprintf(g_simple_last_error, sizeof(g_simple_last_error), "%s", message);
    return 0;
}

static int simple_failf(const char* fmt, ...)
{
    va_list args;
    if (fmt == NULL) {
        return simple_fail("unknown");
    }
    va_start(args, fmt);
    (void)vsnprintf(g_simple_last_error, sizeof(g_simple_last_error), fmt, args);
    va_end(args);
    return 0;
}

static const char* simple_last_error(void)
{
    if (g_simple_last_error[0] == '\0') {
        return "unknown";
    }
    return g_simple_last_error;
}

static int simple_emit_instruction(AivmProgram* program, AivmOpcode opcode, int64_t operand_int)
{
    if (program == NULL) {
        simple_fail("emit instruction: null program");
        return 0;
    }
    if (program->instruction_count >= AIVM_PROGRAM_MAX_INSTRUCTIONS) {
        simple_fail("emit instruction: instruction capacity exceeded");
        return 0;
    }
    program->instruction_storage[program->instruction_count].opcode = opcode;
    program->instruction_storage[program->instruction_count].operand_int = operand_int;
    program->instruction_count += 1U;
    return 1;
}

static int simple_lookup_local(char names[][64], size_t* local_count, const char* name, size_t* out_idx, int create_if_missing)
{
    size_t i;
    if (names == NULL || local_count == NULL || name == NULL || out_idx == NULL) {
        return 0;
    }
    for (i = 0U; i < *local_count; i += 1U) {
        if (strcmp(names[i], name) == 0) {
            *out_idx = i;
            return 1;
        }
    }
    if (!create_if_missing || *local_count >= 256U) {
        return 0;
    }
    if (snprintf(names[*local_count], 64U, "%s", name) >= 64) {
        return 0;
    }
    *out_idx = *local_count;
    *local_count += 1U;
    return 1;
}

static int simple_add_string_const(AivmProgram* program, const char* value, size_t* out_idx)
{
    size_t i;
    size_t len;
    size_t base;
    if (program == NULL || value == NULL || out_idx == NULL) {
        simple_fail("add string const: invalid args");
        return 0;
    }
    for (i = 0U; i < program->constant_count; i += 1U) {
        if (program->constant_storage[i].type == AIVM_VAL_STRING &&
            program->constant_storage[i].string_value != NULL &&
            strcmp(program->constant_storage[i].string_value, value) == 0) {
            *out_idx = i;
            return 1;
        }
    }
    if (program->constant_count >= AIVM_PROGRAM_MAX_CONSTANTS) {
        simple_fail("add string const: constant capacity exceeded");
        return 0;
    }
    len = strlen(value);
    base = program->string_storage_used;
    if (base + len + 1U > AIVM_PROGRAM_MAX_STRING_BYTES) {
        simple_fail("add string const: string storage exceeded");
        return 0;
    }
    memcpy(&program->string_storage[base], value, len + 1U);
    program->string_storage_used += (len + 1U);
    program->constant_storage[program->constant_count] = aivm_value_string(&program->string_storage[base]);
    *out_idx = program->constant_count;
    program->constant_count += 1U;
    return 1;
}

static int simple_add_number_const(AivmProgram* program, double value, size_t* out_idx)
{
    size_t i;
    if (program == NULL || out_idx == NULL) {
        simple_fail("add number const: invalid args");
        return 0;
    }
    for (i = 0U; i < program->constant_count; i += 1U) {
        if (program->constant_storage[i].type == AIVM_VAL_NUMBER &&
            program->constant_storage[i].number_value == value) {
            *out_idx = i;
            return 1;
        }
    }
    if (program->constant_count >= AIVM_PROGRAM_MAX_CONSTANTS) {
        simple_fail("add number const: constant capacity exceeded");
        return 0;
    }
    program->constant_storage[program->constant_count] = aivm_value_number(value);
    *out_idx = program->constant_count;
    program->constant_count += 1U;
    return 1;
}

static int simple_compile_expr_node(const SimpleNodeView* node, AivmProgram* program, char locals[][64], size_t* local_count)
{
    if (node == NULL || program == NULL || locals == NULL || local_count == NULL) {
        return 0;
    }
    if (strcmp(node->kind, "Lit") == 0) {
        char value[256];
        int value_is_quoted = parse_attr_value_is_quoted(node->attrs, "value");
        if (!parse_attr_span(node->attrs, "value", value, sizeof(value))) {
            return simple_fail("lit missing value");
        }
        if (!value_is_quoted && (strcmp(value, "true") == 0 || strcmp(value, "false") == 0)) {
            return simple_emit_instruction(program, AIVM_OP_PUSH_BOOL, (strcmp(value, "true") == 0) ? 1 : 0);
        }
        if (!value_is_quoted && strcmp(value, "null") == 0) {
            if (program->constant_count >= AIVM_PROGRAM_MAX_CONSTANTS) {
                return simple_fail("lit null constant capacity exceeded");
            }
            program->constant_storage[program->constant_count] = aivm_value_null();
            if (!simple_emit_instruction(program, AIVM_OP_CONST, (int64_t)program->constant_count)) {
                return 0;
            }
            program->constant_count += 1U;
            return 1;
        }
        if (!value_is_quoted) {
            char* end = NULL;
            long long parsed = strtoll(value, &end, 10);
            if (end != value && *end == '\0') {
                return simple_emit_instruction(program, AIVM_OP_PUSH_INT, (int64_t)parsed);
            }
            if (strchr(value, '.') != NULL) {
                double parsed_number = strtod(value, &end);
                size_t number_idx = 0U;
                if (end != value && *end == '\0') {
                    if (!simple_add_number_const(program, parsed_number, &number_idx)) {
                        return 0;
                    }
                    return simple_emit_instruction(program, AIVM_OP_CONST, (int64_t)number_idx);
                }
            }
        }
        {
            char unescaped[256];
            size_t idx = 0U;
            if (!unescape_string(value, unescaped, sizeof(unescaped))) {
                return simple_failf("string literal decode failed: raw=%s attrs=%s", value, node->attrs);
            }
            if (!simple_add_string_const(program, unescaped, &idx)) {
                return 0;
            }
            return simple_emit_instruction(program, AIVM_OP_CONST, (int64_t)idx);
        }
    }
        if (strcmp(node->kind, "Var") == 0) {
        char name[64];
        size_t idx = 0U;
        if (!parse_attr_span(node->attrs, "name", name, sizeof(name))) {
            return simple_fail("var missing name");
        }
        if (!simple_lookup_local(locals, local_count, name, &idx, 0)) {
            return simple_failf("unknown local variable: %s", name);
        }
        return simple_emit_instruction(program, AIVM_OP_LOAD_LOCAL, (int64_t)idx);
    }
    if (strcmp(node->kind, "StrConcat") == 0) {
        const char* c = node->body_start;
        int first = 1;
        SimpleNodeView child;
        while (simple_parse_next_node(c, node->body_end, &child)) {
            if (!simple_compile_expr_node(&child, program, locals, local_count)) {
                return 0;
            }
            if (!first && !simple_emit_instruction(program, AIVM_OP_STR_CONCAT, 0)) {
                return 0;
            }
            first = 0;
            c = child.next;
        }
        return !first;
    }
    if (strcmp(node->kind, "StrEscape") == 0) {
        SimpleNodeView child;
        if (!simple_parse_next_node(node->body_start, node->body_end, &child)) {
            return simple_fail("StrEscape missing arg");
        }
        if (!simple_compile_expr_node(&child, program, locals, local_count)) {
            return 0;
        }
        return simple_emit_instruction(program, AIVM_OP_STR_ESCAPE, 0);
    }
    if (strcmp(node->kind, "Add") == 0 || strcmp(node->kind, "Sub") == 0 ||
        strcmp(node->kind, "Mul") == 0 || strcmp(node->kind, "Div") == 0 ||
        strcmp(node->kind, "Mod") == 0 || strcmp(node->kind, "Pow") == 0 ||
        strcmp(node->kind, "Lt") == 0 ||
        strcmp(node->kind, "Eq") == 0 || strcmp(node->kind, "ToString") == 0) {
        SimpleNodeView first;
        SimpleNodeView second;
        if (strcmp(node->kind, "ToString") == 0) {
            if (!simple_parse_next_node(node->body_start, node->body_end, &first) ||
                !simple_compile_expr_node(&first, program, locals, local_count)) {
                return simple_fail("ToString missing arg");
            }
            return simple_emit_instruction(program, AIVM_OP_TO_STRING, 0);
        }
        if (!simple_parse_next_node(node->body_start, node->body_end, &first) ||
            !simple_parse_next_node(first.next, node->body_end, &second) ||
            !simple_compile_expr_node(&first, program, locals, local_count) ||
            !simple_compile_expr_node(&second, program, locals, local_count)) {
            return simple_failf("%s missing args", node->kind);
        }
        if (strcmp(node->kind, "Add") == 0) return simple_emit_instruction(program, AIVM_OP_ADD_INT, 0);
        if (strcmp(node->kind, "Sub") == 0) return simple_emit_instruction(program, AIVM_OP_SUB_NUM, 0);
        if (strcmp(node->kind, "Mul") == 0) return simple_emit_instruction(program, AIVM_OP_MUL_NUM, 0);
        if (strcmp(node->kind, "Div") == 0) return simple_emit_instruction(program, AIVM_OP_DIV_NUM, 0);
        if (strcmp(node->kind, "Mod") == 0) return simple_emit_instruction(program, AIVM_OP_MOD_NUM, 0);
        if (strcmp(node->kind, "Pow") == 0) return simple_emit_instruction(program, AIVM_OP_POW_NUM, 0);
        if (strcmp(node->kind, "Lt") == 0) return simple_emit_instruction(program, AIVM_OP_LT_NUM, 0);
        return simple_emit_instruction(program, AIVM_OP_EQ, 0);
    }
    return simple_failf("unsupported expr kind: %s", node->kind);
}

static int simple_emit_void_const(AivmProgram* program);

static int parse_simple_program_aos_to_program_text(const char* source, AivmProgram* out_program)
{
    const char* program_pos;
    const char* first_open;
    const char* program_close;
    const char* p;
    char locals[256][64];
    size_t local_count = 0U;
    int saw_return = 0;

    g_simple_last_error[0] = '\0';

    if (source == NULL || out_program == NULL) {
        return simple_fail("missing source/program");
    }
    program_pos = strstr(source, "Program#");
    if (program_pos == NULL) {
        program_pos = strstr(source, "Program");
    }
    if (program_pos == NULL || strstr(source, "Bytecode#") != NULL || strstr(source, "Bytecode(") != NULL) {
        return simple_fail("missing Program# or bytecode input");
    }

    first_open = strchr(program_pos, '{');
    if (first_open == NULL || !simple_find_matching_brace(first_open, source + strlen(source), &program_close)) {
        return simple_fail("program braces not found");
    }

    aivm_program_clear(out_program);
    out_program->instructions = out_program->instruction_storage;
    out_program->constants = out_program->constant_storage;
    out_program->format_version = 2U;
    out_program->format_flags = 0U;

    p = first_open + 1;
    while (p < program_close) {
        SimpleNodeView node;
        if (!simple_parse_next_node(p, program_close, &node)) {
            break;
        }
        if (strcmp(node.kind, "Export") == 0) {
            p = node.next;
            continue;
        }
        if (strcmp(node.kind, "Import") == 0) {
            p = node.next;
            continue;
        }
        if (strcmp(node.kind, "Let") == 0) {
            char name[64];
            size_t local_idx = 0U;
            SimpleNodeView expr;
            if (!parse_attr_span(node.attrs, "name", name, sizeof(name))) {
                return simple_fail("let missing name");
            }
            if (!simple_lookup_local(locals, &local_count, name, &local_idx, 1)) {
                return simple_fail("let local lookup failed");
            }
            if (!simple_parse_next_node(node.body_start, node.body_end, &expr)) {
                return simple_fail("let missing expression");
            }
            if (!simple_compile_expr_node(&expr, out_program, locals, &local_count)) {
                return simple_failf("let expression compile failed for '%s' (kind=%s)", name, expr.kind);
            }
            if (!simple_emit_instruction(out_program, AIVM_OP_STORE_LOCAL, (int64_t)local_idx)) {
                return simple_fail("let STORE_LOCAL emit failed");
            }
            p = node.next;
            continue;
        }
        if (strcmp(node.kind, "Call") == 0) {
            char target[128];
            const char* mapped = NULL;
            size_t target_idx = 0U;
            const char* c = NULL;
            size_t arg_count = 0U;
            SimpleNodeView arg;
            if (!parse_attr_span(node.attrs, "target", target, sizeof(target))) {
                return simple_fail("call missing target");
            }
            if (strcmp(target, "io.print") == 0 || strcmp(target, "io.write") == 0 || strcmp(target, "sys.stdout.writeLine") == 0) {
                mapped = "sys.stdout.writeLine";
            } else if (strcmp(target, "io.fileExists") == 0) {
                mapped = "sys.fs.file.exists";
            } else if (strcmp(target, "io.readFile") == 0) {
                mapped = "sys.fs.file.read";
            } else if (strcmp(target, "io.writeFile") == 0) {
                mapped = "sys.fs.file.write";
            } else if (strcmp(target, "io.makeDir") == 0) {
                mapped = "sys.fs.dir.create";
            } else if (starts_with(target, "sys.")) {
                mapped = target;
            } else {
                return simple_fail("call unsupported target");
            }
            if (!simple_add_string_const(out_program, mapped, &target_idx)) {
                return simple_fail("call target const add failed");
            }
            if (!simple_emit_instruction(out_program, AIVM_OP_CONST, (int64_t)target_idx)) {
                return simple_fail("call target const emit failed");
            }
            c = node.body_start;
            while (simple_parse_next_node(c, node.body_end, &arg)) {
                if (!simple_compile_expr_node(&arg, out_program, locals, &local_count)) {
                    return simple_fail("call argument compile failed");
                }
                arg_count += 1U;
                c = arg.next;
            }
            if (arg_count > AIVM_VM_MAX_SYSCALL_ARGS) {
                return simple_fail("call has too many arguments");
            }
            if (!simple_emit_instruction(out_program, AIVM_OP_CALL_SYS, (int64_t)arg_count) ||
                !simple_emit_instruction(out_program, AIVM_OP_POP, 0)) {
                return simple_fail("call emit failed");
            }
            p = node.next;
            continue;
        }
        if (strcmp(node.kind, "Return") == 0) {
            SimpleNodeView expr;
            if (!simple_parse_next_node(node.body_start, node.body_end, &expr)) {
                if (!simple_emit_void_const(out_program) ||
                    !simple_emit_instruction(out_program, AIVM_OP_RETURN, 0)) {
                    return simple_fail("empty return emit failed");
                }
            } else if (!simple_compile_expr_node(&expr, out_program, locals, &local_count) ||
                       !simple_emit_instruction(out_program, AIVM_OP_RETURN, 0)) {
                return simple_fail("return emit failed");
            }
            saw_return = 1;
            p = node.next;
            continue;
        }
        return simple_fail("unsupported top-level node");
    }

    if (!saw_return && !simple_emit_instruction(out_program, AIVM_OP_HALT, 0)) {
        return simple_fail("halt emit failed");
    }
    return out_program->instruction_count > 0U;
}

static int parse_simple_program_graph_to_program_file(const char* aos_path, AivmProgram* out_program);

static int parse_simple_program_aos_to_program_file(const char* aos_path, AivmProgram* out_program)
{
    if (aos_path == NULL || out_program == NULL) {
        return 0;
    }
    return parse_simple_program_graph_to_program_file(aos_path, out_program);
}

#define SIMPLE_MAX_SOURCES 256
#define SIMPLE_MAX_FUNCS 1024
#define SIMPLE_MAX_FIXUPS 2048
#define SIMPLE_MAX_LOCALS 1024
#define SIMPLE_MAX_LOOP_DEPTH 128
#define SIMPLE_MAX_LOOP_FIXUPS 1024

typedef struct {
    char path[PATH_MAX];
    char* text;
} SimpleSource;

typedef struct {
    char name[64];
    char params_raw[256];
    const char* body_start;
    const char* body_end;
    int source_index;
    int compiled;
    int compiling;
    size_t entry_ip;
} SimpleFnDef;

typedef struct {
    size_t instruction_index;
    char target[64];
} SimpleCallFixup;

typedef struct {
    size_t start_ip;
    size_t break_sites[SIMPLE_MAX_LOOP_FIXUPS];
    size_t break_count;
    size_t continue_sites[SIMPLE_MAX_LOOP_FIXUPS];
    size_t continue_count;
} SimpleLoopFrame;

typedef struct {
    AivmProgram* program;
    SimpleSource sources[SIMPLE_MAX_SOURCES];
    size_t source_count;
    SimpleFnDef funcs[SIMPLE_MAX_FUNCS];
    size_t func_count;
    SimpleCallFixup fixups[SIMPLE_MAX_FIXUPS];
    size_t fixup_count;
    SimpleLoopFrame loop_frames[SIMPLE_MAX_LOOP_DEPTH];
    size_t loop_depth;
    size_t next_local_slot;
    char entry_export[64];
} SimpleCompileContext;

typedef struct {
    char names[SIMPLE_MAX_LOCALS][64];
    size_t slots[SIMPLE_MAX_LOCALS];
    size_t count;
    SimpleCompileContext* ctx;
} SimpleLocals;

static int simple_locals_lookup(SimpleLocals* locals, const char* name, size_t* out_slot, int create_if_missing);
static int simple_compile_expr_ext(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx);
static int simple_compile_stmt_ext(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx,
    int* out_did_return);
static int simple_compile_block_ext(
    const SimpleNodeView* block_node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx,
    int* out_did_return);
static int simple_emit_empty_string_const(AivmProgram* program)
{
    size_t empty_const_idx = 0U;
    if (program == NULL) {
        return 0;
    }
    if (!simple_add_string_const(program, "", &empty_const_idx)) {
        return simple_fail("failed adding empty string const");
    }
    return simple_emit_instruction(program, AIVM_OP_CONST, (int64_t)empty_const_idx);
}

static int simple_add_void_const(AivmProgram* program, size_t* out_idx)
{
    size_t i;
    if (program == NULL || out_idx == NULL) {
        return 0;
    }
    for (i = 0U; i < program->constant_count; i += 1U) {
        if (program->constant_storage[i].type == AIVM_VAL_VOID) {
            *out_idx = i;
            return 1;
        }
    }
    if (program->constant_count >= AIVM_PROGRAM_MAX_CONSTANTS) {
        return simple_fail("add void const: constant capacity exceeded");
    }
    *out_idx = program->constant_count;
    program->constant_storage[program->constant_count] = aivm_value_void();
    program->constant_count += 1U;
    return 1;
}

static int simple_emit_void_const(AivmProgram* program)
{
    size_t void_const_idx = 0U;
    if (program == NULL) {
        return 0;
    }
    if (!simple_add_void_const(program, &void_const_idx)) {
        return simple_fail("failed adding void const");
    }
    return simple_emit_instruction(program, AIVM_OP_CONST, (int64_t)void_const_idx);
}

static int simple_compile_fn_by_index(SimpleCompileContext* ctx, size_t fn_index);

static int simple_add_source(SimpleCompileContext* ctx, const char* path, const char* text, int* out_index)
{
    size_t i;
    size_t text_len;
    char* copy;
    if (ctx == NULL || path == NULL || text == NULL || out_index == NULL) {
        return 0;
    }
    for (i = 0U; i < ctx->source_count; i += 1U) {
        if (strcmp(ctx->sources[i].path, path) == 0) {
            *out_index = (int)i;
            return 1;
        }
    }
    if (ctx->source_count >= SIMPLE_MAX_SOURCES) {
        return 0;
    }
    text_len = strlen(text);
    copy = (char*)malloc(text_len + 1U);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, text, text_len + 1U);
    if (snprintf(ctx->sources[ctx->source_count].path, sizeof(ctx->sources[ctx->source_count].path), "%s", path) >=
        (int)sizeof(ctx->sources[ctx->source_count].path)) {
        free(copy);
        return 0;
    }
    ctx->sources[ctx->source_count].text = copy;
    *out_index = (int)ctx->source_count;
    ctx->source_count += 1U;
    return 1;
}

static int simple_find_func(SimpleCompileContext* ctx, const char* name, size_t* out_index)
{
    size_t i;
    if (ctx == NULL || name == NULL || out_index == NULL) {
        return 0;
    }
    for (i = 0U; i < ctx->func_count; i += 1U) {
        if (strcmp(ctx->funcs[i].name, name) == 0) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

static int simple_add_func(
    SimpleCompileContext* ctx,
    const char* name,
    const char* params_raw,
    const char* body_start,
    const char* body_end,
    int source_index)
{
    if (ctx == NULL || name == NULL || params_raw == NULL || body_start == NULL || body_end == NULL) {
        return 0;
    }
    if (ctx->func_count >= SIMPLE_MAX_FUNCS) {
        return 0;
    }
    if (simple_find_func(ctx, name, &(size_t){0})) {
        return 1;
    }
    if (snprintf(ctx->funcs[ctx->func_count].name, sizeof(ctx->funcs[ctx->func_count].name), "%s", name) >=
        (int)sizeof(ctx->funcs[ctx->func_count].name)) {
        return 0;
    }
    if (snprintf(
            ctx->funcs[ctx->func_count].params_raw,
            sizeof(ctx->funcs[ctx->func_count].params_raw),
            "%s",
            params_raw) >= (int)sizeof(ctx->funcs[ctx->func_count].params_raw)) {
        return 0;
    }
    ctx->funcs[ctx->func_count].body_start = body_start;
    ctx->funcs[ctx->func_count].body_end = body_end;
    ctx->funcs[ctx->func_count].source_index = source_index;
    ctx->funcs[ctx->func_count].compiled = 0;
    ctx->funcs[ctx->func_count].compiling = 0;
    ctx->funcs[ctx->func_count].entry_ip = 0U;
    ctx->func_count += 1U;
    return 1;
}

static int simple_split_params(const char* raw, char out_params[][64], size_t* out_count)
{
    const char* p;
    size_t count = 0U;
    if (raw == NULL || out_params == NULL || out_count == NULL) {
        return 0;
    }
    p = raw;
    while (*p != '\0') {
        const char* start;
        const char* end;
        while (*p == ' ' || *p == '\t' || *p == ',') {
            p += 1;
        }
        if (*p == '\0') {
            break;
        }
        start = p;
        while (*p != '\0' && *p != ',') {
            p += 1;
        }
        end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
            end -= 1;
        }
        if (end > start) {
            size_t n = (size_t)(end - start);
            if (count >= SIMPLE_MAX_LOCALS || n + 1U > 64U) {
                return 0;
            }
            memcpy(out_params[count], start, n);
            out_params[count][n] = '\0';
            count += 1U;
        }
        if (*p == ',') {
            p += 1;
        }
    }
    *out_count = count;
    return 1;
}

static int simple_param_count(const char* raw, size_t* out_count)
{
    char params[SIMPLE_MAX_LOCALS][64];
    return simple_split_params(raw, params, out_count);
}

static int simple_add_fixup(SimpleCompileContext* ctx, size_t instruction_index, const char* target)
{
    if (ctx == NULL || target == NULL) {
        return 0;
    }
    if (ctx->fixup_count >= SIMPLE_MAX_FIXUPS) {
        return 0;
    }
    ctx->fixups[ctx->fixup_count].instruction_index = instruction_index;
    if (snprintf(ctx->fixups[ctx->fixup_count].target, sizeof(ctx->fixups[ctx->fixup_count].target), "%s", target) >=
        (int)sizeof(ctx->fixups[ctx->fixup_count].target)) {
        return 0;
    }
    ctx->fixup_count += 1U;
    return 1;
}

static int simple_resolve_path(const char* base_file, const char* import_path, char* out_path, size_t out_path_len)
{
    char base_dir[PATH_MAX];
    char joined[PATH_MAX];
    if (base_file == NULL || import_path == NULL || out_path == NULL || out_path_len == 0U) {
        return 0;
    }
    if (import_path[0] == '/') {
        return canonical_existing_path(import_path, out_path, out_path_len);
    }
    if (!dirname_of(base_file, base_dir, sizeof(base_dir))) {
        return 0;
    }
    if (!join_path(base_dir, import_path, joined, sizeof(joined))) {
        return 0;
    }
    return canonical_existing_path(joined, out_path, out_path_len);
}

static int simple_resolve_sdk_root(const char* base_file, char* out_root, size_t out_len)
{
    const char* env;
    char exe_dir[PATH_MAX];
    char parent_dir[PATH_MAX];
    char project_dir[PATH_MAX];

    if (out_root == NULL || out_len == 0U) {
        return 0;
    }

    env = getenv("AILANG_SDK_ROOT");
    if (env != NULL && env[0] != '\0') {
        return snprintf(out_root, out_len, "%s", env) >= 0 && strlen(env) < out_len;
    }

    if (base_file != NULL &&
        simple_find_lock_dir_for_source(base_file, project_dir, sizeof(project_dir)) &&
        simple_project_config_get_string(project_dir, "sdkRoot", out_root, out_len)) {
        return 1;
    }

    if (g_airun_runtime_exe_path[0] == '\0' ||
        !dirname_of(g_airun_runtime_exe_path, exe_dir, sizeof(exe_dir)) ||
        !dirname_of(exe_dir, parent_dir, sizeof(parent_dir))) {
        return 0;
    }

    return snprintf(out_root, out_len, "%s", parent_dir) >= 0 && strlen(parent_dir) < out_len;
}

static int simple_resolve_sdk_import_path(
    const char* base_file,
    const char* sdk_name,
    const char* import_path,
    char* out_path,
    size_t out_path_len)
{
    char sdk_root[PATH_MAX];
    char candidate[PATH_MAX];
    char src_candidate[PATH_MAX];

    if (sdk_name == NULL || import_path == NULL || out_path == NULL ||
        strcmp(sdk_name, "ailang") != 0 ||
        import_path[0] == '/' ||
        strstr(import_path, "..") != NULL) {
        return 0;
    }
    if (!simple_resolve_sdk_root(base_file, sdk_root, sizeof(sdk_root)) ||
        !join_path(sdk_root, import_path, candidate, sizeof(candidate))) {
        return 0;
    }
    if (file_exists(candidate)) {
        return canonical_existing_path(candidate, out_path, out_path_len);
    }

    if (starts_with(import_path, "std/") || starts_with(import_path, "compiler/")) {
        char src_path[PATH_MAX];
        if (join_path("src", import_path, src_path, sizeof(src_path)) &&
            join_path(sdk_root, src_path, src_candidate, sizeof(src_candidate)) &&
            file_exists(src_candidate)) {
            return canonical_existing_path(src_candidate, out_path, out_path_len);
        }
    }

    return 0;
}

static int simple_find_lock_dir_for_source(const char* source_file, char* out_project_dir, size_t out_len)
{
    char source_dir[PATH_MAX];
    char current[PATH_MAX];
    char lock_path[PATH_MAX];
    char manifest_path[PATH_MAX];
    if (source_file == NULL || out_project_dir == NULL || out_len == 0U) {
        return 0;
    }
    if (!dirname_of(source_file, source_dir, sizeof(source_dir))) {
        return 0;
    }
    if (snprintf(current, sizeof(current), "%s", source_dir) >= (int)sizeof(current)) {
        return 0;
    }
    for (;;) {
        char parent_dir[PATH_MAX];
        if (join_path(current, "ailang.lock.toml", lock_path, sizeof(lock_path)) && file_exists(lock_path)) {
            return snprintf(out_project_dir, out_len, "%s", current) >= 0 &&
                   strlen(current) < out_len;
        }
        if (join_path(current, "project.aiproj", manifest_path, sizeof(manifest_path)) &&
            file_exists(manifest_path) &&
            (g_native_build_project_dir[0] == '\0' || strcmp(current, g_native_build_project_dir) == 0)) {
            return snprintf(out_project_dir, out_len, "%s", current) >= 0 &&
                   strlen(current) < out_len;
        }
        if (!dirname_of(current, parent_dir, sizeof(parent_dir)) || strcmp(parent_dir, current) == 0) {
            break;
        }
        if (snprintf(current, sizeof(current), "%s", parent_dir) >= (int)sizeof(current)) {
            return 0;
        }
    }
    if (g_native_build_project_dir[0] != '\0') {
        return snprintf(out_project_dir, out_len, "%s", g_native_build_project_dir) >= 0 &&
               strlen(g_native_build_project_dir) < out_len;
    }
    return 0;
}

static int simple_is_absolute_path(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }
#ifdef _WIN32
    return isalpha((unsigned char)path[0]) && path[1] == ':';
#else
    return 0;
#endif
}

static int simple_toml_get_from_section(const char* section, const char* key, char* out, size_t out_len)
{
    char needle[96];
    const char* pos;
    const char* start;
    const char* end;
    size_t n;
    if (section == NULL || key == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (snprintf(needle, sizeof(needle), "%s = \"", key) >= (int)sizeof(needle)) {
        return 0;
    }
    pos = strstr(section, needle);
    if (pos == NULL) {
        if (snprintf(needle, sizeof(needle), "%s=\"", key) >= (int)sizeof(needle)) {
            return 0;
        }
        pos = strstr(section, needle);
    }
    if (pos == NULL) {
        return 0;
    }
    start = pos + strlen(needle);
    end = strchr(start, '"');
    if (end == NULL) {
        return 0;
    }
    n = (size_t)(end - start);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return 1;
}

static int simple_project_config_get_string(const char* project_dir, const char* key, char* out, size_t out_len)
{
    static const char* files[] = { "config.toml", "config.local.toml" };
    size_t i;
    int found = 0;
    if (project_dir == NULL || key == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    for (i = 0U; i < sizeof(files) / sizeof(files[0]); i += 1U) {
        char path[PATH_MAX];
        char* text = NULL;
        char value[PATH_MAX];
        if (!join_path(project_dir, files[i], path, sizeof(path)) ||
            !file_exists(path) ||
            !read_text_file_alloc(path, &text)) {
            continue;
        }
        if (simple_toml_get_from_section(text, key, value, sizeof(value))) {
            (void)snprintf(out, out_len, "%s", value);
            found = 1;
        }
        free(text);
    }
    return found && strlen(out) < out_len;
}

static int simple_resolve_project_local_path(
    const char* project_dir,
    const char* configured_path,
    char* out,
    size_t out_len)
{
    if (project_dir == NULL || configured_path == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (simple_is_absolute_path(configured_path)) {
        return canonical_existing_path(configured_path, out, out_len);
    }
    {
        char joined[PATH_MAX];
        if (!join_path(project_dir, configured_path, joined, sizeof(joined))) {
            return 0;
        }
        return canonical_existing_path(joined, out, out_len);
    }
}

static int simple_lock_resolve_package(
    const char* project_dir,
    const char* package_name,
    char* out_package_dir,
    size_t out_len)
{
    char lock_path[PATH_MAX];
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    char* text = NULL;
    const char* p;
    if (project_dir == NULL || package_name == NULL || out_package_dir == NULL) {
        return 0;
    }
    if (!join_path(project_dir, "ailang.lock.toml", lock_path, sizeof(lock_path)) ||
        !read_binary_file(lock_path, &bytes, &byte_count)) {
        return 0;
    }
    text = (char*)malloc(byte_count + 1U);
    if (text == NULL) {
        free(bytes);
        return 0;
    }
    if (byte_count > 0U) {
        memcpy(text, bytes, byte_count);
    }
    text[byte_count] = '\0';
    free(bytes);
    p = text;
    while ((p = strstr(p, "[[package]]")) != NULL) {
        char name[128];
        char package_path[PATH_MAX];
        char package_root[PATH_MAX];
        char package_root_dir[PATH_MAX];
        char local_path[PATH_MAX];
        if (simple_toml_get_from_section(p, "name", name, sizeof(name)) &&
            strcmp(name, package_name) == 0 &&
            simple_toml_get_from_section(p, "path", package_path, sizeof(package_path)) &&
            simple_toml_get_from_section(p, "packageRoot", package_root, sizeof(package_root)) &&
            simple_resolve_project_local_path(project_dir, package_path, local_path, sizeof(local_path)) &&
            join_path(local_path, package_root, package_root_dir, sizeof(package_root_dir)) &&
            canonical_existing_path(package_root_dir, out_package_dir, out_len)) {
            free(text);
            return 1;
        }
        p += strlen("[[package]]");
    }
    free(text);
    return 0;
}

static int simple_resolve_import_path(const char* base_file, const char* attrs, char* out_path, size_t out_path_len)
{
    char import_path[PATH_MAX];
    char package_name[128];
    char sdk_name[128];
    if (attrs == NULL ||
        !parse_attr_span(attrs, "path", import_path, sizeof(import_path))) {
        return 0;
    }
    if (parse_attr_span(attrs, "sdk", sdk_name, sizeof(sdk_name))) {
        return simple_resolve_sdk_import_path(base_file, sdk_name, import_path, out_path, out_path_len);
    }
    if (!parse_attr_span(attrs, "package", package_name, sizeof(package_name))) {
        return simple_resolve_path(base_file, import_path, out_path, out_path_len);
    }
    {
        char project_dir[PATH_MAX];
        char package_root[PATH_MAX];
        if (!simple_find_lock_dir_for_source(base_file, project_dir, sizeof(project_dir)) ||
            !simple_lock_resolve_package(project_dir, package_name, package_root, sizeof(package_root))) {
            return 0;
        }
        {
            char joined[PATH_MAX];
            if (!join_path(package_root, import_path, joined, sizeof(joined))) {
                return 0;
            }
            return canonical_existing_path(joined, out_path, out_path_len);
        }
    }
}

static void print_template_names_in_dir(const char* prefix, const char* dir_path)
{
#ifdef _WIN32
    char pattern[PATH_MAX];
    WIN32_FIND_DATAA data;
    HANDLE h;
    if (!join_path(dir_path, "*", pattern, sizeof(pattern))) {
        return;
    }
    h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
            strcmp(data.cFileName, ".") != 0 &&
            strcmp(data.cFileName, "..") != 0) {
            printf("%s%s\n", prefix == NULL ? "" : prefix, data.cFileName);
        }
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    DIR* dir;
    struct dirent* ent;
    dir = opendir(dir_path);
    if (dir == NULL) {
        return;
    }
    while ((ent = readdir(dir)) != NULL) {
        char child[PATH_MAX];
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (join_path(dir_path, ent->d_name, child, sizeof(child)) && directory_exists(child)) {
            printf("%s%s\n", prefix == NULL ? "" : prefix, ent->d_name);
        }
    }
    closedir(dir);
#endif
}

static AIRUN_MAYBE_UNUSED int handle_template(int argc, char** argv)
{
    const char* kind = "projects";
    const char* project_dir = ".";
    char lock_path[PATH_MAX];
    char* text = NULL;
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    const char* p;
    if (argc >= 3 && strcmp(argv[2], "list") != 0) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Unsupported template command.\" nodeId=template)\n");
        return 2;
    }
    if (argc >= 4 && argv[3] != NULL && argv[3][0] != '-') {
        kind = argv[3];
    }
    if (argc >= 5 && argv[4] != NULL && argv[4][0] != '-') {
        project_dir = argv[4];
    }
    if (strcmp(kind, "projects") != 0 && strcmp(kind, "files") != 0) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Unknown template type.\" nodeId=template)\n");
        return 2;
    }
    if (strcmp(kind, "projects") == 0) {
        printf("cli\n");
        printf("cli-args\n");
    }
    if (!join_path(project_dir, "ailang.lock.toml", lock_path, sizeof(lock_path)) ||
        !read_binary_file(lock_path, &bytes, &byte_count)) {
        return 0;
    }
    text = (char*)malloc(byte_count + 1U);
    if (text == NULL) {
        free(bytes);
        return 0;
    }
    if (byte_count > 0U) {
        memcpy(text, bytes, byte_count);
    }
    text[byte_count] = '\0';
    free(bytes);
    p = text;
    while ((p = strstr(p, "[[package]]")) != NULL) {
        char name[128];
        char package_path[PATH_MAX];
        char package_root[PATH_MAX];
        char package_dir[PATH_MAX];
        char package_root_dir[PATH_MAX];
        char templates_dir[PATH_MAX];
        char typed_templates_dir[PATH_MAX];
        char prefix[160];
        if (simple_toml_get_from_section(p, "name", name, sizeof(name)) &&
            simple_toml_get_from_section(p, "path", package_path, sizeof(package_path)) &&
            simple_toml_get_from_section(p, "packageRoot", package_root, sizeof(package_root)) &&
            join_path(project_dir, package_path, package_dir, sizeof(package_dir)) &&
            join_path(package_dir, package_root, package_root_dir, sizeof(package_root_dir)) &&
            join_path(package_root_dir, "templates", templates_dir, sizeof(templates_dir)) &&
            join_path(templates_dir, kind, typed_templates_dir, sizeof(typed_templates_dir))) {
            (void)snprintf(prefix, sizeof(prefix), "%s/", name);
            print_template_names_in_dir(prefix, typed_templates_dir);
        }
        p += strlen("[[package]]");
    }
    free(text);
    return 0;
}

typedef struct {
    char paths[SIMPLE_MAX_SOURCES][PATH_MAX];
    size_t count;
} SourceGraphSet;

static void source_graph_hash_update_u64(uint64_t* state, const unsigned char* bytes, size_t len)
{
    size_t i;
    if (state == NULL || bytes == NULL) {
        return;
    }
    for (i = 0U; i < len; i += 1U) {
        *state ^= (uint64_t)bytes[i];
        *state *= 1099511628211ULL;
    }
}

static int source_graph_set_contains(const SourceGraphSet* set, const char* path)
{
    size_t i;
    if (set == NULL || path == NULL) {
        return 0;
    }
    for (i = 0U; i < set->count; i += 1U) {
        if (strcmp(set->paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int source_graph_set_add(SourceGraphSet* set, const char* path)
{
    if (set == NULL || path == NULL) {
        return 0;
    }
    if (source_graph_set_contains(set, path)) {
        return 1;
    }
    if (set->count >= SIMPLE_MAX_SOURCES) {
        return 0;
    }
    if (snprintf(set->paths[set->count], sizeof(set->paths[set->count]), "%s", path) >=
        (int)sizeof(set->paths[set->count])) {
        return 0;
    }
    set->count += 1U;
    return 1;
}

static void source_graph_hash_update_pair(
    uint64_t* state_a,
    uint64_t* state_b,
    const unsigned char* bytes,
    size_t len)
{
    source_graph_hash_update_u64(state_a, bytes, len);
    source_graph_hash_update_u64(state_b, bytes, len);
}

static void source_graph_hash_update_pair_text(uint64_t* state_a, uint64_t* state_b, const char* text)
{
    if (text == NULL) {
        return;
    }
    source_graph_hash_update_pair(state_a, state_b, (const unsigned char*)text, strlen(text));
}

static int source_graph_hash_file(
    const char* path,
    SourceGraphSet* visited,
    uint64_t* hash_state_a,
    uint64_t* hash_state_b)
{
    unsigned char* text_bytes = NULL;
    size_t text_len = 0U;
    char* text = NULL;
    const char* program_pos;
    const char* open_brace;
    const char* close_brace;
    const char* cursor;
    if (path == NULL || visited == NULL || hash_state_a == NULL || hash_state_b == NULL) {
        return 0;
    }
    if (source_graph_set_contains(visited, path)) {
        return 1;
    }
    if (!source_graph_set_add(visited, path)) {
        return 0;
    }
    if (!read_binary_file(path, &text_bytes, &text_len)) {
        return 0;
    }
    text = (char*)malloc(text_len + 1U);
    if (text == NULL) {
        free(text_bytes);
        return 0;
    }
    if (text_len > 0U) {
        memcpy(text, text_bytes, text_len);
    }
    text[text_len] = '\0';
    free(text_bytes);

    source_graph_hash_update_pair_text(hash_state_a, hash_state_b, "file:");
    source_graph_hash_update_pair_text(hash_state_a, hash_state_b, path);
    source_graph_hash_update_pair_text(hash_state_a, hash_state_b, "\n");
    source_graph_hash_update_pair(hash_state_a, hash_state_b, (const unsigned char*)text, text_len);
    source_graph_hash_update_pair_text(hash_state_a, hash_state_b, "\n");

    program_pos = strstr(text, "Program#");
    if (program_pos == NULL) {
        program_pos = strstr(text, "Program");
    }
    if (program_pos == NULL) {
        free(text);
        return 1;
    }
    open_brace = strchr(program_pos, '{');
    if (open_brace == NULL || !simple_find_matching_brace(open_brace, text + strlen(text), &close_brace)) {
        free(text);
        return 0;
    }
    cursor = open_brace + 1;
    while (cursor < close_brace) {
        SimpleNodeView node;
        if (!simple_parse_next_node(cursor, close_brace, &node)) {
            break;
        }
        if (strcmp(node.kind, "Import") == 0) {
            char resolved[PATH_MAX];
            if (!simple_resolve_import_path(path, node.attrs, resolved, sizeof(resolved))) {
                free(text);
                return 0;
            }
            if (!source_graph_hash_file(resolved, visited, hash_state_a, hash_state_b)) {
                free(text);
                return 0;
            }
        }
        cursor = node.next;
    }
    free(text);
    return 1;
}

static int compute_source_graph_cache_key(const char* source_aos, char* out_hex, size_t out_hex_len)
{
    uint64_t hash_state_a = 1469598103934665603ULL;
    uint64_t hash_state_b = 1099511628211ULL;
    SourceGraphSet visited;
    char cap_text[64];
    if (source_aos == NULL || out_hex == NULL || out_hex_len < 33U) {
        return 0;
    }
    memset(&visited, 0, sizeof(visited));
    source_graph_hash_update_pair_text(&hash_state_a, &hash_state_b, AIRUN_NATIVE_CACHE_SCHEMA);
    source_graph_hash_update_pair_text(&hash_state_a, &hash_state_b, "|compiler=");
    source_graph_hash_update_pair_text(&hash_state_a, &hash_state_b, AIRUN_NATIVE_COMPILER_FINGERPRINT);
    source_graph_hash_update_pair_text(&hash_state_a, &hash_state_b, "|inst-cap=");
    (void)snprintf(cap_text, sizeof(cap_text), "%u", (unsigned)AIVM_PROGRAM_MAX_INSTRUCTIONS);
    source_graph_hash_update_pair_text(&hash_state_a, &hash_state_b, cap_text);
    if (!source_graph_hash_file(source_aos, &visited, &hash_state_a, &hash_state_b)) {
        return 0;
    }
    (void)snprintf(
        out_hex,
        out_hex_len,
        "%016llx%016llx",
        (unsigned long long)hash_state_a,
        (unsigned long long)hash_state_b);
    return 1;
}

static int ensure_cache_root_for_source(const char* source_aos, char* out_root, size_t out_root_len)
{
    char source_dir[PATH_MAX];
    char project_dir[PATH_MAX];
    char manifest_path[PATH_MAX];
    char parent_dir[PATH_MAX];
    char toolchain_dir[PATH_MAX];
    char cache_dir[PATH_MAX];
    if (source_aos == NULL || out_root == NULL || out_root_len == 0U) {
        return 0;
    }
    if (!dirname_of(source_aos, source_dir, sizeof(source_dir))) {
        return 0;
    }
    if (join_path(source_dir, "project.aiproj", manifest_path, sizeof(manifest_path)) &&
        file_exists(manifest_path)) {
        if (snprintf(project_dir, sizeof(project_dir), "%s", source_dir) >= (int)sizeof(project_dir)) {
            return 0;
        }
    } else if (dirname_of(source_dir, parent_dir, sizeof(parent_dir)) &&
               join_path(parent_dir, "project.aiproj", manifest_path, sizeof(manifest_path)) &&
               file_exists(manifest_path)) {
        if (snprintf(project_dir, sizeof(project_dir), "%s", parent_dir) >= (int)sizeof(project_dir)) {
            return 0;
        }
    } else {
        if (snprintf(project_dir, sizeof(project_dir), "%s", source_dir) >= (int)sizeof(project_dir)) {
            return 0;
        }
    }
    if (!join_path(project_dir, ".toolchain", toolchain_dir, sizeof(toolchain_dir)) ||
        !join_path(toolchain_dir, "cache", cache_dir, sizeof(cache_dir)) ||
        !join_path(cache_dir, "ailang", out_root, out_root_len)) {
        return 0;
    }
    if (!ensure_directory(toolchain_dir) || !ensure_directory(cache_dir) || !ensure_directory(out_root)) {
        return 0;
    }
    return 1;
}

static int simple_collect_from_file(SimpleCompileContext* ctx, const char* path, int allow_entry_export)
{
    char text[131072];
    const char* program_pos;
    const char* open_brace;
    const char* close_brace;
    const char* cursor;
    const char* trace;
    int source_index = -1;

    if (ctx == NULL || path == NULL) {
        return simple_fail("collect: invalid args");
    }
    trace = getenv("AIVM_NATIVE_BUILD_TRACE");
    if (!read_text_file(path, text, sizeof(text))) {
        return simple_failf("collect: failed reading %s", path);
    }
    if (!simple_add_source(ctx, path, text, &source_index)) {
        return simple_failf("collect: failed adding source %s", path);
    }
    /* already collected previously */
    if (ctx->sources[(size_t)source_index].text != NULL &&
        ctx->sources[(size_t)source_index].text != text) {
        /* continue; collection idempotence is handled by func dedupe */
    }

    program_pos = strstr(ctx->sources[(size_t)source_index].text, "Program#");
    if (program_pos == NULL) {
        program_pos = strstr(ctx->sources[(size_t)source_index].text, "Program");
    }
    if (program_pos == NULL) {
        return simple_failf("collect: missing Program in %s", path);
    }
    open_brace = strchr(program_pos, '{');
    if (open_brace == NULL ||
        !simple_find_matching_brace(open_brace, ctx->sources[(size_t)source_index].text + strlen(ctx->sources[(size_t)source_index].text), &close_brace)) {
        return simple_failf("collect: malformed Program braces in %s", path);
    }
    cursor = open_brace + 1;
    while (cursor < close_brace) {
        SimpleNodeView node;
        if (!simple_parse_next_node(cursor, close_brace, &node)) {
            const char* rest = cursor;
            while (rest < close_brace && (*rest == ' ' || *rest == '\t' || *rest == '\r' || *rest == '\n')) {
                rest += 1;
            }
            if (rest >= close_brace) {
                break;
            }
            return simple_failf(
                "collect: parse failed in %s near offset %llu",
                path,
                (unsigned long long)(rest - ctx->sources[(size_t)source_index].text));
        }
        if (trace != NULL && trace[0] != '\0') {
            fprintf(
                stderr,
                "[ailang-native-compile] collect-node kind=%s source=%s start=%llu next=%llu end=%llu\n",
                node.kind,
                path,
                (unsigned long long)(cursor - ctx->sources[(size_t)source_index].text),
                (unsigned long long)(node.next - ctx->sources[(size_t)source_index].text),
                (unsigned long long)(close_brace - ctx->sources[(size_t)source_index].text));
        }
        if (strcmp(node.kind, "Import") == 0) {
            char resolved[PATH_MAX];
            if (!simple_resolve_import_path(path, node.attrs, resolved, sizeof(resolved))) {
                return simple_failf("collect: import missing path in %s attrs=%s", path, node.attrs);
            }
            if (!simple_collect_from_file(ctx, resolved, 0)) {
                return 0;
            }
            cursor = node.next;
            continue;
        }
        if (strcmp(node.kind, "Export") == 0) {
            char export_name[64];
            if (allow_entry_export &&
                ctx->entry_export[0] == '\0' &&
                parse_attr_span(node.attrs, "name", export_name, sizeof(export_name))) {
                (void)snprintf(ctx->entry_export, sizeof(ctx->entry_export), "%s", export_name);
                if (trace != NULL && trace[0] != '\0') {
                    fprintf(stderr, "[ailang-native-compile] collect-export=%s source=%s\n", ctx->entry_export, path);
                }
            }
            cursor = node.next;
            continue;
        }
        if (strcmp(node.kind, "Let") == 0) {
            char let_name[64];
            SimpleNodeView expr;
            if (!parse_attr_span(node.attrs, "name", let_name, sizeof(let_name))) {
                return simple_failf("collect: let missing name in %s", path);
            }
            if (simple_parse_next_node(node.body_start, node.body_end, &expr) &&
                strcmp(expr.kind, "Fn") == 0) {
                char params_raw[256];
                if (!parse_attr_span(expr.attrs, "params", params_raw, sizeof(params_raw))) {
                    params_raw[0] = '\0';
                }
                if (!simple_add_func(ctx, let_name, params_raw, expr.body_start, expr.body_end, source_index)) {
                    return simple_failf("collect: failed adding function %s from %s", let_name, path);
                }
                if (trace != NULL && trace[0] != '\0') {
                    fprintf(stderr, "[ailang-native-compile] collect-fn=%s source=%s\n", let_name, path);
                }
            }
            cursor = node.next;
            continue;
        }
        cursor = node.next;
    }
    return 1;
}

static int simple_locals_lookup(SimpleLocals* locals, const char* name, size_t* out_slot, int create_if_missing)
{
    size_t i;
    if (locals == NULL || name == NULL || out_slot == NULL) {
        return 0;
    }
    for (i = 0U; i < locals->count; i += 1U) {
        if (strcmp(locals->names[i], name) == 0) {
            *out_slot = locals->slots[i];
            return 1;
        }
    }
    if (!create_if_missing || locals->ctx == NULL) {
        return 0;
    }
    if (locals->count >= SIMPLE_MAX_LOCALS) {
        return simple_failf("local table capacity exceeded (name=%s max=%d)", name, SIMPLE_MAX_LOCALS);
    }
    if (locals->count >= AIVM_VM_LOCALS_CAPACITY) {
        return simple_failf(
            "vm local slot capacity exceeded (name=%s max=%d)",
            name,
            AIVM_VM_LOCALS_CAPACITY);
    }
    if (snprintf(locals->names[locals->count], sizeof(locals->names[locals->count]), "%s", name) >=
        (int)sizeof(locals->names[locals->count])) {
        return simple_failf("local name too long: %s", name);
    }
    locals->slots[locals->count] = locals->count;
    *out_slot = locals->slots[locals->count];
    locals->count += 1U;
    return 1;
}

static int simple_compile_var_expr(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals)
{
    char name[64];
    size_t idx = 0U;
    if (node == NULL || program == NULL || locals == NULL) {
        return 0;
    }
    if (!parse_attr_span(node->attrs, "name", name, sizeof(name))) {
        return simple_fail("var missing name");
    }
    if (!simple_locals_lookup(locals, name, &idx, 0)) {
        return simple_failf("unknown local variable: %s", name);
    }
    return simple_emit_instruction(program, AIVM_OP_LOAD_LOCAL, (int64_t)idx);
}

static int simple_compile_call_ext(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx,
    int pop_result)
{
    const char* c;
    SimpleNodeView arg;
    char target[128];
    int is_syscall_target;
    size_t arg_count = 0U;
    if (node == NULL || program == NULL || locals == NULL || ctx == NULL) {
        return 0;
    }
    if (!parse_attr_span(node->attrs, "target", target, sizeof(target))) {
        return simple_fail("call missing target");
    }
    is_syscall_target =
        starts_with(target, "sys.") ||
        strcmp(target, "io.print") == 0 ||
        strcmp(target, "io.write") == 0 ||
        strcmp(target, "io.fileExists") == 0 ||
        strcmp(target, "io.readFile") == 0 ||
        strcmp(target, "io.writeFile") == 0 ||
        strcmp(target, "io.makeDir") == 0 ||
        strcmp(target, "sys.stdout.writeLine") == 0;

    if (is_syscall_target) {
        size_t target_idx = 0U;
        const char* mapped = target;
        if (strcmp(target, "io.print") == 0 || strcmp(target, "io.write") == 0) {
            mapped = "sys.stdout.writeLine";
        } else if (strcmp(target, "io.fileExists") == 0) {
            mapped = "sys.fs.file.exists";
        } else if (strcmp(target, "io.readFile") == 0) {
            mapped = "sys.fs.file.read";
        } else if (strcmp(target, "io.writeFile") == 0) {
            mapped = "sys.fs.file.write";
        } else if (strcmp(target, "io.makeDir") == 0) {
            mapped = "sys.fs.dir.create";
        }
        if (!simple_add_string_const(program, mapped, &target_idx)) {
            return simple_fail("failed adding syscall target const");
        }
        if (!simple_emit_instruction(program, AIVM_OP_CONST, (int64_t)target_idx)) {
            return simple_fail("failed emitting syscall target const");
        }
        c = node->body_start;
        while (simple_parse_next_node(c, node->body_end, &arg)) {
            if (arg_count >= 32U) {
                return simple_fail("call arg count exceeds native compiler limit");
            }
            if (!simple_compile_expr_ext(&arg, program, locals, ctx)) {
                return 0;
            }
            arg_count += 1U;
            c = arg.next;
        }
        if (!simple_emit_instruction(program, AIVM_OP_CALL_SYS, (int64_t)arg_count)) {
            return simple_fail("failed emitting CALL_SYS");
        }
    } else {
        size_t fn_index;
        size_t call_inst_index;
        c = node->body_start;
        while (simple_parse_next_node(c, node->body_end, &arg)) {
            if (arg_count >= 32U) {
                return simple_fail("call arg count exceeds native compiler limit");
            }
            if (!simple_compile_expr_ext(&arg, program, locals, ctx)) {
                return 0;
            }
            arg_count += 1U;
            c = arg.next;
        }
        if (!simple_find_func(ctx, target, &fn_index)) {
            return simple_failf("unknown function target: %s", target);
        }
        {
            size_t param_count = 0U;
            if (!simple_param_count(ctx->funcs[fn_index].params_raw, &param_count)) {
                return simple_failf("failed parsing params for target: %s", target);
            }
            if (arg_count != param_count) {
                return simple_failf(
                    "call target %s expects %llu args, got %llu",
                    target,
                    (unsigned long long)param_count,
                    (unsigned long long)arg_count);
            }
        }
        call_inst_index = program->instruction_count;
        if (!simple_emit_instruction(program, AIVM_OP_CALL, 0)) {
            return simple_fail("failed emitting CALL");
        }
        if (ctx->funcs[fn_index].compiled) {
            program->instruction_storage[call_inst_index].operand_int = (int64_t)ctx->funcs[fn_index].entry_ip;
        } else {
            if (!simple_add_fixup(ctx, call_inst_index, target)) {
                return simple_fail("failed storing call fixup");
            }
        }
    }

    if (pop_result) {
        if (!simple_emit_instruction(program, AIVM_OP_POP, 0)) {
            return simple_fail("failed emitting POP");
        }
    }
    return 1;
}

static int simple_dynamic_candidate_allowed(const char* candidates, const char* name);

static int simple_compile_dynamic_call_ext(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx)
{
    SimpleNodeView target_expr;
    const char* args_start;
    const char* c;
    SimpleNodeView arg;
    size_t arg_count = 0U;
    size_t target_slot = 0U;
    char target_local_name[64];
    size_t end_jumps[128];
    size_t end_jump_count = 0U;
    size_t i;
    int matched_candidate = 0;
    char candidates[512];
    int has_candidates = 0;
    if (node == NULL || program == NULL || locals == NULL || ctx == NULL) {
        return 0;
    }
    has_candidates = parse_attr_span(node->attrs, "candidates", candidates, sizeof(candidates));
    if (!simple_parse_next_node(node->body_start, node->body_end, &target_expr)) {
        return simple_fail("dynamic call missing target expression");
    }
    args_start = target_expr.next;
    c = args_start;
    while (simple_parse_next_node(c, node->body_end, &arg)) {
        if (arg_count >= 32U) {
            return simple_fail("dynamic call arg count exceeds native compiler limit");
        }
        arg_count += 1U;
        c = arg.next;
    }
    if (snprintf(target_local_name, sizeof(target_local_name), "__dynamic_target_%llu", (unsigned long long)locals->count) >=
        (int)sizeof(target_local_name)) {
        return simple_fail("dynamic call target local name overflow");
    }
    if (!simple_compile_expr_ext(&target_expr, program, locals, ctx) ||
        !simple_locals_lookup(locals, target_local_name, &target_slot, 1) ||
        !simple_emit_instruction(program, AIVM_OP_STORE_LOCAL, (int64_t)target_slot)) {
        return simple_fail("dynamic call target compile failed");
    }
    for (i = 0U; i < ctx->func_count; i += 1U) {
        size_t param_count = 0U;
        size_t name_const_idx = 0U;
        size_t jump_false_ip;
        size_t call_inst_index;
        if (!simple_param_count(ctx->funcs[i].params_raw, &param_count)) {
            return simple_failf("failed parsing params for dynamic target: %s", ctx->funcs[i].name);
        }
        if (param_count != arg_count) {
            continue;
        }
        if (has_candidates && !simple_dynamic_candidate_allowed(candidates, ctx->funcs[i].name)) {
            continue;
        }
        if (!simple_add_string_const(program, ctx->funcs[i].name, &name_const_idx) ||
            !simple_emit_instruction(program, AIVM_OP_LOAD_LOCAL, (int64_t)target_slot) ||
            !simple_emit_instruction(program, AIVM_OP_CONST, (int64_t)name_const_idx) ||
            !simple_emit_instruction(program, AIVM_OP_EQ, 0)) {
            return simple_fail("dynamic call target compare emit failed");
        }
        jump_false_ip = program->instruction_count;
        if (!simple_emit_instruction(program, AIVM_OP_JUMP_IF_FALSE, 0)) {
            return simple_fail("dynamic call branch emit failed");
        }
        c = args_start;
        while (simple_parse_next_node(c, node->body_end, &arg)) {
            if (!simple_compile_expr_ext(&arg, program, locals, ctx)) {
                return simple_fail("dynamic call argument compile failed");
            }
            c = arg.next;
        }
        call_inst_index = program->instruction_count;
        if (!simple_emit_instruction(program, AIVM_OP_CALL, 0)) {
            return simple_fail("dynamic call CALL emit failed");
        }
        if (ctx->funcs[i].compiled) {
            program->instruction_storage[call_inst_index].operand_int = (int64_t)ctx->funcs[i].entry_ip;
        } else if (!simple_add_fixup(ctx, call_inst_index, ctx->funcs[i].name)) {
            return simple_fail("dynamic call fixup failed");
        }
        if (end_jump_count >= (sizeof(end_jumps) / sizeof(end_jumps[0]))) {
            return simple_fail("dynamic call branch capacity exceeded");
        }
        end_jumps[end_jump_count++] = program->instruction_count;
        if (!simple_emit_instruction(program, AIVM_OP_JUMP, 0)) {
            return simple_fail("dynamic call end jump emit failed");
        }
        program->instruction_storage[jump_false_ip].operand_int = (int64_t)program->instruction_count;
        matched_candidate = 1;
    }
    {
        size_t missing_target_idx = 0U;
        if (!simple_add_string_const(program, "sys.dynamic.missingFunction", &missing_target_idx) ||
            !simple_emit_instruction(program, AIVM_OP_CONST, (int64_t)missing_target_idx) ||
            !simple_emit_instruction(program, AIVM_OP_LOAD_LOCAL, (int64_t)target_slot) ||
            !simple_emit_instruction(program, AIVM_OP_CALL_SYS, 1)) {
            return simple_fail("dynamic call fallback emit failed");
        }
    }
    for (i = 0U; i < end_jump_count; i += 1U) {
        program->instruction_storage[end_jumps[i]].operand_int = (int64_t)program->instruction_count;
    }
    if (!matched_candidate) {
        if (has_candidates) {
            return simple_failf("dynamic call has no matching candidate with %llu args", (unsigned long long)arg_count);
        }
        return simple_failf("dynamic call has no functions with %llu args", (unsigned long long)arg_count);
    }
    return 1;
}

static int simple_dynamic_candidate_allowed(const char* candidates, const char* name)
{
    const char* p;
    if (candidates == NULL || candidates[0] == '\0') {
        return 1;
    }
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    p = candidates;
    while (*p != '\0') {
        const char* start;
        const char* end;
        size_t n;
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == '\r' || *p == '\n') {
            p += 1;
        }
        start = p;
        while (*p != '\0' && *p != ',') {
            p += 1;
        }
        end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
            end -= 1;
        }
        n = (size_t)(end - start);
        if (n == strlen(name) && strncmp(start, name, n) == 0) {
            return 1;
        }
        if (*p == ',') {
            p += 1;
        }
    }
    return 0;
}

static int simple_compile_if_expr_ext(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx)
{
    SimpleNodeView cond_node;
    SimpleNodeView then_node;
    SimpleNodeView else_node;
    size_t jump_false_ip;
    size_t jump_end_ip;
    if (!simple_parse_next_node(node->body_start, node->body_end, &cond_node) ||
        !simple_parse_next_node(cond_node.next, node->body_end, &then_node) ||
        !simple_parse_next_node(then_node.next, node->body_end, &else_node)) {
        return simple_fail("if expr shape invalid");
    }
    if (!simple_compile_expr_ext(&cond_node, program, locals, ctx)) {
        return 0;
    }
    jump_false_ip = program->instruction_count;
    if (!simple_emit_instruction(program, AIVM_OP_JUMP_IF_FALSE, 0)) {
        return simple_fail("failed emitting JUMP_IF_FALSE");
    }
    if (!simple_compile_expr_ext(&then_node, program, locals, ctx)) {
        return 0;
    }
    jump_end_ip = program->instruction_count;
    if (!simple_emit_instruction(program, AIVM_OP_JUMP, 0)) {
        return simple_fail("failed emitting JUMP");
    }
    program->instruction_storage[jump_false_ip].operand_int = (int64_t)program->instruction_count;
    if (!simple_compile_expr_ext(&else_node, program, locals, ctx)) {
        return 0;
    }
    program->instruction_storage[jump_end_ip].operand_int = (int64_t)program->instruction_count;
    return 1;
}

static int simple_compile_expr_ext(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx)
{
    if (node == NULL || program == NULL || locals == NULL || ctx == NULL) {
        return 0;
    }
    if (strcmp(node->kind, "Call") == 0) {
        return simple_compile_call_ext(node, program, locals, ctx, 0);
    }
    if (strcmp(node->kind, "CallDynamic") == 0) {
        return simple_compile_dynamic_call_ext(node, program, locals, ctx);
    }
    if (strcmp(node->kind, "Var") == 0) {
        return simple_compile_var_expr(node, program, locals);
    }
    if (strcmp(node->kind, "StrConcat") == 0) {
        const char* c = node->body_start;
        int first = 1;
        SimpleNodeView child;
        while (simple_parse_next_node(c, node->body_end, &child)) {
            if (!simple_compile_expr_ext(&child, program, locals, ctx)) {
                return 0;
            }
            if (!first && !simple_emit_instruction(program, AIVM_OP_STR_CONCAT, 0)) {
                return simple_fail("failed emitting STR_CONCAT");
            }
            first = 0;
            c = child.next;
        }
        return !first;
    }
    if (strcmp(node->kind, "StrEscape") == 0) {
        SimpleNodeView child;
        if (!simple_parse_next_node(node->body_start, node->body_end, &child)) {
            return simple_fail("StrEscape missing arg");
        }
        if (!simple_compile_expr_ext(&child, program, locals, ctx)) {
            return 0;
        }
        return simple_emit_instruction(program, AIVM_OP_STR_ESCAPE, 0);
    }
    if (starts_with(node->kind, "Field")) {
        char key[256];
        SimpleNodeView value_node;
        size_t key_idx = 0U;
        if (!parse_attr_span(node->attrs, "key", key, sizeof(key))) {
            return simple_fail("field missing key");
        }
        if (!simple_parse_next_node(node->body_start, node->body_end, &value_node)) {
            return simple_fail("field missing value");
        }
        if (!simple_add_string_const(program, key, &key_idx) ||
            !simple_emit_instruction(program, AIVM_OP_CONST, (int64_t)key_idx) ||
            !simple_compile_expr_ext(&value_node, program, locals, ctx) ||
            !simple_emit_instruction(program, AIVM_OP_MAKE_FIELD_STRING, 0)) {
            return 0;
        }
        return 1;
    }
    if (starts_with(node->kind, "Map")) {
        const char* c = node->body_start;
        SimpleNodeView field_node;
        size_t field_count = 0U;
        while (simple_parse_next_node(c, node->body_end, &field_node)) {
            if (strcmp(field_node.kind, "Field") != 0) {
                return simple_failf("map child must be Field, got %s", field_node.kind);
            }
            if (!simple_compile_expr_ext(&field_node, program, locals, ctx)) {
                return 0;
            }
            field_count += 1U;
            c = field_node.next;
        }
        if (!simple_emit_instruction(program, AIVM_OP_PUSH_INT, (int64_t)field_count) ||
            !simple_emit_instruction(program, AIVM_OP_MAKE_MAP, 0)) {
            return 0;
        }
        return 1;
    }
    if (starts_with(node->kind, "MakeBlock")) {
        SimpleNodeView id_expr;
        if (!simple_parse_next_node(node->body_start, node->body_end, &id_expr)) {
            return simple_fail("MakeBlock missing id expression");
        }
        if (!simple_compile_expr_ext(&id_expr, program, locals, ctx) ||
            !simple_emit_instruction(program, AIVM_OP_MAKE_BLOCK, 0)) {
            return 0;
        }
        return 1;
    }
    if (starts_with(node->kind, "MakeNode")) {
        SimpleNodeView kind_expr;
        SimpleNodeView id_expr;
        if (!simple_parse_next_node(node->body_start, node->body_end, &kind_expr) ||
            !simple_parse_next_node(kind_expr.next, node->body_end, &id_expr)) {
            return simple_fail("MakeNode requires (kind,id)");
        }
        if (!simple_compile_expr_ext(&kind_expr, program, locals, ctx) ||
            !simple_compile_expr_ext(&id_expr, program, locals, ctx) ||
            !simple_emit_instruction(program, AIVM_OP_MAKE_NODE_EMPTY, 0)) {
            return 0;
        }
        return 1;
    }
    if (starts_with(node->kind, "AppendChild")) {
        SimpleNodeView base_node;
        SimpleNodeView child_node;
        if (!simple_parse_next_node(node->body_start, node->body_end, &base_node) ||
            !simple_parse_next_node(base_node.next, node->body_end, &child_node)) {
            return simple_fail("AppendChild requires (node,child)");
        }
        if (!simple_compile_expr_ext(&base_node, program, locals, ctx) ||
            !simple_compile_expr_ext(&child_node, program, locals, ctx) ||
            !simple_emit_instruction(program, AIVM_OP_APPEND_CHILD, 0)) {
            return 0;
        }
        return 1;
    }
    if (starts_with(node->kind, "AppendAttr")) {
        SimpleNodeView base_node;
        SimpleNodeView attr_node;
        if (!simple_parse_next_node(node->body_start, node->body_end, &base_node) ||
            !simple_parse_next_node(base_node.next, node->body_end, &attr_node)) {
            return simple_fail("AppendAttr requires (node,attr)");
        }
        if (!simple_compile_expr_ext(&base_node, program, locals, ctx) ||
            !simple_compile_expr_ext(&attr_node, program, locals, ctx) ||
            !simple_emit_instruction(program, AIVM_OP_APPEND_ATTR, 0)) {
            return 0;
        }
        return 1;
    }
    if (starts_with(node->kind, "MakePair")) {
        SimpleNodeView first_node;
        SimpleNodeView second_node;
        if (!simple_parse_next_node(node->body_start, node->body_end, &first_node) ||
            !simple_parse_next_node(first_node.next, node->body_end, &second_node)) {
            return simple_fail("MakePair requires (first,second)");
        }
        if (!simple_compile_expr_ext(&first_node, program, locals, ctx) ||
            !simple_compile_expr_ext(&second_node, program, locals, ctx) ||
            !simple_emit_instruction(program, AIVM_OP_MAKE_PAIR, 0)) {
            return 0;
        }
        return 1;
    }
    if (starts_with(node->kind, "PairFirst") || starts_with(node->kind, "PairSecond")) {
        SimpleNodeView pair_node;
        if (!simple_parse_next_node(node->body_start, node->body_end, &pair_node)) {
            return simple_failf("%s requires pair operand", node->kind);
        }
        if (!simple_compile_expr_ext(&pair_node, program, locals, ctx) ||
            !simple_emit_instruction(
                program,
                starts_with(node->kind, "PairFirst") ? AIVM_OP_PAIR_FIRST : AIVM_OP_PAIR_SECOND,
                0)) {
            return 0;
        }
        return 1;
    }
    if (starts_with(node->kind, "MakeLitString") || starts_with(node->kind, "MakeLitInt") ||
        starts_with(node->kind, "MakeLitBool") ||
        starts_with(node->kind, "MakeFieldString")) {
        SimpleNodeView id_node;
        SimpleNodeView value_node;
        AivmOpcode make_opcode = AIVM_OP_MAKE_LIT_INT;
        if (starts_with(node->kind, "MakeLitString")) {
            make_opcode = AIVM_OP_MAKE_LIT_STRING;
        } else if (starts_with(node->kind, "MakeLitBool")) {
            make_opcode = AIVM_OP_MAKE_LIT_BOOL;
        } else if (starts_with(node->kind, "MakeFieldString")) {
            make_opcode = AIVM_OP_MAKE_FIELD_STRING;
        }
        if (!simple_parse_next_node(node->body_start, node->body_end, &id_node) ||
            !simple_parse_next_node(id_node.next, node->body_end, &value_node)) {
            return simple_failf("%s requires (id,value)", node->kind);
        }
        if (!simple_compile_expr_ext(&id_node, program, locals, ctx) ||
            !simple_compile_expr_ext(&value_node, program, locals, ctx) ||
            !simple_emit_instruction(program, make_opcode, 0)) {
            return 0;
        }
        return 1;
    }
    if (starts_with(node->kind, "MakeErr")) {
        SimpleNodeView id_node;
        SimpleNodeView code_node;
        SimpleNodeView message_node;
        SimpleNodeView node_id_node;
        if (!simple_parse_next_node(node->body_start, node->body_end, &id_node) ||
            !simple_parse_next_node(id_node.next, node->body_end, &code_node) ||
            !simple_parse_next_node(code_node.next, node->body_end, &message_node) ||
            !simple_parse_next_node(message_node.next, node->body_end, &node_id_node)) {
            return simple_fail("MakeErr requires (id,code,message,nodeId)");
        }
        if (!simple_compile_expr_ext(&id_node, program, locals, ctx) ||
            !simple_compile_expr_ext(&code_node, program, locals, ctx) ||
            !simple_compile_expr_ext(&message_node, program, locals, ctx) ||
            !simple_compile_expr_ext(&node_id_node, program, locals, ctx) ||
            !simple_emit_instruction(program, AIVM_OP_MAKE_ERR, 0)) {
            return 0;
        }
        return 1;
    }
    if (strcmp(node->kind, "StringUtf8ByteCount") == 0 ||
        strcmp(node->kind, "StringFromCodePoint") == 0 ||
        strcmp(node->kind, "StringDecodeUnicodeHex4") == 0) {
        SimpleNodeView value;
        if (!simple_parse_next_node(node->body_start, node->body_end, &value)) {
            return simple_failf("%s missing arg", node->kind);
        }
        if (!simple_compile_expr_ext(&value, program, locals, ctx)) {
            return 0;
        }
        if (strcmp(node->kind, "StringUtf8ByteCount") == 0) {
            return simple_emit_instruction(program, AIVM_OP_STR_UTF8_BYTE_COUNT, 0);
        }
        if (strcmp(node->kind, "StringFromCodePoint") == 0) {
            return simple_emit_instruction(program, AIVM_OP_STR_FROM_CODEPOINT, 0);
        }
        return simple_emit_instruction(program, AIVM_OP_STR_DECODE_UNICODE_HEX4, 0);
    }
    if (strcmp(node->kind, "StringDecodeUnicodeSurrogatePairHex4") == 0) {
        SimpleNodeView high;
        SimpleNodeView low;
        if (!simple_parse_next_node(node->body_start, node->body_end, &high) ||
            !simple_parse_next_node(high.next, node->body_end, &low)) {
            return simple_fail("StringDecodeUnicodeSurrogatePairHex4 missing args");
        }
        if (!simple_compile_expr_ext(&high, program, locals, ctx) ||
            !simple_compile_expr_ext(&low, program, locals, ctx)) {
            return 0;
        }
        return simple_emit_instruction(program, AIVM_OP_STR_DECODE_UNICODE_SURROGATE_PAIR_HEX4, 0);
    }
    if (strcmp(node->kind, "BytesLength") == 0) {
        SimpleNodeView data;
        if (!simple_parse_next_node(node->body_start, node->body_end, &data)) {
            return simple_fail("BytesLength missing arg");
        }
        if (!simple_compile_expr_ext(&data, program, locals, ctx)) {
            return 0;
        }
        return simple_emit_instruction(program, AIVM_OP_BYTES_LENGTH, 0);
    }
    if (strcmp(node->kind, "BytesFromUtf8String") == 0 ||
        strcmp(node->kind, "BytesToUtf8String") == 0 ||
        strcmp(node->kind, "BytesFromBase64") == 0 ||
        strcmp(node->kind, "BytesToBase64") == 0) {
        SimpleNodeView value;
        if (!simple_parse_next_node(node->body_start, node->body_end, &value)) {
            return simple_failf("%s missing arg", node->kind);
        }
        if (!simple_compile_expr_ext(&value, program, locals, ctx)) {
            return 0;
        }
        if (strcmp(node->kind, "BytesFromUtf8String") == 0) {
            return simple_emit_instruction(program, AIVM_OP_BYTES_FROM_UTF8_STRING, 0);
        }
        if (strcmp(node->kind, "BytesToUtf8String") == 0) {
            return simple_emit_instruction(program, AIVM_OP_BYTES_TO_UTF8_STRING, 0);
        }
        if (strcmp(node->kind, "BytesFromBase64") == 0) {
            return simple_emit_instruction(program, AIVM_OP_BYTES_FROM_BASE64, 0);
        }
        return simple_emit_instruction(program, AIVM_OP_BYTES_TO_BASE64, 0);
    }
    if (strcmp(node->kind, "BytesAt") == 0 || strcmp(node->kind, "BytesConcat") == 0) {
        SimpleNodeView left;
        SimpleNodeView right;
        if (!simple_parse_next_node(node->body_start, node->body_end, &left) ||
            !simple_parse_next_node(left.next, node->body_end, &right)) {
            return simple_failf("%s missing args", node->kind);
        }
        if (!simple_compile_expr_ext(&left, program, locals, ctx) ||
            !simple_compile_expr_ext(&right, program, locals, ctx)) {
            return 0;
        }
        if (strcmp(node->kind, "BytesAt") == 0) {
            return simple_emit_instruction(program, AIVM_OP_BYTES_AT, 0);
        }
        return simple_emit_instruction(program, AIVM_OP_BYTES_CONCAT, 0);
    }
    if (strcmp(node->kind, "BytesSlice") == 0) {
        SimpleNodeView data;
        SimpleNodeView start;
        SimpleNodeView length;
        if (!simple_parse_next_node(node->body_start, node->body_end, &data) ||
            !simple_parse_next_node(data.next, node->body_end, &start) ||
            !simple_parse_next_node(start.next, node->body_end, &length)) {
            return simple_fail("BytesSlice missing args");
        }
        if (!simple_compile_expr_ext(&data, program, locals, ctx) ||
            !simple_compile_expr_ext(&start, program, locals, ctx) ||
            !simple_compile_expr_ext(&length, program, locals, ctx)) {
            return 0;
        }
        return simple_emit_instruction(program, AIVM_OP_BYTES_SLICE, 0);
    }
    if (strcmp(node->kind, "StringSlice") == 0 || strcmp(node->kind, "StringRemove") == 0 ||
        strcmp(node->kind, "StringFind") == 0) {
        SimpleNodeView text;
        SimpleNodeView start;
        SimpleNodeView third;
        if (!simple_parse_next_node(node->body_start, node->body_end, &text) ||
            !simple_parse_next_node(text.next, node->body_end, &start) ||
            !simple_parse_next_node(start.next, node->body_end, &third)) {
            return simple_failf("%s missing args", node->kind);
        }
        if (!simple_compile_expr_ext(&text, program, locals, ctx) ||
            !simple_compile_expr_ext(&start, program, locals, ctx) ||
            !simple_compile_expr_ext(&third, program, locals, ctx)) {
            return 0;
        }
        if (strcmp(node->kind, "StringSlice") == 0) {
            return simple_emit_instruction(program, AIVM_OP_STR_SUBSTRING, 0);
        }
        if (strcmp(node->kind, "StringFind") == 0) {
            return simple_emit_instruction(program, AIVM_OP_STR_FIND, 0);
        }
        return simple_emit_instruction(program, AIVM_OP_STR_REMOVE, 0);
    }
    if (strcmp(node->kind, "Add") == 0 || strcmp(node->kind, "Sub") == 0 ||
        strcmp(node->kind, "Mul") == 0 || strcmp(node->kind, "Div") == 0 ||
        strcmp(node->kind, "Mod") == 0 || strcmp(node->kind, "Pow") == 0 ||
        strcmp(node->kind, "Lt") == 0 ||
        strcmp(node->kind, "Eq") == 0 || strcmp(node->kind, "ToString") == 0 ||
        strcmp(node->kind, "AttrCount") == 0 || strcmp(node->kind, "AttrKey") == 0 ||
        strcmp(node->kind, "AttrValueString") == 0 || strcmp(node->kind, "AttrValueInt") == 0 ||
        strcmp(node->kind, "AttrValueBool") == 0 || strcmp(node->kind, "AttrValueKind") == 0 ||
        strcmp(node->kind, "ChildCount") == 0 || strcmp(node->kind, "ChildAt") == 0 ||
        strcmp(node->kind, "NodeKind") == 0 || strcmp(node->kind, "NodeId") == 0) {
        const char* c = node->body_start;
        SimpleNodeView first;
        SimpleNodeView second;
        if (strcmp(node->kind, "ToString") == 0 ||
            strcmp(node->kind, "AttrCount") == 0 ||
            strcmp(node->kind, "ChildCount") == 0 ||
            strcmp(node->kind, "NodeKind") == 0 ||
            strcmp(node->kind, "NodeId") == 0) {
            if (!simple_parse_next_node(c, node->body_end, &first)) {
                return simple_failf("%s missing arg", node->kind);
            }
            if (!simple_compile_expr_ext(&first, program, locals, ctx)) {
                return 0;
            }
            if (strcmp(node->kind, "ToString") == 0) return simple_emit_instruction(program, AIVM_OP_TO_STRING, 0);
            if (strcmp(node->kind, "AttrCount") == 0) return simple_emit_instruction(program, AIVM_OP_ATTR_COUNT, 0);
            if (strcmp(node->kind, "ChildCount") == 0) return simple_emit_instruction(program, AIVM_OP_CHILD_COUNT, 0);
            if (strcmp(node->kind, "NodeKind") == 0) return simple_emit_instruction(program, AIVM_OP_NODE_KIND, 0);
            return simple_emit_instruction(program, AIVM_OP_NODE_ID, 0);
        }
        if (!simple_parse_next_node(c, node->body_end, &first) ||
            !simple_parse_next_node(first.next, node->body_end, &second)) {
            return simple_failf("%s missing args", node->kind);
        }
        if (!simple_compile_expr_ext(&first, program, locals, ctx) ||
            !simple_compile_expr_ext(&second, program, locals, ctx)) {
            return 0;
        }
        if (strcmp(node->kind, "Add") == 0) return simple_emit_instruction(program, AIVM_OP_ADD_INT, 0);
        if (strcmp(node->kind, "Sub") == 0) return simple_emit_instruction(program, AIVM_OP_SUB_NUM, 0);
        if (strcmp(node->kind, "Mul") == 0) return simple_emit_instruction(program, AIVM_OP_MUL_NUM, 0);
        if (strcmp(node->kind, "Div") == 0) return simple_emit_instruction(program, AIVM_OP_DIV_NUM, 0);
        if (strcmp(node->kind, "Mod") == 0) return simple_emit_instruction(program, AIVM_OP_MOD_NUM, 0);
        if (strcmp(node->kind, "Pow") == 0) return simple_emit_instruction(program, AIVM_OP_POW_NUM, 0);
        if (strcmp(node->kind, "Lt") == 0) return simple_emit_instruction(program, AIVM_OP_LT_NUM, 0);
        if (strcmp(node->kind, "Eq") == 0) return simple_emit_instruction(program, AIVM_OP_EQ, 0);
        if (strcmp(node->kind, "AttrKey") == 0) return simple_emit_instruction(program, AIVM_OP_ATTR_KEY, 0);
        if (strcmp(node->kind, "AttrValueString") == 0) return simple_emit_instruction(program, AIVM_OP_ATTR_VALUE_STRING, 0);
        if (strcmp(node->kind, "AttrValueInt") == 0) return simple_emit_instruction(program, AIVM_OP_ATTR_VALUE_INT, 0);
        if (strcmp(node->kind, "AttrValueBool") == 0) return simple_emit_instruction(program, AIVM_OP_ATTR_VALUE_BOOL, 0);
        if (strcmp(node->kind, "AttrValueKind") == 0) return simple_emit_instruction(program, AIVM_OP_ATTR_VALUE_KIND, 0);
        return simple_emit_instruction(program, AIVM_OP_CHILD_AT, 0);
    }
    if (strcmp(node->kind, "If") == 0) {
        return simple_compile_if_expr_ext(node, program, locals, ctx);
    }
    if (strcmp(node->kind, "Block") == 0) {
        const char* c = node->body_start;
        SimpleNodeView child;
        int emitted_value = 0;
        while (simple_parse_next_node(c, node->body_end, &child)) {
            SimpleNodeView next_child;
            int has_next = simple_parse_next_node(child.next, node->body_end, &next_child);
            if (has_next) {
                int did_return = 0;
                if (!simple_compile_stmt_ext(&child, program, locals, ctx, &did_return)) {
                    return 0;
                }
                if (did_return) {
                    return 1;
                }
                c = child.next;
                continue;
            }
            if (strcmp(child.kind, "Let") == 0 ||
                strcmp(child.kind, "Return") == 0 ||
                strcmp(child.kind, "Break") == 0 ||
                strcmp(child.kind, "Continue") == 0 ||
                strcmp(child.kind, "Loop") == 0) {
                int did_return = 0;
                if (!simple_compile_stmt_ext(&child, program, locals, ctx, &did_return)) {
                    return 0;
                }
                if (did_return) {
                    return 1;
                }
                if (!simple_emit_empty_string_const(program)) {
                    return 0;
                }
            } else if (!simple_compile_expr_ext(&child, program, locals, ctx)) {
                return 0;
            }
            emitted_value = 1;
            c = child.next;
        }
        if (!emitted_value) {
            if (!simple_emit_empty_string_const(program)) {
                return 0;
            }
        }
        return 1;
    }
    {
        const char* trace = getenv("AIVM_NATIVE_BUILD_TRACE");
        if (trace != NULL && trace[0] != '\0') {
            fprintf(stderr, "[ailang-native-compile] fallback-expr kind=%s\n", node->kind);
        }
    }
    return simple_compile_expr_node(node, program, locals->names, &locals->count);
}

static int simple_compile_if_stmt_ext(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx,
    int* out_did_return)
{
    SimpleNodeView cond_node;
    SimpleNodeView then_node;
    SimpleNodeView else_node;
    size_t jump_false_ip;
    size_t jump_end_ip;
    int then_return = 0;
    int else_return = 0;
    if (!simple_parse_next_node(node->body_start, node->body_end, &cond_node) ||
        !simple_parse_next_node(cond_node.next, node->body_end, &then_node) ||
        !simple_parse_next_node(then_node.next, node->body_end, &else_node)) {
        return simple_fail("if stmt shape invalid");
    }
    if (!simple_compile_expr_ext(&cond_node, program, locals, ctx)) {
        return 0;
    }
    jump_false_ip = program->instruction_count;
    if (!simple_emit_instruction(program, AIVM_OP_JUMP_IF_FALSE, 0)) {
        return 0;
    }
    if (strcmp(then_node.kind, "Block") == 0) {
        if (!simple_compile_block_ext(&then_node, program, locals, ctx, &then_return)) {
            return 0;
        }
    } else if (!simple_compile_expr_ext(&then_node, program, locals, ctx) ||
               !simple_emit_instruction(program, AIVM_OP_POP, 0)) {
        return 0;
    }
    jump_end_ip = program->instruction_count;
    if (!simple_emit_instruction(program, AIVM_OP_JUMP, 0)) {
        return 0;
    }
    program->instruction_storage[jump_false_ip].operand_int = (int64_t)program->instruction_count;
    if (strcmp(else_node.kind, "Block") == 0) {
        if (!simple_compile_block_ext(&else_node, program, locals, ctx, &else_return)) {
            return 0;
        }
    } else if (!simple_compile_expr_ext(&else_node, program, locals, ctx) ||
               !simple_emit_instruction(program, AIVM_OP_POP, 0)) {
        return 0;
    }
    program->instruction_storage[jump_end_ip].operand_int = (int64_t)program->instruction_count;
    if (out_did_return != NULL) {
        *out_did_return = then_return && else_return;
    }
    return 1;
}

static int simple_compile_loop_stmt_ext(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx,
    int* out_did_return)
{
    SimpleNodeView body_node;
    SimpleLoopFrame* loop_frame;
    size_t loop_start_ip;
    size_t i;
    int body_return = 0;
    if (out_did_return != NULL) {
        *out_did_return = 0;
    }
    if (!simple_parse_next_node(node->body_start, node->body_end, &body_node)) {
        return simple_fail("loop stmt shape invalid");
    }
    if (ctx->loop_depth >= SIMPLE_MAX_LOOP_DEPTH) {
        return simple_fail("loop nesting depth exceeded");
    }

    loop_start_ip = program->instruction_count;
    loop_frame = &ctx->loop_frames[ctx->loop_depth];
    memset(loop_frame, 0, sizeof(*loop_frame));
    loop_frame->start_ip = loop_start_ip;
    ctx->loop_depth += 1U;
    if (strcmp(body_node.kind, "Block") == 0) {
        if (!simple_compile_block_ext(&body_node, program, locals, ctx, &body_return)) {
            ctx->loop_depth -= 1U;
            return 0;
        }
    } else if (!simple_compile_expr_ext(&body_node, program, locals, ctx) ||
               !simple_emit_instruction(program, AIVM_OP_POP, 0)) {
        ctx->loop_depth -= 1U;
        return 0;
    }
    if (!simple_emit_instruction(program, AIVM_OP_JUMP, (int64_t)loop_start_ip)) {
        ctx->loop_depth -= 1U;
        return 0;
    }
    for (i = 0U; i < loop_frame->break_count; i += 1U) {
        program->instruction_storage[loop_frame->break_sites[i]].operand_int = (int64_t)program->instruction_count;
    }
    for (i = 0U; i < loop_frame->continue_count; i += 1U) {
        program->instruction_storage[loop_frame->continue_sites[i]].operand_int = (int64_t)loop_start_ip;
    }
    ctx->loop_depth -= 1U;
    if (out_did_return != NULL) {
        (void)body_return;
        *out_did_return = 0;
    }
    return 1;
}

static int simple_compile_stmt_ext(
    const SimpleNodeView* node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx,
    int* out_did_return)
{
    if (out_did_return != NULL) {
        *out_did_return = 0;
    }
    if (node == NULL || program == NULL || locals == NULL || ctx == NULL) {
        return 0;
    }
    if (strcmp(node->kind, "Let") == 0) {
        char name[64];
        size_t slot;
        SimpleNodeView expr;
        if (!parse_attr_span(node->attrs, "name", name, sizeof(name))) {
            return simple_fail("let stmt missing name");
        }
        if (!simple_locals_lookup(locals, name, &slot, 1)) {
            return simple_failf("local allocation failed for %s", name);
        }
        if (!simple_parse_next_node(node->body_start, node->body_end, &expr)) {
            return simple_fail("let stmt missing expression");
        }
        if (!simple_compile_expr_ext(&expr, program, locals, ctx)) {
            return 0;
        }
        if (!simple_emit_instruction(program, AIVM_OP_STORE_LOCAL, (int64_t)slot)) {
            return simple_fail("let stmt STORE_LOCAL failed");
        }
        return 1;
    }
    if (strcmp(node->kind, "Call") == 0) {
        return simple_compile_call_ext(node, program, locals, ctx, 1);
    }
    if (strcmp(node->kind, "Break") == 0) {
        SimpleLoopFrame* loop_frame;
        size_t jump_ip;
        if (ctx->loop_depth == 0U) {
            return simple_fail("break used outside loop");
        }
        loop_frame = &ctx->loop_frames[ctx->loop_depth - 1U];
        if (loop_frame->break_count >= SIMPLE_MAX_LOOP_FIXUPS) {
            return simple_fail("too many break statements in loop");
        }
        jump_ip = program->instruction_count;
        if (!simple_emit_instruction(program, AIVM_OP_JUMP, 0)) {
            return 0;
        }
        loop_frame->break_sites[loop_frame->break_count] = jump_ip;
        loop_frame->break_count += 1U;
        return 1;
    }
    if (strcmp(node->kind, "Continue") == 0) {
        SimpleLoopFrame* loop_frame;
        size_t jump_ip;
        if (ctx->loop_depth == 0U) {
            return simple_fail("continue used outside loop");
        }
        loop_frame = &ctx->loop_frames[ctx->loop_depth - 1U];
        if (loop_frame->continue_count >= SIMPLE_MAX_LOOP_FIXUPS) {
            return simple_fail("too many continue statements in loop");
        }
        jump_ip = program->instruction_count;
        if (!simple_emit_instruction(program, AIVM_OP_JUMP, 0)) {
            return 0;
        }
        loop_frame->continue_sites[loop_frame->continue_count] = jump_ip;
        loop_frame->continue_count += 1U;
        return 1;
    }
    if (strcmp(node->kind, "Return") == 0) {
        SimpleNodeView expr;
        if (!simple_parse_next_node(node->body_start, node->body_end, &expr)) {
            if (!simple_emit_void_const(program)) {
                return simple_fail("empty return emit failed");
            }
        } else if (!simple_compile_expr_ext(&expr, program, locals, ctx)) {
            return 0;
        }
        if (!simple_emit_instruction(program, AIVM_OP_RETURN, 0)) {
            return simple_fail("return emit failed");
        }
        if (out_did_return != NULL) {
            *out_did_return = 1;
        }
        return 1;
    }
    if (strcmp(node->kind, "If") == 0) {
        return simple_compile_if_stmt_ext(node, program, locals, ctx, out_did_return);
    }
    if (strcmp(node->kind, "Loop") == 0) {
        return simple_compile_loop_stmt_ext(node, program, locals, ctx, out_did_return);
    }
    if (strcmp(node->kind, "Block") == 0) {
        return simple_compile_block_ext(node, program, locals, ctx, out_did_return);
    }
    if (!simple_compile_expr_ext(node, program, locals, ctx)) {
        return 0;
    }
    return simple_emit_instruction(program, AIVM_OP_POP, 0);
}

static int simple_compile_block_ext(
    const SimpleNodeView* block_node,
    AivmProgram* program,
    SimpleLocals* locals,
    SimpleCompileContext* ctx,
    int* out_did_return)
{
    const char* c;
    size_t scope_local_count = 0U;
    int did_return = 0;
    if (out_did_return != NULL) {
        *out_did_return = 0;
    }
    if (block_node == NULL || program == NULL || locals == NULL || ctx == NULL) {
        return 0;
    }
    scope_local_count = locals->count;
    c = block_node->body_start;
    while (simple_parse_next_node(c, block_node->body_end, &(SimpleNodeView){0})) {
        SimpleNodeView child;
        if (!simple_parse_next_node(c, block_node->body_end, &child)) {
            break;
        }
        if (!simple_compile_stmt_ext(&child, program, locals, ctx, &did_return)) {
            locals->count = scope_local_count;
            return 0;
        }
        if (did_return) {
            locals->count = scope_local_count;
            if (out_did_return != NULL) {
                *out_did_return = 1;
            }
            return 1;
        }
        c = child.next;
    }
    locals->count = scope_local_count;
    return 1;
}

static int simple_compile_fn_by_index(SimpleCompileContext* ctx, size_t fn_index)
{
    SimpleFnDef* fn;
    SimpleLocals locals;
    char params[SIMPLE_MAX_LOCALS][64];
    size_t param_count = 0U;
    size_t i;
    SimpleNodeView block_node;
    int did_return = 0;
    const char* trace;
    size_t before_count = 0U;
    if (ctx == NULL || fn_index >= ctx->func_count || ctx->program == NULL) {
        return 0;
    }
    fn = &ctx->funcs[fn_index];
    trace = getenv("AIVM_NATIVE_BUILD_TRACE");
    if (fn->compiled) {
        return 1;
    }
    if (fn->compiling) {
        return 1;
    }
    fn->compiling = 1;
    before_count = ctx->program->instruction_count;
    fn->entry_ip = ctx->program->instruction_count;
    if (trace != NULL && trace[0] != '\0') {
        fprintf(
            stderr,
            "[ailang-native-compile] fn-start=%s inst_before=%llu\n",
            fn->name,
            (unsigned long long)before_count);
    }

    memset(&locals, 0, sizeof(locals));
    locals.ctx = ctx;
    if (!simple_split_params(fn->params_raw, params, &param_count)) {
        return simple_failf("invalid params for function %s", fn->name);
    }
    for (i = 0U; i < param_count; i += 1U) {
        size_t slot;
        if (!simple_locals_lookup(&locals, params[i], &slot, 1)) {
            return simple_failf("failed to allocate param %s", params[i]);
        }
    }
    for (i = param_count; i > 0U; i -= 1U) {
        size_t slot = 0U;
        if (!simple_locals_lookup(&locals, params[i - 1U], &slot, 0) ||
            !simple_emit_instruction(ctx->program, AIVM_OP_STORE_LOCAL, (int64_t)slot)) {
            return simple_failf("failed param prologue for %s", fn->name);
        }
    }
    if (!simple_parse_next_node(fn->body_start, fn->body_end, &block_node) ||
        strcmp(block_node.kind, "Block") != 0) {
        return simple_failf("function %s missing Block body", fn->name);
    }
    if (!simple_compile_block_ext(&block_node, ctx->program, &locals, ctx, &did_return)) {
        return 0;
    }
    if (!did_return) {
        if (!simple_emit_void_const(ctx->program)) {
            return simple_failf("failed implicit void for %s", fn->name);
        }
        if (!simple_emit_instruction(ctx->program, AIVM_OP_RETURN, 0)) {
            return simple_failf("failed implicit return for %s", fn->name);
        }
    }
    fn->compiled = 1;
    fn->compiling = 0;
    if (trace != NULL && trace[0] != '\0') {
        fprintf(
            stderr,
            "[ailang-native-compile] fn=%s inst_before=%llu inst_after=%llu\n",
            fn->name,
            (unsigned long long)before_count,
            (unsigned long long)ctx->program->instruction_count);
    }
    return 1;
}

static int parse_simple_program_graph_to_program_file(const char* aos_path, AivmProgram* out_program)
{
    SimpleCompileContext ctx;
    size_t entry_index;
    size_t bootstrap_call_ip;
    size_t i;
    (void)snprintf(g_simple_last_error, sizeof(g_simple_last_error), "%s", "graph compile failed (unknown)");
    if (aos_path == NULL || out_program == NULL) {
        return simple_fail("graph compile: invalid args");
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.program = out_program;
    ctx.next_local_slot = 0U;

    if (!simple_collect_from_file(&ctx, aos_path, 1)) {
        return 0;
    }
    if (ctx.entry_export[0] == '\0') {
        (void)snprintf(ctx.entry_export, sizeof(ctx.entry_export), "%s", "start");
    }
    if (!simple_find_func(&ctx, ctx.entry_export, &entry_index)) {
        return simple_failf("entry export '%s' not found", ctx.entry_export);
    }

    aivm_program_clear(out_program);
    out_program->instructions = out_program->instruction_storage;
    out_program->constants = out_program->constant_storage;
    out_program->format_version = 2U;
    out_program->format_flags = 0U;

    {
        char params[SIMPLE_MAX_LOCALS][64];
        size_t param_count = 0U;
        if (!simple_split_params(ctx.funcs[entry_index].params_raw, params, &param_count)) {
            return simple_failf("graph compile: invalid entry params for %s", ctx.funcs[entry_index].name);
        }
        if (param_count > 0U) {
            size_t argv_target = 0U;
            if (!simple_add_string_const(out_program, "sys.process.args", &argv_target) ||
                !simple_emit_instruction(out_program, AIVM_OP_CONST, (int64_t)argv_target) ||
                !simple_emit_instruction(out_program, AIVM_OP_CALL_SYS, 0)) {
                return simple_fail("graph compile: failed emitting argv bootstrap");
            }
        }
    }
    bootstrap_call_ip = out_program->instruction_count;
    if (!simple_emit_instruction(out_program, AIVM_OP_CALL, 0) ||
        !simple_emit_instruction(out_program, AIVM_OP_HALT, 0)) {
        return simple_fail("graph compile: failed emitting bootstrap call/halt");
    }

    if (!simple_compile_fn_by_index(&ctx, entry_index)) {
        return 0;
    }
    out_program->instruction_storage[bootstrap_call_ip].operand_int = (int64_t)ctx.funcs[entry_index].entry_ip;

    for (i = 0U; i < ctx.fixup_count; i += 1U) {
        size_t target_index;
        if (!simple_find_func(&ctx, ctx.fixups[i].target, &target_index)) {
            return simple_failf("unresolved call target %s", ctx.fixups[i].target);
        }
        if (!ctx.funcs[target_index].compiled) {
            if (!simple_compile_fn_by_index(&ctx, target_index)) {
                return 0;
            }
        }
        if (ctx.fixups[i].instruction_index >= out_program->instruction_count) {
            return simple_fail("call fixup index out of range");
        }
        out_program->instruction_storage[ctx.fixups[i].instruction_index].operand_int =
            (int64_t)ctx.funcs[target_index].entry_ip;
    }

    for (i = 0U; i < ctx.source_count; i += 1U) {
        free(ctx.sources[i].text);
        ctx.sources[i].text = NULL;
    }
    if (out_program->instruction_count == 0U) {
        return simple_fail("graph compile: produced empty program");
    }
    return 1;
}

static int run_native_simple_program_aos(
    const char* aos_path,
    const char* const* process_argv,
    size_t process_argv_count,
    const NativeDebugOptions* debug_options)
{
    AivmProgram program;

    if (!parse_simple_program_aos_to_program_file(aos_path, &program)) {
        return -1;
    }
    return run_native_compiled_program(
        &program,
        "Native simple source execution failed.",
        process_argv,
        process_argv_count,
        debug_options);
}

static int run_native_bytecode_aos(
    const char* aos_path,
    const char* const* process_argv,
    size_t process_argv_count,
    const NativeDebugOptions* debug_options)
{
    AivmProgram program;

    if (!parse_bytecode_aos_to_program_file(aos_path, &program, 0)) {
        return -1;
    }
    return run_native_compiled_program(
        &program,
        "Native bytecode program execution failed.",
        process_argv,
        process_argv_count,
        debug_options);
}

static int run_native_bundle(
    const char* bundle_path,
    const char* const* process_argv,
    size_t process_argv_count,
    const NativeDebugOptions* debug_options)
{
    return run_native_bytecode_aos(bundle_path, process_argv, process_argv_count, debug_options);
}

static void write_u32_le(FILE* f, uint32_t value)
{
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xffU);
    (void)fwrite(bytes, 1U, 4U, f);
}

static void write_i64_le(FILE* f, int64_t value)
{
    uint64_t u = (uint64_t)value;
    uint8_t bytes[8];
    bytes[0] = (uint8_t)(u & 0xffU);
    bytes[1] = (uint8_t)((u >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((u >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((u >> 24U) & 0xffU);
    bytes[4] = (uint8_t)((u >> 32U) & 0xffU);
    bytes[5] = (uint8_t)((u >> 40U) & 0xffU);
    bytes[6] = (uint8_t)((u >> 48U) & 0xffU);
    bytes[7] = (uint8_t)((u >> 56U) & 0xffU);
    (void)fwrite(bytes, 1U, 8U, f);
}

static void write_f64_le(FILE* f, double value)
{
    uint64_t u = 0U;
    memcpy(&u, &value, sizeof(u));
    write_i64_le(f, (int64_t)u);
}

static int write_program_as_aibc1(const AivmProgram* program, const char* out_path)
{
    FILE* f;
    uint32_t section_count = 1U;
    uint32_t inst_payload_size;
    uint32_t const_payload_size = 4U;
    size_t i;

    if (program == NULL || out_path == NULL || program->instruction_count == 0U) {
        return 0;
    }

    for (i = 0U; i < program->constant_count; i += 1U) {
        AivmValue v = program->constant_storage[i];
        if (v.type == AIVM_VAL_INT) {
            const_payload_size += 1U + 8U;
        } else if (v.type == AIVM_VAL_NUMBER) {
            const_payload_size += 1U + 8U;
        } else if (v.type == AIVM_VAL_BOOL) {
            const_payload_size += 1U + 1U;
        } else if (v.type == AIVM_VAL_NULL) {
            const_payload_size += 1U;
        } else if (v.type == AIVM_VAL_STRING) {
            size_t len = (v.string_value == NULL) ? 0U : strlen(v.string_value);
            if (len > 0xffffffffU) {
                return 0;
            }
            const_payload_size += 1U + 4U + (uint32_t)len;
        } else if (v.type == AIVM_VAL_BYTES) {
            if (v.bytes_value.length > 0xffffffffU) {
                return 0;
            }
            const_payload_size += 1U + 4U + (uint32_t)v.bytes_value.length;
        } else {
            const_payload_size += 1U;
        }
    }
    if (program->constant_count > 0U) {
        section_count = 2U;
    }

    if (program->instruction_count > (size_t)((0xffffffffU - 4U) / 12U)) {
        return 0;
    }
    inst_payload_size = 4U + (uint32_t)(program->instruction_count * 12U);

    f = fopen(out_path, "wb");
    if (f == NULL) {
        return 0;
    }

    (void)fwrite("AIBC", 1U, 4U, f);
    write_u32_le(f, 2U);
    write_u32_le(f, 0U);
    write_u32_le(f, section_count);

    write_u32_le(f, AIVM_PROGRAM_SECTION_INSTRUCTIONS);
    write_u32_le(f, inst_payload_size);
    write_u32_le(f, (uint32_t)program->instruction_count);
    for (i = 0U; i < program->instruction_count; i += 1U) {
        write_u32_le(f, (uint32_t)program->instruction_storage[i].opcode);
        write_i64_le(f, program->instruction_storage[i].operand_int);
    }

    if (section_count == 2U) {
        write_u32_le(f, AIVM_PROGRAM_SECTION_CONSTANTS);
        write_u32_le(f, const_payload_size);
        write_u32_le(f, (uint32_t)program->constant_count);
        for (i = 0U; i < program->constant_count; i += 1U) {
            AivmValue v = program->constant_storage[i];
            if (v.type == AIVM_VAL_INT) {
                (void)fputc(1, f);
                write_i64_le(f, v.int_value);
            } else if (v.type == AIVM_VAL_NUMBER) {
                (void)fputc(7, f);
                write_f64_le(f, v.number_value);
            } else if (v.type == AIVM_VAL_BOOL) {
                (void)fputc(2, f);
                (void)fputc(v.bool_value ? 1 : 0, f);
            } else if (v.type == AIVM_VAL_NULL) {
                (void)fputc(6, f);
            } else if (v.type == AIVM_VAL_STRING) {
                uint32_t len = (uint32_t)((v.string_value == NULL) ? 0U : strlen(v.string_value));
                (void)fputc(3, f);
                write_u32_le(f, len);
                if (len > 0U) {
                    (void)fwrite(v.string_value, 1U, len, f);
                }
            } else if (v.type == AIVM_VAL_BYTES) {
                uint32_t len = (uint32_t)v.bytes_value.length;
                (void)fputc(5, f);
                write_u32_le(f, len);
                if (len > 0U && v.bytes_value.data != NULL) {
                    (void)fwrite(v.bytes_value.data, 1U, len, f);
                }
            } else {
                (void)fputc(4, f);
            }
        }
    }

    if (fclose(f) != 0) {
        return 0;
    }
    return 1;
}

typedef struct {
    const char* program_path;
    int app_arg_start;
    int app_arg_count;
    int use_cache;
    const char* log_level;
    const char* target;
    const char* runner;
    const char* artifact_type;
    const char* target_version;
    const char* ui_platform;
    const char* wasm_profile;
    const char* out_dir;
    int port;
    int port_explicit;
    int open_browser;
    int target_option_count;
    char* target_options[AILANG_CLI_MAX_TARGET_OPTIONS];
} RunTarget;

static int package_target_option_takes_value(const char* arg);
static int package_target_option_is_inline(const char* arg);

static int derive_build_out_dir(const char* program_input, char* out_dir, size_t out_dir_len);
static int build_input_to_aibc1(
    const char* program_input,
    const char* out_dir,
    char* out_app_path,
    size_t out_app_path_len,
    int use_cache);

static int resolve_project_run_tool(const char* input, char* out_tool, size_t out_tool_len)
{
    char manifest_path[PATH_MAX];
    char manifest_text[8192];
    if (input == NULL || out_tool == NULL || out_tool_len == 0U) {
        return 0;
    }
    out_tool[0] = '\0';
    if (!resolve_manifest_path_for_input(input, manifest_path, sizeof(manifest_path)) ||
        !read_text_file(manifest_path, manifest_text, sizeof(manifest_text)) ||
        !extract_manifest_attr(manifest_text, "runTool", out_tool, out_tool_len)) {
        return 0;
    }
    return out_tool[0] != '\0';
}

static int ui_platform_is_valid(const char* platform)
{
    return platform == NULL ||
           strcmp(platform, "console") == 0 ||
           strcmp(platform, "desktop") == 0 ||
           strcmp(platform, "web") == 0 ||
           strcmp(platform, "server") == 0 ||
           strcmp(platform, "tablet") == 0 ||
           strcmp(platform, "phone") == 0 ||
           strcmp(platform, "watch") == 0;
}

static int run_target_is_wasm(const char* target)
{
    return target != NULL && strcmp(target, "wasm32") == 0;
}

static int run_target_is_native_host(const char* target)
{
    const char* host = host_rid();
    if (target == NULL || target[0] == '\0') {
        return 1;
    }
    if (strcmp(target, host) == 0) {
        return 1;
    }
#if defined(__APPLE__)
    return strcmp(target, "macos") == 0 ||
           strcmp(target, "macOS") == 0 ||
           strcmp(target, "osx") == 0;
#elif defined(_WIN32)
    return strcmp(target, "windows") == 0;
#else
    return strcmp(target, "linux") == 0;
#endif
}

typedef struct {
    char package_name[128];
    char id[128];
    char default_runner[128];
    char artifact_types[512];
    char options[512];
    char run_tools[512];
    char publish_tools[512];
} PackageTargetLockRecord;

static const char* skip_toml_ws(const char* p)
{
    while (p != NULL && (*p == ' ' || *p == '\t')) {
        p += 1;
    }
    return p;
}

static int toml_extract_quoted_value(const char* value, char* out, size_t out_len)
{
    const char* start;
    const char* end;
    size_t n;
    if (value == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    value = skip_toml_ws(value);
    if (value == NULL || *value != '"') {
        return 0;
    }
    start = value + 1;
    end = start;
    while (*end != '\0' && *end != '"' && *end != '\n' && *end != '\r') {
        end += 1;
    }
    if (*end != '"') {
        return 0;
    }
    n = (size_t)(end - start);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return 1;
}

static int toml_extract_raw_value(const char* value, char* out, size_t out_len)
{
    const char* end;
    size_t n;
    if (value == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    value = skip_toml_ws(value);
    if (value == NULL) {
        return 0;
    }
    end = value;
    while (*end != '\0' && *end != '\n' && *end != '\r' && *end != '#') {
        end += 1;
    }
    while (end > value && (end[-1] == ' ' || end[-1] == '\t')) {
        end -= 1;
    }
    n = (size_t)(end - value);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out, value, n);
    out[n] = '\0';
    return n > 0U;
}

static int toml_section_get_value(
    const char* section_start,
    const char* section_end,
    const char* key,
    int quoted,
    char* out,
    size_t out_len)
{
    const char* line;
    size_t key_len;
    if (section_start == NULL || section_end == NULL || key == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    out[0] = '\0';
    key_len = strlen(key);
    line = section_start;
    while (line < section_end && *line != '\0') {
        const char* p = skip_toml_ws(line);
        const char* value;
        const char* newline = strchr(line, '\n');
        if (newline == NULL || newline > section_end) {
            newline = section_end;
        }
        if (p != NULL &&
            p + key_len < newline &&
            memcmp(p, key, key_len) == 0) {
            p += key_len;
            p = skip_toml_ws(p);
            if (p != NULL && p < newline && *p == '=') {
                value = p + 1;
                return quoted
                    ? toml_extract_quoted_value(value, out, out_len)
                    : toml_extract_raw_value(value, out, out_len);
            }
        }
        if (newline >= section_end || *newline == '\0') {
            break;
        }
        line = newline + 1;
    }
    return 0;
}

static int toml_array_contains_quoted(const char* raw_array, const char* value)
{
    const char* p;
    size_t value_len;
    if (raw_array == NULL || value == NULL || value[0] == '\0') {
        return 0;
    }
    value_len = strlen(value);
    p = raw_array;
    while ((p = strchr(p, '"')) != NULL) {
        const char* start = p + 1;
        const char* end = strchr(start, '"');
        if (end == NULL) {
            return 0;
        }
        if ((size_t)(end - start) == value_len && memcmp(start, value, value_len) == 0) {
            return 1;
        }
        p = end + 1;
    }
    return 0;
}

static int toml_array_first_quoted(const char* raw_array, char* out, size_t out_len)
{
    const char* start;
    const char* end;
    size_t n;
    if (raw_array == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    out[0] = '\0';
    start = strchr(raw_array, '"');
    if (start == NULL) {
        return 0;
    }
    start += 1;
    end = strchr(start, '"');
    if (end == NULL) {
        return 0;
    }
    n = (size_t)(end - start);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return n > 0U;
}

static int find_package_target_in_lock(
    const char* project_dir,
    const char* target_id,
    PackageTargetLockRecord* out_record)
{
    char lock_path[PATH_MAX];
    char* lock_text = NULL;
    const char* section;
    if (project_dir == NULL || target_id == NULL || target_id[0] == '\0' || out_record == NULL) {
        return 0;
    }
    memset(out_record, 0, sizeof(*out_record));
    if (!join_path(project_dir, "ailang.lock.toml", lock_path, sizeof(lock_path)) ||
        !read_text_file_alloc(lock_path, &lock_text)) {
        return 0;
    }
    section = lock_text;
    while ((section = strstr(section, "[[target]]")) != NULL) {
        const char* section_end = strstr(section + 10, "[[target]]");
        char id[128];
        char aliases[512];
        if (section_end == NULL) {
            section_end = lock_text + strlen(lock_text);
        }
        id[0] = '\0';
        aliases[0] = '\0';
        (void)toml_section_get_value(section, section_end, "id", 1, id, sizeof(id));
        (void)toml_section_get_value(section, section_end, "aliases", 0, aliases, sizeof(aliases));
        if (strcmp(id, target_id) == 0 || toml_array_contains_quoted(aliases, target_id)) {
            (void)snprintf(out_record->id, sizeof(out_record->id), "%s", id);
            (void)toml_section_get_value(section, section_end, "package", 1, out_record->package_name, sizeof(out_record->package_name));
            (void)toml_section_get_value(section, section_end, "defaultRunner", 1, out_record->default_runner, sizeof(out_record->default_runner));
            (void)toml_section_get_value(section, section_end, "artifactTypes", 0, out_record->artifact_types, sizeof(out_record->artifact_types));
            (void)toml_section_get_value(section, section_end, "options", 0, out_record->options, sizeof(out_record->options));
            (void)toml_section_get_value(section, section_end, "runTools", 0, out_record->run_tools, sizeof(out_record->run_tools));
            (void)toml_section_get_value(section, section_end, "publishTools", 0, out_record->publish_tools, sizeof(out_record->publish_tools));
            free(lock_text);
            return out_record->id[0] != '\0';
        }
        section = section_end;
    }
    free(lock_text);
    return 0;
}

static int validate_required_commands(const char* target_id, const char* operation, const char* raw_tools)
{
    const char* p;
    char found[PATH_MAX];
    if (raw_tools == NULL || raw_tools[0] == '\0') {
        return 1;
    }
    p = raw_tools;
    while ((p = strchr(p, '"')) != NULL) {
        const char* start = p + 1;
        const char* end = strchr(start, '"');
        char command[128];
        size_t n;
        if (end == NULL) {
            return 0;
        }
        n = (size_t)(end - start);
        if (n > 0U) {
            if (n + 1U > sizeof(command)) {
                fprintf(stderr,
                    "Err#err1(code=PKG001 message=\"Target requirement command name is too long.\" nodeId=target)\n");
                return 0;
            }
            memcpy(command, start, n);
            command[n] = '\0';
            if (!find_executable_on_path(command, found, sizeof(found))) {
                fprintf(stderr,
                    "Err#err1(code=PKG001 message=\"AILANG-PKG-REQ001: %s requires %s for %s. Missing command: %s\" nodeId=target)\n",
                    target_id,
                    command,
                    operation,
                    command);
                return 0;
            }
        }
        p = end + 1;
    }
    return 1;
}

static int package_target_option_name_from_flag(const char* arg, char* out, size_t out_len)
{
    const char* start;
    const char* end;
    size_t n;
    if (arg == NULL || out == NULL || out_len == 0U || !starts_with(arg, "--")) {
        return 0;
    }
    if (strcmp(arg, "--target-option") == 0 || starts_with(arg, "--target-option=")) {
        return 0;
    }
    start = arg + 2;
    end = strchr(start, '=');
    if (end == NULL) {
        end = start + strlen(start);
    }
    n = (size_t)(end - start);
    if (n == 0U || n + 1U > out_len) {
        return 0;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return 1;
}

static int package_target_option_name_from_assignment(const char* text, char* out, size_t out_len)
{
    const char* end;
    size_t n;
    if (text == NULL || out == NULL || out_len == 0U || text[0] == '\0') {
        return 0;
    }
    end = strchr(text, '=');
    if (end == NULL) {
        end = text + strlen(text);
    }
    n = (size_t)(end - text);
    if (n == 0U || n + 1U > out_len) {
        return 0;
    }
    memcpy(out, text, n);
    out[n] = '\0';
    return 1;
}

static int validate_package_target_options(
    const PackageTargetLockRecord* record,
    int target_option_count,
    char** target_options)
{
    int i;
    if (record == NULL || target_option_count <= 0 || target_options == NULL) {
        return 1;
    }
    for (i = 0; i < target_option_count; i += 1) {
        char option_name[128];
        option_name[0] = '\0';
        if (strcmp(target_options[i], "--target-option") == 0) {
            if (i + 1 >= target_option_count ||
                !package_target_option_name_from_assignment(target_options[i + 1], option_name, sizeof(option_name))) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --target-option value. Expected name=value.\" nodeId=target)\n");
                return 0;
            }
            i += 1;
        } else if (starts_with(target_options[i], "--target-option=")) {
            if (!package_target_option_name_from_assignment(target_options[i] + 16, option_name, sizeof(option_name))) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --target-option value. Expected name=value.\" nodeId=target)\n");
                return 0;
            }
        } else if (!package_target_option_name_from_flag(target_options[i], option_name, sizeof(option_name))) {
            continue;
        }

        if (record->options[0] != '\0' && !toml_array_contains_quoted(record->options, option_name)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Package target does not declare requested option: %s\" nodeId=target)\n",
                option_name);
            return 0;
        }
    }
    return 1;
}

static int delegate_package_target_operation(
    const char* operation,
    const char* program_path,
    const char* target_id,
    const char* explicit_runner,
    const char* explicit_artifact_type,
    const char* explicit_target_version,
    const char* out_dir,
    int target_option_count,
    char** target_options,
    int app_arg_count,
    char** app_args)
{
    char project_dir[PATH_MAX];
    char error[512];
    char artifact_type[64];
    const char* runner;
    char** tool_args = NULL;
    int tool_arg_count;
    int tool_exit = 0;
    int index = 0;
    int i;
    PackageTargetLockRecord record;
    AilangPackageManagerOptions package_options;

    if (operation == NULL || program_path == NULL || target_id == NULL || target_id[0] == '\0') {
        return -1;
    }
    if (!resolve_project_dir_for_cache(program_path, project_dir, sizeof(project_dir))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Could not resolve project directory for package target.\" nodeId=target)\n");
        return 2;
    }
    if (!find_package_target_in_lock(project_dir, target_id, &record)) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Unknown package target. Run 'ailang package restore' after adding the target package.\" nodeId=target)\n");
        return 2;
    }

    runner = (explicit_runner != NULL && explicit_runner[0] != '\0') ? explicit_runner : record.default_runner;
    if (runner == NULL || runner[0] == '\0') {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Package target has no runner. Pass --runner or set defaultRunner.\" nodeId=runner)\n");
        return 2;
    }

    artifact_type[0] = '\0';
    if (explicit_artifact_type != NULL && explicit_artifact_type[0] != '\0') {
        if (!toml_array_contains_quoted(record.artifact_types, explicit_artifact_type)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Package target does not support requested artifact type.\" nodeId=target)\n");
            return 2;
        }
        (void)snprintf(artifact_type, sizeof(artifact_type), "%s", explicit_artifact_type);
    } else if (!toml_array_first_quoted(record.artifact_types, artifact_type, sizeof(artifact_type))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Package target has no supported artifact types.\" nodeId=target)\n");
        return 2;
    }

    if (!validate_required_commands(
            record.id,
            operation,
            strcmp(operation, "run") == 0 ? record.run_tools : record.publish_tools)) {
        return 2;
    }
    if (!validate_package_target_options(&record, target_option_count, target_options)) {
        return 2;
    }

    tool_arg_count = 8 +
        ((explicit_target_version != NULL && explicit_target_version[0] != '\0') ? 2 : 0) +
        ((out_dir != NULL && out_dir[0] != '\0') ? 2 : 0) +
        target_option_count +
        app_arg_count;
    tool_args = (char**)calloc((size_t)tool_arg_count, sizeof(char*));
    if (tool_args == NULL) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Package target argument allocation failed.\" nodeId=target)\n");
        return 2;
    }
    tool_args[index++] = (char*)operation;
    tool_args[index++] = (char*)program_path;
    tool_args[index++] = "--target";
    tool_args[index++] = (char*)record.id;
    tool_args[index++] = "--artifact-type";
    tool_args[index++] = artifact_type;
    if (explicit_target_version != NULL && explicit_target_version[0] != '\0') {
        tool_args[index++] = "--target-version";
        tool_args[index++] = (char*)explicit_target_version;
    }
    if (out_dir != NULL && out_dir[0] != '\0') {
        tool_args[index++] = "--out";
        tool_args[index++] = (char*)out_dir;
    }
    for (i = 0; i < target_option_count; i += 1) {
        tool_args[index++] = target_options[i];
    }
    tool_args[index++] = "--package-target";
    tool_args[index++] = "--";
    for (i = 0; i < app_arg_count; i += 1) {
        tool_args[index++] = app_args[i];
    }

    memset(&package_options, 0, sizeof(package_options));
    package_options.project_dir = project_dir;
    if (g_airun_runtime_exe_path[0] != '\0') {
#ifdef _WIN32
        (void)_putenv_s("AILANG_BIN", g_airun_runtime_exe_path);
#else
        (void)setenv("AILANG_BIN", g_airun_runtime_exe_path, 1);
#endif
    }
    error[0] = '\0';
    if (!ailang_package_manager_try_run_interactive_tool(
            &package_options,
            runner,
            index,
            tool_args,
            &tool_exit,
            error,
            sizeof(error))) {
        free(tool_args);
        if (error[0] != '\0') {
            fprintf(stderr, "Err#err1(code=PKG001 message=\"%s\" nodeId=target)\n", error);
        } else {
            fprintf(stderr,
                "Err#err1(code=PKG001 message=\"Package target runner is not installed: %s\" nodeId=target)\n",
                runner);
        }
        return 2;
    }
    free(tool_args);
    return tool_exit;
}

static const char* derive_ui_platform_from_target(const char* target)
{
    if (run_target_is_wasm(target)) {
        return "web";
    }
    if (target == NULL || target[0] == '\0') {
        return "desktop";
    }
    if (starts_with(target, "ios-") || strcmp(target, "ios") == 0 ||
        starts_with(target, "android-") || strcmp(target, "android") == 0) {
        return "phone";
    }
    if (starts_with(target, "watchos-") || strcmp(target, "watchos") == 0) {
        return "watch";
    }
    return "desktop";
}

static int configure_runtime_context(const char* target, const char* platform)
{
    const char* effective_platform = (platform != NULL && platform[0] != '\0')
        ? platform
        : derive_ui_platform_from_target(target);
    const char* effective_target = (target != NULL && target[0] != '\0')
        ? target
        : host_rid();
    if (!ui_platform_is_valid(effective_platform)) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Unsupported --platform value.\" nodeId=platform)\n");
        return 0;
    }
#ifdef _WIN32
    return _putenv_s("AILANG_RUNTIME_PLATFORM", effective_platform) == 0 &&
           _putenv_s("AILANG_RUNTIME_TARGET", effective_target) == 0;
#else
    return setenv("AILANG_RUNTIME_PLATFORM", effective_platform, 1) == 0 &&
           setenv("AILANG_RUNTIME_TARGET", effective_target, 1) == 0;
#endif
}

static int delegate_run_to_project_tool(const RunTarget* target, char** argv)
{
    char tool_name[128];
    char manifest_path[PATH_MAX];
    char project_dir[PATH_MAX];
    char error[512];
    char restore_output[AILANG_NATIVE_BRIDGE_MAX_STRING];
    const char* tool_args_static[2];
    char** tool_args = NULL;
    int tool_arg_count;
    int tool_exit = 0;
    int i;
    AilangPackageManagerOptions package_options;
    if (target == NULL || argv == NULL || getenv("AILANG_DISABLE_RUN_TOOL_DISPATCH") != NULL) {
        return -1;
    }
    if (!resolve_project_run_tool(target->program_path, tool_name, sizeof(tool_name))) {
        return -1;
    }
    if (!resolve_manifest_path_for_input(target->program_path, manifest_path, sizeof(manifest_path)) ||
        !dirname_of(manifest_path, project_dir, sizeof(project_dir))) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Could not resolve project directory for run tool.\" nodeId=runTool)\n");
        return 2;
    }
    tool_arg_count = 2 + target->app_arg_count;
    tool_args = (char**)calloc((size_t)tool_arg_count, sizeof(char*));
    if (tool_args == NULL) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Run tool argument allocation failed.\" nodeId=runTool)\n");
        return 2;
    }
    tool_args_static[0] = "run";
    tool_args_static[1] = target->program_path;
    tool_args[0] = (char*)tool_args_static[0];
    tool_args[1] = (char*)tool_args_static[1];
    for (i = 0; i < target->app_arg_count; i += 1) {
        tool_args[2 + i] = argv[target->app_arg_start + i];
    }
    memset(&package_options, 0, sizeof(package_options));
    package_options.project_dir = project_dir;
    if (g_airun_runtime_exe_path[0] != '\0') {
#ifdef _WIN32
        (void)_putenv_s("AILANG_BIN", g_airun_runtime_exe_path);
#else
        (void)setenv("AILANG_BIN", g_airun_runtime_exe_path, 1);
#endif
    }
    error[0] = '\0';
    if (ailang_package_manager_try_run_interactive_tool(
            &package_options,
            tool_name,
            tool_arg_count,
            tool_args,
            &tool_exit,
            error,
            sizeof(error))) {
        free(tool_args);
        return tool_exit;
    }
    error[0] = '\0';
    if (ailang_package_manager_restore(&package_options, restore_output, sizeof(restore_output), error, sizeof(error)) &&
        ailang_package_manager_try_run_interactive_tool(
            &package_options,
            tool_name,
            tool_arg_count,
            tool_args,
            &tool_exit,
            error,
            sizeof(error))) {
        free(tool_args);
        return tool_exit;
    }
    free(tool_args);
    if (error[0] != '\0') {
        fprintf(stderr, "Err#err1(code=PKG001 message=\"%s\" nodeId=runTool)\n", error);
    } else {
        fprintf(stderr, "Err#err1(code=PKG001 message=\"project run tool not installed: %s\" nodeId=runTool)\n", tool_name);
    }
    return 2;
}

static int parse_run_target(int argc, char** argv, int start_index, RunTarget* out_target)
{
    int i;
    const char* program_path = NULL;
    int app_arg_start = -1;
    int use_cache = 1;
    const char* log_level = NULL;
    const char* target = NULL;
    const char* runner = NULL;
    const char* artifact_type = NULL;
    const char* target_version = NULL;
    const char* ui_platform = NULL;
    const char* wasm_profile = "spa";
    const char* out_dir = "dist-wasm";
    int port = 8768;
    int port_explicit = 0;
    int open_browser = 1;
    int target_option_count = 0;
    char* target_options[AILANG_CLI_MAX_TARGET_OPTIONS];

    if (out_target == NULL) {
        return 2;
    }
    for (i = start_index; i < argc; i++) {
        const char* arg = argv[i];
        if (strcmp(arg, "--") == 0) {
            if (app_arg_start < 0) {
                app_arg_start = i + 1;
            }
            break;
        }
        if (strcmp(arg, "--no-cache") == 0 && app_arg_start < 0) {
            use_cache = 0;
            continue;
        }
        if (strcmp(arg, "--target") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Missing --target value.\" nodeId=argv)\n");
                return 2;
            }
            target = argv[++i];
            continue;
        }
        if (starts_with(arg, "--target=") && app_arg_start < 0) {
            target = arg + 9;
            continue;
        }
        if (strcmp(arg, "--runner") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Missing --runner value.\" nodeId=argv)\n");
                return 2;
            }
            runner = argv[++i];
            continue;
        }
        if (starts_with(arg, "--runner=") && app_arg_start < 0) {
            runner = arg + 9;
            continue;
        }
        if ((strcmp(arg, "--type") == 0 || strcmp(arg, "--artifact-type") == 0) && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Missing artifact type value.\" nodeId=argv)\n");
                return 2;
            }
            artifact_type = argv[++i];
            continue;
        }
        if (starts_with(arg, "--type=") && app_arg_start < 0) {
            artifact_type = arg + 7;
            continue;
        }
        if (starts_with(arg, "--artifact-type=") && app_arg_start < 0) {
            artifact_type = arg + 16;
            continue;
        }
        if (strcmp(arg, "--target-version") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Missing --target-version value.\" nodeId=argv)\n");
                return 2;
            }
            target_version = argv[++i];
            continue;
        }
        if (starts_with(arg, "--target-version=") && app_arg_start < 0) {
            target_version = arg + 17;
            continue;
        }
        if (strcmp(arg, "--platform") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Missing --platform value.\" nodeId=argv)\n");
                return 2;
            }
            ui_platform = argv[++i];
            continue;
        }
        if (starts_with(arg, "--platform=") && app_arg_start < 0) {
            ui_platform = arg + 11;
            continue;
        }
        if (strcmp(arg, "--wasm-profile") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Missing --wasm-profile value.\" nodeId=argv)\n");
                return 2;
            }
            wasm_profile = argv[++i];
            continue;
        }
        if (starts_with(arg, "--wasm-profile=") && app_arg_start < 0) {
            wasm_profile = arg + 15;
            continue;
        }
        if (strcmp(arg, "--out") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Missing --out value.\" nodeId=argv)\n");
                return 2;
            }
            out_dir = argv[++i];
            continue;
        }
        if (starts_with(arg, "--out=") && app_arg_start < 0) {
            out_dir = arg + 6;
            continue;
        }
        if (strcmp(arg, "--port") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc || !parse_int(argv[i + 1], &port) || port <= 0 || port > 65535) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Invalid --port value.\" nodeId=argv)\n");
                return 2;
            }
            i += 1;
            port_explicit = 1;
            continue;
        }
        if (starts_with(arg, "--port=") && app_arg_start < 0) {
            if (!parse_int(arg + 7, &port) || port <= 0 || port > 65535) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Invalid --port value.\" nodeId=argv)\n");
                return 2;
            }
            port_explicit = 1;
            continue;
        }
        if (strcmp(arg, "--no-open") == 0 && app_arg_start < 0) {
            open_browser = 0;
            continue;
        }
        if (package_target_option_takes_value(arg) && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Missing package target option value.\" nodeId=argv)\n");
                return 2;
            }
            if (target_option_count + 2 > AILANG_CLI_MAX_TARGET_OPTIONS) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Too many package target options.\" nodeId=argv)\n");
                return 2;
            }
            target_options[target_option_count++] = argv[i];
            target_options[target_option_count++] = argv[++i];
            continue;
        }
        if (package_target_option_is_inline(arg) && app_arg_start < 0) {
            if (target_option_count + 1 > AILANG_CLI_MAX_TARGET_OPTIONS) {
                fprintf(stderr, "Err#err1(code=RUN001 message=\"Too many package target options.\" nodeId=argv)\n");
                return 2;
            }
            target_options[target_option_count++] = argv[i];
            continue;
        }
        if (strcmp(arg, "--log-level") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --log-level value.\" nodeId=argv)\n");
                return 2;
            }
            log_level = argv[i + 1];
            i += 1;
            continue;
        }
        if (starts_with(arg, "--log-level=") && app_arg_start < 0) {
            log_level = arg + 12;
            continue;
        }
        if (app_arg_start < 0 && starts_with(arg, "--vm=")) {
            const char* mode = arg + 5;
            if (strcmp(mode, "c") != 0 && !is_reserved_cv_selector(mode)) {
                return print_unsupported_vm_mode(mode);
            }
            continue;
        }
        if (program_path == NULL && arg[0] != '-' && app_arg_start < 0) {
            program_path = arg;
            continue;
        }
        if (program_path == NULL && arg[0] == '-' && app_arg_start < 0) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Unsupported flag for native C runtime.\" nodeId=argv)\n");
            return 2;
        }
        if (app_arg_start < 0) {
            app_arg_start = i;
        }
        break;
    }

    /* Preserve wrapper contract: default to current project directory when omitted. */
    out_target->program_path = (program_path != NULL) ? program_path : ".";
    if (app_arg_start >= 0 && app_arg_start <= argc) {
        out_target->app_arg_start = app_arg_start;
        out_target->app_arg_count = argc - app_arg_start;
    } else {
        out_target->app_arg_start = argc;
        out_target->app_arg_count = 0;
    }
    out_target->use_cache = use_cache;
    out_target->log_level = log_level;
    out_target->target = target;
    out_target->runner = runner;
    out_target->artifact_type = artifact_type;
    out_target->target_version = target_version;
    out_target->ui_platform = ui_platform;
    out_target->wasm_profile = wasm_profile;
    out_target->out_dir = out_dir;
    out_target->port = port;
    out_target->port_explicit = port_explicit;
    out_target->open_browser = open_browser;
    out_target->target_option_count = target_option_count;
    for (i = 0; i < target_option_count; i += 1) {
        out_target->target_options[i] = target_options[i];
    }
    return 0;
}

static int absolute_existing_path(const char* path, char* output, size_t output_len)
{
    if (path == NULL || output == NULL || output_len == 0U) {
        return 0;
    }
#ifdef _WIN32
    return _fullpath(output, path, output_len) != NULL;
#else
    return realpath(path, output) != NULL;
#endif
}

static int package_target_option_takes_value(const char* arg)
{
    return strcmp(arg, "--arch") == 0 ||
           strcmp(arg, "--boot") == 0 ||
           strcmp(arg, "--image") == 0 ||
           strcmp(arg, "--partition") == 0 ||
           strcmp(arg, "--feature") == 0 ||
           strcmp(arg, "--splash-background") == 0 ||
           strcmp(arg, "--splash-foreground") == 0 ||
           strcmp(arg, "--target-option") == 0;
}

static int package_target_option_is_inline(const char* arg)
{
    return starts_with(arg, "--arch=") ||
           starts_with(arg, "--boot=") ||
           starts_with(arg, "--image=") ||
           starts_with(arg, "--partition=") ||
           starts_with(arg, "--feature=") ||
           starts_with(arg, "--splash-background=") ||
           starts_with(arg, "--splash-foreground=") ||
           starts_with(arg, "--target-option=");
}

static void sleep_milliseconds(int milliseconds)
{
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

static int local_port_is_listening(int port)
{
    NativeSocket client;
    struct sockaddr_in address;
    int connected = 0;
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return 0;
    }
#endif
    client = socket(AF_INET, SOCK_STREAM, 0);
    if (client != NATIVE_INVALID_SOCKET) {
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons((uint16_t)port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connected = connect(client, (struct sockaddr*)&address, sizeof(address)) == 0;
        native_socket_close(client);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return connected;
}

#ifdef _WIN32
static int wait_for_local_server_process(int port, HANDLE process_handle)
#else
static int wait_for_local_server_process(int port, pid_t child)
#endif
{
    int attempt;
    for (attempt = 0; attempt < 100; attempt += 1) {
#ifdef _WIN32
        DWORD process_exit = STILL_ACTIVE;
        if (!GetExitCodeProcess(process_handle, &process_exit) || process_exit != STILL_ACTIVE) {
            return 0;
        }
#else
        int status = 0;
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child || result < 0) {
            return 0;
        }
#endif
        if (local_port_is_listening(port)) {
            return 1;
        }
        sleep_milliseconds(50);
    }
    return 0;
}

static int run_wasm_browser_target(const RunTarget* target, char** argv)
{
    char* publish_argv[10];
    int publish_argc = 0;
    int publish_rc;
    char project_dir[PATH_MAX];
    char package_root[PATH_MAX];
    char source_dir[PATH_MAX];
    char server_source[PATH_MAX];
    char build_out[PATH_MAX];
    char built_app[PATH_MAX];
    char server_root[PATH_MAX];
    char absolute_out[PATH_MAX];
    char port_text[16];
    char url[128];
    char package_error[512];
    char restore_output[AILANG_NATIVE_BRIDGE_MAX_STRING];
    int selected_port;
    AilangPackageManagerOptions package_options;
    if (target == NULL || argv == NULL || strcmp(target->target, "wasm32") != 0) {
        return -1;
    }
    if (strcmp(target->wasm_profile, "spa") != 0 && strcmp(target->wasm_profile, "fullstack") != 0) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Browser run requires wasm profile spa or fullstack.\" nodeId=wasmProfile)\n");
        return 2;
    }
    if (!resolve_project_dir_for_cache(target->program_path, project_dir, sizeof(project_dir))) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Could not resolve project directory.\" nodeId=wasmServer)\n");
        return 2;
    }
    memset(&package_options, 0, sizeof(package_options));
    package_options.project_dir = project_dir;
    package_error[0] = '\0';
    if (!ailang_package_manager_restore(
            &package_options, restore_output, sizeof(restore_output), package_error, sizeof(package_error))) {
        fprintf(stderr, "Err#err1(code=PKG001 message=\"Package restore failed: %s\" nodeId=wasmServer)\n", package_error);
        return 2;
    }
    if (!ends_with(target->program_path, ".aibc1") &&
        !ends_with(target->program_path, ".aibundle")) {
        if (!derive_build_out_dir(target->program_path, build_out, sizeof(build_out)) ||
            build_input_to_aibc1(
                target->program_path,
                build_out,
                built_app,
                sizeof(built_app),
                target->use_cache) != 1) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Native build failed: %s\" nodeId=build)\n",
                native_build_error());
            return 2;
        }
    }
    publish_argv[publish_argc++] = argv[0];
    publish_argv[publish_argc++] = "publish";
    publish_argv[publish_argc++] = (char*)target->program_path;
    publish_argv[publish_argc++] = "--target";
    publish_argv[publish_argc++] = "wasm32";
    publish_argv[publish_argc++] = "--wasm-profile";
    publish_argv[publish_argc++] = (char*)target->wasm_profile;
    publish_argv[publish_argc++] = "--out";
    publish_argv[publish_argc++] = (char*)target->out_dir;
    publish_rc = handle_publish(publish_argc, publish_argv);
    if (publish_rc != 0) {
        return publish_rc;
    }
    package_error[0] = '\0';
    if (!ailang_package_manager_find_package_root(
            &package_options, "std-http", package_root, sizeof(package_root), package_error, sizeof(package_error)) ||
        !join_path(package_root, "src/net", source_dir, sizeof(source_dir)) ||
        !join_path(source_dir, "http_dev_server.aos", server_source, sizeof(server_source)) ||
        !file_exists(server_source)) {
        fprintf(stderr,
            "Err#err1(code=PKG001 message=\"std-http dev server is unavailable; add and restore std-http. %s\" nodeId=wasmServer)\n",
            package_error);
        return 2;
    }
    if (strcmp(target->wasm_profile, "fullstack") == 0) {
        if (!join_path(target->out_dir, "www", server_root, sizeof(server_root))) {
            fprintf(stderr, "Err#err1(code=RUN001 message=\"Wasm server path overflow.\" nodeId=wasmServer)\n");
            return 2;
        }
    } else if (snprintf(server_root, sizeof(server_root), "%s", target->out_dir) >= (int)sizeof(server_root)) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Wasm server path overflow.\" nodeId=wasmServer)\n");
        return 2;
    }
    if (!absolute_existing_path(server_root, absolute_out, sizeof(absolute_out))) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Could not resolve wasm output directory.\" nodeId=wasmServer)\n");
        return 2;
    }
    selected_port = target->port;
    if (target->port_explicit) {
        if (local_port_is_listening(selected_port)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Requested port %d is already in use.\" nodeId=wasmServer)\n",
                selected_port);
            return 2;
        }
    } else {
        while (selected_port < 65535 && local_port_is_listening(selected_port)) {
            selected_port += 1;
        }
        if (local_port_is_listening(selected_port)) {
            fprintf(stderr, "Err#err1(code=RUN001 message=\"No available local WASM server port.\" nodeId=wasmServer)\n");
            return 2;
        }
    }
    (void)snprintf(port_text, sizeof(port_text), "%d", selected_port);
    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%d/", selected_port);
    printf("AiLang WASM: %s\n", url);
    printf("AiLang WASM output: %s\n", absolute_out);
    fflush(stdout);
#ifdef _WIN32
    {
        intptr_t process_handle;
        DWORD process_exit = 2U;
        (void)_putenv_s("AILANG_DISABLE_RUN_TOOL_DISPATCH", "1");
        process_handle = _spawnl(
            _P_NOWAIT,
            g_airun_runtime_exe_path,
            g_airun_runtime_exe_path,
            "run",
            server_source,
            "--",
            absolute_out,
            port_text,
            NULL);
        (void)_putenv_s("AILANG_DISABLE_RUN_TOOL_DISPATCH", "");
        if (process_handle == -1) {
            fprintf(stderr, "Err#err1(code=RUN001 message=\"Failed to launch AiLang wasm server.\" nodeId=wasmServer)\n");
            return 2;
        }
        if (!wait_for_local_server_process(selected_port, (HANDLE)process_handle)) {
            TerminateProcess((HANDLE)process_handle, 2);
            CloseHandle((HANDLE)process_handle);
            fprintf(stderr, "Err#err1(code=RUN001 message=\"AiLang wasm server did not become ready.\" nodeId=wasmServer)\n");
            return 2;
        }
        if (target->open_browser && !native_host_open_default(url)) {
            fprintf(stderr, "AiLang WASM warning: could not open the default browser; open %s\n", url);
        }
        if (WaitForSingleObject((HANDLE)process_handle, INFINITE) != WAIT_OBJECT_0 ||
            !GetExitCodeProcess((HANDLE)process_handle, &process_exit)) {
            CloseHandle((HANDLE)process_handle);
            return 2;
        }
        CloseHandle((HANDLE)process_handle);
        return (int)process_exit;
    }
#else
    {
        pid_t child = fork();
        int status = 0;
        if (child < 0) {
            fprintf(stderr, "Err#err1(code=RUN001 message=\"Failed to launch AiLang wasm server.\" nodeId=wasmServer)\n");
            return 2;
        }
        if (child == 0) {
            (void)setenv("AILANG_DISABLE_RUN_TOOL_DISPATCH", "1", 1);
            execl(
                g_airun_runtime_exe_path,
                g_airun_runtime_exe_path,
                "run",
                server_source,
                "--",
                absolute_out,
                port_text,
                (char*)NULL);
            _exit(127);
        }
        if (!wait_for_local_server_process(selected_port, child)) {
            (void)kill(child, SIGTERM);
            (void)waitpid(child, &status, 0);
            fprintf(stderr, "Err#err1(code=RUN001 message=\"AiLang wasm server did not become ready.\" nodeId=wasmServer)\n");
            return 2;
        }
        if (target->open_browser && !native_host_open_default(url)) {
            fprintf(stderr, "AiLang WASM warning: could not open the default browser; open %s\n", url);
        }
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        return 2;
    }
#endif
}

static int run_via_resolved_input(
    const char* input,
    const char* const* process_argv,
    size_t process_argv_count,
    const NativeDebugOptions* debug_options)
{
    char resolved[PATH_MAX];
    char source_aos[PATH_MAX];
    airun_log_message(
        AIRUN_LOG_TRACE,
        "runtime",
        "run-via-resolved-input start input=%s argv_count=%llu debug_mode=%s bundle=%d",
        (input == NULL) ? "" : input,
        (unsigned long long)process_argv_count,
        (debug_options == NULL || debug_options->debug_mode == NULL) ? "off" : debug_options->debug_mode,
        (debug_options == NULL) ? 0 : debug_options->emit_bundle);
    if (input != NULL && ends_with(input, ".aibundle") && file_exists(input)) {
        airun_log_message(AIRUN_LOG_TRACE, "runtime", "resolved-input kind=aibundle path=%s", input);
        int rc = run_native_bundle(input, process_argv, process_argv_count, debug_options);
        if (rc >= 0) {
            airun_log_message(AIRUN_LOG_TRACE, "runtime", "resolved-input done kind=aibundle rc=%d", rc);
            return rc;
        }
        fprintf(stderr,
            "Err#err1(code=DEV008 message=\"Native bundle input uses unsupported bytecode fields. Provide supported Bytecode# shape.\" nodeId=program)\n");
        return 2;
    }
    if (resolve_input_to_aibc1(input, resolved, sizeof(resolved))) {
        airun_log_message(AIRUN_LOG_TRACE, "runtime", "resolved-input kind=aibc1 path=%s", resolved);
        return run_native_aibc1(resolved, process_argv, process_argv_count, debug_options);
    }
    if (resolve_input_to_aos(input, source_aos, sizeof(source_aos))) {
        airun_log_message(AIRUN_LOG_TRACE, "runtime", "resolved-input kind=aos path=%s", source_aos);
        int rc = run_native_bytecode_aos(source_aos, process_argv, process_argv_count, debug_options);
        if (rc >= 0) {
            airun_log_message(AIRUN_LOG_TRACE, "runtime", "resolved-input done kind=bytecode-aos rc=%d", rc);
            return rc;
        }
        if (source_file_looks_like_bytecode_aos(source_aos)) {
            fprintf(stderr,
                "Err#err1(code=DEV008 message=\"Native bytecode AOS input uses unsupported fields. Build precompiled .aibc1 or use supported instruction attributes.\" nodeId=program)\n");
            return 2;
        }
        rc = run_native_simple_program_aos(source_aos, process_argv, process_argv_count, debug_options);
        if (rc >= 0) {
            airun_log_message(AIRUN_LOG_TRACE, "runtime", "resolved-input done kind=simple-program-aos rc=%d", rc);
            return rc;
        }
    }
    airun_log_message(AIRUN_LOG_TRACE, "runtime", "resolved-input failed input=%s", (input == NULL) ? "" : input);
    fprintf(stderr,
        "Err#err1(code=DEV008 message=\"Native C runtime cannot compile this source/project shape yet. Provide .aibc1 or supported Program#/Bytecode# AOS.\" nodeId=program)\n");
    return 2;
}

static int handle_run(int argc, char** argv)
{
    RunTarget target;
    int parse_rc = 0;
    char out_dir[PATH_MAX];
    char app_path[PATH_MAX];
    int build_rc;
    memset(&target, 0, sizeof(target));
    parse_rc = parse_run_target(argc, argv, 2, &target);
    if (parse_rc != 0) {
        return parse_rc;
    }
    if (!configure_runtime_context(target.target, target.ui_platform)) {
        return 2;
    }
    airun_configure_log_level(target.log_level);
    airun_reset_injected_events();

    if (run_target_is_wasm(target.target)) {
        int wasm_rc = run_wasm_browser_target(&target, argv);
        if (wasm_rc >= 0) {
            return wasm_rc;
        }
        fprintf(stderr, "Err#err1(code=RUN001 message=\"run --target wasm32 failed to start browser target.\" nodeId=target)\n");
        return 2;
    }

    if (!run_target_is_native_host(target.target)) {
        return delegate_package_target_operation(
            "run",
            target.program_path,
            target.target,
            target.runner,
            target.artifact_type,
            target.target_version,
            target.out_dir,
            target.target_option_count,
            target.target_options,
            target.app_arg_count,
            argc > target.app_arg_start ? &argv[target.app_arg_start] : NULL);
    }

    {
        int delegated_rc = delegate_run_to_project_tool(&target, argv);
        if (delegated_rc >= 0) {
            return delegated_rc;
        }
    }

    if (target.program_path != NULL &&
        !ends_with(target.program_path, ".aibc1") &&
        !ends_with(target.program_path, ".aibundle")) {
        if (!derive_build_out_dir(target.program_path, out_dir, sizeof(out_dir))) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Native build failed: could not derive output directory.\" nodeId=build)\n");
            return 2;
        }
        build_rc = build_input_to_aibc1(
            target.program_path,
            out_dir,
            app_path,
            sizeof(app_path),
            target.use_cache);
        if (build_rc != 1) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Native build failed: %s\" nodeId=build)\n",
                native_build_error());
            return 2;
        }
        return run_native_aibc1(
            app_path,
            (const char* const*)&argv[target.app_arg_start],
            (size_t)target.app_arg_count,
            NULL);
    }

    return run_via_resolved_input(
        target.program_path,
        (const char* const*)&argv[target.app_arg_start],
        (size_t)target.app_arg_count,
        NULL);
}

static AIRUN_MAYBE_UNUSED int handle_serve(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    fprintf(stderr,
        "Err#err1(code=DEV008 message=\"serve is not part of native ailang runtime surface.\" nodeId=command)\n");
    return 2;
}

static int handle_debug_run_mode(
    int argc,
    char** argv,
    int start_index,
    const char* default_log_level,
    int default_emit_bundle,
    const char* default_out_dir,
    int interact_mode)
{
    const char* program_path = NULL;
    int app_arg_start = -1;
    int i;
    const char* out_dir = default_out_dir;
    const char* debug_mode = "off";
    const char* log_level = default_log_level;
    int use_cache = 1;
    NativeDebugOptions debug_options;
    int rc;
    int build_rc;
    char config_path[PATH_MAX];
    char build_out_dir[PATH_MAX];
    char built_app_path[PATH_MAX];
    debug_options.emit_bundle = default_emit_bundle;
    debug_options.out_dir = NULL;
    debug_options.input_path = NULL;
    debug_options.debug_mode = "off";
    airun_reset_injected_events();
    g_airun_interact_state.enabled = interact_mode;

    for (i = start_index; i < argc; i += 1) {
        const char* arg = argv[i];
        if (strcmp(arg, "--") == 0) {
            app_arg_start = i + 1;
            break;
        }
        if (strcmp(arg, "--out") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --out value.\" nodeId=argv)\n");
                return 2;
            }
            out_dir = argv[i + 1];
            debug_options.emit_bundle = 1;
            i += 1;
            continue;
        }
        if (starts_with(arg, "--out=") && app_arg_start < 0) {
            out_dir = arg + 6;
            debug_options.emit_bundle = 1;
            continue;
        }
        if (strcmp(arg, "--no-cache") == 0 && app_arg_start < 0) {
            use_cache = 0;
            continue;
        }
        if (strcmp(arg, "--debug-mode") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --debug-mode value.\" nodeId=argv)\n");
                return 2;
            }
            debug_mode = argv[i + 1];
            i += 1;
            continue;
        }
        if (starts_with(arg, "--debug-mode=") && app_arg_start < 0) {
            debug_mode = arg + 13;
            continue;
        }
        if (strcmp(arg, "--log-level") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --log-level value.\" nodeId=argv)\n");
                return 2;
            }
            log_level = argv[i + 1];
            i += 1;
            continue;
        }
        if (starts_with(arg, "--log-level=") && app_arg_start < 0) {
            log_level = arg + 12;
            continue;
        }
        if (strcmp(arg, "--inject-click") == 0 && app_arg_start < 0) {
            int x = 0;
            int y = 0;
            if ((i + 1) >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --inject-click value.\" nodeId=argv)\n");
                return 2;
            }
            if (!airun_parse_click_point(argv[i + 1], &x, &y) ||
                !airun_queue_injected_click(x, y)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-click value. Expected x,y.\" nodeId=argv)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (starts_with(arg, "--inject-click=") && app_arg_start < 0) {
            int x = 0;
            int y = 0;
            if (!airun_parse_click_point(arg + 15, &x, &y) ||
                !airun_queue_injected_click(x, y)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-click value. Expected x,y.\" nodeId=argv)\n");
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--inject-key") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc || !airun_queue_injected_key(argv[i + 1], "")) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing or invalid --inject-key value.\" nodeId=argv)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (starts_with(arg, "--inject-key=") && app_arg_start < 0) {
            if (!airun_queue_injected_key(arg + 13, "")) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-key value.\" nodeId=argv)\n");
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--inject-key-at") == 0 && app_arg_start < 0) {
            int x = 0;
            int y = 0;
            char key_name[64];
            const char* text_value = NULL;
            if ((i + 1) >= argc ||
                !airun_parse_key_at_value(argv[i + 1], &x, &y, key_name, sizeof(key_name), &text_value) ||
                !airun_queue_injected_key_at(x, y, key_name, text_value)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing or invalid --inject-key-at value. Expected x,y,key[,text].\" nodeId=argv)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (starts_with(arg, "--inject-key-at=") && app_arg_start < 0) {
            int x = 0;
            int y = 0;
            char key_name[64];
            const char* text_value = NULL;
            if (!airun_parse_key_at_value(arg + 16, &x, &y, key_name, sizeof(key_name), &text_value) ||
                !airun_queue_injected_key_at(x, y, key_name, text_value)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-key-at value. Expected x,y,key[,text].\" nodeId=argv)\n");
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--inject-text") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc || !airun_queue_injected_text(argv[i + 1])) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing or invalid --inject-text value.\" nodeId=argv)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (starts_with(arg, "--inject-text=") && app_arg_start < 0) {
            if (!airun_queue_injected_text(arg + 14)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-text value.\" nodeId=argv)\n");
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--inject-text-at") == 0 && app_arg_start < 0) {
            int x = 0;
            int y = 0;
            const char* text = NULL;
            if ((i + 1) >= argc || !airun_parse_text_at_value(argv[i + 1], &x, &y, &text) ||
                !airun_queue_injected_text_at(x, y, text)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing or invalid --inject-text-at value. Expected x,y,text.\" nodeId=argv)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (starts_with(arg, "--inject-text-at=") && app_arg_start < 0) {
            int x = 0;
            int y = 0;
            const char* text = NULL;
            if (!airun_parse_text_at_value(arg + 17, &x, &y, &text) ||
                !airun_queue_injected_text_at(x, y, text)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-text-at value. Expected x,y,text.\" nodeId=argv)\n");
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--inject-wait") == 0 && app_arg_start < 0) {
            char* end = NULL;
            long parsed = 0L;
            if ((i + 1) >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --inject-wait value.\" nodeId=argv)\n");
                return 2;
            }
            parsed = strtol(argv[i + 1], &end, 10);
            if (end == argv[i + 1] || *end != '\0' || parsed < 0L || parsed > 1000000L ||
                !airun_queue_injected_wait((int)parsed)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-wait value.\" nodeId=argv)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (starts_with(arg, "--inject-wait=") && app_arg_start < 0) {
            char* end = NULL;
            long parsed = strtol(arg + 14, &end, 10);
            if (end == arg + 14 || *end != '\0' || parsed < 0L || parsed > 1000000L ||
                !airun_queue_injected_wait((int)parsed)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-wait value.\" nodeId=argv)\n");
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--inject-close") == 0 && app_arg_start < 0) {
            if (!airun_queue_injected_close()) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-close value.\" nodeId=argv)\n");
                return 2;
            }
            continue;
        }
        if (strcmp(arg, "--inject-script") == 0 && app_arg_start < 0) {
            if ((i + 1) >= argc || !airun_queue_injected_script_file(argv[i + 1])) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing or invalid --inject-script value.\" nodeId=argv)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (starts_with(arg, "--inject-script=") && app_arg_start < 0) {
            if (!airun_queue_injected_script_file(arg + 16)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --inject-script value.\" nodeId=argv)\n");
                return 2;
            }
            continue;
        }
        if (app_arg_start < 0 && starts_with(arg, "--vm=")) {
            const char* mode = arg + 5;
            if (strcmp(mode, "c") != 0 && !is_reserved_cv_selector(mode)) {
                return print_unsupported_vm_mode(mode);
            }
            continue;
        }
        if (program_path == NULL && arg[0] != '-' && app_arg_start < 0) {
            program_path = arg;
            continue;
        }
        if (program_path == NULL && arg[0] == '-' && app_arg_start < 0) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Unsupported flag for native C runtime.\" nodeId=argv)\n");
            return 2;
        }
        if (app_arg_start < 0) {
            app_arg_start = i;
        }
        break;
    }
    if (program_path == NULL) {
        program_path = ".";
    }
    if (app_arg_start < 0 || app_arg_start > argc) {
        app_arg_start = argc;
    }
    airun_configure_log_level(log_level);
    if (debug_options.emit_bundle && out_dir != NULL) {
        debug_options.out_dir = out_dir;
        debug_options.input_path = program_path;
    }
    debug_options.debug_mode = debug_mode;
    if (interact_mode) {
        airun_interact_drain_stdin();
    }
    airun_log_message(
        AIRUN_LOG_TRACE,
        "debug",
        "debug-run start program=%s debug_mode=%s emit_bundle=%d out_dir=%s app_arg_count=%d interact=%d",
        (program_path == NULL) ? "" : program_path,
        (debug_mode == NULL) ? "off" : debug_mode,
        debug_options.emit_bundle,
        (debug_options.out_dir == NULL) ? "" : debug_options.out_dir,
        argc - app_arg_start,
        interact_mode);

    if (program_path != NULL &&
        !ends_with(program_path, ".aibc1") &&
        !ends_with(program_path, ".aibundle")) {
        if (!derive_build_out_dir(program_path, build_out_dir, sizeof(build_out_dir))) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Native build failed: could not derive output directory.\" nodeId=build)\n");
            return 2;
        }
        build_rc = build_input_to_aibc1(
            program_path,
            build_out_dir,
            built_app_path,
            sizeof(built_app_path),
            use_cache);
        if (build_rc != 1) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Native build failed: %s\" nodeId=build)\n",
                native_build_error());
            return 2;
        }
        rc = run_native_aibc1(
            built_app_path,
            (const char* const*)&argv[app_arg_start],
            (size_t)(argc - app_arg_start),
            &debug_options);
    } else {
        rc = run_via_resolved_input(
            program_path,
            (const char* const*)&argv[app_arg_start],
            (size_t)(argc - app_arg_start),
            &debug_options);
    }
    airun_log_message(AIRUN_LOG_TRACE, "debug", "debug-run done rc=%d program=%s", rc, (program_path == NULL) ? "" : program_path);
    if (debug_options.emit_bundle &&
        debug_options.out_dir != NULL &&
        join_path(debug_options.out_dir, "config.toml", config_path, sizeof(config_path)) &&
        !file_exists(config_path)) {
        (void)write_native_debug_bundle(&debug_options, NULL, NULL, rc, 1, "native-debug-run-pre-exec-failure");
    }
    return rc;
}

static AIRUN_MAYBE_UNUSED int handle_debug(int argc, char** argv)
{
    if (argc >= 4 && strcmp(argv[2], "profile") == 0) {
        const char* program_path = NULL;
        const char* out_path = ".tmp/aivm-mem-audit.toml";
        char build_out_dir[PATH_MAX];
        char built_app_path[PATH_MAX];
        char report_dir[PATH_MAX];
        int use_cache = 1;
        int iterations = 20;
        int64_t max_growth_kb = 2048;
        int app_arg_start = -1;
        int i;
        int rc = 0;
        int sample_count = 0;
        int status_fail = 0;
        int64_t rss_series[1024];
        char status_text[32];
        FILE* report = NULL;

        if (argc > 1027) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Too many debug profile arguments.\" nodeId=argv)\n");
            return 2;
        }
        for (i = 3; i < argc; i += 1) {
            const char* arg = argv[i];
            if (strcmp(arg, "--") == 0) {
                app_arg_start = i + 1;
                break;
            }
            if (strcmp(arg, "--out") == 0) {
                if ((i + 1) >= argc) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Missing --out value.\" nodeId=argv)\n");
                    return 2;
                }
                out_path = argv[++i];
                continue;
            }
            if (strcmp(arg, "--iterations") == 0) {
                if ((i + 1) >= argc || !parse_int(argv[i + 1], &iterations) || iterations <= 0 || iterations > 1024) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Invalid --iterations value.\" nodeId=argv)\n");
                    return 2;
                }
                i += 1;
                continue;
            }
            if (strcmp(arg, "--max-growth-kb") == 0) {
                int parsed_growth = 0;
                if ((i + 1) >= argc || !parse_int(argv[i + 1], &parsed_growth) || parsed_growth < 0) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Invalid --max-growth-kb value.\" nodeId=argv)\n");
                    return 2;
                }
                max_growth_kb = (int64_t)parsed_growth;
                i += 1;
                continue;
            }
            if (strcmp(arg, "--no-cache") == 0) {
                use_cache = 0;
                continue;
            }
            if (starts_with(arg, "--vm=")) {
                const char* mode = arg + 5;
                if (strcmp(mode, "c") != 0 && !is_reserved_cv_selector(mode)) {
                    return print_unsupported_vm_mode(mode);
                }
                continue;
            }
            if (program_path == NULL && arg[0] != '-') {
                program_path = arg;
                continue;
            }
            if (program_path == NULL && arg[0] == '-') {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Unsupported flag for native debug profile.\" nodeId=argv)\n");
                return 2;
            }
            app_arg_start = i;
            break;
        }
        if (program_path == NULL) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Missing debug profile input.\" nodeId=argv)\n");
            return 2;
        }
        if (app_arg_start < 0 || app_arg_start > argc) {
            app_arg_start = argc;
        }
        if (!dirname_of(out_path, report_dir, sizeof(report_dir))) {
            if (snprintf(report_dir, sizeof(report_dir), "%s", ".") >= (int)sizeof(report_dir)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Failed to prepare debug profile output directory.\" nodeId=out)\n");
                return 2;
            }
        }
        if (!ensure_directory_recursive(report_dir)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Failed to prepare debug profile output directory.\" nodeId=out)\n");
            return 2;
        }

        if (!ends_with(program_path, ".aibc1") && !ends_with(program_path, ".aibundle")) {
            if (!derive_build_out_dir(program_path, build_out_dir, sizeof(build_out_dir))) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Native build failed: could not derive output directory.\" nodeId=build)\n");
                return 2;
            }
            if (build_input_to_aibc1(program_path, build_out_dir, built_app_path, sizeof(built_app_path), use_cache) != 1) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Native build failed: %s\" nodeId=build)\n",
                    native_build_error());
                return 2;
            }
            program_path = built_app_path;
        }

        for (i = 0; i < iterations; i += 1) {
            int64_t rss_kb = 0;
            rc = run_via_resolved_input(
                program_path,
                (const char* const*)&argv[app_arg_start],
                (size_t)(argc - app_arg_start),
                NULL);
            if (rc != 0) {
                break;
            }
            if (!native_get_current_rss_kb(&rss_kb)) {
                rc = 3;
                break;
            }
            rss_series[sample_count] = rss_kb;
            sample_count += 1;
        }

        if (rc == 0 && sample_count > 0) {
            int64_t growth_kb = rss_series[sample_count - 1] - rss_series[0];
            if (growth_kb > max_growth_kb) {
                status_fail = 1;
            }
        }

        report = fopen(out_path, "wb");
        if (report == NULL) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Failed to write debug profile report.\" nodeId=out)\n");
            return 2;
        }
        if (rc != 0) {
            (void)snprintf(status_text, sizeof(status_text), "%s", "error");
        } else if (status_fail) {
            (void)snprintf(status_text, sizeof(status_text), "%s", "fail");
        } else {
            (void)snprintf(status_text, sizeof(status_text), "%s", "pass");
        }
        fprintf(report, "format = \"aivm_debug_mem_v1\"\n");
        fprintf(report, "target = \"%s\"\n", program_path);
        fprintf(report, "iterations = %d\n", iterations);
        fprintf(report, "completed_iterations = %d\n", sample_count);
        fprintf(report, "max_growth_kb_threshold = %lld\n", (long long)max_growth_kb);
        fprintf(report, "status = \"%s\"\n", status_text);
        fprintf(report, "run_rc = %d\n", rc);
        if (sample_count > 0) {
            int64_t first_rss = rss_series[0];
            int64_t last_rss = rss_series[sample_count - 1];
            int64_t growth_kb = last_rss - first_rss;
            fprintf(report, "first_rss_kb = %lld\n", (long long)first_rss);
            fprintf(report, "last_rss_kb = %lld\n", (long long)last_rss);
            fprintf(report, "rss_growth_kb = %lld\n", (long long)growth_kb);
            fprintf(report, "rss_series_kb = [");
            for (i = 0; i < sample_count; i += 1) {
                if (i > 0) {
                    fprintf(report, ", ");
                }
                fprintf(report, "%lld", (long long)rss_series[i]);
            }
            fprintf(report, "]\n");
        } else {
            fprintf(report, "first_rss_kb = 0\n");
            fprintf(report, "last_rss_kb = 0\n");
            fprintf(report, "rss_growth_kb = 0\n");
            fprintf(report, "rss_series_kb = []\n");
        }
        fclose(report);
        if (rc != 0) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Native debug profile execution failed.\" nodeId=profile)\n");
            return 3;
        }
        if (status_fail) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Native debug profile exceeded RSS growth threshold.\" nodeId=profile)\n");
            return 1;
        }
        printf("Ok#ok1(type=string value=\"%s\")\n", out_path);
        return 0;
    }
    if (argc >= 4 && strcmp(argv[2], "disasm") == 0) {
        const char* input = argv[3];
        char resolved_program[PATH_MAX];
        char source_aos[PATH_MAX];
        unsigned char* bytes = NULL;
        size_t byte_count = 0U;
        AivmProgram program;
        AivmProgramLoadResult load_result;
        size_t start = 0U;
        size_t end = 0U;
        int parsed_index = 0;
        size_t i = 0U;
        if (input == NULL || input[0] == '\0') {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Missing debug disasm input.\" nodeId=argv)\n");
            return 2;
        }
        if (argc >= 5) {
            if (!parse_nonnegative_int(argv[4], &parsed_index) || parsed_index < 0) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid debug disasm start index.\" nodeId=argv)\n");
                return 2;
            }
            start = (size_t)parsed_index;
        }
        if (argc >= 6) {
            if (!parse_nonnegative_int(argv[5], &parsed_index) || parsed_index < 0) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid debug disasm end index.\" nodeId=argv)\n");
                return 2;
            }
            end = (size_t)parsed_index;
        }
        aivm_program_init(&program, NULL, 0U);
        if (ends_with(input, ".aos") && resolve_input_to_aos(input, source_aos, sizeof(source_aos))) {
            if (!parse_bytecode_aos_to_program_file(source_aos, &program, 0) &&
                !parse_simple_program_aos_to_program_file(source_aos, &program)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Could not compile source input for debug disasm.\" nodeId=disasm)\n");
                aivm_program_clear(&program);
                return 2;
            }
        } else {
            if (!resolve_input_to_aibc1(input, resolved_program, sizeof(resolved_program))) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Could not resolve .aibc1 input for debug disasm.\" nodeId=argv)\n");
                return 2;
            }
            if (!read_binary_file(resolved_program, &bytes, &byte_count)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Could not read .aibc1 input for debug disasm.\" nodeId=argv)\n");
                return 2;
            }
            load_result = aivm_program_load_aibc1(bytes, byte_count, &program);
            free(bytes);
            if (load_result.status != AIVM_PROGRAM_OK) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"AiBC1 load failed for debug disasm.\" detail=\"%s\" nodeId=disasm)\n",
                    aivm_program_status_code(load_result.status));
                aivm_program_clear(&program);
                return 2;
            }
        }
        if (program.instruction_count == 0U) {
            printf("Ok#ok1(type=string value=\"\")\n");
            aivm_program_clear(&program);
            return 0;
        }
        if (argc < 6) {
            end = start + 32U;
        }
        if (start >= program.instruction_count) {
            start = program.instruction_count - 1U;
        }
        if (end >= program.instruction_count) {
            end = program.instruction_count - 1U;
        }
        for (i = start; i <= end && i < program.instruction_count; i += 1U) {
            const AivmInstruction* inst = &program.instructions[i];
            printf("%llu\t%s\t%lld\n",
                (unsigned long long)i,
                aivm_opcode_name(inst->opcode),
                (long long)inst->operand_int);
        }
        aivm_program_clear(&program);
        return 0;
    }
    if (argc >= 4 && strcmp(argv[2], "dns") == 0) {
        const char* host = argv[3];
        uint16_t port = 443U;
        int parsed_port = 0;
        struct sockaddr_in addr;
        char error_text[256];
        char ip_text[INET_ADDRSTRLEN];
        if (argc >= 5) {
            if (!parse_int(argv[4], &parsed_port) || parsed_port > 65535) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid debug dns port.\" nodeId=argv)\n");
                return 2;
            }
            port = (uint16_t)parsed_port;
        }
        if (host == NULL || host[0] == '\0') {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Missing debug dns host.\" nodeId=argv)\n");
            return 2;
        }
        error_text[0] = '\0';
        memset(&addr, 0, sizeof(addr));
        memset(ip_text, 0, sizeof(ip_text));
        if (!native_net_resolve_ipv4_detail(host, port, SOCK_STREAM, 0, &addr, error_text, sizeof(error_text))) {
            fprintf(stderr,
                "Err#err1(code=NET001 message=\"DNS lookup failed.\" detail=\"%s\" nodeId=dns)\n",
                error_text[0] == '\0' ? "dns_failed" : error_text);
            return 1;
        }
        if (inet_ntop(AF_INET, &addr.sin_addr, ip_text, sizeof(ip_text)) == NULL) {
            fprintf(stderr,
                "Err#err1(code=NET001 message=\"DNS lookup succeeded but address formatting failed.\" nodeId=dns)\n");
            return 1;
        }
        printf("Ok#ok1(type=string value=\"%s\")\n", ip_text);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[2], "run") == 0) {
        return handle_debug_run_mode(argc, argv, 3, NULL, 0, NULL, 0);
    }
    if (argc >= 4 && strcmp(argv[2], "trace") == 0 && strcmp(argv[3], "run") == 0) {
        return handle_debug_run_mode(argc, argv, 4, "trace", 0, NULL, 0);
    }
    if (argc >= 4 && strcmp(argv[2], "capture") == 0 && strcmp(argv[3], "run") == 0) {
        return handle_debug_run_mode(argc, argv, 4, "trace", 1, ".artifacts/debug/native-capture", 0);
    }
    if (argc >= 4 && strcmp(argv[2], "interact") == 0 && strcmp(argv[3], "run") == 0) {
        return handle_debug_run_mode(argc, argv, 4, "trace", 0, NULL, 1);
    }
    fprintf(stderr,
        "Err#err1(code=DEV008 message=\"Native debug supports: debug run <program>, debug trace run <program>, debug capture run <program>, debug interact run <program>, debug profile <program>, debug dns <host> [port], debug disasm <program.aibc1|program.aos> [start] [end]\" nodeId=command)\n");
    return 2;
}

static AIRUN_MAYBE_UNUSED int handle_repl(void)
{
    char line[1024];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t n = strlen(line);
        while (n > 0U && (line[n - 1U] == '\n' || line[n - 1U] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0U) {
            continue;
        }
        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            return 0;
        }
        if (strcmp(line, "version") == 0) {
            printf("ailang-native-c version=%s channel=%s commit=%s abi=%u\n",
                AILANG_BUILD_VERSION,
                AILANG_BUILD_CHANNEL,
                AILANG_BUILD_COMMIT,
                aivm_c_abi_version());
            continue;
        }
        if (starts_with(line, "run ")) {
        return run_via_resolved_input(line + 4, NULL, 0U, NULL);
        }

        fprintf(stderr,
            "Err#err1(code=DEV008 message=\"Unsupported repl command.\" nodeId=repl)\n");
        return 2;
    }

    return 0;
}

static int parse_int(const char* text, int* out)
{
    char* end = NULL;
    long v;

    if (text == NULL || out == NULL) {
        return 0;
    }

    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || v <= 0L || v > 100000000L) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int parse_nonnegative_int(const char* text, int* out)
{
    char* end = NULL;
    long v;

    if (text == NULL || out == NULL) {
        return 0;
    }

    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < 0L || v > 100000000L) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int native_get_current_rss_kb(int64_t* out_kb)
{
    if (out_kb == NULL) {
        return 0;
    }
#ifdef _WIN32
    {
        PROCESS_MEMORY_COUNTERS counters;
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
            return 0;
        }
        *out_kb = (int64_t)(counters.WorkingSetSize / 1024U);
        return 1;
    }
#elif defined(__APPLE__)
    {
        struct mach_task_basic_info info;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) {
            return 0;
        }
        *out_kb = (int64_t)(info.resident_size / 1024U);
        return 1;
    }
#else
    {
        FILE* f = fopen("/proc/self/statm", "rb");
        long rss_pages = 0L;
        long ignored_pages = 0L;
        long page_size = sysconf(_SC_PAGESIZE);
        if (f == NULL) {
            return 0;
        }
        if (fscanf(f, "%ld %ld", &ignored_pages, &rss_pages) != 2) {
            fclose(f);
            return 0;
        }
        fclose(f);
        if (page_size <= 0L) {
            return 0;
        }
        *out_kb = (int64_t)(((uint64_t)rss_pages * (uint64_t)page_size) / 1024U);
        return 1;
    }
#endif
}

static int bench_syscall_sink(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    (void)arg_count;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int bench_execute_program_iterations(const AivmProgram* program, int iterations, uint64_t* out_ticks)
{
    AivmSyscallBinding bindings[3];
    int i;
    uint64_t ticks = 0U;

    if (program == NULL || out_ticks == NULL || iterations <= 0) {
        return 0;
    }

    bindings[0].target = "sys.stdout.writeLine";
    bindings[0].handler = bench_syscall_sink;
    bindings[1].target = "io.print";
    bindings[1].handler = bench_syscall_sink;
    bindings[2].target = "io.write";
    bindings[2].handler = bench_syscall_sink;

    for (i = 0; i < iterations; i += 1) {
        AivmCResult result = aivm_c_execute_program_with_syscalls(program, bindings, 3U);
        if (!result.loaded || result.load_status != AIVM_PROGRAM_OK || !result.ok || result.status == AIVM_VM_STATUS_ERROR) {
            return 0;
        }
        ticks += (uint64_t)program->instruction_count;
    }

    *out_ticks = ticks;
    return 1;
}

static AIRUN_MAYBE_UNUSED int handle_bench(int argc, char** argv)
{
    int iterations = 10000;
    int human = 0;
    int i;
    int failures = 0;
    AivmInstruction loop_instructions[2];
    uint64_t runtime_loop_ticks = 0U;
    uint64_t compiler_program_ticks = 0U;
    uint64_t compiler_bytecode_ticks = 0U;
    uint64_t app_hello_ticks = 0U;
    uint64_t app_echo_ticks = 0U;
    uint64_t app_bundle_ticks = 0U;
    const char* compiler_program_status = "ok";
    const char* compiler_bytecode_status = "ok";
    const char* runtime_loop_status = "ok";
    const char* app_hello_status = "ok";
    const char* app_echo_status = "ok";
    const char* app_bundle_status = "ok";
    static const char* program_bench_source =
        "Program#p1 {\n"
        "  Let#l1(name=message) { Lit#s1(value=\"Hello from VM\") }\n"
        "  Call#c1(target=io.print) { Var#v1(name=message) }\n"
        "}";
    static const char* bytecode_bench_source =
        "Bytecode#bc1(magic=\"AIBC\" format=\"AiBC1\" version=2 flags=0) {\n"
        "  Const#k0(kind=string value=\"hello\")\n"
        "  Func#f1(name=main params=\"argv\" locals=\"\") {\n"
        "    Inst#i1(op=HALT)\n"
        "  }\n"
        "}";
    static const char* app_hello_source =
        "Program#p1 {\n"
        "  Let#l1(name=message) { Lit#s1(value=\"Hello from VM\") }\n"
        "  Call#c1(target=io.print) { Var#v1(name=message) }\n"
        "}";
    static const char* app_echo_source =
        "Program#p1 {\n"
        "  Let#l1(name=line) { Lit#s0(value=\"vm-echo\") }\n"
        "  Let#l2(name=out) {\n"
        "    StrConcat#s1 {\n"
        "      Lit#s2(value=\"echo:\")\n"
        "      Var#v1(name=line)\n"
        "    }\n"
        "  }\n"
        "  Call#c2(target=io.print) { Var#v2(name=out) }\n"
        "}";
    static const char* app_bundle_source =
        "Bytecode#bc1(flags=0 format=\"AiBC1\" magic=\"AIBC\" version=2) {\n"
        "  Const#k0(kind=int value=0)\n"
        "  Func#f_main(locals=\"argv,start\" name=main params=\"argv\") {\n"
        "    Inst#i0(a=0 op=CONST)\n"
        "    Inst#i1(a=1 op=STORE_LOCAL)\n"
        "    Inst#i2(op=RETURN)\n"
        "  }\n"
        "}";

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--human") == 0) {
            human = 1;
            continue;
        }
        if (strcmp(argv[i], "--iterations") == 0) {
            if (i + 1 >= argc || !parse_int(argv[i + 1], &iterations)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Invalid --iterations value.\" nodeId=argv)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Unsupported bench flag.\" nodeId=argv)\n");
        return 2;
    }

    loop_instructions[0].opcode = AIVM_OP_PUSH_INT;
    loop_instructions[0].operand_int = 1;
    loop_instructions[1].opcode = AIVM_OP_HALT;
    loop_instructions[1].operand_int = 0;

    for (i = 0; i < iterations; i += 1) {
        AivmCResult result = aivm_c_execute_instructions(loop_instructions, 2U);
        if (!result.ok || result.status == AIVM_VM_STATUS_ERROR) {
            runtime_loop_status = "fail";
            failures += 1;
            break;
        }
        runtime_loop_ticks += 2U;
    }

    for (i = 0; i < iterations; i += 1) {
        AivmProgram program;
        if (!parse_simple_program_aos_to_program_text(program_bench_source, &program)) {
            compiler_program_status = "fail";
            failures += 1;
            break;
        }
        compiler_program_ticks += (uint64_t)(program.instruction_count + program.constant_count);
    }

    for (i = 0; i < iterations; i += 1) {
        AivmProgram program;
        if (!parse_bytecode_aos_to_program_text(bytecode_bench_source, &program, 0)) {
            compiler_bytecode_status = "fail";
            failures += 1;
            break;
        }
        compiler_bytecode_ticks += (uint64_t)(program.instruction_count + program.constant_count);
    }

    {
        AivmProgram hello_program;
        if (!parse_simple_program_aos_to_program_text(app_hello_source, &hello_program) ||
            !bench_execute_program_iterations(&hello_program, iterations, &app_hello_ticks)) {
            app_hello_status = "fail";
            failures += 1;
        }
    }

    {
        AivmProgram echo_program;
        if (!parse_simple_program_aos_to_program_text(app_echo_source, &echo_program) ||
            !bench_execute_program_iterations(&echo_program, iterations, &app_echo_ticks)) {
            app_echo_status = "fail";
            failures += 1;
        }
    }

    {
        AivmProgram bundle_program;
        if (!parse_bytecode_aos_to_program_text(app_bundle_source, &bundle_program, 0) ||
            !bench_execute_program_iterations(&bundle_program, iterations, &app_bundle_ticks)) {
            app_bundle_status = "fail";
            failures += 1;
        }
    }

    if (human) {
        printf("name\tstatus\tunit\tvalue\n");
        printf("runtime_loop\t%s\tvm_ticks\t%llu\n", runtime_loop_status, (unsigned long long)runtime_loop_ticks);
        printf("compiler_parse_program\t%s\tvm_ticks\t%llu\n", compiler_program_status, (unsigned long long)compiler_program_ticks);
        printf("compiler_parse_bytecode\t%s\tvm_ticks\t%llu\n", compiler_bytecode_status, (unsigned long long)compiler_bytecode_ticks);
        printf("app_run_hello\t%s\tvm_ticks\t%llu\n", app_hello_status, (unsigned long long)app_hello_ticks);
        printf("app_run_echo\t%s\tvm_ticks\t%llu\n", app_echo_status, (unsigned long long)app_echo_ticks);
        printf("app_run_bundle\t%s\tvm_ticks\t%llu\n", app_bundle_status, (unsigned long long)app_bundle_ticks);
    } else {
        printf("Ok#ok1(type=int value=%d)\n", iterations - failures);
    }
    if (failures > 0) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Native benchmark VM execution failed.\" nodeId=bench)\n");
        return 3;
    }
    return 0;
}

static int derive_build_out_dir(const char* program_input, char* out_dir, size_t out_dir_len)
{
    if (program_input == NULL || out_dir == NULL || out_dir_len == 0U) {
        return 0;
    }
    if (directory_exists(program_input)) {
        return snprintf(out_dir, out_dir_len, "%s", program_input) < (int)out_dir_len;
    }
    if (ends_with(program_input, "project.aiproj") ||
        ends_with(program_input, ".aos") ||
        ends_with(program_input, ".aibc1")) {
        return dirname_of(program_input, out_dir, out_dir_len);
    }
    return snprintf(out_dir, out_dir_len, ".") < (int)out_dir_len;
}

static char g_native_build_error[256];

static void set_native_build_error(const char* message)
{
    if (message == NULL) {
        message = "unknown";
    }
    (void)snprintf(g_native_build_error, sizeof(g_native_build_error), "%s", message);
}

static void set_native_build_errorf(const char* fmt, ...)
{
    va_list args;
    if (fmt == NULL) {
        set_native_build_error("unknown");
        return;
    }
    va_start(args, fmt);
    (void)vsnprintf(g_native_build_error, sizeof(g_native_build_error), fmt, args);
    va_end(args);
}

static const char* native_build_error(void)
{
    if (g_native_build_error[0] == '\0') {
        return "unknown";
    }
    return g_native_build_error;
}

static int build_input_to_aibc1(
    const char* program_input,
    const char* out_dir,
    char* out_app_path,
    size_t out_app_path_len,
    int use_cache)
{
    char resolved_program[PATH_MAX];
    char source_aos[PATH_MAX];
    char cache_root[PATH_MAX];
    char cache_key[40];
    char cache_key_dir[PATH_MAX];
    char cache_app_path[PATH_MAX];
    AivmProgram program;
    int explicit_aibc1_input;
    int explicit_aos_input;

    g_native_build_error[0] = '\0';

    if (program_input == NULL || out_dir == NULL || out_app_path == NULL || out_app_path_len == 0U) {
        set_native_build_error("invalid build arguments");
        return 0;
    }
    if (!ensure_directory(out_dir)) {
        set_native_build_error("failed to create output directory");
        return 0;
    }
    if (!join_path(out_dir, "app.aibc1", out_app_path, out_app_path_len)) {
        set_native_build_error("output path overflow");
        return 0;
    }

    explicit_aibc1_input = ends_with(program_input, ".aibc1");
    explicit_aos_input = ends_with(program_input, ".aos");
    g_native_build_project_dir[0] = '\0';
    {
        char manifest_path[PATH_MAX];
        if (resolve_manifest_path_for_input(program_input, manifest_path, sizeof(manifest_path))) {
            (void)dirname_of(manifest_path, g_native_build_project_dir, sizeof(g_native_build_project_dir));
        }
    }
    if (explicit_aibc1_input) {
        if (!resolve_input_to_aibc1(program_input, resolved_program, sizeof(resolved_program))) {
            set_native_build_error("could not resolve explicit .aibc1 input");
            return 0;
        }
        if (strcmp(resolved_program, out_app_path) == 0) {
            return 1;
        }
        if (!copy_file(resolved_program, out_app_path)) {
            set_native_build_error("failed to copy resolved app.aibc1");
            return 0;
        }
        return 1;
    }

    /* Prefer an existing canonical bytecode artifact over recompiling source through
       the native simple compiler for project-level inputs. Explicit .aos builds must
       rebuild that source file instead of silently copying a sibling app.aibc1. */
    if (!explicit_aos_input &&
        resolve_input_to_aibc1(program_input, resolved_program, sizeof(resolved_program)) &&
        file_exists(resolved_program) &&
        is_supported_aibc1_file(resolved_program)) {
        if (same_file_path(resolved_program, out_app_path)) {
            resolved_program[0] = '\0';
        } else if (!copy_file(resolved_program, out_app_path)) {
            set_native_build_error("failed to copy resolved sibling app.aibc1");
            return 0;
        }
        if (resolved_program[0] != '\0') {
            return 1;
        }
    }

    if (!resolve_input_to_aos(program_input, source_aos, sizeof(source_aos))) {
        set_native_build_error("could not resolve source .aos from input");
        return 0;
    }

    if (use_cache &&
        compute_source_graph_cache_key(source_aos, cache_key, sizeof(cache_key)) &&
        ensure_cache_root_for_source(source_aos, cache_root, sizeof(cache_root)) &&
        join_path(cache_root, cache_key, cache_key_dir, sizeof(cache_key_dir)) &&
        ensure_directory(cache_key_dir) &&
        join_path(cache_key_dir, "app.aibc1", cache_app_path, sizeof(cache_app_path)) &&
        file_exists(cache_app_path) &&
        is_supported_aibc1_file(cache_app_path)) {
        if (strcmp(cache_app_path, out_app_path) == 0 || copy_file(cache_app_path, out_app_path)) {
            return 1;
        }
    }

    if (parse_bytecode_aos_to_program_file(source_aos, &program, 0) ||
        parse_simple_program_aos_to_program_file(source_aos, &program)) {
        if (!write_program_as_aibc1(&program, out_app_path)) {
            set_native_build_errorf(
                "failed writing app.aibc1 (inst=%llu const=%llu)",
                (unsigned long long)program.instruction_count,
                (unsigned long long)program.constant_count);
            return 0;
        }
        if (use_cache &&
            compute_source_graph_cache_key(source_aos, cache_key, sizeof(cache_key)) &&
            ensure_cache_root_for_source(source_aos, cache_root, sizeof(cache_root)) &&
            join_path(cache_root, cache_key, cache_key_dir, sizeof(cache_key_dir)) &&
            ensure_directory(cache_key_dir) &&
            join_path(cache_key_dir, "app.aibc1", cache_app_path, sizeof(cache_app_path))) {
            (void)copy_file(out_app_path, cache_app_path);
        }
        return 1;
    }

    {
        const char* detail = simple_last_error();
        if (detail == NULL || strcmp(detail, "unknown") == 0) {
            set_native_build_errorf("native simple compile failed for %s", source_aos);
        } else {
            set_native_build_error(detail);
        }
    }
    return 0;
}

static int delete_directory_recursive_portable(const char* path)
{
    if (path == NULL) {
        return 0;
    }
    if (!directory_exists(path)) {
        return 1;
    }
#ifdef _WIN32
    return native_delete_dir_recursive_windows(path);
#else
    return native_delete_dir_recursive_posix(path);
#endif
}

static int resolve_project_dir_for_cache(const char* input, char* out_project_dir, size_t out_project_dir_len)
{
    char source_aos[PATH_MAX];
    char manifest_path[PATH_MAX];
    char source_dir[PATH_MAX];
    char parent_dir[PATH_MAX];
    if (input == NULL || out_project_dir == NULL || out_project_dir_len == 0U) {
        return 0;
    }
    if (directory_exists(input)) {
        if (join_path(input, "project.aiproj", manifest_path, sizeof(manifest_path)) &&
            file_exists(manifest_path)) {
            return snprintf(out_project_dir, out_project_dir_len, "%s", input) < (int)out_project_dir_len;
        }
        if (dirname_of(input, parent_dir, sizeof(parent_dir)) &&
            join_path(parent_dir, "project.aiproj", manifest_path, sizeof(manifest_path)) &&
            file_exists(manifest_path)) {
            return snprintf(out_project_dir, out_project_dir_len, "%s", parent_dir) < (int)out_project_dir_len;
        }
        return snprintf(out_project_dir, out_project_dir_len, "%s", input) < (int)out_project_dir_len;
    }
    if (ends_with(input, "project.aiproj") ||
        ends_with(input, ".aos") ||
        ends_with(input, ".aibc1") ||
        ends_with(input, ".aibundle")) {
        return dirname_of(input, out_project_dir, out_project_dir_len);
    }
    if (resolve_input_to_aos(input, source_aos, sizeof(source_aos))) {
        if (!dirname_of(source_aos, source_dir, sizeof(source_dir))) {
            return 0;
        }
        if (join_path(source_dir, "project.aiproj", manifest_path, sizeof(manifest_path)) &&
            file_exists(manifest_path)) {
            return snprintf(out_project_dir, out_project_dir_len, "%s", source_dir) < (int)out_project_dir_len;
        }
        if (dirname_of(source_dir, parent_dir, sizeof(parent_dir)) &&
            join_path(parent_dir, "project.aiproj", manifest_path, sizeof(manifest_path)) &&
            file_exists(manifest_path)) {
            return snprintf(out_project_dir, out_project_dir_len, "%s", parent_dir) < (int)out_project_dir_len;
        }
        return snprintf(out_project_dir, out_project_dir_len, "%s", source_dir) < (int)out_project_dir_len;
    }
    return snprintf(out_project_dir, out_project_dir_len, ".") < (int)out_project_dir_len;
}

static AIRUN_MAYBE_UNUSED int handle_clean(int argc, char** argv)
{
    const char* target = ".";
    int target_set = 0;
    char project_dir[PATH_MAX];
    char toolchain_dir[PATH_MAX];
    char cache_dir[PATH_MAX];
    char ailang_cache_dir[PATH_MAX];
    int i;

    for (i = 2; i < argc; i += 1) {
        if (argv[i][0] == '-') {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Unsupported clean argument.\" nodeId=argv)\n");
            return 2;
        }
        if (!target_set) {
            target = argv[i];
            target_set = 1;
            continue;
        }
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Unsupported clean argument.\" nodeId=argv)\n");
        return 2;
    }

    if (!resolve_project_dir_for_cache(target, project_dir, sizeof(project_dir)) ||
        !join_path(project_dir, ".toolchain", toolchain_dir, sizeof(toolchain_dir)) ||
        !join_path(toolchain_dir, "cache", cache_dir, sizeof(cache_dir)) ||
        !join_path(cache_dir, "ailang", ailang_cache_dir, sizeof(ailang_cache_dir))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to resolve cache path.\" nodeId=clean)\n");
        return 2;
    }

    if (!directory_exists(ailang_cache_dir)) {
        printf("Ok#ok1(type=bool value=true)\n");
        return 0;
    }
    if (!delete_directory_recursive_portable(ailang_cache_dir)) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to delete ailang cache directory.\" nodeId=clean)\n");
        return 2;
    }
    printf("Ok#ok1(type=bool value=true)\n");
    return 0;
}

static AIRUN_MAYBE_UNUSED int handle_build(int argc, char** argv)
{
    const char* program_input = NULL;
    const char* out_dir = NULL;
    const char* build_target_input = NULL;
    char default_out[PATH_MAX];
    char app_path[PATH_MAX];
    char resolved_source_input[PATH_MAX];
    int use_cache = 1;
    int i;
    int build_rc;

    for (i = 2; i < argc; i += 1) {
        if (strcmp(argv[i], "--out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --out value.\" nodeId=argv)\n");
                return 2;
            }
            out_dir = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--no-cache") == 0) {
            use_cache = 0;
            continue;
        }
        if (program_input == NULL && argv[i][0] != '-') {
            program_input = argv[i];
            continue;
        }
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Unsupported build argument.\" nodeId=argv)\n");
        return 2;
    }

    if (program_input == NULL) {
        program_input = ".";
    }
    if (out_dir == NULL) {
        if (!derive_build_out_dir(program_input, default_out, sizeof(default_out))) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Failed to derive build output directory.\" nodeId=outDir)\n");
            return 2;
        }
        out_dir = default_out;
    }

    (void)resolved_source_input;
    build_target_input = program_input;

    build_rc = build_input_to_aibc1(build_target_input, out_dir, app_path, sizeof(app_path), use_cache);
    if (build_rc == 1) {
        printf("Ok#ok1(type=string value=\"%s\")\n", app_path);
        return 0;
    }
    fprintf(stderr,
        "Err#err1(code=RUN001 message=\"Build failed to emit app.aibc1 (%s).\" nodeId=build)\n",
        native_build_error());
    return 2;
}

static int compare_test_path_entries(const void* left, const void* right)
{
    const char* left_text = (const char*)left;
    const char* right_text = (const char*)right;
    return strcmp(left_text, right_text);
}

static int collect_test_aos_files(const char* dir_path, char paths[][PATH_MAX], size_t* count, size_t capacity)
{
    if (dir_path == NULL || paths == NULL || count == NULL) {
        return 0;
    }
    if (!directory_exists(dir_path)) {
        return 1;
    }
#ifdef _WIN32
    {
        char pattern[PATH_MAX];
        WIN32_FIND_DATAA data;
        HANDLE h;
        if (!join_path(dir_path, "*.aos", pattern, sizeof(pattern))) {
            return 0;
        }
        h = FindFirstFileA(pattern, &data);
        if (h == INVALID_HANDLE_VALUE) {
            return 1;
        }
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                if (*count >= capacity ||
                    !join_path(dir_path, data.cFileName, paths[*count], PATH_MAX)) {
                    FindClose(h);
                    return 0;
                }
                *count += 1U;
            }
        } while (FindNextFileA(h, &data));
        FindClose(h);
    }
#else
    {
        DIR* dir = opendir(dir_path);
        struct dirent* ent;
        if (dir == NULL) {
            return 1;
        }
        while ((ent = readdir(dir)) != NULL) {
            char child[PATH_MAX];
            if (!ends_with(ent->d_name, ".aos")) {
                continue;
            }
            if (*count >= capacity ||
                !join_path(dir_path, ent->d_name, child, sizeof(child)) ||
                !file_exists(child) ||
                snprintf(paths[*count], PATH_MAX, "%s", child) >= PATH_MAX) {
                closedir(dir);
                return 0;
            }
            *count += 1U;
        }
        closedir(dir);
    }
#endif
    return 1;
}

static int append_project_tests_from_dir(
    const char* dir_path,
    char paths[][PATH_MAX],
    size_t* count,
    size_t capacity)
{
    char project_manifest[PATH_MAX];
    if (dir_path == NULL || paths == NULL || count == NULL) {
        return 0;
    }
    if (!directory_exists(dir_path)) {
        return 1;
    }
    if (!join_path(dir_path, "project.aiproj", project_manifest, sizeof(project_manifest))) {
        return 0;
    }
    if (file_exists(project_manifest)) {
        if (*count >= capacity || snprintf(paths[*count], PATH_MAX, "%s", project_manifest) >= PATH_MAX) {
            return 0;
        }
        *count += 1U;
        return 1;
    }
    return collect_test_aos_files(dir_path, paths, count, capacity);
}

static int run_one_project_test(const char* test_path, const char* out_root, size_t index, int use_cache)
{
    char out_dir[PATH_MAX];
    char app_path[PATH_MAX];
    int build_rc;
    int run_rc;
    if (test_path == NULL || out_root == NULL) {
        return 2;
    }
    if (snprintf(out_dir, sizeof(out_dir), "%s%c%04llu", out_root, AIVM_PATH_SEP, (unsigned long long)index) >= (int)sizeof(out_dir) ||
        !ensure_directory_recursive(out_dir)) {
        fprintf(stderr,
            "Err#err1(code=AILANG022 message=\"Failed to create test output directory.\" nodeId=test)\n");
        return 2;
    }
    build_rc = build_input_to_aibc1(test_path, out_dir, app_path, sizeof(app_path), use_cache);
    if (build_rc != 1) {
        fprintf(stderr,
            "Err#err1(code=AILANG022 message=\"Test build failed: %s\" nodeId=test)\n",
            native_build_error());
        return 2;
    }
    run_rc = run_native_aibc1(app_path, NULL, 0U, NULL);
    if (run_rc != 0) {
        fprintf(stderr,
            "Err#err1(code=AILANG022 message=\"Test failed.\" nodeId=test)\n");
        return run_rc;
    }
    return 0;
}

static AIRUN_MAYBE_UNUSED int handle_test(int argc, char** argv)
{
    const char* target = ".";
    int target_set = 0;
    int use_cache = 1;
    int i;
    char project_dir[PATH_MAX];
    char lower_tests[PATH_MAX];
    char upper_tests[PATH_MAX];
    char toolchain_dir[PATH_MAX];
    char out_root[PATH_MAX];
    char test_paths[256][PATH_MAX];
    size_t test_count = 0U;
    size_t test_index;

    for (i = 2; i < argc; i += 1) {
        if (strcmp(argv[i], "--no-cache") == 0) {
            use_cache = 0;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr,
                "Err#err1(code=AILANG021 message=\"Unsupported test argument.\" nodeId=argv)\n");
            return 2;
        }
        if (!target_set) {
            target = argv[i];
            target_set = 1;
            continue;
        }
        fprintf(stderr,
            "Err#err1(code=AILANG021 message=\"Unsupported test argument.\" nodeId=argv)\n");
        return 2;
    }

    if (!resolve_project_dir_for_cache(target, project_dir, sizeof(project_dir)) ||
        !join_path(project_dir, "tests", lower_tests, sizeof(lower_tests)) ||
        !join_path(project_dir, "Tests", upper_tests, sizeof(upper_tests)) ||
        !join_path(project_dir, ".toolchain", toolchain_dir, sizeof(toolchain_dir)) ||
        !join_path(toolchain_dir, "test", out_root, sizeof(out_root)) ||
        !ensure_directory_recursive(out_root)) {
        fprintf(stderr,
            "Err#err1(code=AILANG021 message=\"Failed to resolve project test paths.\" nodeId=test)\n");
        return 2;
    }

    if (!append_project_tests_from_dir(lower_tests, test_paths, &test_count, sizeof(test_paths) / sizeof(test_paths[0])) ||
        ((!directory_exists(lower_tests) || !directory_exists(upper_tests) || !same_file_path(lower_tests, upper_tests)) &&
         !append_project_tests_from_dir(upper_tests, test_paths, &test_count, sizeof(test_paths) / sizeof(test_paths[0])))) {
        fprintf(stderr,
            "Err#err1(code=AILANG021 message=\"Failed to discover project tests.\" nodeId=test)\n");
        return 2;
    }

    qsort(test_paths, test_count, sizeof(test_paths[0]), compare_test_path_entries);
    for (test_index = 0U; test_index < test_count; test_index += 1U) {
        int rc = run_one_project_test(test_paths[test_index], out_root, test_index + 1U, use_cache);
        if (rc != 0) {
            return rc;
        }
    }

    printf("Ok#ok1(type=int value=%llu)\n", (unsigned long long)test_count);
    return 0;
}

static AIRUN_MAYBE_UNUSED int handle_publish(int argc, char** argv)
{
    const char* program_input = NULL;
    const char* target = NULL;
    const char* runner = NULL;
    const char* artifact_type = NULL;
    const char* target_version = NULL;
    const char* out_dir = "dist";
    const char* wasm_profile = "spa";
    const char* wasm_fullstack_host_target_arg = NULL;
    int wasm_profile_explicit = 0;
    int target_option_count = 0;
    char* target_options[AILANG_CLI_MAX_TARGET_OPTIONS];
    char resolved_program[PATH_MAX];
    char source_aos[PATH_MAX];
    char artifact_dir[PATH_MAX];
    char runtime_bin[64];
    char runtime_web_bin[64];
    char runtime_web_wasm_bin[64];
    char publish_app_name[128];
    char publish_runtime_name[160];
    char runtime_src[PATH_MAX];
    char runtime_dst[PATH_MAX];
    char runtime_web_src[PATH_MAX];
    char runtime_web_wasm_src[PATH_MAX];
    char runtime_web_dst[PATH_MAX];
    char runtime_web_wasm_dst[PATH_MAX];
    char wasm_app_dst[PATH_MAX];
    char app_dst[PATH_MAX];
    char manifest_target[64];
    char manifest_wasm_fullstack_host_target[64];
    char wasm_fullstack_host_target[64];
    unsigned char* publish_app_bytes = NULL;
    size_t publish_app_size = 0U;
    AivmProgram publish_program;
    int i;

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --target value.\" nodeId=argv)\n");
                return 2;
            }
            target = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --out value.\" nodeId=argv)\n");
                return 2;
            }
            out_dir = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--runner") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --runner value.\" nodeId=argv)\n");
                return 2;
            }
            runner = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--type") == 0 || strcmp(argv[i], "--artifact-type") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing artifact type value.\" nodeId=argv)\n");
                return 2;
            }
            artifact_type = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--target-version") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --target-version value.\" nodeId=argv)\n");
                return 2;
            }
            target_version = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--wasm-profile") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --wasm-profile value.\" nodeId=argv)\n");
                return 2;
            }
            wasm_profile = argv[++i];
            wasm_profile_explicit = 1;
            continue;
        }
        if (strcmp(argv[i], "--wasm-fullstack-host-target") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing --wasm-fullstack-host-target value.\" nodeId=argv)\n");
                return 2;
            }
            wasm_fullstack_host_target_arg = argv[++i];
            continue;
        }
        if (package_target_option_takes_value(argv[i])) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Missing package target option value.\" nodeId=argv)\n");
                return 2;
            }
            if (target_option_count + 2 > AILANG_CLI_MAX_TARGET_OPTIONS) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Too many package target options.\" nodeId=argv)\n");
                return 2;
            }
            target_options[target_option_count++] = argv[i];
            target_options[target_option_count++] = argv[++i];
            continue;
        }
        if (package_target_option_is_inline(argv[i])) {
            if (target_option_count + 1 > AILANG_CLI_MAX_TARGET_OPTIONS) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Too many package target options.\" nodeId=argv)\n");
                return 2;
            }
            target_options[target_option_count++] = argv[i];
            continue;
        }
        if (program_input == NULL && argv[i][0] != '-') {
            program_input = argv[i];
            continue;
        }
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Unsupported publish argument.\" nodeId=argv)\n");
        return 2;
    }

    if (program_input == NULL) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Missing publish program path.\" nodeId=argv)\n");
        return 2;
    }

    if ((program_input != NULL && ends_with(program_input, ".aos")) ||
        !resolve_input_to_aibc1(program_input, resolved_program, sizeof(resolved_program))) {
        if (resolve_input_to_aos(program_input, source_aos, sizeof(source_aos))) {
            AivmProgram program;
            if (parse_bytecode_aos_to_program_file(source_aos, &program, 1)) {
                if (!ensure_directory(out_dir)) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Failed to create publish output directory.\" nodeId=outDir)\n");
                    return 2;
                }
                if (!join_path(out_dir, "app.aibc1", app_dst, sizeof(app_dst))) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"App destination path overflow.\" nodeId=publish)\n");
                    return 2;
                }
                if (!write_program_as_aibc1(&program, app_dst)) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Failed to emit native AiBC1 for publish.\" nodeId=publish)\n");
                    return 2;
                }
                resolved_program[0] = '\0';
            } else {
                if (parse_simple_program_aos_to_program_file(source_aos, &program)) {
                    if (!ensure_directory(out_dir)) {
                        fprintf(stderr,
                            "Err#err1(code=RUN001 message=\"Failed to create publish output directory.\" nodeId=outDir)\n");
                        return 2;
                    }
                    if (!join_path(out_dir, "app.aibc1", app_dst, sizeof(app_dst))) {
                        fprintf(stderr,
                            "Err#err1(code=RUN001 message=\"App destination path overflow.\" nodeId=publish)\n");
                        return 2;
                    }
                    if (!write_program_as_aibc1(&program, app_dst)) {
                        fprintf(stderr,
                            "Err#err1(code=RUN001 message=\"Failed to emit native AiBC1 for publish.\" nodeId=publish)\n");
                        return 2;
                    }
                    resolved_program[0] = '\0';
                } else {
                    if (source_file_looks_like_bytecode_aos(source_aos)) {
                        fprintf(stderr,
                            "Err#err1(code=DEV008 message=\"Native publish cannot encode this bytecode AOS shape yet. Build precompiled .aibc1.\" nodeId=program)\n");
                        return 2;
                    }
                    fprintf(stderr,
                        "Err#err1(code=DEV008 message=\"Publish needs prebuilt .aibc1 unless source is bytecode-style AOS.\" nodeId=program)\n");
                    return 2;
                }
            }
        } else {
            fprintf(stderr,
                "Err#err1(code=DEV008 message=\"Publish needs prebuilt .aibc1 unless source is bytecode-style AOS.\" nodeId=program)\n");
            return 2;
        }
    }

    if (target == NULL) {
        int manifest_target_rc = resolve_publish_target_from_manifest(program_input, manifest_target, sizeof(manifest_target));
        if (manifest_target_rc == -1) {
            fprintf(stderr,
                "Err#err1(code=VAL002 message=\"Project publishTargets must contain exactly one target or use --target explicitly.\" nodeId=project)\n");
            return 2;
        }
        if (manifest_target_rc == 1) {
            target = manifest_target;
        } else {
            target = host_rid();
        }
    }

    wasm_profile = wasm_profile_normalize(wasm_profile);
    if (!wasm_profile_is_valid(wasm_profile)) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Unsupported wasm profile.\" nodeId=wasmProfile)\n");
        return 2;
    }
    if (wasm_profile_explicit && strcmp(target, "wasm32") != 0) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"--wasm-profile requires --target wasm32.\" nodeId=wasmProfile)\n");
        return 2;
    }
    if (wasm_fullstack_host_target_arg != NULL && strcmp(target, "wasm32") != 0) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"--wasm-fullstack-host-target requires --target wasm32.\" nodeId=wasmFullstackHostTarget)\n");
        return 2;
    }
    if (wasm_fullstack_host_target_arg != NULL && strcmp(wasm_profile, "fullstack") != 0) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"--wasm-fullstack-host-target requires --wasm-profile fullstack.\" nodeId=wasmFullstackHostTarget)\n");
        return 2;
    }

    if (!parse_target_to_artifact(target, artifact_dir, sizeof(artifact_dir), runtime_bin, sizeof(runtime_bin))) {
        return delegate_package_target_operation(
            "publish",
            program_input,
            target,
            runner,
            artifact_type,
            target_version,
            out_dir,
            target_option_count,
            target_options,
            0,
            NULL);
    }
    if (!resolve_runtime_artifact_dir(artifact_dir, artifact_dir, sizeof(artifact_dir))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Runtime artifact path overflow.\" nodeId=publish)\n");
        return 2;
    }
    if (!derive_publish_app_name(program_input, publish_app_name, sizeof(publish_app_name))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to derive publish app name.\" nodeId=publish)\n");
        return 2;
    }
    if (strstr(runtime_bin, ".exe") != NULL) {
        if (snprintf(publish_runtime_name, sizeof(publish_runtime_name), "%s.exe", publish_app_name) >= (int)sizeof(publish_runtime_name)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Publish app name overflow.\" nodeId=publish)\n");
            return 2;
        }
    } else {
        if (snprintf(publish_runtime_name, sizeof(publish_runtime_name), "%s", publish_app_name) >= (int)sizeof(publish_runtime_name)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Publish app name overflow.\" nodeId=publish)\n");
            return 2;
        }
    }

    if (!ensure_directory(out_dir)) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to create publish output directory.\" nodeId=outDir)\n");
        return 2;
    }

    if (resolved_program[0] != '\0') {
        if (!join_path(out_dir, "app.aibc1", app_dst, sizeof(app_dst))) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"App destination path overflow.\" nodeId=publish)\n");
            return 2;
        }
        if (!copy_file(resolved_program, app_dst)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Failed to copy app bytecode for publish.\" nodeId=publish)\n");
            return 2;
        }
    }

    if (!join_path(out_dir, "app.aibc1", app_dst, sizeof(app_dst))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"App destination path overflow.\" nodeId=publish)\n");
        return 2;
    }
    if (!read_binary_file(app_dst, &publish_app_bytes, &publish_app_size) ||
        aivm_program_load_aibc1(publish_app_bytes, publish_app_size, &publish_program).status != AIVM_PROGRAM_OK) {
        free(publish_app_bytes);
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to inspect published app bytecode.\" nodeId=publish)\n");
        return 2;
    }
    free(publish_app_bytes);
    publish_app_bytes = NULL;

    if (strcmp(target, "wasm32") == 0) {
        if (!join_path(artifact_dir, runtime_bin, runtime_src, sizeof(runtime_src))) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Runtime source path overflow.\" nodeId=publish)\n");
            return 2;
        }
        if (snprintf(runtime_web_bin, sizeof(runtime_web_bin), "aivm-runtime-wasm32-web.mjs") >= (int)sizeof(runtime_web_bin)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Wasm runtime metadata overflow.\" nodeId=publish)\n");
            return 2;
        }
        if (snprintf(runtime_web_wasm_bin, sizeof(runtime_web_wasm_bin), "aivm-runtime-wasm32-web.wasm") >= (int)sizeof(runtime_web_wasm_bin)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Wasm runtime web binary metadata overflow.\" nodeId=publish)\n");
            return 2;
        }
        if (!join_path(artifact_dir, runtime_web_bin, runtime_web_src, sizeof(runtime_web_src))) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Wasm web runtime source path overflow.\" nodeId=publish)\n");
            return 2;
        }
        if (!join_path(artifact_dir, runtime_web_wasm_bin, runtime_web_wasm_src, sizeof(runtime_web_wasm_src))) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Wasm web runtime binary source path overflow.\" nodeId=publish)\n");
            return 2;
        }
        if (snprintf(publish_runtime_name, sizeof(publish_runtime_name), "%s.wasm", publish_app_name) >= (int)sizeof(publish_runtime_name)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Publish wasm runtime name overflow.\" nodeId=publish)\n");
            return 2;
        }
        if (!join_path(out_dir, publish_runtime_name, runtime_dst, sizeof(runtime_dst))) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Runtime destination path overflow.\" nodeId=publish)\n");
            return 2;
        }
        if (!copy_runtime_file(runtime_src, runtime_dst)) {
            fprintf(stderr,
                "Err#err1(code=RUN001 message=\"Failed to copy runtime for target RID. Build target runtime first.\" nodeId=publish)\n");
            return 2;
        }
        emit_wasm_profile_warnings(wasm_profile, &publish_program);

        if (strcmp(wasm_profile, "cli") == 0) {
            if (!emit_wasm_cli_launchers(out_dir, publish_runtime_name, publish_app_name)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Failed to emit wasm cli launchers.\" nodeId=publish)\n");
                return 2;
            }
        } else if (strcmp(wasm_profile, "spa") == 0) {
            if (!join_path(out_dir, runtime_web_bin, runtime_web_dst, sizeof(runtime_web_dst)) ||
                !join_path(out_dir, runtime_web_wasm_bin, runtime_web_wasm_dst, sizeof(runtime_web_wasm_dst)) ||
                !copy_runtime_file(runtime_web_src, runtime_web_dst) ||
                !copy_runtime_file(runtime_web_wasm_src, runtime_web_wasm_dst) ||
                !emit_wasm_spa_files(out_dir)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Failed to emit wasm web package files.\" nodeId=publish)\n");
                return 2;
            }
        } else if (strcmp(wasm_profile, "fullstack") == 0) {
            char www_dir[PATH_MAX];
            char fullstack_host_artifact_dir[PATH_MAX];
            char fullstack_host_runtime_bin[64];
            char fullstack_host_runtime_name[160];
            char fullstack_host_runtime_src[PATH_MAX];
            char fullstack_host_runtime_root_dst[PATH_MAX];
            char legacy_run_dst[PATH_MAX];
            char legacy_run_ps1_dst[PATH_MAX];
            char legacy_client_dir[PATH_MAX];
            char legacy_server_dir[PATH_MAX];
            wasm_fullstack_host_target[0] = '\0';
            if (wasm_fullstack_host_target_arg != NULL) {
                if (snprintf(wasm_fullstack_host_target, sizeof(wasm_fullstack_host_target), "%s", wasm_fullstack_host_target_arg) >= (int)sizeof(wasm_fullstack_host_target)) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Wasm fullstack host target RID overflow.\" nodeId=wasmFullstackHostTarget)\n");
                    return 2;
                }
            } else if (resolve_publish_wasm_fullstack_host_target_from_manifest(program_input, manifest_wasm_fullstack_host_target, sizeof(manifest_wasm_fullstack_host_target))) {
                if (snprintf(wasm_fullstack_host_target, sizeof(wasm_fullstack_host_target), "%s", manifest_wasm_fullstack_host_target) >= (int)sizeof(wasm_fullstack_host_target)) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Project wasm fullstack host target RID overflow.\" nodeId=project)\n");
                    return 2;
                }
            } else {
                if (snprintf(wasm_fullstack_host_target, sizeof(wasm_fullstack_host_target), "%s", host_rid()) >= (int)sizeof(wasm_fullstack_host_target)) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Host RID overflow.\" nodeId=publish)\n");
                    return 2;
                }
            }
            if (strcmp(wasm_fullstack_host_target, "wasm32") == 0 ||
                !parse_target_to_artifact(
                    wasm_fullstack_host_target,
                    fullstack_host_artifact_dir,
                    sizeof(fullstack_host_artifact_dir),
                    fullstack_host_runtime_bin,
                    sizeof(fullstack_host_runtime_bin))) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Unsupported wasm fullstack host target RID.\" nodeId=wasmFullstackHostTarget)\n");
                return 2;
            }
            if (!resolve_runtime_artifact_dir(
                    fullstack_host_artifact_dir,
                    fullstack_host_artifact_dir,
                    sizeof(fullstack_host_artifact_dir))) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Wasm fullstack host runtime artifact path overflow.\" nodeId=publish)\n");
                return 2;
            }
            if (strstr(fullstack_host_runtime_bin, ".exe") != NULL) {
                if (snprintf(fullstack_host_runtime_name, sizeof(fullstack_host_runtime_name), "%s.exe", publish_app_name) >= (int)sizeof(fullstack_host_runtime_name)) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Wasm fullstack runtime name overflow.\" nodeId=publish)\n");
                    return 2;
                }
            } else {
                if (snprintf(fullstack_host_runtime_name, sizeof(fullstack_host_runtime_name), "%s", publish_app_name) >= (int)sizeof(fullstack_host_runtime_name)) {
                    fprintf(stderr,
                        "Err#err1(code=RUN001 message=\"Wasm fullstack runtime name overflow.\" nodeId=publish)\n");
                    return 2;
                }
            }
            if (!join_path(out_dir, "www", www_dir, sizeof(www_dir)) ||
                !ensure_directory(www_dir)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Failed to create wasm fullstack www directory.\" nodeId=publish)\n");
                return 2;
            }
            if (!join_path(fullstack_host_artifact_dir, fullstack_host_runtime_bin, fullstack_host_runtime_src, sizeof(fullstack_host_runtime_src)) ||
                !join_path(out_dir, fullstack_host_runtime_name, fullstack_host_runtime_root_dst, sizeof(fullstack_host_runtime_root_dst)) ||
                !join_path(out_dir, "run", legacy_run_dst, sizeof(legacy_run_dst)) ||
                !join_path(out_dir, "run.ps1", legacy_run_ps1_dst, sizeof(legacy_run_ps1_dst)) ||
                !join_path(out_dir, "client", legacy_client_dir, sizeof(legacy_client_dir)) ||
                !join_path(out_dir, "server", legacy_server_dir, sizeof(legacy_server_dir)) ||
                !copy_runtime_file(fullstack_host_runtime_src, fullstack_host_runtime_root_dst) ||
                !remove_file_if_exists(legacy_run_dst) ||
                !remove_file_if_exists(legacy_run_ps1_dst)
#ifdef _WIN32
                || (directory_exists(legacy_client_dir) && !native_delete_dir_recursive_windows(legacy_client_dir))
                || (directory_exists(legacy_server_dir) && !native_delete_dir_recursive_windows(legacy_server_dir))
#else
                || (directory_exists(legacy_client_dir) && !native_delete_dir_recursive_posix(legacy_client_dir))
                || (directory_exists(legacy_server_dir) && !native_delete_dir_recursive_posix(legacy_server_dir))
#endif
                ) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Failed to prepare wasm fullstack package layout.\" nodeId=publish)\n");
                return 2;
            }
            if (!join_path(www_dir, "app.aibc1", wasm_app_dst, sizeof(wasm_app_dst)) ||
                !copy_file(app_dst, wasm_app_dst) ||
                !join_path(www_dir, publish_runtime_name, runtime_dst, sizeof(runtime_dst)) ||
                !copy_runtime_file(runtime_src, runtime_dst) ||
                !join_path(www_dir, runtime_web_bin, runtime_web_dst, sizeof(runtime_web_dst)) ||
                !join_path(www_dir, runtime_web_wasm_bin, runtime_web_wasm_dst, sizeof(runtime_web_wasm_dst)) ||
                !copy_runtime_file(runtime_web_src, runtime_web_dst) ||
                !copy_runtime_file(runtime_web_wasm_src, runtime_web_wasm_dst) ||
                !emit_wasm_fullstack_layout(out_dir)) {
                fprintf(stderr,
                    "Err#err1(code=RUN001 message=\"Failed to emit wasm fullstack package files.\" nodeId=publish)\n");
                return 2;
            }
        }

        printf("Ok#ok1(type=string value=\"publish-complete\")\n");
        return 0;
    }

    if (!join_path(artifact_dir, runtime_bin, runtime_src, sizeof(runtime_src))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Runtime source path overflow.\" nodeId=publish)\n");
        return 2;
    }
    if (!join_path(out_dir, publish_runtime_name, runtime_dst, sizeof(runtime_dst))) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Runtime destination path overflow.\" nodeId=publish)\n");
        return 2;
    }

    if (!copy_runtime_file(runtime_src, runtime_dst)) {
        fprintf(stderr,
            "Err#err1(code=RUN001 message=\"Failed to copy runtime for target RID. Build target runtime first.\" nodeId=publish)\n");
        return 2;
    }
    emit_native_target_capability_warnings(target, &publish_program);

    printf("Ok#ok1(type=string value=\"publish-complete\")\n");
    return 0;
}

int main(int argc, char** argv)
{
    char exe_path[PATH_MAX];
    char exe_dir[PATH_MAX];
    char bundled_aibc1[PATH_MAX];
    char bundled_www_dir[PATH_MAX];
    char bundled_www_index[PATH_MAX];
    const char* exe_base;

    g_airun_runtime_exe_path[0] = '\0';

    if (argv != NULL &&
        resolve_executable_path(argv[0], exe_path, sizeof(exe_path)) &&
        dirname_of(exe_path, exe_dir, sizeof(exe_dir)) &&
        join_path(exe_dir, "app.aibc1", bundled_aibc1, sizeof(bundled_aibc1)) &&
        file_exists(bundled_aibc1)) {
        (void)snprintf(g_airun_runtime_exe_path, sizeof(g_airun_runtime_exe_path), "%s", exe_path);
        exe_base = path_basename_ptr(exe_path);
        if (exe_base != NULL &&
            strcmp(exe_base, "ailang") != 0 &&
            strcmp(exe_base, "ailang.exe") != 0) {
            if (join_path(exe_dir, "www", bundled_www_dir, sizeof(bundled_www_dir)) &&
                join_path(bundled_www_dir, "index.html", bundled_www_index, sizeof(bundled_www_index)) &&
                file_exists(bundled_www_index)) {
                return run_native_fullstack_server(bundled_www_dir);
            }
            const char* const* app_argv = (argc > 1) ? (const char* const*)&argv[1] : NULL;
            size_t app_argc = (argc > 1) ? (size_t)(argc - 1) : 0U;
            return run_native_aibc1(bundled_aibc1, app_argv, app_argc, NULL);
        }
    }

    if (argc <= 1 || argv == NULL) {
        print_usage();
        return 2;
    }

    if (argv != NULL && argv[0] != NULL && g_airun_runtime_exe_path[0] == '\0') {
        if (!resolve_executable_path(argv[0], g_airun_runtime_exe_path, sizeof(g_airun_runtime_exe_path))) {
            (void)snprintf(g_airun_runtime_exe_path, sizeof(g_airun_runtime_exe_path), "%s", argv[0]);
        }
    }

    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
#ifdef AIRUN_MINIMAL_RUNTIME
        printf("aivm-runtime-native-c version=%s channel=%s commit=%s abi=%u\n",
            AILANG_BUILD_VERSION,
            AILANG_BUILD_CHANNEL,
            AILANG_BUILD_COMMIT,
            aivm_c_abi_version());
#else
        printf("ailang-native-c version=%s channel=%s commit=%s abi=%u\n",
            AILANG_BUILD_VERSION,
            AILANG_BUILD_CHANNEL,
            AILANG_BUILD_COMMIT,
            aivm_c_abi_version());
#endif
        return 0;
    }
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[1], "run") == 0) {
        return handle_run(argc, argv);
    }
#ifdef AIRUN_MINIMAL_RUNTIME
    fprintf(stderr,
        "Err#err1(code=DEV008 message=\"Command is not part of minimal aivm-runtime surface.\" nodeId=command)\n");
    return 2;
#else
    if (strcmp(argv[1], "build") == 0) {
        return handle_build(argc, argv);
    }
    if (strcmp(argv[1], "init") == 0) {
        return handle_init(argc, argv);
    }
    if (strcmp(argv[1], "template") == 0) {
        return handle_template(argc, argv);
    }
    if (strcmp(argv[1], "package") == 0) {
        return ailang_package_manager_cli(argc, argv);
    }
    if (strcmp(argv[1], "serve") == 0) {
        return handle_serve(argc, argv);
    }
    if (strcmp(argv[1], "debug") == 0) {
        return handle_debug(argc, argv);
    }
    if (strcmp(argv[1], "repl") == 0) {
        return handle_repl();
    }
    if (strcmp(argv[1], "bench") == 0) {
        return handle_bench(argc, argv);
    }
    if (strcmp(argv[1], "test") == 0) {
        return handle_test(argc, argv);
    }
    if (strcmp(argv[1], "publish") == 0) {
        return handle_publish(argc, argv);
    }
    if (strcmp(argv[1], "clean") == 0) {
        return handle_clean(argc, argv);
    }

    {
        AilangPackageManagerOptions package_options;
        int package_tool_exit = 0;
        char package_tool_error[512];
        memset(&package_options, 0, sizeof(package_options));
        package_tool_error[0] = '\0';
        if (ailang_package_manager_try_run_tool(
                &package_options,
                argv[1],
                argc - 2,
                argc > 2 ? &argv[2] : NULL,
                &package_tool_exit,
                package_tool_error,
                sizeof(package_tool_error))) {
            return package_tool_exit;
        }
        if (package_tool_error[0] != '\0') {
            fprintf(stderr, "Err#err1(code=PKG001 message=\"%s\" nodeId=package)\n", package_tool_error);
            return 2;
        }
    }

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    print_usage();
    return 2;
#endif
}
