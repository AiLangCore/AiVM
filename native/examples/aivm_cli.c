#if !defined(_WIN32)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#else
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#ifdef _WIN32
#define PATH_MAX MAX_PATH
#else
#define PATH_MAX 4096
#endif
#endif

#include "aivm_c_api.h"
#include "aivm_program.h"
#include "aivm_runtime.h"
#include "sys/aivm_syscall.h"

#if defined(AIVM_DEBUG_RUNTIME)
#define AIVM_CLI_NAME "aivm-debug"
#else
#define AIVM_CLI_NAME "aivm"
#endif

static uint8_t* g_cli_bytes_scratch = NULL;
static char* g_cli_string_scratch = NULL;
static AivmVm* g_native_active_vm = NULL;
#if defined(AIVM_DEBUG_RUNTIME)
static FILE* g_debug_stdout_capture = NULL;
#endif

#define NATIVE_PROCESS_CAPACITY 32U
#define NATIVE_PROCESS_READ_CHUNK 4096U
#define AIRUN_LOG_TRACE 3
static int g_airun_log_level = 0;

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

static int read_file(const char* path, uint8_t** out_bytes, size_t* out_size);

#if defined(AIVM_DEBUG_RUNTIME)
static int ensure_directory(const char* path)
{
    char buffer[PATH_MAX];
    size_t length;
    size_t index;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    length = strlen(path);
    if (length >= sizeof(buffer)) {
        return 0;
    }
    memcpy(buffer, path, length + 1U);
    for (index = 1U; index < length; index += 1U) {
        if (buffer[index] == '/' || buffer[index] == '\\') {
            char saved = buffer[index];
            buffer[index] = '\0';
            if (buffer[0] != '\0') {
#if defined(_WIN32)
                if (_mkdir(buffer) != 0 && errno != EEXIST) {
                    buffer[index] = saved;
                    return 0;
                }
#else
                if (mkdir(buffer, 0777) != 0 && errno != EEXIST) {
                    buffer[index] = saved;
                    return 0;
                }
#endif
            }
            buffer[index] = saved;
        }
    }
#if defined(_WIN32)
    if (_mkdir(buffer) == 0 || errno == EEXIST) {
        return 1;
    }
#else
    if (mkdir(buffer, 0777) == 0 || errno == EEXIST) {
        return 1;
    }
#endif
    return 0;
}

static int join_path(const char* dir, const char* name, char* out, size_t out_len)
{
    int written;
    const char* separator = "/";
#if defined(_WIN32)
    separator = "\\";
#endif
    if (dir == NULL || name == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    written = snprintf(out, out_len, "%s%s%s", dir, separator, name);
    return written >= 0 && (size_t)written < out_len;
}

static void write_toml_string(FILE* file, const char* text)
{
    const unsigned char* cursor = (const unsigned char*)((text == NULL) ? "" : text);
    fputc('"', file);
    while (*cursor != '\0') {
        switch (*cursor) {
            case '\\':
                fputs("\\\\", file);
                break;
            case '"':
                fputs("\\\"", file);
                break;
            case '\n':
                fputs("\\n", file);
                break;
            case '\r':
                fputs("\\r", file);
                break;
            case '\t':
                fputs("\\t", file);
                break;
            default:
                if (*cursor < 32U) {
                    fprintf(file, "\\u%04x", (unsigned int)*cursor);
                } else {
                    fputc((int)*cursor, file);
                }
                break;
        }
        cursor += 1;
    }
    fputc('"', file);
}

static const char* cli_opcode_name(AivmOpcode opcode)
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
        default: return "UNKNOWN";
    }
}

static const char* cli_value_type_name(AivmValueType type)
{
    switch (type) {
        case AIVM_VAL_VOID: return "void";
        case AIVM_VAL_INT: return "int";
        case AIVM_VAL_BOOL: return "bool";
        case AIVM_VAL_NULL: return "null";
        case AIVM_VAL_STRING: return "string";
        case AIVM_VAL_BYTES: return "bytes";
        case AIVM_VAL_NODE: return "node";
        default: return "unknown";
    }
}

static void format_value_preview(const AivmValue* value, char* out, size_t out_len)
{
    if (out == NULL || out_len == 0U) {
        return;
    }
    out[0] = '\0';
    if (value == NULL) {
        (void)snprintf(out, out_len, "missing");
        return;
    }
    switch (value->type) {
        case AIVM_VAL_INT:
            (void)snprintf(out, out_len, "int(%lld)", (long long)value->int_value);
            break;
        case AIVM_VAL_BOOL:
            (void)snprintf(out, out_len, "bool(%s)", value->bool_value ? "true" : "false");
            break;
        case AIVM_VAL_NULL:
            (void)snprintf(out, out_len, "null");
            break;
        case AIVM_VAL_STRING:
            (void)snprintf(out, out_len, "string(\"%.48s\")", value->string_value == NULL ? "" : value->string_value);
            break;
        case AIVM_VAL_BYTES:
            (void)snprintf(out, out_len, "bytes(len=%llu)", (unsigned long long)value->bytes_value.length);
            break;
        case AIVM_VAL_NODE:
            (void)snprintf(out, out_len, "node(%lld)", (long long)value->node_handle);
            break;
        case AIVM_VAL_VOID:
            (void)snprintf(out, out_len, "void");
            break;
        default:
            (void)snprintf(out, out_len, "%s", cli_value_type_name(value->type));
            break;
    }
}
#endif

static void airun_log_message(int level, const char* category, const char* fmt, ...)
{
    (void)level;
    (void)category;
    (void)fmt;
}

static int native_vm_lookup_node_record(const AivmVm* vm, int64_t handle, const AivmNodeRecord** out_node)
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

static const char* native_vm_node_first_string_attr(const AivmVm* vm, int64_t handle)
{
    const AivmNodeRecord* node = NULL;
    size_t index;
    if (!native_vm_lookup_node_record(vm, handle, &node)) {
        return NULL;
    }
    for (index = 0U; index < node->attr_count; index += 1U) {
        const AivmNodeAttr* attr = &vm->node_attrs[node->attr_start + index];
        if (attr->kind == AIVM_NODE_ATTR_STRING) {
            return attr->string_value == NULL ? "" : attr->string_value;
        }
    }
    return NULL;
}

static int native_host_open_default(const char* path_or_url)
{
    (void)path_or_url;
    return 0;
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

#include "../ailang_cli/airun_process_host.inc"

static int aivm_cli_stdout_write_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    switch (args[0].type) {
        case AIVM_VAL_STRING:
            printf("%s\n", args[0].string_value == NULL ? "" : args[0].string_value);
#if defined(AIVM_DEBUG_RUNTIME)
            if (g_debug_stdout_capture != NULL) {
                fprintf(g_debug_stdout_capture, "%s\n", args[0].string_value == NULL ? "" : args[0].string_value);
            }
#endif
            break;
        case AIVM_VAL_INT:
            printf("%lld\n", (long long)args[0].int_value);
#if defined(AIVM_DEBUG_RUNTIME)
            if (g_debug_stdout_capture != NULL) {
                fprintf(g_debug_stdout_capture, "%lld\n", (long long)args[0].int_value);
            }
#endif
            break;
        case AIVM_VAL_BOOL:
            printf("%s\n", args[0].bool_value ? "true" : "false");
#if defined(AIVM_DEBUG_RUNTIME)
            if (g_debug_stdout_capture != NULL) {
                fprintf(g_debug_stdout_capture, "%s\n", args[0].bool_value ? "true" : "false");
            }
#endif
            break;
        case AIVM_VAL_NULL:
            printf("null\n");
#if defined(AIVM_DEBUG_RUNTIME)
            if (g_debug_stdout_capture != NULL) {
                fprintf(g_debug_stdout_capture, "null\n");
            }
#endif
            break;
        case AIVM_VAL_VOID:
            printf("void\n");
#if defined(AIVM_DEBUG_RUNTIME)
            if (g_debug_stdout_capture != NULL) {
                fprintf(g_debug_stdout_capture, "void\n");
            }
#endif
            break;
        default:
            printf("<value>\n");
#if defined(AIVM_DEBUG_RUNTIME)
            if (g_debug_stdout_capture != NULL) {
                fprintf(g_debug_stdout_capture, "<value>\n");
            }
#endif
            break;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int aivm_cli_path_exists(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    FILE* file;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    file = fopen(args[0].string_value, "rb");
    if (file != NULL) {
        fclose(file);
        *result = aivm_value_bool(1);
        return AIVM_SYSCALL_OK;
    }
#if defined(_WIN32)
    *result = aivm_value_bool(_access(args[0].string_value, 0) == 0);
#else
    {
        struct stat st;
        *result = aivm_value_bool(stat(args[0].string_value, &st) == 0);
    }
#endif
    return AIVM_SYSCALL_OK;
}

static int aivm_cli_dir_create(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int rc;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
#if defined(_WIN32)
    rc = _mkdir(args[0].string_value);
#else
    rc = mkdir(args[0].string_value, 0777);
#endif
    if (rc != 0 && errno != EEXIST) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int aivm_cli_file_delete(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int deleted;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
#if defined(_WIN32)
    deleted = DeleteFileA(args[0].string_value) != 0;
#else
    deleted = unlink(args[0].string_value) == 0;
#endif
    *result = aivm_value_bool(deleted);
    return AIVM_SYSCALL_OK;
}

#if defined(_WIN32)
static int aivm_cli_join_path(char* out_path, size_t out_size, const char* parent, const char* child)
{
    int written = snprintf(out_path, out_size, "%s\\%s", parent, child);
    return written > 0 && (size_t)written < out_size;
}

static int aivm_cli_dir_delete_recursive(const char* path)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA entry;
    HANDLE handle;
    int ok = 1;

    if (snprintf(pattern, sizeof(pattern), "%s\\*", path) <= 0) {
        return 0;
    }
    handle = FindFirstFileA(pattern, &entry);
    if (handle == INVALID_HANDLE_VALUE) {
        return RemoveDirectoryA(path) != 0;
    }
    do {
        char child[MAX_PATH];
        if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0) {
            continue;
        }
        if (!aivm_cli_join_path(child, sizeof(child), path, entry.cFileName)) {
            ok = 0;
            break;
        }
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!aivm_cli_dir_delete_recursive(child)) {
                ok = 0;
                break;
            }
        } else if (DeleteFileA(child) == 0) {
            ok = 0;
            break;
        }
    } while (FindNextFileA(handle, &entry) != 0);
    FindClose(handle);
    return ok && RemoveDirectoryA(path) != 0;
}
#else
static int aivm_cli_join_path(char* out_path, size_t out_size, const char* parent, const char* child)
{
    int written = snprintf(out_path, out_size, "%s/%s", parent, child);
    return written > 0 && (size_t)written < out_size;
}

