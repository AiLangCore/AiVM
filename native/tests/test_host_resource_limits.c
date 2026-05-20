#define AIRUN_ALLOW_INTERNAL_UI_FALLBACK 1
#define AIRUN_TEST_HOST_RESOURCE_LIMIT_BYTES 8
#define AIRUN_TEST_HOST_RESOURCE_LIMIT_WORKER_COUNT 2
#define main airun_embedded_main_for_test
#include "../ailang_cli/ailang.c"
#undef main

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d\n", __LINE__); \
            remove("aivm-host-limit-small.bin"); \
            remove("aivm-host-limit-large.bin"); \
            remove("aivm-host-limit-write.bin"); \
            remove("aivm-host-limit-chunk-write.bin"); \
            return 1; \
        } \
    } while (0)

static int write_test_file(const char* path, const unsigned char* bytes, size_t byte_count)
{
    FILE* file = fopen(path, "wb");
    size_t wrote = 0U;
    if (file == NULL) {
        return 0;
    }
    if (byte_count > 0U) {
        wrote = fwrite(bytes, 1U, byte_count, file);
    }
    if (fclose(file) != 0) {
        return 0;
    }
    return wrote == byte_count;
}

int main(void)
{
    const unsigned char small_bytes[] = { 'o', 'k' };
    const unsigned char large_bytes[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8' };
    AivmValue args[2];
    AivmValue one_arg[1];
    AivmValue result;
    AivmSyscallStatus status;
    AivmRuntimeProfileLimits limits;
    int64_t handle;

    remove("aivm-host-limit-small.bin");
    remove("aivm-host-limit-large.bin");
    remove("aivm-host-limit-write.bin");
    remove("aivm-host-limit-chunk-write.bin");

    limits = aivm_runtime_profile_limits(AIVM_RUNTIME_PROFILE_PRODUCTION);
    CHECK(limits.file_read_bytes == AIVM_VM_FILE_READ_BYTES);
    CHECK(limits.file_write_bytes == AIVM_VM_FILE_WRITE_BYTES);
    CHECK(limits.network_read_bytes == AIVM_VM_NETWORK_READ_BYTES);
    CHECK(limits.process_count == AIVM_VM_PROCESS_COUNT);
    CHECK(limits.worker_count == AIVM_VM_WORKER_COUNT);

    CHECK(write_test_file("aivm-host-limit-small.bin", small_bytes, sizeof(small_bytes)));
    CHECK(write_test_file("aivm-host-limit-large.bin", large_bytes, sizeof(large_bytes)));

    args[0] = aivm_value_string("aivm-host-limit-small.bin");
    status = native_syscall_fs_file_read("sys.fs.file.read", args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BYTES);
    CHECK(result.bytes_value.length == sizeof(small_bytes));

    args[0] = aivm_value_string("aivm-host-limit-large.bin");
    status = native_syscall_fs_file_read("sys.fs.file.read", args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_RESOURCE_LIMIT);
    CHECK(strcmp(aivm_syscall_status_code(status), "AIVMS007") == 0);

    args[0] = aivm_value_string("aivm-host-limit-write.bin");
    args[1] = aivm_value_bytes(small_bytes, sizeof(small_bytes));
    status = native_syscall_fs_file_write("sys.fs.file.write", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);

    args[1] = aivm_value_bytes(large_bytes, sizeof(large_bytes));
    status = native_syscall_fs_file_write("sys.fs.file.write", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_RESOURCE_LIMIT);
    CHECK(strcmp(aivm_syscall_status_message(status), "Syscall resource limit exceeded.") == 0);

    args[0] = aivm_value_string("aivm-host-limit-large.bin");
    status = native_syscall_fs_file_open_read("sys.fs.file.openRead", args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value > 0);
    handle = result.int_value;

    args[0] = aivm_value_int(handle);
    args[1] = aivm_value_int(4);
    status = native_syscall_fs_file_read_chunk("sys.fs.file.readChunk", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BYTES);
    CHECK(result.bytes_value.length == 4U);
    CHECK(memcmp(result.bytes_value.data, "0123", 4U) == 0);

    args[1] = aivm_value_int(9);
    status = native_syscall_fs_file_read_chunk("sys.fs.file.readChunk", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_RESOURCE_LIMIT);

    one_arg[0] = aivm_value_int(handle);
    status = native_syscall_fs_file_close("sys.fs.file.close", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 1);

    status = native_syscall_fs_file_close("sys.fs.file.close", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 0);

    args[0] = aivm_value_string("aivm-host-limit-chunk-write.bin");
    status = native_syscall_fs_file_open_write("sys.fs.file.openWrite", args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value > 0);
    handle = result.int_value;

    args[0] = aivm_value_int(handle);
    args[1] = aivm_value_bytes((const uint8_t*)"ABCD", 4U);
    status = native_syscall_fs_file_write_chunk("sys.fs.file.writeChunk", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 4);

    args[1] = aivm_value_bytes(large_bytes, sizeof(large_bytes));
    status = native_syscall_fs_file_write_chunk("sys.fs.file.writeChunk", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_RESOURCE_LIMIT);

    one_arg[0] = aivm_value_int(handle);
    status = native_syscall_fs_file_close("sys.fs.file.close", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 1);

    args[0] = aivm_value_string("aivm-host-limit-chunk-write.bin");
    status = native_syscall_fs_file_read("sys.fs.file.read", args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BYTES);
    CHECK(result.bytes_value.length == 4U);
    CHECK(memcmp(result.bytes_value.data, "ABCD", 4U) == 0);

    args[0] = aivm_value_string("sleep");
    args[1] = aivm_value_string("1000");
    status = native_syscall_worker_start("sys.worker.start", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value > 0);

    status = native_syscall_worker_start("sys.worker.start", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value > 0);

    status = native_syscall_worker_start("sys.worker.start", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_RESOURCE_LIMIT);
    CHECK(strcmp(aivm_syscall_status_code(status), "AIVMS007") == 0);

    remove("aivm-host-limit-small.bin");
    remove("aivm-host-limit-large.bin");
    remove("aivm-host-limit-write.bin");
    remove("aivm-host-limit-chunk-write.bin");
    return 0;
}
