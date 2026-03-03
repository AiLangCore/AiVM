#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aivm_program.h"
#include "aivm_runtime.h"
#include "aivm_vm.h"

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

static int native_syscall_http_get(
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
    if (arg_count != 1U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_string("ERR http_get unavailable in wasm runtime.");
    return AIVM_SYSCALL_OK;
}

int main(int argc, char** argv)
{
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    AivmProgram program;
    AivmProgramLoadResult load_result;
    AivmSyscallBinding bindings[5];
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

    bindings[0].target = "sys.stdout_writeLine";
    bindings[0].handler = native_syscall_stdout_write_line;
    bindings[1].target = "io.print";
    bindings[1].handler = native_syscall_stdout_write_line;
    bindings[2].target = "io.write";
    bindings[2].handler = native_syscall_stdout_write_line;
    bindings[3].target = "sys.process_argv";
    bindings[3].handler = native_syscall_process_argv;
    bindings[4].target = "sys.http_get";
    bindings[4].handler = native_syscall_http_get;

    if (!aivm_execute_program_with_syscalls_and_argv(
            &program,
            bindings,
            5U,
            app_argv,
            app_argc,
            &vm) ||
        vm.status == AIVM_VM_STATUS_ERROR) {
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