static int aivm_cli_dir_delete_recursive(const char* path)
{
    DIR* dir;
    struct dirent* entry;
    int ok = 1;

    dir = opendir(path);
    if (dir == NULL) {
        return rmdir(path) == 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        char child[PATH_MAX];
        struct stat st;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!aivm_cli_join_path(child, sizeof(child), path, entry->d_name)) {
            ok = 0;
            break;
        }
        if (lstat(child, &st) != 0) {
            ok = 0;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            if (!aivm_cli_dir_delete_recursive(child)) {
                ok = 0;
                break;
            }
        } else if (unlink(child) != 0) {
            ok = 0;
            break;
        }
    }
    closedir(dir);
    return ok && rmdir(path) == 0;
}
#endif

static int aivm_cli_dir_delete(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int recursive;
    int deleted;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 2U ||
        args == NULL ||
        args[0].type != AIVM_VAL_STRING ||
        args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_BOOL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    recursive = args[1].bool_value != 0;
#if defined(_WIN32)
    deleted = recursive ? aivm_cli_dir_delete_recursive(args[0].string_value) : RemoveDirectoryA(args[0].string_value) != 0;
#else
    deleted = recursive ? aivm_cli_dir_delete_recursive(args[0].string_value) : rmdir(args[0].string_value) == 0;
#endif
    *result = aivm_value_bool(deleted);
    return AIVM_SYSCALL_OK;
}

static int aivm_cli_file_write(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    FILE* file;
    size_t written;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 2U ||
        args == NULL ||
        args[0].type != AIVM_VAL_STRING ||
        args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_BYTES) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    file = fopen(args[0].string_value, "wb");
    if (file == NULL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    written = fwrite(args[1].bytes_value.data, 1U, args[1].bytes_value.length, file);
    fclose(file);
    if (written != args[1].bytes_value.length) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int aivm_cli_file_read(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    uint8_t* bytes;
    size_t byte_count;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!read_file(args[0].string_value, &bytes, &byte_count)) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    free(g_cli_bytes_scratch);
    g_cli_bytes_scratch = bytes;
    *result = aivm_value_bytes(g_cli_bytes_scratch, byte_count);
    return AIVM_SYSCALL_OK;
}

static int aivm_cli_bytes_from_utf8_string(
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
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bytes((const uint8_t*)args[0].string_value, strlen(args[0].string_value));
    return AIVM_SYSCALL_OK;
}

static int aivm_cli_bytes_to_utf8_string(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    char* text;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_BYTES) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    text = (char*)malloc(args[0].bytes_value.length + 1U);
    if (text == NULL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[0].bytes_value.length > 0U) {
        memcpy(text, args[0].bytes_value.data, args[0].bytes_value.length);
    }
    text[args[0].bytes_value.length] = '\0';
    free(g_cli_string_scratch);
    g_cli_string_scratch = text;
    *result = aivm_value_string(g_cli_string_scratch);
    return AIVM_SYSCALL_OK;
}

static int aivm_cli_str_find(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* haystack;
    const char* needle;
    const char* found;
    size_t start;
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
        args[1].string_value == NULL ||
        args[2].int_value < 0) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    haystack = args[0].string_value;
    needle = args[1].string_value;
    start = (size_t)args[2].int_value;
    if (start > strlen(haystack)) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    found = strstr(haystack + start, needle);
    *result = aivm_value_int(found == NULL ? -1 : (int64_t)(found - haystack));
    return AIVM_SYSCALL_OK;
}

static int aivm_cli_str_substring(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* text;
    size_t text_length;
    size_t start;
    size_t length;
    char* output;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 3U ||
        args == NULL ||
        args[0].type != AIVM_VAL_STRING ||
        args[1].type != AIVM_VAL_INT ||
        args[2].type != AIVM_VAL_INT ||
        args[0].string_value == NULL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    text = args[0].string_value;
    text_length = strlen(text);
    if (args[1].int_value <= 0) {
        start = 0U;
    } else if ((uint64_t)args[1].int_value >= (uint64_t)text_length) {
        start = text_length;
    } else {
        start = (size_t)args[1].int_value;
    }
    if (args[2].int_value <= 0) {
        length = 0U;
    } else if ((uint64_t)args[2].int_value > (uint64_t)(text_length - start)) {
        length = text_length - start;
    } else {
        length = (size_t)args[2].int_value;
    }
    output = (char*)malloc(length + 1U);
    if (output == NULL) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (length > 0U) {
        memcpy(output, text + start, length);
    }
    output[length] = '\0';
    free(g_cli_string_scratch);
    g_cli_string_scratch = output;
    *result = aivm_value_string(g_cli_string_scratch);
    return AIVM_SYSCALL_OK;
}

static void print_usage(FILE* stream)
{
    fprintf(stream, "Usage: %s --version\n", AIVM_CLI_NAME);
    fprintf(stream, "       %s <program.aibc1> [args...]\n", AIVM_CLI_NAME);
#if defined(AIVM_DEBUG_RUNTIME)
    fprintf(stream, "       %s profile <program.aibc1> [args...]\n", AIVM_CLI_NAME);
    fprintf(stream, "       %s benchmark [--iterations <n>] <program.aibc1> [args...]\n", AIVM_CLI_NAME);
    fprintf(stream, "       %s debug capture run <program.aibc1> [--out <dir>] [--profile <production|debug|tooling>] [--allow <capability>] [--deny <capability>] [args...]\n", AIVM_CLI_NAME);
    fprintf(stream, "       %s explain <debug-run-dir>\n", AIVM_CLI_NAME);
    fprintf(stream, "       %s suggest <debug-run-dir>\n", AIVM_CLI_NAME);
    fprintf(stream, "       %s inspect <stack|memory|profile|syscalls> <debug-run-dir>\n", AIVM_CLI_NAME);
    fprintf(stream, "       %s compare <left-debug-run-dir> <right-debug-run-dir>\n", AIVM_CLI_NAME);
#endif
}

static int read_file(const char* path, uint8_t** out_bytes, size_t* out_size)
{
    FILE* file;
    long length;
    uint8_t* bytes;
    size_t read_count;

    *out_bytes = NULL;
    *out_size = 0U;
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "aivm: failed to open %s: %s\n", path, strerror(errno));
        return 0;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "aivm: failed to seek %s\n", path);
        return 0;
    }
    length = ftell(file);
    if (length < 0L) {
        fclose(file);
        fprintf(stderr, "aivm: failed to measure %s\n", path);
        return 0;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        fprintf(stderr, "aivm: failed to rewind %s\n", path);
        return 0;
    }
    bytes = (uint8_t*)malloc((size_t)length);
    if (bytes == NULL && length > 0L) {
        fclose(file);
        fprintf(stderr, "aivm: failed to allocate %ld bytes\n", length);
        return 0;
    }
    read_count = fread(bytes, 1U, (size_t)length, file);
    fclose(file);
    if (read_count != (size_t)length) {
        free(bytes);
        fprintf(stderr, "aivm: failed to read %s\n", path);
        return 0;
    }
    *out_bytes = bytes;
    *out_size = (size_t)length;
    return 1;
}

#if defined(AIVM_DEBUG_RUNTIME)
typedef struct DebugCapabilityOverride
{
    AivmSyscallCapabilityGroup capability;
    int allow;
} DebugCapabilityOverride;

static void write_empty_debug_file(const char* artifact_dir, const char* file_name)
{
    char path[PATH_MAX];
    FILE* file;
    if (!join_path(artifact_dir, file_name, path, sizeof(path))) {
        return;
    }
    file = fopen(path, "ab");
    if (file != NULL) {
        fclose(file);
    }
}

typedef struct DebugArtifactBudget
{
    size_t limit_bytes;
    size_t used_bytes;
    int exceeded;
} DebugArtifactBudget;

