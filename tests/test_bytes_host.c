#define AIRUN_ALLOW_INTERNAL_UI_FALLBACK 1
#define main airun_embedded_main_for_test
#include "../../../AiCLI/native/airun.c"
#undef main

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d\n", __LINE__); \
            return 1; \
        } \
    } while (0)

int main(void)
{
    const uint8_t left_raw[3] = { 1U, 2U, 3U };
    const uint8_t right_raw[2] = { 4U, 5U };
    const uint8_t utf8_raw[5] = { 'h', 'e', 'l', 'l', 'o' };
    const uint8_t empty_raw[1] = { 0U };
    const uint8_t nul_raw[3] = { 'a', 0U, 'b' };
    const uint8_t invalid_utf8_raw[2] = { 0xC0U, 0xAFU };
    const uint8_t truncated_utf8_raw[2] = { 0xE2U, 0x82U };
    const uint8_t out_of_range_utf8_raw[4] = { 0xF4U, 0x90U, 0x80U, 0x80U };
    const char* png_base64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIW2P4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==";
    AivmValue one_arg[1];
    AivmValue two_args[2];
    AivmValue three_args[3];
    AivmValue result;
    AivmSyscallStatus status;
    int64_t worker_ok = -1;
    int64_t worker_fail = -1;
    int64_t worker_sleep = -1;

    one_arg[0] = aivm_value_bytes(left_raw, 3U);
    status = native_syscall_bytes_length("sys.bytes.length", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 3);

    two_args[0] = aivm_value_bytes(left_raw, 3U);
    two_args[1] = aivm_value_int(1);
    status = native_syscall_bytes_at("sys.bytes.at", two_args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 2);

    two_args[1] = aivm_value_int(99);
    status = native_syscall_bytes_at("sys.bytes.at", two_args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == -1);

    three_args[0] = aivm_value_bytes(left_raw, 3U);
    three_args[1] = aivm_value_int(1);
    three_args[2] = aivm_value_int(2);
    status = native_syscall_bytes_slice("sys.bytes.slice", three_args, 3U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BYTES && result.bytes_value.length == 2U);
    CHECK(result.bytes_value.data != NULL);
    CHECK(result.bytes_value.data[0] == 2U && result.bytes_value.data[1] == 3U);

    two_args[0] = aivm_value_bytes(left_raw, 3U);
    two_args[1] = aivm_value_bytes(right_raw, 2U);
    status = native_syscall_bytes_concat("sys.bytes.concat", two_args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BYTES && result.bytes_value.length == 5U);
    CHECK(result.bytes_value.data[0] == 1U &&
          result.bytes_value.data[1] == 2U &&
          result.bytes_value.data[2] == 3U &&
          result.bytes_value.data[3] == 4U &&
          result.bytes_value.data[4] == 5U);

    one_arg[0] = aivm_value_bytes(left_raw, 3U);
    status = native_syscall_bytes_to_base64("sys.bytes.toBase64", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING);
    CHECK(aivm_value_equals(aivm_value_string(result.string_value), aivm_value_string("AQID")) == 1);

    one_arg[0] = aivm_value_string("AQID");
    status = native_syscall_bytes_from_base64("sys.bytes.fromBase64", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BYTES && result.bytes_value.length == 3U);
    CHECK(result.bytes_value.data[0] == 1U &&
          result.bytes_value.data[1] == 2U &&
          result.bytes_value.data[2] == 3U);

    one_arg[0] = aivm_value_string("not-base64!!");
    status = native_syscall_bytes_from_base64("sys.bytes.fromBase64", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_INVALID);

    one_arg[0] = aivm_value_bytes(utf8_raw, 5U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING);
    CHECK(aivm_value_equals(aivm_value_string(result.string_value), aivm_value_string("hello")) == 1);

    one_arg[0] = aivm_value_string("hello");
    status = native_syscall_bytes_from_utf8_string("sys.bytes.fromUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BYTES && result.bytes_value.length == 5U);
    CHECK(memcmp(result.bytes_value.data, utf8_raw, 5U) == 0);

    one_arg[0] = aivm_value_bytes(empty_raw, 0U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING);
    CHECK(aivm_value_equals(aivm_value_string(result.string_value), aivm_value_string("")) == 1);

    one_arg[0] = aivm_value_bytes(nul_raw, 3U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING);
    CHECK(aivm_value_equals(aivm_value_string(result.string_value), aivm_value_string("")) == 1);

    one_arg[0] = aivm_value_bytes(invalid_utf8_raw, 2U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING);
    CHECK(aivm_value_equals(aivm_value_string(result.string_value), aivm_value_string("")) == 1);

    one_arg[0] = aivm_value_bytes(truncated_utf8_raw, 2U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING);
    CHECK(aivm_value_equals(aivm_value_string(result.string_value), aivm_value_string("")) == 1);

    one_arg[0] = aivm_value_bytes(out_of_range_utf8_raw, 4U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING);
    CHECK(aivm_value_equals(aivm_value_string(result.string_value), aivm_value_string("")) == 1);

    one_arg[0] = aivm_value_bytes(utf8_raw, NATIVE_BYTES_SCRATCH_CAPACITY + 1U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_INVALID);

    {
        size_t png_len = 0U;
        size_t rgba_len = 0U;
        CHECK(native_bytes_from_base64(png_base64, NULL, 0U, &png_len) == 1);
        CHECK(png_len > 0U && png_len < NATIVE_BYTES_SCRATCH_CAPACITY);
        CHECK(native_bytes_from_base64(png_base64, g_native_bytes_scratch, png_len, &png_len) == 1);
        two_args[0] = aivm_value_bytes(g_native_bytes_scratch, png_len);
        two_args[1] = aivm_value_string("image/png");
        status = native_syscall_image_decode_to_rgba_base64("sys.image.decodeToRgbaBase64", two_args, 2U, &result);
#ifdef __APPLE__
        CHECK(status == AIVM_SYSCALL_OK);
        CHECK(result.type == AIVM_VAL_STRING);
        CHECK(result.string_value != NULL && strlen(result.string_value) > 0U);
        CHECK(native_bytes_from_base64(result.string_value, NULL, 0U, &rgba_len) == 1);
        CHECK(rgba_len == 4U);
#else
        CHECK(status == AIVM_SYSCALL_ERR_INVALID);
#endif
    }

    two_args[0] = aivm_value_string("echo");
    two_args[1] = aivm_value_string("worker-ok");
    status = native_syscall_worker_start("sys.worker.start", two_args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value > 0);
    worker_ok = result.int_value;

    two_args[0] = aivm_value_string("fail");
    two_args[1] = aivm_value_string("worker-fail");
    status = native_syscall_worker_start("sys.worker.start", two_args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value > 0);
    worker_fail = result.int_value;

    two_args[0] = aivm_value_string("sleep");
    two_args[1] = aivm_value_string("2");
    status = native_syscall_worker_start("sys.worker.start", two_args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value > 0);
    worker_sleep = result.int_value;

    one_arg[0] = aivm_value_int(worker_ok);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 0);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 1);
    status = native_syscall_worker_result("sys.worker.result", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "worker-ok") == 0);
    status = native_syscall_worker_error("sys.worker.error", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "") == 0);

    one_arg[0] = aivm_value_int(worker_fail);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 0);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == -1);
    status = native_syscall_worker_result("sys.worker.result", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "") == 0);
    status = native_syscall_worker_error("sys.worker.error", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "worker-fail") == 0);

    one_arg[0] = aivm_value_int(worker_sleep);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 0);
    status = native_syscall_worker_cancel("sys.worker.cancel", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 1);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == -2);
    status = native_syscall_worker_error("sys.worker.error", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "canceled") == 0);
    status = native_syscall_worker_cancel("sys.worker.cancel", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 0);

    one_arg[0] = aivm_value_int(99999);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == -3);
    status = native_syscall_worker_result("sys.worker.result", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "") == 0);
    status = native_syscall_worker_error("sys.worker.error", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "unknown_worker") == 0);
    status = native_syscall_worker_cancel("sys.worker.cancel", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 0);

    return 0;
}
