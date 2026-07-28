static char* g_native_storage_value_scratch = NULL;
static size_t g_native_storage_value_scratch_capacity = 0U;

#include "airun_storage_path.h"

static int native_storage_scratch_ensure_capacity(size_t required_capacity)
{
    char* grown;
    size_t new_capacity;
    if (required_capacity == 0U) {
        required_capacity = 1U;
    }
    if (required_capacity <= g_native_storage_value_scratch_capacity &&
        g_native_storage_value_scratch != NULL) {
        return 1;
    }
    new_capacity = (g_native_storage_value_scratch_capacity == 0U) ? 256U : g_native_storage_value_scratch_capacity;
    while (new_capacity < required_capacity) {
        if (new_capacity > (SIZE_MAX / 2U)) {
            new_capacity = required_capacity;
            break;
        }
        new_capacity *= 2U;
    }
    grown = (char*)realloc(g_native_storage_value_scratch, new_capacity);
    if (grown == NULL) {
        return 0;
    }
    g_native_storage_value_scratch = grown;
    g_native_storage_value_scratch_capacity = new_capacity;
    return 1;
}

static int native_storage_home(char* out, size_t out_len)
{
    const char* ailang_home;
    const char* user_home;
    if (out == NULL || out_len == 0U) {
        return 0;
    }
    ailang_home = getenv("AILANG_HOME");
    if (ailang_home != NULL && ailang_home[0] != '\0') {
        return snprintf(out, out_len, "%s", ailang_home) >= 0 && strlen(ailang_home) < out_len;
    }
#ifdef _WIN32
    user_home = getenv("USERPROFILE");
#else
    user_home = getenv("HOME");
#endif
    if (user_home == NULL || user_home[0] == '\0') {
        return 0;
    }
    return snprintf(out, out_len, "%s%c.ailang", user_home, AIVM_PATH_SEP) >= 0 &&
           strlen(out) < out_len;
}

static int native_storage_local_dir(const char* app_id, char* out, size_t out_len)
{
    char root[PATH_MAX];
    char storage_root[PATH_MAX];
    char app_segment[256];
    if (!native_storage_home(root, sizeof(root)) ||
        !native_storage_sanitize_segment(app_id, app_segment, sizeof(app_segment)) ||
        !join_path(root, "storage", storage_root, sizeof(storage_root)) ||
        !join_path(storage_root, "local", root, sizeof(root)) ||
        !join_path(root, app_segment, out, out_len)) {
        return 0;
    }
    return 1;
}

static int native_storage_local_path(const char* app_id, const char* key, char* out, size_t out_len)
{
    char dir[PATH_MAX];
    char key_segment[256];
    char file_segment[320];
    if (!native_storage_local_dir(app_id, dir, sizeof(dir)) ||
        !native_storage_sanitize_segment(key, key_segment, sizeof(key_segment))) {
        return 0;
    }
    if (snprintf(file_segment, sizeof(file_segment), "%s.txt", key_segment) < 0 ||
        strlen(file_segment) >= sizeof(file_segment)) {
        return 0;
    }
    return join_path(dir, file_segment, out, out_len);
}

static int native_storage_validate_key_args(
    const AivmValue* args,
    size_t arg_count)
{
    return args != NULL &&
           arg_count == 2U &&
           args[0].type == AIVM_VAL_STRING &&
           args[0].string_value != NULL &&
           args[1].type == AIVM_VAL_STRING &&
           args[1].string_value != NULL;
}

static int native_storage_validate_set_args(
    const AivmValue* args,
    size_t arg_count)
{
    return args != NULL &&
           arg_count == 3U &&
           args[0].type == AIVM_VAL_STRING &&
           args[0].string_value != NULL &&
           args[1].type == AIVM_VAL_STRING &&
           args[1].string_value != NULL &&
           args[2].type == AIVM_VAL_STRING &&
           args[2].string_value != NULL;
}

static int native_syscall_storage_local_available(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    char root[PATH_MAX];
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_bool(native_storage_home(root, sizeof(root)) ? 1 : 0);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_storage_local_get(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    char path[PATH_MAX];
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (!native_storage_validate_key_args(args, arg_count)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_storage_local_path(args[0].string_value, args[1].string_value, path, sizeof(path))) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!file_exists(path)) {
        *result = aivm_value_string("");
        return AIVM_SYSCALL_OK;
    }
    if (!read_binary_file(path, &bytes, &byte_count)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!native_storage_scratch_ensure_capacity(byte_count + 1U)) {
        free(bytes);
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (byte_count > 0U) {
        memcpy(g_native_storage_value_scratch, bytes, byte_count);
    }
    g_native_storage_value_scratch[byte_count] = '\0';
    free(bytes);
    *result = aivm_value_string(g_native_storage_value_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_storage_local_set(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    char dir[PATH_MAX];
    char path[PATH_MAX];
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (!native_storage_validate_set_args(args, arg_count)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_storage_local_dir(args[0].string_value, dir, sizeof(dir)) ||
        !native_storage_local_path(args[0].string_value, args[1].string_value, path, sizeof(path))) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bool(
        ensure_directory_recursive(dir) &&
        write_text_file(path, args[2].string_value));
    return AIVM_SYSCALL_OK;
}

static int native_syscall_storage_local_delete(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    char path[PATH_MAX];
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (!native_storage_validate_key_args(args, arg_count)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_storage_local_path(args[0].string_value, args[1].string_value, path, sizeof(path))) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bool((remove(path) == 0 || errno == ENOENT) ? 1 : 0);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_storage_local_exists(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    char path[PATH_MAX];
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (!native_storage_validate_key_args(args, arg_count)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!native_storage_local_path(args[0].string_value, args[1].string_value, path, sizeof(path))) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bool(file_exists(path) ? 1 : 0);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_storage_secure_available(
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
    *result = aivm_value_bool(0);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_storage_secure_get(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (!native_storage_validate_key_args(args, arg_count)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_string("");
    return AIVM_SYSCALL_OK;
}

static int native_syscall_storage_secure_bool_false(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int valid_args;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    valid_args = native_storage_validate_key_args(args, arg_count) ||
                 native_storage_validate_set_args(args, arg_count);
    if (!valid_args) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_bool(0);
    return AIVM_SYSCALL_OK;
}
