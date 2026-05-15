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
            break;
        case AIVM_VAL_INT:
            printf("%lld\n", (long long)args[0].int_value);
            break;
        case AIVM_VAL_BOOL:
            printf("%s\n", args[0].bool_value ? "true" : "false");
            break;
        case AIVM_VAL_NULL:
            printf("null\n");
            break;
        case AIVM_VAL_VOID:
            printf("void\n");
            break;
        default:
            printf("<value>\n");
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

static int execute_bytes(
    const uint8_t* bytes,
    size_t byte_count,
    const char* const* process_argv,
    size_t process_argv_count)
{
    AivmProgram program;
    static AivmVm vm;
    AivmProgramLoadResult load_result;
    int ok;
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
        fprintf(
            stderr,
            "aivm: load failed: %s at byte %zu\n",
            aivm_program_status_message(load_result.status),
            load_result.error_offset);
        return 2;
    }

    g_native_active_vm = &vm;
    ok = aivm_execute_program_with_syscalls_and_argv(
        &program,
        bindings,
        sizeof(bindings) / sizeof(bindings[0]),
        process_argv,
        process_argv_count,
        &vm);
    g_native_active_vm = NULL;

    if (!ok) {
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
            return (int)top.int_value;
        }
    }
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
    exit_code = execute_bytes(bytes, byte_count, process_argv, process_argv_count);
    free(bytes);
    return exit_code;
}

#if defined(AIVM_DEBUG_RUNTIME)
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
        exit_code = execute_bytes(bytes, byte_count, process_argv, process_argv_count);
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