typedef struct DebugArtifactFile
{
    FILE* file;
    DebugArtifactBudget* budget;
    char path[PATH_MAX];
    char temp_path[PATH_MAX];
} DebugArtifactFile;

static size_t debug_artifact_file_size(const char* path)
{
    struct stat info;
    if (path == NULL || stat(path, &info) != 0 || info.st_size < 0) {
        return 0U;
    }
    return (size_t)info.st_size;
}

static void debug_artifact_budget_init(DebugArtifactBudget* budget, size_t limit_bytes)
{
    if (budget == NULL) {
        return;
    }
    budget->limit_bytes = limit_bytes;
    budget->used_bytes = 0U;
    budget->exceeded = 0;
}

static void debug_artifact_budget_count_existing(DebugArtifactBudget* budget, const char* artifact_dir, const char* file_name)
{
    char path[PATH_MAX];
    size_t size;
    if (budget == NULL || artifact_dir == NULL || file_name == NULL ||
        !join_path(artifact_dir, file_name, path, sizeof(path))) {
        return;
    }
    size = debug_artifact_file_size(path);
    if (budget->used_bytes > (size_t)-1 - size) {
        budget->used_bytes = (size_t)-1;
        budget->exceeded = 1;
        return;
    }
    budget->used_bytes += size;
    if (budget->limit_bytes > 0U && budget->used_bytes > budget->limit_bytes) {
        budget->exceeded = 1;
    }
}

static FILE* debug_artifact_open(DebugArtifactBudget* budget, const char* artifact_dir, const char* file_name, DebugArtifactFile* artifact)
{
    int written;
    if (budget == NULL || artifact_dir == NULL || file_name == NULL || artifact == NULL || budget->exceeded) {
        return NULL;
    }
    memset(artifact, 0, sizeof(*artifact));
    artifact->budget = budget;
    if (!join_path(artifact_dir, file_name, artifact->path, sizeof(artifact->path))) {
        return NULL;
    }
    written = snprintf(artifact->temp_path, sizeof(artifact->temp_path), "%s.tmp", artifact->path);
    if (written < 0 || (size_t)written >= sizeof(artifact->temp_path)) {
        return NULL;
    }
    artifact->file = fopen(artifact->temp_path, "wb");
    return artifact->file;
}

static void debug_artifact_close(DebugArtifactFile* artifact)
{
    size_t size;
    DebugArtifactBudget* budget;
    if (artifact == NULL || artifact->file == NULL || artifact->budget == NULL) {
        return;
    }
    fclose(artifact->file);
    artifact->file = NULL;
    budget = artifact->budget;
    size = debug_artifact_file_size(artifact->temp_path);
    if (budget->limit_bytes > 0U &&
        (budget->used_bytes > budget->limit_bytes ||
         size > budget->limit_bytes - budget->used_bytes)) {
        remove(artifact->temp_path);
        budget->exceeded = 1;
        return;
    }
    remove(artifact->path);
    if (rename(artifact->temp_path, artifact->path) != 0) {
        remove(artifact->temp_path);
        budget->exceeded = 1;
        return;
    }
    budget->used_bytes += size;
}

static size_t debug_current_pc(const AivmProgram* program, const AivmVm* vm)
{
    if (vm == NULL) {
        return 0U;
    }
    if (program != NULL && vm->instruction_pointer < program->instruction_count) {
        return vm->instruction_pointer;
    }
    if (vm->recent_opcode_count > 0U) {
        return vm->recent_opcodes[vm->recent_opcode_count - 1U].instruction_pointer;
    }
    return vm->instruction_pointer;
}

static const char* debug_current_opcode_name(const AivmProgram* program, const AivmVm* vm)
{
    if (vm == NULL) {
        return "UNKNOWN";
    }
    if (program != NULL && vm->instruction_pointer < program->instruction_count) {
        return cli_opcode_name(program->instructions[vm->instruction_pointer].opcode);
    }
    if (vm->recent_opcode_count > 0U) {
        return cli_opcode_name((AivmOpcode)vm->recent_opcodes[vm->recent_opcode_count - 1U].opcode);
    }
    return "UNKNOWN";
}

static void write_debug_stack_trace(FILE* file, const AivmProgram* program, const AivmVm* vm)
{
    size_t frame;
    size_t current_pc;
    const char* current_opcode;
    if (file == NULL || vm == NULL) {
        return;
    }
    current_pc = debug_current_pc(program, vm);
    current_opcode = debug_current_opcode_name(program, vm);
    fprintf(file, "format = \"aivm_debug_stack_trace_v1\"\n");
    fprintf(file, "current_pc = %llu\n", (unsigned long long)current_pc);
    fprintf(file, "current_opcode = ");
    write_toml_string(file, current_opcode);
    fprintf(file, "\n");
    fprintf(file, "frames = [\n");
    fprintf(
        file,
        "  { index = 0, role = \"current\", function = \"main\", pc = %llu, opcode = ",
        (unsigned long long)current_pc);
    write_toml_string(file, current_opcode);
    fprintf(file, ", locals_base = 0, return_pc = 0 },\n");
    for (frame = 0U; frame < vm->call_frame_count; frame += 1U) {
        const AivmCallFrame* call_frame = &vm->call_frames[vm->call_frame_count - frame - 1U];
        fprintf(
            file,
            "  { index = %llu, role = \"caller\", function = \"unknown\", pc = %llu, opcode = \"UNKNOWN\", locals_base = %llu, return_pc = %llu },\n",
            (unsigned long long)(frame + 1U),
            (unsigned long long)call_frame->return_instruction_pointer,
            (unsigned long long)call_frame->locals_base,
            (unsigned long long)call_frame->return_instruction_pointer);
    }
    fprintf(file, "]\n");
}

