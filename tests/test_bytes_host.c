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
    AivmValue one_arg[1];
    AivmValue two_args[2];
    AivmValue three_args[3];
    AivmValue result;
    AivmSyscallStatus status;

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

    one_arg[0] = aivm_value_bytes(empty_raw, 0U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING);
    CHECK(aivm_value_equals(aivm_value_string(result.string_value), aivm_value_string("")) == 1);

    one_arg[0] = aivm_value_bytes(nul_raw, 3U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_INVALID);

    one_arg[0] = aivm_value_bytes(invalid_utf8_raw, 2U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_INVALID);

    one_arg[0] = aivm_value_bytes(truncated_utf8_raw, 2U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_INVALID);

    one_arg[0] = aivm_value_bytes(out_of_range_utf8_raw, 4U);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_INVALID);

    one_arg[0] = aivm_value_bytes(utf8_raw, NATIVE_BYTES_SCRATCH_CAPACITY);
    status = native_syscall_bytes_to_utf8_string("sys.bytes.toUtf8String", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_ERR_INVALID);

    return 0;
}
