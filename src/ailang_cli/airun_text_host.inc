static int native_syscall_image_decode_to_rgba_base64(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 2U || args == NULL ||
        args[0].type != AIVM_VAL_BYTES ||
        args[1].type != AIVM_VAL_STRING ||
        args[1].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (args[0].bytes_value.length > 0U && args[0].bytes_value.data == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    return native_image_decode_to_rgba_base64(
        args[0].bytes_value.data,
        args[0].bytes_value.length,
        args[1].string_value,
        result);
}