static void write_debug_artifacts(
    const char* artifact_dir,
    const char* program_path,
    const AivmProgram* program,
    const AivmVm* vm,
    int ok,
    int exit_code,
    double elapsed_seconds)
{
    char path[PATH_MAX];
    FILE* file;
    char stack0[96];
    char stack1[96];
    char local0[96];
    char local1[96];
    DebugArtifactBudget budget;
    DebugArtifactFile artifact;
    AivmRuntimeProfile runtime_profile = vm == NULL ? aivm_runtime_default_profile() : vm->runtime_profile;
    AivmRuntimeProfileLimits profile_limits = aivm_runtime_profile_limits(runtime_profile);

    if (artifact_dir == NULL || artifact_dir[0] == '\0') {
        return;
    }
    if (!ensure_directory(artifact_dir)) {
        fprintf(stderr, "%s: failed to create debug artifact directory: %s\n", AIVM_CLI_NAME, artifact_dir);
        return;
    }

    write_empty_debug_file(artifact_dir, "stdout.txt");
    if (g_debug_stdout_capture != NULL) {
        fflush(g_debug_stdout_capture);
    }
    debug_artifact_budget_init(&budget, profile_limits.debug_artifact_bytes);
    debug_artifact_budget_count_existing(&budget, artifact_dir, "stdout.txt");
    debug_artifact_budget_count_existing(&budget, artifact_dir, "stderr.txt");

    if (join_path(artifact_dir, "stderr.txt", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "stderr.txt", &artifact);
        if (file != NULL) {
            if (!ok && vm != NULL) {
                fprintf(
                    file,
                    "aivm: execution failed: status=%d error=%d",
                    (int)vm->status,
                    (int)vm->error);
                if (aivm_vm_error_detail(vm) != NULL && aivm_vm_error_detail(vm)[0] != '\0') {
                    fprintf(file, " detail=%s", aivm_vm_error_detail(vm));
                }
                fprintf(file, "\n");
            }
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "config.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "config.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_config_v1\"\n");
            fprintf(file, "runtime = \"aivm-debug\"\n");
            fprintf(file, "runtime_profile = ");
            write_toml_string(file, aivm_runtime_profile_name(runtime_profile));
            fprintf(file, "\n");
            fprintf(file, "syscall_capability_policy_mask = %llu\n",
                vm == NULL ? 0ULL : (unsigned long long)vm->syscall_policy.allowed_capability_mask);
            fprintf(file, "abi = %u\n", (unsigned int)aivm_c_abi_version());
            fprintf(file, "program = ");
            write_toml_string(file, program_path);
            fprintf(file, "\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "diagnostics.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "diagnostics.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_diagnostics_v1\"\n");
            fprintf(file, "runtime_profile = ");
            write_toml_string(file, aivm_runtime_profile_name(runtime_profile));
            fprintf(file, "\n");
            fprintf(file, "status = ");
            write_toml_string(file, ok ? "ok" : "error");
            fprintf(file, "\n");
            fprintf(file, "phase = \"execute\"\n");
            fprintf(file, "exit_code = %d\n", exit_code);
            fprintf(file, "vm_status = %d\n", vm == NULL ? 0 : (int)vm->status);
            fprintf(file, "vm_error = %d\n", vm == NULL ? 0 : (int)vm->error);
            fprintf(file, "vm_error_code = ");
            write_toml_string(file, vm == NULL ? "AIVM000" : aivm_vm_error_code(vm->error));
            fprintf(file, "\n");
            fprintf(file, "vm_error_message = ");
            write_toml_string(file, vm == NULL ? "" : aivm_vm_error_message(vm->error));
            fprintf(file, "\n");
            fprintf(file, "detail = ");
            write_toml_string(file, vm == NULL ? "" : aivm_vm_error_detail(vm));
            fprintf(file, "\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "stack_trace.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "stack_trace.toml", &artifact);
        if (file != NULL) {
            write_debug_stack_trace(file, program, vm);
            debug_artifact_close(&artifact);
        }
    }

    stack0[0] = '\0';
    stack1[0] = '\0';
    local0[0] = '\0';
    local1[0] = '\0';
    if (vm != NULL && vm->stack_count > 0U) {
        format_value_preview(&vm->stack[vm->stack_count - 1U], stack0, sizeof(stack0));
    }
    if (vm != NULL && vm->stack_count > 1U) {
        format_value_preview(&vm->stack[vm->stack_count - 2U], stack1, sizeof(stack1));
    }
    if (vm != NULL && vm->locals_count > 0U) {
        format_value_preview(&vm->locals[0], local0, sizeof(local0));
    }
    if (vm != NULL && vm->locals_count > 1U) {
        format_value_preview(&vm->locals[1], local1, sizeof(local1));
    }

    if (join_path(artifact_dir, "vm_trace.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "vm_trace.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_vm_trace_v1\"\n");
            fprintf(file, "last = { pc = %llu, opcode = ",
                (unsigned long long)debug_current_pc(program, vm));
            write_toml_string(file, debug_current_opcode_name(program, vm));
            fprintf(file, " }\n");
            fprintf(file, "stack = { count = %llu, top0 = ",
                (unsigned long long)(vm == NULL ? 0U : vm->stack_count));
            write_toml_string(file, stack0);
            fprintf(file, ", top1 = ");
            write_toml_string(file, stack1);
            fprintf(file, " }\n");
            fprintf(file, "locals = { count = %llu, local0 = ",
                (unsigned long long)(vm == NULL ? 0U : vm->locals_count));
            write_toml_string(file, local0);
            fprintf(file, ", local1 = ");
            write_toml_string(file, local1);
            fprintf(file, " }\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "memory.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "memory.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_memory_v1\"\n");
            fprintf(file, "stack_count = %llu\n", (unsigned long long)(vm == NULL ? 0U : vm->stack_count));
            fprintf(file, "locals_count = %llu\n", (unsigned long long)(vm == NULL ? 0U : vm->locals_count));
            fprintf(file, "string_arena_used = %llu\n", (unsigned long long)(vm == NULL ? 0U : vm->string_arena_used));
            fprintf(file, "string_arena_high_water = %llu\n", (unsigned long long)(vm == NULL ? 0U : vm->string_arena_high_water));
            fprintf(file, "bytes_arena_used = %llu\n", (unsigned long long)(vm == NULL ? 0U : vm->bytes_arena_used));
            fprintf(file, "bytes_arena_high_water = %llu\n", (unsigned long long)(vm == NULL ? 0U : vm->bytes_arena_high_water));
            fprintf(file, "node_count = %llu\n", (unsigned long long)(vm == NULL ? 0U : vm->node_count));
            fprintf(file, "node_high_water = %llu\n", (unsigned long long)(vm == NULL ? 0U : vm->node_high_water));
            fprintf(file, "limits = { stack_capacity = %llu, call_frame_capacity = %llu, locals_capacity = %llu, string_arena_capacity = %llu, bytes_arena_capacity = %llu, node_capacity = %llu, node_attr_capacity = %llu, node_child_capacity = %llu, task_capacity = %llu, par_value_capacity = %llu, file_read_bytes = %llu, file_write_bytes = %llu, network_read_bytes = %llu, network_write_bytes = %llu, process_count = %llu, worker_count = %llu, ui_window_count = %llu, debug_artifact_bytes = %llu, syscall_elapsed_ms = %llu }\n",
                (unsigned long long)profile_limits.stack_capacity,
                (unsigned long long)profile_limits.call_frame_capacity,
                (unsigned long long)profile_limits.locals_capacity,
                (unsigned long long)profile_limits.string_arena_capacity,
                (unsigned long long)profile_limits.bytes_arena_capacity,
                (unsigned long long)profile_limits.node_capacity,
                (unsigned long long)profile_limits.node_attr_capacity,
                (unsigned long long)profile_limits.node_child_capacity,
                (unsigned long long)profile_limits.task_capacity,
                (unsigned long long)profile_limits.par_value_capacity,
                (unsigned long long)profile_limits.file_read_bytes,
                (unsigned long long)profile_limits.file_write_bytes,
                (unsigned long long)profile_limits.network_read_bytes,
                (unsigned long long)profile_limits.network_write_bytes,
                (unsigned long long)profile_limits.process_count,
                (unsigned long long)profile_limits.worker_count,
                (unsigned long long)profile_limits.ui_window_count,
                (unsigned long long)profile_limits.debug_artifact_bytes,
                (unsigned long long)profile_limits.syscall_elapsed_ms);
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "profile.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "profile.toml", &artifact);
        if (file != NULL) {
            size_t opcode_index;
            size_t syscall_index;
            fprintf(file, "format = \"aivm_debug_profile_v1\"\n");
            fprintf(file, "status = ");
            write_toml_string(file, ok ? "ok" : "error");
            fprintf(file, "\n");
            fprintf(file, "elapsed_seconds = %.9f\n", elapsed_seconds);
            fprintf(file, "instruction_count = %llu\n",
                (unsigned long long)(vm == NULL ? 0U : vm->profile_instruction_count));
            fprintf(file, "opcode_counts = [\n");
            if (vm != NULL) {
                for (opcode_index = 0U;
                     opcode_index < (sizeof(vm->profile_opcode_counts) / sizeof(vm->profile_opcode_counts[0]));
                     opcode_index += 1U) {
                    if (vm->profile_opcode_counts[opcode_index] == 0U) {
                        continue;
                    }
                    fprintf(file, "  { opcode = ");
                    write_toml_string(file, cli_opcode_name((AivmOpcode)opcode_index));
                    fprintf(file, ", count = %llu },\n",
                        (unsigned long long)vm->profile_opcode_counts[opcode_index]);
                }
            }
            fprintf(file, "]\n");
            fprintf(file, "syscall_count = %llu\n",
                (unsigned long long)(vm == NULL ? 0U : vm->profile_syscall_count));
            fprintf(file, "syscall_elapsed_seconds = %.9f\n",
                vm == NULL ? 0.0 : vm->profile_syscall_elapsed_seconds);
            fprintf(file, "syscall_counts = [\n");
            if (vm != NULL) {
                for (syscall_index = 0U; syscall_index < vm->profile_syscall_target_count; syscall_index += 1U) {
                    fprintf(file, "  { target = ");
                    write_toml_string(file, vm->profile_syscall_targets[syscall_index].target);
                    fprintf(file, ", count = %llu, elapsed_seconds = %.9f },\n",
                        (unsigned long long)vm->profile_syscall_targets[syscall_index].count,
                        vm->profile_syscall_targets[syscall_index].elapsed_seconds);
                }
            }
            fprintf(file, "]\n");
            fprintf(file, "note = \"Profiler timings use the host C clock and are diagnostic only.\"\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "syscall_trace.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "syscall_trace.toml", &artifact);
        if (file != NULL) {
            size_t syscall_index;
            fprintf(file, "format = \"aivm_debug_syscall_trace_v1\"\n");
            fprintf(file, "syscall_count = %llu\n",
                (unsigned long long)(vm == NULL ? 0U : vm->profile_syscall_count));
            fprintf(file, "syscall_elapsed_seconds = %.9f\n",
                vm == NULL ? 0.0 : vm->profile_syscall_elapsed_seconds);
            fprintf(file, "syscalls = [\n");
            if (vm != NULL) {
                for (syscall_index = 0U; syscall_index < vm->profile_syscall_target_count; syscall_index += 1U) {
                    fprintf(file, "  { target = ");
                    write_toml_string(file, vm->profile_syscall_targets[syscall_index].target);
                    fprintf(file, ", count = %llu, elapsed_seconds = %.9f },\n",
                        (unsigned long long)vm->profile_syscall_targets[syscall_index].count,
                        vm->profile_syscall_targets[syscall_index].elapsed_seconds);
                }
            }
            fprintf(file, "]\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "suggestions.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "suggestions.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_suggestions_v1\"\n");
            fprintf(file, "next = [\"aivm-debug explain %s\", \"aivm-debug inspect stack %s\"]\n", artifact_dir, artifact_dir);
            debug_artifact_close(&artifact);
        }
    }
}

static void write_debug_load_failure_artifacts(
    const char* artifact_dir,
    const char* program_path,
    AivmRuntimeProfile runtime_profile,
    const AivmSyscallCapabilityPolicy* syscall_policy,
    AivmProgramLoadResult load_result)
{
    char path[PATH_MAX];
    FILE* file;
    DebugArtifactBudget budget;
    DebugArtifactFile artifact;
    AivmRuntimeProfileLimits profile_limits = aivm_runtime_profile_limits(runtime_profile);

    if (artifact_dir == NULL || artifact_dir[0] == '\0') {
        return;
    }
    if (!ensure_directory(artifact_dir)) {
        fprintf(stderr, "%s: failed to create debug artifact directory: %s\n", AIVM_CLI_NAME, artifact_dir);
        return;
    }

    write_empty_debug_file(artifact_dir, "stdout.txt");
    if (g_debug_stdout_capture != NULL) {
        fflush(g_debug_stdout_capture);
    }
    debug_artifact_budget_init(&budget, profile_limits.debug_artifact_bytes);
    debug_artifact_budget_count_existing(&budget, artifact_dir, "stdout.txt");
    debug_artifact_budget_count_existing(&budget, artifact_dir, "stderr.txt");

    if (join_path(artifact_dir, "stderr.txt", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "stderr.txt", &artifact);
        if (file != NULL) {
            fprintf(file, "aivm: load failed: %s at byte %zu\n",
                aivm_program_status_message(load_result.status),
                load_result.error_offset);
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "config.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "config.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_config_v1\"\n");
            fprintf(file, "runtime = \"aivm-debug\"\n");
            fprintf(file, "runtime_profile = ");
            write_toml_string(file, aivm_runtime_profile_name(runtime_profile));
            fprintf(file, "\n");
            fprintf(file, "syscall_capability_policy_mask = %llu\n",
                syscall_policy == NULL ? 0ULL : (unsigned long long)syscall_policy->allowed_capability_mask);
            fprintf(file, "abi = %u\n", (unsigned int)aivm_c_abi_version());
            fprintf(file, "program = ");
            write_toml_string(file, program_path);
            fprintf(file, "\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "diagnostics.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "diagnostics.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_diagnostics_v1\"\n");
            fprintf(file, "runtime_profile = ");
            write_toml_string(file, aivm_runtime_profile_name(runtime_profile));
            fprintf(file, "\n");
            fprintf(file, "status = \"error\"\n");
            fprintf(file, "phase = \"load\"\n");
            fprintf(file, "exit_code = 2\n");
            fprintf(file, "program_status = %d\n", (int)load_result.status);
            fprintf(file, "program_error_code = ");
            write_toml_string(file, aivm_program_status_code(load_result.status));
            fprintf(file, "\n");
            fprintf(file, "program_error_message = ");
            write_toml_string(file, aivm_program_status_message(load_result.status));
            fprintf(file, "\n");
            fprintf(file, "program_error_offset = %llu\n", (unsigned long long)load_result.error_offset);
            fprintf(file, "vm_status = 0\n");
            fprintf(file, "vm_error = 0\n");
            fprintf(file, "vm_error_code = \"AIVM000\"\n");
            fprintf(file, "vm_error_message = \"No error.\"\n");
            fprintf(file, "detail = ");
            write_toml_string(file, aivm_program_status_message(load_result.status));
            fprintf(file, "\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "stack_trace.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "stack_trace.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_stack_trace_v1\"\n");
            fprintf(file, "current_pc = 0\n");
            fprintf(file, "current_opcode = \"LOAD_FAILED\"\n");
            fprintf(file, "frames = []\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "vm_trace.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "vm_trace.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_vm_trace_v1\"\n");
            fprintf(file, "last = { pc = 0, opcode = \"LOAD_FAILED\" }\n");
            fprintf(file, "stack = { count = 0, top0 = \"\", top1 = \"\" }\n");
            fprintf(file, "locals = { count = 0, local0 = \"\", local1 = \"\" }\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "memory.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "memory.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_memory_v1\"\n");
            fprintf(file, "stack_count = 0\n");
            fprintf(file, "locals_count = 0\n");
            fprintf(file, "string_arena_used = 0\n");
            fprintf(file, "string_arena_high_water = 0\n");
            fprintf(file, "bytes_arena_used = 0\n");
            fprintf(file, "bytes_arena_high_water = 0\n");
            fprintf(file, "node_count = 0\n");
            fprintf(file, "node_high_water = 0\n");
            fprintf(file, "limits = { stack_capacity = %llu, call_frame_capacity = %llu, locals_capacity = %llu, string_arena_capacity = %llu, bytes_arena_capacity = %llu, node_capacity = %llu, node_attr_capacity = %llu, node_child_capacity = %llu, task_capacity = %llu, par_value_capacity = %llu, file_read_bytes = %llu, file_write_bytes = %llu, network_read_bytes = %llu, network_write_bytes = %llu, process_count = %llu, worker_count = %llu, ui_window_count = %llu, debug_artifact_bytes = %llu, syscall_elapsed_ms = %llu }\n",
                (unsigned long long)profile_limits.stack_capacity,
                (unsigned long long)profile_limits.call_frame_capacity,
                (unsigned long long)profile_limits.locals_capacity,
                (unsigned long long)profile_limits.string_arena_capacity,
                (unsigned long long)profile_limits.bytes_arena_capacity,
                (unsigned long long)profile_limits.node_capacity,
                (unsigned long long)profile_limits.node_attr_capacity,
                (unsigned long long)profile_limits.node_child_capacity,
                (unsigned long long)profile_limits.task_capacity,
                (unsigned long long)profile_limits.par_value_capacity,
                (unsigned long long)profile_limits.file_read_bytes,
                (unsigned long long)profile_limits.file_write_bytes,
                (unsigned long long)profile_limits.network_read_bytes,
                (unsigned long long)profile_limits.network_write_bytes,
                (unsigned long long)profile_limits.process_count,
                (unsigned long long)profile_limits.worker_count,
                (unsigned long long)profile_limits.ui_window_count,
                (unsigned long long)profile_limits.debug_artifact_bytes,
                (unsigned long long)profile_limits.syscall_elapsed_ms);
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "profile.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "profile.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_profile_v1\"\n");
            fprintf(file, "status = \"load_failed\"\n");
            fprintf(file, "elapsed_seconds = 0.000000000\n");
            fprintf(file, "instruction_count = 0\n");
            fprintf(file, "opcode_counts = []\n");
            fprintf(file, "syscall_count = 0\n");
            fprintf(file, "syscall_elapsed_seconds = 0.000000000\n");
            fprintf(file, "syscall_counts = []\n");
            fprintf(file, "note = \"Program load failed before VM execution.\"\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "syscall_trace.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "syscall_trace.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_syscall_trace_v1\"\n");
            fprintf(file, "syscall_count = 0\n");
            fprintf(file, "syscall_elapsed_seconds = 0.000000000\n");
            fprintf(file, "syscalls = []\n");
            debug_artifact_close(&artifact);
        }
    }

    if (join_path(artifact_dir, "suggestions.toml", path, sizeof(path))) {
        file = debug_artifact_open(&budget, artifact_dir, "suggestions.toml", &artifact);
        if (file != NULL) {
            fprintf(file, "format = \"aivm_debug_suggestions_v1\"\n");
            fprintf(file, "next = [\"aivm-debug explain %s\", \"verify the input is a valid AiBC v2 file\"]\n", artifact_dir);
            debug_artifact_close(&artifact);
        }
    }
}

