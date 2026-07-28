static int native_syscall_crypto_random_bytes(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t len;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT || args[0].int_value < 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    len = (size_t)args[0].int_value;
    if (len == 0U) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    if (len > NATIVE_BYTES_SCRATCH_CAPACITY) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
#ifdef _WIN32
    {
        size_t i;
        for (i = 0U; i < len; i += 1U) {
            unsigned int v = 0U;
            if (rand_s(&v) != 0) {
                result->type = AIVM_VAL_VOID;
                return AIVM_SYSCALL_ERR_INVALID;
            }
            g_native_bytes_scratch[i] = (uint8_t)(v & 0xFFU);
        }
    }
#else
    {
        int fd = open("/dev/urandom", O_RDONLY);
        size_t read_total = 0U;
        if (fd < 0) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        while (read_total < len) {
            ssize_t got = read(fd, g_native_bytes_scratch + read_total, len - read_total);
            if (got <= 0) {
                close(fd);
                result->type = AIVM_VAL_VOID;
                return AIVM_SYSCALL_ERR_INVALID;
            }
            read_total += (size_t)got;
        }
        close(fd);
    }
#endif
    *result = aivm_value_bytes(g_native_bytes_scratch, len);
    return AIVM_SYSCALL_OK;
}
