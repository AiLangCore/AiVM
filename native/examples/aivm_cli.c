#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aivm_c_api.h"
#include "aivm_program.h"

static void print_usage(FILE* stream)
{
    fprintf(stream, "Usage: aivm --version\n");
    fprintf(stream, "       aivm run <program.aibc1>\n");
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

static int run_program(const char* path)
{
    uint8_t* bytes;
    size_t byte_count;
    AivmCResult result;

    if (!read_file(path, &bytes, &byte_count)) {
        return 1;
    }
    result = aivm_c_execute_aibc1(bytes, byte_count);
    free(bytes);
    if (!result.loaded) {
        fprintf(
            stderr,
            "aivm: load failed: %s at byte %zu\n",
            aivm_program_status_message(result.load_status),
            result.load_error_offset);
        return 2;
    }
    if (!result.ok) {
        fprintf(
            stderr,
            "aivm: execution failed: status=%d error=%d",
            (int)result.status,
            (int)result.error);
        fprintf(stderr, "\n");
        return 3;
    }
    if (result.has_exit_code) {
        return result.exit_code;
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("aivm abi=%u\n", (unsigned int)aivm_c_abi_version());
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage(stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "run") == 0) {
        return run_program(argv[2]);
    }
    print_usage(stderr);
    return 64;
}