static void print_matching_toml_value(const char* path, const char* key, const char* label)
{
    FILE* file = fopen(path, "rb");
    char line[1024];
    size_t key_len = strlen(key);
    if (file == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, key, key_len) == 0) {
            char* value = strchr(line, '=');
            if (value != NULL) {
                value += 1;
                while (*value == ' ' || *value == '\t') {
                    value += 1;
                }
                printf("%s: %s", label, value);
                if (strchr(value, '\n') == NULL) {
                    printf("\n");
                }
                break;
            }
        }
    }
    fclose(file);
}

static void print_matching_toml_prefix(const char* path, const char* prefix, const char* label)
{
    FILE* file = fopen(path, "rb");
    char line[1024];
    size_t prefix_len = strlen(prefix);
    if (file == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, prefix, prefix_len) == 0) {
            printf("%s: %s", label, line);
            if (strchr(line, '\n') == NULL) {
                printf("\n");
            }
            break;
        }
    }
    fclose(file);
}

static void print_all_matching_toml_prefix(const char* path, const char* prefix, const char* label)
{
    FILE* file = fopen(path, "rb");
    char line[1024];
    size_t prefix_len = strlen(prefix);
    if (file == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, prefix, prefix_len) == 0) {
            printf("%s: %s", label, line);
            if (strchr(line, '\n') == NULL) {
                printf("\n");
            }
        }
    }
    fclose(file);
}

static int read_matching_toml_value(const char* path, const char* key, char* out, size_t out_len)
{
    FILE* file = fopen(path, "rb");
    char line[1024];
    size_t key_len = strlen(key);
    if (out == NULL || out_len == 0U) {
        if (file != NULL) {
            fclose(file);
        }
        return 0;
    }
    out[0] = '\0';
    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, key, key_len) == 0) {
            char* value = strchr(line, '=');
            size_t value_len;
            if (value == NULL) {
                continue;
            }
            value += 1;
            while (*value == ' ' || *value == '\t') {
                value += 1;
            }
            value_len = strlen(value);
            while (value_len > 0U && (value[value_len - 1U] == '\n' || value[value_len - 1U] == '\r')) {
                value_len -= 1U;
            }
            if (value_len >= out_len) {
                value_len = out_len - 1U;
            }
            memcpy(out, value, value_len);
            out[value_len] = '\0';
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}

