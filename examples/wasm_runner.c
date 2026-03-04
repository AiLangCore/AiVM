#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "aivm_program.h"
#include "aivm_runtime.h"
#include "sys/aivm_syscall_contracts.h"
#include "aivm_vm.h"

static const char* g_wasm_syscall_error_message = NULL;
static const char* g_wasm_syscall_error_code = NULL;
static char g_wasm_syscall_error_message_buf[256];

static int native_syscall_unavailable(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)args;
    (void)arg_count;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL) {
        target = "sys.unknown";
    }
    if (snprintf(
            g_wasm_syscall_error_message_buf,
            sizeof(g_wasm_syscall_error_message_buf),
            "%s is not available on this target.",
            target) <= 0) {
        g_wasm_syscall_error_message = "syscall is not available on this target.";
    } else {
        g_wasm_syscall_error_message = g_wasm_syscall_error_message_buf;
    }
    g_wasm_syscall_error_code = "RUN101";
    result->type = AIVM_VAL_VOID;
    return AIVM_SYSCALL_ERR_NOT_FOUND;
}

static int read_binary_file(const char* path, unsigned char** out_bytes, size_t* out_size)
{
    FILE* f;
    unsigned char* bytes;
    size_t capacity = 0U;
    size_t used = 0U;
    size_t nread;
    unsigned char chunk[4096];

    if (path == NULL || out_bytes == NULL || out_size == NULL) {
        return 0;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    bytes = NULL;
    for (;;) {
        nread = fread(chunk, 1U, sizeof(chunk), f);
        if (nread > 0U) {
            unsigned char* grown;
            if (used + nread > capacity) {
                size_t new_capacity = (capacity == 0U) ? 4096U : capacity;
                while (new_capacity < used + nread) {
                    if (new_capacity > ((size_t)-1) / 2U) {
                        free(bytes);
                        fclose(f);
                        return 0;
                    }
                    new_capacity *= 2U;
                }
                grown = (unsigned char*)realloc(bytes, new_capacity);
                if (grown == NULL) {
                    free(bytes);
                    fclose(f);
                    return 0;
                }
                bytes = grown;
                capacity = new_capacity;
            }
            memcpy(bytes + used, chunk, nread);
            used += nread;
        }
        if (nread < sizeof(chunk)) {
            if (ferror(f) != 0) {
                free(bytes);
                fclose(f);
                return 0;
            }
            break;
        }
    }
    fclose(f);

    *out_bytes = bytes;
    *out_size = used;
    return 1;
}

static int read_binary_stdin(unsigned char** out_bytes, size_t* out_size)
{
    unsigned char* bytes;
    size_t capacity = 0U;
    size_t used = 0U;
    size_t nread;
    unsigned char chunk[4096];

    if (out_bytes == NULL || out_size == NULL) {
        return 0;
    }

    bytes = NULL;
    for (;;) {
        nread = fread(chunk, 1U, sizeof(chunk), stdin);
        if (nread > 0U) {
            unsigned char* grown;
            if (used + nread > capacity) {
                size_t new_capacity = (capacity == 0U) ? 4096U : capacity;
                while (new_capacity < used + nread) {
                    if (new_capacity > ((size_t)-1) / 2U) {
                        free(bytes);
                        return 0;
                    }
                    new_capacity *= 2U;
                }
                grown = (unsigned char*)realloc(bytes, new_capacity);
                if (grown == NULL) {
                    free(bytes);
                    return 0;
                }
                bytes = grown;
                capacity = new_capacity;
            }
            memcpy(bytes + used, chunk, nread);
            used += nread;
        }
        if (nread < sizeof(chunk)) {
            if (ferror(stdin) != 0) {
                free(bytes);
                return 0;
            }
            break;
        }
    }

    *out_bytes = bytes;
    *out_size = used;
    return 1;
}

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

static int native_syscall_process_argv(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_node(1);
    return AIVM_SYSCALL_OK;
}

int main(int argc, char** argv)
{
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    AivmProgram program;
    AivmProgramLoadResult load_result;
    AivmSyscallBinding bindings[128];
    size_t binding_count = 0U;
    uint32_t syscall_id;
    static AivmVm vm;
    const char* const* app_argv = NULL;
    size_t app_argc = 0U;

    if (argc < 2 || argv == NULL) {
        fprintf(stderr, "Usage: aivm-runtime-wasm32 <app.aibc1|- for stdin> [args...]\n");
        return 2;
    }

    if (argc > 2) {
        app_argv = (const char* const*)&argv[2];
        app_argc = (size_t)(argc - 2);
    }

    if ((strcmp(argv[1], "-") == 0 && !read_binary_stdin(&bytes, &byte_count)) ||
        (strcmp(argv[1], "-") != 0 && !read_binary_file(argv[1], &bytes, &byte_count))) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Failed to read AiBC1 file.\" nodeId=program)\n");
        return 2;
    }

    load_result = aivm_program_load_aibc1(bytes, byte_count, &program);
    free(bytes);
    if (load_result.status != AIVM_PROGRAM_OK) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Failed to load AiBC1 program.\" nodeId=program)\n");
        return 2;
    }

    for (syscall_id = 0U; syscall_id <= 255U; syscall_id += 1U) {
        const AivmSyscallContract* contract = aivm_syscall_contract_find_by_id(syscall_id);
        if (contract == NULL) {
            continue;
        }
        if (binding_count >= (sizeof(bindings) / sizeof(bindings[0]))) {
            fprintf(stderr, "Err#err1(code=RUN001 message=\"Wasm syscall binding overflow.\" nodeId=syscall)\n");
            return 2;
        }
        bindings[binding_count].target = contract->target;
        bindings[binding_count].handler = native_syscall_unavailable;
        if (strcmp(contract->target, "sys.stdout.writeLine") == 0 ||
            strcmp(contract->target, "io.print") == 0 ||
            strcmp(contract->target, "io.write") == 0) {
            bindings[binding_count].handler = native_syscall_stdout_write_line;
        } else if (strcmp(contract->target, "sys.process.args") == 0) {
            bindings[binding_count].handler = native_syscall_process_argv;
        }
        binding_count += 1U;
    }

    if (!aivm_execute_program_with_syscalls_and_argv(
            &program,
            bindings,
            binding_count,
            app_argv,
            app_argc,
            &vm) ||
        vm.status == AIVM_VM_STATUS_ERROR) {
        if (g_wasm_syscall_error_message != NULL) {
            fprintf(
                stderr,
                "Err#err1(code=%s message=\"%s\" nodeId=vm)\n",
                (g_wasm_syscall_error_code != NULL) ? g_wasm_syscall_error_code : "RUN001",
                g_wasm_syscall_error_message);
            return 3;
        }
        fprintf(stderr, "Err#err1(code=RUN001 message=\"AiBC1 execution failed.\" nodeId=vm)\n");
        return 3;
    }
    if (vm.status == AIVM_VM_STATUS_HALTED && vm.stack_count > 0U) {
        const AivmValue* top = &vm.stack[vm.stack_count - 1U];
        if (top->type == AIVM_VAL_INT) {
            printf("Ok#ok1(type=int value=%d)\n", (int)top->int_value);
            return (int)top->int_value;
        }
    }

    return 0;
}
