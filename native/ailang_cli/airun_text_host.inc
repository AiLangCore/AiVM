static int native_syscall_bytes_length(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_BYTES) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_int((int64_t)args[0].bytes_value.length);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_at(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t index;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 2U || args == NULL || args[0].type != AIVM_VAL_BYTES || args[1].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (args[1].int_value < 0) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    index = (size_t)args[1].int_value;
    if (index >= args[0].bytes_value.length || args[0].bytes_value.data == NULL) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_int((int64_t)args[0].bytes_value.data[index]);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_slice(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t start;
    size_t length;
    size_t end;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 3U || args == NULL || args[0].type != AIVM_VAL_BYTES || args[1].type != AIVM_VAL_INT || args[2].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (args[1].int_value <= 0) {
        start = 0U;
    } else if ((uint64_t)args[1].int_value >= (uint64_t)args[0].bytes_value.length) {
        start = args[0].bytes_value.length;
    } else {
        start = (size_t)args[1].int_value;
    }
    if (args[2].int_value <= 0) {
        length = 0U;
    } else {
        length = (size_t)args[2].int_value;
    }
    end = start + length;
    if (end < start || end > args[0].bytes_value.length) {
        end = args[0].bytes_value.length;
    }
    if (start >= end || args[0].bytes_value.data == NULL) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_bytes(&args[0].bytes_value.data[start], end - start);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_concat(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t left_len;
    size_t right_len;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 2U || args == NULL || args[0].type != AIVM_VAL_BYTES || args[1].type != AIVM_VAL_BYTES) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    left_len = args[0].bytes_value.length;
    right_len = args[1].bytes_value.length;
    if (left_len == 0U && right_len == 0U) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    if (left_len + right_len > NATIVE_BYTES_SCRATCH_CAPACITY) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (left_len > 0U && args[0].bytes_value.data != NULL) {
        memcpy(g_native_bytes_scratch, args[0].bytes_value.data, left_len);
    } else if (left_len > 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (right_len > 0U && args[1].bytes_value.data != NULL) {
        memcpy(g_native_bytes_scratch + left_len, args[1].bytes_value.data, right_len);
    } else if (right_len > 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bytes(g_native_bytes_scratch, left_len + right_len);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_from_base64(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t out_len = 0U;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_bytes_from_base64(args[0].string_value, NULL, 0U, &out_len)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (out_len == 0U) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    if (out_len > NATIVE_BYTES_SCRATCH_CAPACITY) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_bytes_from_base64(args[0].string_value, g_native_bytes_scratch, out_len, &out_len)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bytes(g_native_bytes_scratch, out_len);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_to_base64(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t in_len;
    size_t out_len;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_BYTES) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    in_len = args[0].bytes_value.length;
    out_len = ((in_len + 2U) / 3U) * 4U;
    if (out_len + 1U > NATIVE_BYTES_SCRATCH_CAPACITY) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_bytes_to_base64(args[0].bytes_value.data, in_len, g_native_base64_scratch, out_len + 1U)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_string(g_native_base64_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_to_utf8_string(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t i;
    size_t in_len;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_BYTES) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    in_len = args[0].bytes_value.length;
    if (in_len == 0U) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    if (args[0].bytes_value.data == NULL || in_len + 1U > NATIVE_BYTES_SCRATCH_CAPACITY) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_is_valid_utf8_without_nul(args[0].bytes_value.data, in_len)) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    for (i = 0U; i < in_len; i += 1U) {
        g_native_base64_scratch[i] = (char)args[0].bytes_value.data[i];
    }
    g_native_base64_scratch[in_len] = '\0';
    *result = aivm_value_string(g_native_base64_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_from_utf8_string(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t in_len;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    in_len = strlen(args[0].string_value);
    if (in_len == 0U) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    if (in_len > NATIVE_BYTES_SCRATCH_CAPACITY) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    memcpy(g_native_bytes_scratch, args[0].string_value, in_len);
    *result = aivm_value_bytes(g_native_bytes_scratch, in_len);
    return AIVM_SYSCALL_OK;
}

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

static int native_syscall_str_from_codepoint(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    uint32_t cp;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (args[0].int_value < 0 || args[0].int_value > 0x10FFFFLL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    cp = (uint32_t)args[0].int_value;
    if (cp >= 0xD800U && cp <= 0xDFFFU) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (cp <= 0x7FU) {
        g_native_utf8_scratch[0] = (char)cp;
        g_native_utf8_scratch[1] = '\0';
    } else if (cp <= 0x7FFU) {
        g_native_utf8_scratch[0] = (char)(0xC0U | (cp >> 6U));
        g_native_utf8_scratch[1] = (char)(0x80U | (cp & 0x3FU));
        g_native_utf8_scratch[2] = '\0';
    } else if (cp <= 0xFFFFU) {
        g_native_utf8_scratch[0] = (char)(0xE0U | (cp >> 12U));
        g_native_utf8_scratch[1] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
        g_native_utf8_scratch[2] = (char)(0x80U | (cp & 0x3FU));
        g_native_utf8_scratch[3] = '\0';
    } else {
        g_native_utf8_scratch[0] = (char)(0xF0U | (cp >> 18U));
        g_native_utf8_scratch[1] = (char)(0x80U | ((cp >> 12U) & 0x3FU));
        g_native_utf8_scratch[2] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
        g_native_utf8_scratch[3] = (char)(0x80U | (cp & 0x3FU));
        g_native_utf8_scratch[4] = '\0';
    }
    *result = aivm_value_string(g_native_utf8_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_str_utf8_byte_count(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_int((int64_t)strlen(args[0].string_value));
    return AIVM_SYSCALL_OK;
}

static int native_syscall_str_substring(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* text;
    size_t rune_count;
    size_t start_rune;
    size_t end_rune;
    size_t start_byte;
    size_t end_byte;
    size_t copy_length;
    int64_t end_index;
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
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    text = args[0].string_value;
    if (args[2].int_value <= 0) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    rune_count = native_utf8_rune_count(text);
    start_rune = native_clamp_rune_index(args[1].int_value, rune_count);
    end_index = native_safe_add_i64(args[1].int_value, args[2].int_value);
    end_rune = native_clamp_rune_index(end_index, rune_count);
    if (end_rune < start_rune) {
        end_rune = start_rune;
    }
    start_byte = native_utf8_byte_offset_for_rune(text, start_rune);
    end_byte = native_utf8_byte_offset_for_rune(text, end_rune);
    copy_length = end_byte - start_byte;
    if (!native_string_scratch_ensure_capacity(copy_length + 1U)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (copy_length > 0U) {
        memcpy(g_native_string_scratch, text + start_byte, copy_length);
    }
    g_native_string_scratch[copy_length] = '\0';
    *result = aivm_value_string(g_native_string_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_str_find(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* text;
    const char* pattern;
    size_t text_runes;
    size_t pattern_runes;
    size_t start_rune;
    size_t candidate_rune;
    size_t candidate_byte;
    size_t pattern_bytes;
    size_t haystack_bytes;
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
        args[1].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    text = args[0].string_value;
    pattern = args[1].string_value;
    text_runes = native_utf8_rune_count(text);
    pattern_runes = native_utf8_rune_count(pattern);
    start_rune = native_clamp_rune_index(args[2].int_value, text_runes);
    if (pattern_runes == 0U) {
        *result = aivm_value_int((int64_t)start_rune);
        return AIVM_SYSCALL_OK;
    }
    if (pattern_runes > text_runes || start_rune > text_runes - pattern_runes) {
        *result = aivm_value_int(-1);
        return AIVM_SYSCALL_OK;
    }
    pattern_bytes = strlen(pattern);
    haystack_bytes = strlen(text);
    candidate_byte = native_utf8_byte_offset_for_rune(text, start_rune);
    for (candidate_rune = start_rune; candidate_byte + pattern_bytes <= haystack_bytes; candidate_rune += 1U) {
        if (memcmp(text + candidate_byte, pattern, pattern_bytes) == 0) {
            *result = aivm_value_int((int64_t)candidate_rune);
            return AIVM_SYSCALL_OK;
        }
        if (candidate_rune >= text_runes - pattern_runes) {
            break;
        }
        candidate_byte = native_utf8_byte_offset_for_rune(text, candidate_rune + 1U);
    }
    *result = aivm_value_int(-1);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_str_remove(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* text;
    size_t input_length;
    size_t rune_count;
    size_t start_rune;
    size_t end_rune;
    size_t start_byte;
    size_t end_byte;
    size_t output_length;
    int64_t end_index;
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
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    text = args[0].string_value;
    input_length = strlen(text);
    if (args[2].int_value <= 0) {
        if (!native_string_scratch_ensure_capacity(input_length + 1U)) {
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_INVALID;
        }
        if (input_length > 0U) {
            memcpy(g_native_string_scratch, text, input_length);
        }
        g_native_string_scratch[input_length] = '\0';
        *result = aivm_value_string(g_native_string_scratch);
        return AIVM_SYSCALL_OK;
    }
    rune_count = native_utf8_rune_count(text);
    start_rune = native_clamp_rune_index(args[1].int_value, rune_count);
    end_index = native_safe_add_i64(args[1].int_value, args[2].int_value);
    end_rune = native_clamp_rune_index(end_index, rune_count);
    if (end_rune < start_rune) {
        end_rune = start_rune;
    }
    start_byte = native_utf8_byte_offset_for_rune(text, start_rune);
    end_byte = native_utf8_byte_offset_for_rune(text, end_rune);
    output_length = input_length - (end_byte - start_byte);
    if (!native_string_scratch_ensure_capacity(output_length + 1U)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (start_byte > 0U) {
        memcpy(g_native_string_scratch, text, start_byte);
    }
    if (end_byte < input_length) {
        memcpy(g_native_string_scratch + start_byte, text + end_byte, input_length - end_byte);
    }
    g_native_string_scratch[output_length] = '\0';
    *result = aivm_value_string(g_native_string_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_str_decode_unicode_hex4(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* text;
    size_t len;
    uint32_t cp;
    char* end_ptr = NULL;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    text = args[0].string_value;
    len = strlen(text);
    if (len != 4U) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    cp = (uint32_t)strtoul(text, &end_ptr, 16);
    if (end_ptr == NULL || *end_ptr != '\0') {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    if (cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    if (cp <= 0x7FU) {
        g_native_utf8_scratch[0] = (char)cp;
        g_native_utf8_scratch[1] = '\0';
    } else if (cp <= 0x7FFU) {
        g_native_utf8_scratch[0] = (char)(0xC0U | (cp >> 6U));
        g_native_utf8_scratch[1] = (char)(0x80U | (cp & 0x3FU));
        g_native_utf8_scratch[2] = '\0';
    } else if (cp <= 0xFFFFU) {
        g_native_utf8_scratch[0] = (char)(0xE0U | (cp >> 12U));
        g_native_utf8_scratch[1] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
        g_native_utf8_scratch[2] = (char)(0x80U | (cp & 0x3FU));
        g_native_utf8_scratch[3] = '\0';
    } else {
        g_native_utf8_scratch[0] = (char)(0xF0U | (cp >> 18U));
        g_native_utf8_scratch[1] = (char)(0x80U | ((cp >> 12U) & 0x3FU));
        g_native_utf8_scratch[2] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
        g_native_utf8_scratch[3] = (char)(0x80U | (cp & 0x3FU));
        g_native_utf8_scratch[4] = '\0';
    }
    *result = aivm_value_string(g_native_utf8_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_str_decode_unicode_surrogate_pair_hex4(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* high_text;
    const char* low_text;
    char* end_ptr = NULL;
    uint32_t high_surrogate;
    uint32_t low_surrogate;
    uint32_t cp;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 2U ||
        args == NULL ||
        args[0].type != AIVM_VAL_STRING ||
        args[1].type != AIVM_VAL_STRING ||
        args[0].string_value == NULL ||
        args[1].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    high_text = args[0].string_value;
    low_text = args[1].string_value;
    if (strlen(high_text) != 4U || strlen(low_text) != 4U) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    high_surrogate = (uint32_t)strtoul(high_text, &end_ptr, 16);
    if (end_ptr == NULL || *end_ptr != '\0') {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    low_surrogate = (uint32_t)strtoul(low_text, &end_ptr, 16);
    if (end_ptr == NULL || *end_ptr != '\0') {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    if (high_surrogate < 0xD800U || high_surrogate > 0xDBFFU ||
        low_surrogate < 0xDC00U || low_surrogate > 0xDFFFU) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    cp = 0x10000U + ((high_surrogate - 0xD800U) << 10U) + (low_surrogate - 0xDC00U);
    g_native_utf8_scratch[0] = (char)(0xF0U | (cp >> 18U));
    g_native_utf8_scratch[1] = (char)(0x80U | ((cp >> 12U) & 0x3FU));
    g_native_utf8_scratch[2] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
    g_native_utf8_scratch[3] = (char)(0x80U | (cp & 0x3FU));
    g_native_utf8_scratch[4] = '\0';
    *result = aivm_value_string(g_native_utf8_scratch);
    return AIVM_SYSCALL_OK;
}