static int read_debug_value(
    const char* artifact_dir,
    const char* file_name,
    const char* key,
    char* out,
    size_t out_len)
{
    char path[PATH_MAX];
    if (!join_path(artifact_dir, file_name, path, sizeof(path))) {
        if (out != NULL && out_len > 0U) {
            out[0] = '\0';
        }
        return 0;
    }
    return read_matching_toml_value(path, key, out, out_len);
}

static int explain_debug_run(const char* artifact_dir)
{
    char path[PATH_MAX];
    if (artifact_dir == NULL || artifact_dir[0] == '\0') {
        fprintf(stderr, "%s: explain requires a debug-run directory\n", AIVM_CLI_NAME);
        return 64;
    }
    printf("aivm-debug explain\n");
    printf("artifact_dir: %s\n", artifact_dir);
    if (join_path(artifact_dir, "diagnostics.toml", path, sizeof(path))) {
        print_matching_toml_value(path, "runtime_profile", "runtime_profile");
        print_matching_toml_value(path, "status", "status");
        print_matching_toml_value(path, "phase", "phase");
        print_matching_toml_value(path, "exit_code", "exit_code");
        print_matching_toml_value(path, "program_error_code", "program_error_code");
        print_matching_toml_value(path, "program_error_message", "program_error_message");
        print_matching_toml_value(path, "program_error_offset", "program_error_offset");
        print_matching_toml_value(path, "vm_error_code", "vm_error_code");
        print_matching_toml_value(path, "vm_error_message", "vm_error_message");
        print_matching_toml_value(path, "detail", "detail");
    }
    if (join_path(artifact_dir, "stack_trace.toml", path, sizeof(path))) {
        print_matching_toml_value(path, "current_pc", "current_pc");
        print_matching_toml_value(path, "current_opcode", "current_opcode");
    }
    if (join_path(artifact_dir, "memory.toml", path, sizeof(path))) {
        print_matching_toml_value(path, "stack_count", "stack_count");
        print_matching_toml_value(path, "locals_count", "locals_count");
        print_matching_toml_value(path, "node_count", "node_count");
        print_matching_toml_value(path, "limits", "limits");
    }
    return 0;
}

static int suggest_debug_run(const char* artifact_dir)
{
    char status[128];
    char phase[128];
    char error_code[128];
    char program_error_code[128];
    char opcode[128];
    if (artifact_dir == NULL || artifact_dir[0] == '\0') {
        fprintf(stderr, "%s: suggest requires a debug-run directory\n", AIVM_CLI_NAME);
        return 64;
    }
    (void)read_debug_value(artifact_dir, "diagnostics.toml", "status", status, sizeof(status));
    (void)read_debug_value(artifact_dir, "diagnostics.toml", "phase", phase, sizeof(phase));
    (void)read_debug_value(artifact_dir, "diagnostics.toml", "vm_error_code", error_code, sizeof(error_code));
    (void)read_debug_value(artifact_dir, "diagnostics.toml", "program_error_code", program_error_code, sizeof(program_error_code));
    (void)read_debug_value(artifact_dir, "stack_trace.toml", "current_opcode", opcode, sizeof(opcode));

    printf("aivm-debug suggest\n");
    printf("artifact_dir: %s\n", artifact_dir);
    printf("status: %s\n", status[0] == '\0' ? "\"unknown\"" : status);
    printf("phase: %s\n", phase[0] == '\0' ? "\"unknown\"" : phase);
    if (program_error_code[0] != '\0') {
        printf("program_error_code: %s\n", program_error_code);
    }
    printf("vm_error_code: %s\n", error_code[0] == '\0' ? "\"unknown\"" : error_code);
    printf("current_opcode: %s\n", opcode[0] == '\0' ? "\"unknown\"" : opcode);
    printf("next:\n");
    printf("- aivm-debug explain %s\n", artifact_dir);
    if (strcmp(phase, "\"load\"") == 0) {
        printf("- verify the input is a valid AiBC v2 file\n");
        printf("- rebuild the program before debugging VM execution\n");
    } else if (strcmp(status, "\"error\"") == 0 || (error_code[0] != '\0' && strcmp(error_code, "\"AIVM000\"") != 0)) {
        printf("- aivm-debug inspect stack %s\n", artifact_dir);
        printf("- aivm-debug inspect memory %s\n", artifact_dir);
        printf("- inspect diagnostics.toml detail before changing code\n");
    } else {
        printf("- aivm-debug inspect profile %s\n", artifact_dir);
        printf("- aivm-debug inspect syscalls %s\n", artifact_dir);
    }
    printf("- capture a second run and compare when validating a fix\n");
    return 0;
}

static void print_compare_value(
    const char* left_dir,
    const char* right_dir,
    const char* file_name,
    const char* key,
    const char* label)
{
    char left[256];
    char right[256];
    int has_left = read_debug_value(left_dir, file_name, key, left, sizeof(left));
    int has_right = read_debug_value(right_dir, file_name, key, right, sizeof(right));
    printf("%s: left=%s right=%s changed=%s\n",
        label,
        has_left ? left : "<missing>",
        has_right ? right : "<missing>",
        (has_left == has_right && has_left != 0 && strcmp(left, right) == 0) ? "false" : "true");
}

static int compare_debug_runs(const char* left_dir, const char* right_dir)
{
    if (left_dir == NULL || left_dir[0] == '\0' || right_dir == NULL || right_dir[0] == '\0') {
        fprintf(stderr, "%s: compare requires two debug-run directories\n", AIVM_CLI_NAME);
        return 64;
    }
    printf("aivm-debug compare\n");
    printf("left: %s\n", left_dir);
    printf("right: %s\n", right_dir);
    print_compare_value(left_dir, right_dir, "diagnostics.toml", "status", "status");
    print_compare_value(left_dir, right_dir, "diagnostics.toml", "phase", "phase");
    print_compare_value(left_dir, right_dir, "diagnostics.toml", "exit_code", "exit_code");
    print_compare_value(left_dir, right_dir, "diagnostics.toml", "program_error_code", "program_error_code");
    print_compare_value(left_dir, right_dir, "diagnostics.toml", "vm_error_code", "vm_error_code");
    print_compare_value(left_dir, right_dir, "diagnostics.toml", "detail", "detail");
    print_compare_value(left_dir, right_dir, "stack_trace.toml", "current_pc", "current_pc");
    print_compare_value(left_dir, right_dir, "stack_trace.toml", "current_opcode", "current_opcode");
    print_compare_value(left_dir, right_dir, "memory.toml", "stack_count", "stack_count");
    print_compare_value(left_dir, right_dir, "memory.toml", "locals_count", "locals_count");
    print_compare_value(left_dir, right_dir, "memory.toml", "node_count", "node_count");
    print_compare_value(left_dir, right_dir, "profile.toml", "instruction_count", "instruction_count");
    print_compare_value(left_dir, right_dir, "profile.toml", "syscall_count", "syscall_count");
    print_compare_value(left_dir, right_dir, "profile.toml", "syscall_elapsed_seconds", "syscall_elapsed_seconds");
    return 0;
}

static int inspect_debug_stack(const char* artifact_dir)
{
    char path[PATH_MAX];
    if (artifact_dir == NULL || artifact_dir[0] == '\0') {
        fprintf(stderr, "%s: inspect stack requires a debug-run directory\n", AIVM_CLI_NAME);
        return 64;
    }
    printf("aivm-debug inspect stack\n");
    printf("artifact_dir: %s\n", artifact_dir);
    if (!join_path(artifact_dir, "stack_trace.toml", path, sizeof(path))) {
        return 1;
    }
    print_matching_toml_value(path, "current_pc", "current_pc");
    print_matching_toml_value(path, "current_opcode", "current_opcode");
    print_matching_toml_prefix(path, "  { index = 0,", "current_frame");
    return 0;
}

static int inspect_debug_memory(const char* artifact_dir)
{
    char path[PATH_MAX];
    if (artifact_dir == NULL || artifact_dir[0] == '\0') {
        fprintf(stderr, "%s: inspect memory requires a debug-run directory\n", AIVM_CLI_NAME);
        return 64;
    }
    printf("aivm-debug inspect memory\n");
    printf("artifact_dir: %s\n", artifact_dir);
    if (!join_path(artifact_dir, "memory.toml", path, sizeof(path))) {
        return 1;
    }
    print_matching_toml_value(path, "stack_count", "stack_count");
    print_matching_toml_value(path, "locals_count", "locals_count");
    print_matching_toml_value(path, "string_arena_used", "string_arena_used");
    print_matching_toml_value(path, "string_arena_high_water", "string_arena_high_water");
    print_matching_toml_value(path, "bytes_arena_used", "bytes_arena_used");
    print_matching_toml_value(path, "bytes_arena_high_water", "bytes_arena_high_water");
    print_matching_toml_value(path, "node_count", "node_count");
    print_matching_toml_value(path, "node_high_water", "node_high_water");
    print_matching_toml_value(path, "limits", "limits");
    return 0;
}

