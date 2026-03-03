#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aivm_c_api.h"
#include "aivm_program.h"

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
    if (bytes == NULL && length > 0L) {
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
    AivmSyscallBinding bindings[4];
    AivmCResult result;
    const char* const* app_argv = NULL;
    size_t app_argc = 0U;

    if (argc < 2 || argv == NULL) {
        fprintf(stderr, "Usage: aivm-runtime-wasm32 <app.aibc1> [args...]\n");
        return 2;
    }

    if (argc > 2) {
        app_argv = (const char* const*)&argv[2];
        app_argc = (size_t)(argc - 2);
    }

    if (!read_binary_file(argv[1], &bytes, &byte_count)) {
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

    result = aivm_c_execute_program_with_syscalls_and_argv(
        &program,
        bindings,
        4U,
        app_argv,
        app_argc);

    if (!result.loaded || result.load_status != AIVM_PROGRAM_OK) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Failed to load native program.\" nodeId=program)\n");
        return 2;
    }
    if (!result.ok || result.status == AIVM_VM_STATUS_ERROR) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"AiBC1 execution failed.\" nodeId=vm)\n");
        return 3;
    }
    if (result.has_exit_code) {
        printf("Ok#ok1(type=int value=%d)\n", result.exit_code);
        return result.exit_code;
    }

    return 0;
}