static int inspect_debug_profile(const char* artifact_dir)
{
    char path[PATH_MAX];
    if (artifact_dir == NULL || artifact_dir[0] == '\0') {
        fprintf(stderr, "%s: inspect profile requires a debug-run directory\n", AIVM_CLI_NAME);
        return 64;
    }
    printf("aivm-debug inspect profile\n");
    printf("artifact_dir: %s\n", artifact_dir);
    if (!join_path(artifact_dir, "profile.toml", path, sizeof(path))) {
        return 1;
    }
    print_matching_toml_value(path, "status", "status");
    print_matching_toml_value(path, "elapsed_seconds", "elapsed_seconds");
    print_matching_toml_value(path, "instruction_count", "instruction_count");
    print_all_matching_toml_prefix(path, "  { opcode =", "opcode_count");
    print_matching_toml_value(path, "syscall_count", "syscall_count");
    print_matching_toml_value(path, "syscall_elapsed_seconds", "syscall_elapsed_seconds");
    print_all_matching_toml_prefix(path, "  { target =", "syscall_count_by_target");
    print_matching_toml_value(path, "note", "note");
    return 0;
}

static int inspect_debug_syscalls(const char* artifact_dir)
{
    char path[PATH_MAX];
    if (artifact_dir == NULL || artifact_dir[0] == '\0') {
        fprintf(stderr, "%s: inspect syscalls requires a debug-run directory\n", AIVM_CLI_NAME);
        return 64;
    }
    printf("aivm-debug inspect syscalls\n");
    printf("artifact_dir: %s\n", artifact_dir);
    if (!join_path(artifact_dir, "syscall_trace.toml", path, sizeof(path))) {
        return 1;
    }
    print_matching_toml_value(path, "format", "format");
    print_matching_toml_value(path, "syscall_count", "syscall_count");
    print_matching_toml_value(path, "syscall_elapsed_seconds", "syscall_elapsed_seconds");
    print_all_matching_toml_prefix(path, "  { target =", "syscall");
    return 0;
}

static int inspect_debug_run(int argc, char** argv)
{
    if (argc != 4) {
        print_usage(stderr);
        return 64;
    }
    if (strcmp(argv[2], "stack") == 0) {
        return inspect_debug_stack(argv[3]);
    }
    if (strcmp(argv[2], "memory") == 0) {
        return inspect_debug_memory(argv[3]);
    }
    if (strcmp(argv[2], "profile") == 0) {
        return inspect_debug_profile(argv[3]);
    }
    if (strcmp(argv[2], "syscalls") == 0) {
        return inspect_debug_syscalls(argv[3]);
    }
    fprintf(stderr, "%s: unknown inspect target: %s\n", AIVM_CLI_NAME, argv[2]);
    return 64;
}
#endif

static int execute_bytes(
    const uint8_t* bytes,
    size_t byte_count,
    const char* const* process_argv,
    size_t process_argv_count,
    const char* debug_artifact_dir,
    const char* program_path,
    AivmRuntimeProfile runtime_profile,
    const AivmSyscallCapabilityPolicy* syscall_policy)
{
    AivmProgram program;
    static AivmVm vm;
    AivmProgramLoadResult load_result;
    int ok;
#if defined(AIVM_DEBUG_RUNTIME)
    clock_t profile_start;
    clock_t profile_end;
    double elapsed_seconds;
#endif
    static const AivmSyscallBinding bindings[] = {
        { "sys.stdout.writeLine", aivm_cli_stdout_write_line },
        { "io.print", aivm_cli_stdout_write_line },
        { "io.write", aivm_cli_stdout_write_line },
        { "sys.process.exit", native_syscall_process_exit },
        { "sys.process.args", native_syscall_process_argv },
        { "sys.process.cwd", native_syscall_process_cwd },
        { "sys.process.env.get", native_syscall_process_env_get },
        { "sys.process.spawn", native_syscall_process_spawn },
        { "sys.process.wait", native_syscall_process_wait },
        { "sys.process.kill", native_syscall_process_kill },
        { "sys.process.stdout.read", native_syscall_process_stdout_read },
        { "sys.process.stderr.read", native_syscall_process_stderr_read },
        { "sys.process.poll", native_syscall_process_poll },
        { "sys.host.openDefault", native_syscall_host_open_default },
        { "sys.fs.path.exists", aivm_cli_path_exists },
        { "sys.fs.dir.create", aivm_cli_dir_create },
        { "sys.fs.dir.delete", aivm_cli_dir_delete },
        { "sys.fs.file.delete", aivm_cli_file_delete },
        { "sys.fs.file.write", aivm_cli_file_write },
        { "sys.fs.file.read", aivm_cli_file_read },
        { "sys.bytes.fromUtf8String", aivm_cli_bytes_from_utf8_string },
        { "sys.bytes.toUtf8String", aivm_cli_bytes_to_utf8_string },
        { "sys.str.find", aivm_cli_str_find },
        { "sys.str.substring", aivm_cli_str_substring }
    };

    load_result = aivm_program_load_aibc1(bytes, byte_count, &program);
    if (load_result.status != AIVM_PROGRAM_OK) {
#if defined(AIVM_DEBUG_RUNTIME)
        write_debug_load_failure_artifacts(debug_artifact_dir, program_path, runtime_profile, syscall_policy, load_result);
#else
        (void)debug_artifact_dir;
        (void)program_path;
        (void)runtime_profile;
        (void)syscall_policy;
#endif
        fprintf(
            stderr,
            "aivm: load failed: %s at byte %zu\n",
            aivm_program_status_message(load_result.status),
            load_result.error_offset);
        return 2;
    }

    g_native_active_vm = &vm;
#if defined(AIVM_DEBUG_RUNTIME)
    profile_start = clock();
#endif
    aivm_init_with_syscalls_and_argv(
        &vm,
        &program,
        bindings,
        sizeof(bindings) / sizeof(bindings[0]),
        process_argv,
        process_argv_count);
    aivm_set_runtime_profile(&vm, runtime_profile);
    if (syscall_policy != NULL) {
        aivm_set_syscall_policy(&vm, syscall_policy);
    }
    aivm_run(&vm);
    ok = vm.status != AIVM_VM_STATUS_ERROR;
#if defined(AIVM_DEBUG_RUNTIME)
    profile_end = clock();
    elapsed_seconds = (double)(profile_end - profile_start) / (double)CLOCKS_PER_SEC;
#endif
    g_native_active_vm = NULL;

    if (!ok) {
#if defined(AIVM_DEBUG_RUNTIME)
        write_debug_artifacts(debug_artifact_dir, program_path, &program, &vm, 0, 3, elapsed_seconds);
#else
        (void)debug_artifact_dir;
        (void)program_path;
#endif
        fprintf(
            stderr,
            "aivm: execution failed: status=%d error=%d",
            (int)vm.status,
            (int)vm.error);
        if (aivm_vm_error_detail(&vm) != NULL && aivm_vm_error_detail(&vm)[0] != '\0') {
            fprintf(stderr, " detail=%s", aivm_vm_error_detail(&vm));
        }
        fprintf(stderr, "\n");
        return 3;
    }
    if (vm.status == AIVM_VM_STATUS_HALTED && vm.stack_count > 0U) {
        AivmValue top = vm.stack[vm.stack_count - 1U];
        if (top.type == AIVM_VAL_INT) {
#if defined(AIVM_DEBUG_RUNTIME)
            write_debug_artifacts(debug_artifact_dir, program_path, &program, &vm, 1, (int)top.int_value, elapsed_seconds);
#endif
            return (int)top.int_value;
        }
    }
#if defined(AIVM_DEBUG_RUNTIME)
    write_debug_artifacts(debug_artifact_dir, program_path, &program, &vm, 1, 0, elapsed_seconds);
#endif
    return 0;
}

static int run_program(const char* path, const char* const* process_argv, size_t process_argv_count)
{
    uint8_t* bytes;
    size_t byte_count;
    int exit_code;

    if (!read_file(path, &bytes, &byte_count)) {
        return 1;
    }
    exit_code = execute_bytes(
        bytes,
        byte_count,
        process_argv,
        process_argv_count,
        NULL,
        path,
        aivm_runtime_default_profile(),
        NULL);
    free(bytes);
    return exit_code;
}

#if defined(AIVM_DEBUG_RUNTIME)
static int debug_capture_run(int argc, char** argv)
{
    const char* artifact_dir = ".tmp/aivm-debug-run";
    const char* program_path = NULL;
    const char* const* process_argv = NULL;
    size_t process_argv_count = 0U;
    AivmRuntimeProfile runtime_profile = aivm_runtime_default_profile();
    AivmSyscallCapabilityPolicy syscall_policy;
    DebugCapabilityOverride capability_overrides[32];
    size_t capability_override_count = 0U;
    uint8_t* bytes;
    size_t byte_count;
    int exit_code;
    int i;
    size_t override_index;
    char stdout_path[PATH_MAX];
    char stderr_path[PATH_MAX];

    if (argc < 5) {
        print_usage(stderr);
        return 64;
    }
    for (i = 4; i < argc; i += 1) {
        if (strcmp(argv[i], "--out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --out requires a directory\n", AIVM_CLI_NAME);
                return 64;
            }
            artifact_dir = argv[i + 1];
            i += 1;
        } else if (strncmp(argv[i], "--out=", 6U) == 0) {
            artifact_dir = argv[i] + 6;
        } else if (strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc || !aivm_runtime_profile_from_name(argv[i + 1], &runtime_profile)) {
                fprintf(stderr, "%s: --profile must be production, debug, or tooling\n", AIVM_CLI_NAME);
                return 64;
            }
            i += 1;
        } else if (strncmp(argv[i], "--profile=", 10U) == 0) {
            if (!aivm_runtime_profile_from_name(argv[i] + 10, &runtime_profile)) {
                fprintf(stderr, "%s: --profile must be production, debug, or tooling\n", AIVM_CLI_NAME);
                return 64;
            }
        } else if (strcmp(argv[i], "--allow") == 0 || strcmp(argv[i], "--deny") == 0) {
            AivmSyscallCapabilityGroup capability;
            if (i + 1 >= argc || !aivm_syscall_capability_from_name(argv[i + 1], &capability)) {
                fprintf(stderr, "%s: %s requires a syscall capability group\n", AIVM_CLI_NAME, argv[i]);
                return 64;
            }
            if (capability_override_count >= sizeof(capability_overrides) / sizeof(capability_overrides[0])) {
                fprintf(stderr, "%s: too many capability policy overrides\n", AIVM_CLI_NAME);
                return 64;
            }
            capability_overrides[capability_override_count].capability = capability;
            capability_overrides[capability_override_count].allow = strcmp(argv[i], "--allow") == 0;
            capability_override_count += 1U;
            i += 1;
        } else if (strncmp(argv[i], "--allow=", 8U) == 0 || strncmp(argv[i], "--deny=", 7U) == 0) {
            AivmSyscallCapabilityGroup capability;
            const int is_allow = strncmp(argv[i], "--allow=", 8U) == 0;
            const char* capability_name = argv[i] + (is_allow ? 8 : 7);
            if (!aivm_syscall_capability_from_name(capability_name, &capability)) {
                fprintf(stderr, "%s: %s is not a known syscall capability group\n", AIVM_CLI_NAME, capability_name);
                return 64;
            }
            if (capability_override_count >= sizeof(capability_overrides) / sizeof(capability_overrides[0])) {
                fprintf(stderr, "%s: too many capability policy overrides\n", AIVM_CLI_NAME);
                return 64;
            }
            capability_overrides[capability_override_count].capability = capability;
            capability_overrides[capability_override_count].allow = is_allow;
            capability_override_count += 1U;
        } else if (strcmp(argv[i], "--") == 0) {
            if (program_path == NULL) {
                fprintf(stderr, "%s: debug capture run requires a program before --\n", AIVM_CLI_NAME);
                return 64;
            }
            if (i + 1 < argc) {
                process_argv = (const char* const*)&argv[i + 1];
                process_argv_count = (size_t)(argc - i - 1);
            }
            break;
        } else if (program_path == NULL) {
            program_path = argv[i];
        } else {
            process_argv = (const char* const*)&argv[i];
            process_argv_count = (size_t)(argc - i);
            break;
        }
    }

    if (program_path == NULL) {
        print_usage(stderr);
        return 64;
    }
    if (runtime_profile == AIVM_RUNTIME_PROFILE_PRODUCTION) {
        aivm_syscall_policy_allow_production_default(&syscall_policy);
    } else {
        aivm_syscall_policy_allow_all(&syscall_policy);
    }
    for (override_index = 0U; override_index < capability_override_count; override_index += 1U) {
        if (capability_overrides[override_index].allow) {
            aivm_syscall_policy_allow_group(&syscall_policy, capability_overrides[override_index].capability);
        } else {
            aivm_syscall_policy_deny_group(&syscall_policy, capability_overrides[override_index].capability);
        }
    }
    if (!read_file(program_path, &bytes, &byte_count)) {
        return 1;
    }
    if (!ensure_directory(artifact_dir)) {
        fprintf(stderr, "%s: failed to create debug artifact directory: %s\n", AIVM_CLI_NAME, artifact_dir);
        free(bytes);
        return 1;
    }
    if (join_path(artifact_dir, "stdout.txt", stdout_path, sizeof(stdout_path))) {
        g_debug_stdout_capture = fopen(stdout_path, "wb");
    }
    if (join_path(artifact_dir, "stderr.txt", stderr_path, sizeof(stderr_path))) {
        FILE* stderr_capture = fopen(stderr_path, "wb");
        if (stderr_capture != NULL) {
            fclose(stderr_capture);
        }
    }
    exit_code = execute_bytes(
        bytes,
        byte_count,
        process_argv,
        process_argv_count,
        artifact_dir,
        program_path,
        runtime_profile,
        &syscall_policy);
    if (g_debug_stdout_capture != NULL) {
        fclose(g_debug_stdout_capture);
        g_debug_stdout_capture = NULL;
    }
    free(bytes);
    return exit_code;
}

static int parse_positive_ulong(const char* text, unsigned long* out_value)
{
    char* end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0UL) {
        return 0;
    }
    *out_value = value;
    return 1;
}

static int benchmark_program(int argc, char** argv)
{
    const char* path = NULL;
    const char* const* process_argv = NULL;
    size_t process_argv_count = 0U;
    unsigned long iterations = 10UL;
    unsigned long i;
    uint8_t* bytes;
    size_t byte_count;
    clock_t start;
    clock_t end;
    double elapsed_seconds;
    int exit_code = 0;

    for (i = 2UL; i < (unsigned long)argc; i += 1UL) {
        if (strcmp(argv[i], "--iterations") == 0) {
            if (i + 1UL >= (unsigned long)argc || !parse_positive_ulong(argv[i + 1UL], &iterations)) {
                fprintf(stderr, "%s: invalid --iterations value\n", AIVM_CLI_NAME);
                return 64;
            }
            i += 1UL;
        } else if (strncmp(argv[i], "--iterations=", 13U) == 0) {
            if (!parse_positive_ulong(argv[i] + 13, &iterations)) {
                fprintf(stderr, "%s: invalid --iterations value\n", AIVM_CLI_NAME);
                return 64;
            }
        } else if (path == NULL) {
            path = argv[i];
            if (i + 1UL < (unsigned long)argc) {
                process_argv = (const char* const*)&argv[i + 1UL];
                process_argv_count = (size_t)((unsigned long)argc - i - 1UL);
                break;
            }
        } else {
            fprintf(stderr, "%s: unexpected argument: %s\n", AIVM_CLI_NAME, argv[i]);
            return 64;
        }
    }

    if (path == NULL) {
        print_usage(stderr);
        return 64;
    }
    if (!read_file(path, &bytes, &byte_count)) {
        return 1;
    }

    start = clock();
    for (i = 0UL; i < iterations; i += 1UL) {
        exit_code = execute_bytes(
            bytes,
            byte_count,
            process_argv,
            process_argv_count,
            NULL,
            path,
            aivm_runtime_default_profile(),
            NULL);
        if (exit_code != 0) {
            free(bytes);
            return exit_code;
        }
    }
    end = clock();
    free(bytes);

    elapsed_seconds = (double)(end - start) / (double)CLOCKS_PER_SEC;
    printf(
        "aivm.benchmark iterations=%lu elapsed_seconds=%.6f average_seconds=%.9f\n",
        iterations,
        elapsed_seconds,
        elapsed_seconds / (double)iterations);
    return 0;
}
#endif

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
#if defined(AIVM_DEBUG_RUNTIME)
        printf("aivm-debug abi=%u diagnostics=enabled\n", (unsigned int)aivm_c_abi_version());
#else
        printf("aivm abi=%u diagnostics=stripped\n", (unsigned int)aivm_c_abi_version());
#endif
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage(stdout);
        return 0;
    }
#if defined(AIVM_DEBUG_RUNTIME)
    if (argc >= 2 && strcmp(argv[1], "explain") == 0) {
        if (argc != 3) {
            print_usage(stderr);
            return 64;
        }
        return explain_debug_run(argv[2]);
    }
    if (argc >= 2 && strcmp(argv[1], "suggest") == 0) {
        if (argc != 3) {
            print_usage(stderr);
            return 64;
        }
        return suggest_debug_run(argv[2]);
    }
    if (argc >= 2 && strcmp(argv[1], "compare") == 0) {
        if (argc != 4) {
            print_usage(stderr);
            return 64;
        }
        return compare_debug_runs(argv[2], argv[3]);
    }
    if (argc >= 2 && strcmp(argv[1], "inspect") == 0) {
        return inspect_debug_run(argc, argv);
    }
    if (argc >= 4 &&
        strcmp(argv[1], "debug") == 0 &&
        strcmp(argv[2], "capture") == 0 &&
        strcmp(argv[3], "run") == 0) {
        return debug_capture_run(argc, argv);
    }
    if (argc >= 3 && strcmp(argv[1], "profile") == 0) {
        const char* const* process_argv = (argc > 3) ? (const char* const*)&argv[3] : NULL;
        size_t process_argv_count = (argc > 3) ? (size_t)(argc - 3) : 0U;
        return run_program(argv[2], process_argv, process_argv_count);
    }
    if (argc >= 3 && strcmp(argv[1], "benchmark") == 0) {
        return benchmark_program(argc, argv);
    }
#endif
    if (argc >= 2 && argv[1][0] != '-') {
        const char* const* process_argv = (argc > 2) ? (const char* const*)&argv[2] : NULL;
        size_t process_argv_count = (argc > 2) ? (size_t)(argc - 2) : 0U;
        return run_program(argv[1], process_argv, process_argv_count);
    }
    print_usage(stderr);
    return 64;
}
