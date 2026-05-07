#ifdef _WIN32
static int native_delete_dir_recursive_windows(const char* path)
{
    char pattern[PATH_MAX];
    WIN32_FIND_DATAA entry;
    HANDLE find_handle;
    if (!join_path(path, "*", pattern, sizeof(pattern))) {
        return 0;
    }
    find_handle = FindFirstFileA(pattern, &entry);
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            char child[PATH_MAX];
            if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0) {
                continue;
            }
            if (!join_path(path, entry.cFileName, child, sizeof(child))) {
                FindClose(find_handle);
                return 0;
            }
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if (!native_delete_dir_recursive_windows(child)) {
                    FindClose(find_handle);
                    return 0;
                }
            } else {
                if (!DeleteFileA(child)) {
                    FindClose(find_handle);
                    return 0;
                }
            }
        } while (FindNextFileA(find_handle, &entry) != 0);
        FindClose(find_handle);
    }
    return RemoveDirectoryA(path) != 0 ? 1 : 0;
}
#endif

#ifndef _WIN32
static int native_delete_dir_recursive_posix(const char* path)
{
    DIR* dir;
    struct dirent* entry;
    dir = opendir(path);
    if (dir == NULL) {
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        char child[PATH_MAX];
        struct stat st;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!join_path(path, entry->d_name, child, sizeof(child))) {
            (void)closedir(dir);
            return 0;
        }
        if (lstat(child, &st) != 0) {
            (void)closedir(dir);
            return 0;
        }
        if (S_ISDIR(st.st_mode)) {
            if (!native_delete_dir_recursive_posix(child)) {
                (void)closedir(dir);
                return 0;
            }
        } else if (remove(child) != 0) {
            (void)closedir(dir);
            return 0;
        }
    }
    (void)closedir(dir);
    return rmdir(path) == 0 ? 1 : 0;
}
#endif

static int native_syscall_fs_file_exists(
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
    *result = aivm_value_bool(file_exists(args[0].string_value) ? 1 : 0);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_fs_path_exists(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    struct stat st;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_bool(stat(args[0].string_value, &st) == 0 ? 1 : 0);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_fs_dir_create(
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
    if (!ensure_directory_recursive(args[0].string_value)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static unsigned char* g_native_file_bytes_scratch = NULL;
static size_t g_native_file_bytes_scratch_capacity = 0U;

static int native_ensure_file_bytes_scratch(size_t needed)
{
    unsigned char* grown;
    size_t new_capacity;
    if (needed <= g_native_file_bytes_scratch_capacity) {
        return 1;
    }
    new_capacity = g_native_file_bytes_scratch_capacity == 0U ? 4096U : g_native_file_bytes_scratch_capacity;
    while (new_capacity < needed) {
        if (new_capacity > (SIZE_MAX / 2U)) {
            return 0;
        }
        new_capacity *= 2U;
    }
    grown = (unsigned char*)realloc(g_native_file_bytes_scratch, new_capacity);
    if (grown == NULL) {
        return 0;
    }
    g_native_file_bytes_scratch = grown;
    g_native_file_bytes_scratch_capacity = new_capacity;
    return 1;
}

static int native_syscall_fs_file_read(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!read_binary_file(args[0].string_value, &bytes, &byte_count)) {
        *result = aivm_value_bytes(NULL, 0U);
        return AIVM_SYSCALL_OK;
    }
    if (!native_ensure_file_bytes_scratch(byte_count)) {
        free(bytes);
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (byte_count > 0U) {
        memcpy(g_native_file_bytes_scratch, bytes, byte_count);
    }
    free(bytes);
    *result = aivm_value_bytes(g_native_file_bytes_scratch, byte_count);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_fs_file_write(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    FILE* fp;
    size_t wrote = 0U;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 2U ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_BYTES) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    fp = fopen(args[0].string_value, "wb");
    if (fp == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[1].bytes_value.length > 0U && args[1].bytes_value.data != NULL) {
        wrote = fwrite(args[1].bytes_value.data, 1U, args[1].bytes_value.length, fp);
    }
    if (fclose(fp) != 0 || wrote != args[1].bytes_value.length) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_fs_file_delete(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    struct stat st;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (stat(args[0].string_value, &st) != 0) {
        *result = aivm_value_bool(0);
        return AIVM_SYSCALL_OK;
    }
#ifdef _WIN32
    if ((st.st_mode & _S_IFREG) == 0) {
#else
    if (!S_ISREG(st.st_mode)) {
#endif
        *result = aivm_value_bool(0);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_bool(remove(args[0].string_value) == 0 ? 1 : 0);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_fs_dir_delete(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* path;
    int recursive;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 2U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL || args[1].type != AIVM_VAL_BOOL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    path = args[0].string_value;
    recursive = args[1].bool_value ? 1 : 0;
#ifdef _WIN32
    if (recursive) {
        *result = aivm_value_bool(native_delete_dir_recursive_windows(path) ? 1 : 0);
    } else {
        *result = aivm_value_bool(_rmdir(path) == 0 ? 1 : 0);
    }
#else
    if (recursive) {
        *result = aivm_value_bool(native_delete_dir_recursive_posix(path) ? 1 : 0);
    } else {
        *result = aivm_value_bool(rmdir(path) == 0 ? 1 : 0);
    }
#endif
    return AIVM_SYSCALL_OK;
}

static int native_fs_dir_count_entries(const char* path, int64_t* out_count)
{
    int64_t count = 0;
    if (path == NULL || out_count == NULL) {
        return 0;
    }
#ifdef _WIN32
    {
        char pattern[PATH_MAX];
        WIN32_FIND_DATAA entry;
        HANDLE find_handle;
        if (!join_path(path, "*", pattern, sizeof(pattern))) {
            return 0;
        }
        find_handle = FindFirstFileA(pattern, &entry);
        if (find_handle == INVALID_HANDLE_VALUE) {
            return 0;
        }
        do {
            if (strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0) {
                continue;
            }
            count += 1;
        } while (FindNextFileA(find_handle, &entry) != 0);
        FindClose(find_handle);
    }
#else
    {
        DIR* dir;
        struct dirent* entry;
        dir = opendir(path);
        if (dir == NULL) {
            return 0;
        }
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            count += 1;
        }
        (void)closedir(dir);
    }
#endif
    *out_count = count;
    return 1;
}

static int native_syscall_fs_dir_list(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    AivmNodeAttr attrs[3];
    int64_t node_handle;
    int64_t count = 0;
    int exists = 0;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (g_native_active_vm == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (directory_exists(args[0].string_value) && native_fs_dir_count_entries(args[0].string_value, &count)) {
        exists = 1;
    }
    attrs[0].key = "path";
    attrs[0].kind = AIVM_NODE_ATTR_STRING;
    attrs[0].string_value = args[0].string_value;
    attrs[1].key = "exists";
    attrs[1].kind = AIVM_NODE_ATTR_BOOL;
    attrs[1].bool_value = exists;
    attrs[2].key = "count";
    attrs[2].kind = AIVM_NODE_ATTR_INT;
    attrs[2].int_value = count;
    if (!native_vm_append_host_node(g_native_active_vm, "Map", "fs_dir_list", attrs, 3U, &node_handle)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_node(node_handle);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_fs_path_stat(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    struct stat st;
    AivmNodeAttr attrs[5];
    int64_t node_handle;
    int exists = 0;
    int is_dir = 0;
    int64_t size = 0;
    int64_t mtime_unix_ms = 0;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (g_native_active_vm == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (stat(args[0].string_value, &st) == 0) {
        exists = 1;
#ifdef _WIN32
        is_dir = ((st.st_mode & _S_IFDIR) != 0) ? 1 : 0;
#else
        is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
#endif
        size = (int64_t)st.st_size;
        mtime_unix_ms = (int64_t)st.st_mtime * 1000LL;
    }
    attrs[0].key = "path";
    attrs[0].kind = AIVM_NODE_ATTR_STRING;
    attrs[0].string_value = args[0].string_value;
    attrs[1].key = "exists";
    attrs[1].kind = AIVM_NODE_ATTR_BOOL;
    attrs[1].bool_value = exists;
    attrs[2].key = "isDir";
    attrs[2].kind = AIVM_NODE_ATTR_BOOL;
    attrs[2].bool_value = is_dir;
    attrs[3].key = "size";
    attrs[3].kind = AIVM_NODE_ATTR_INT;
    attrs[3].int_value = size;
    attrs[4].key = "mtimeUnixMs";
    attrs[4].kind = AIVM_NODE_ATTR_INT;
    attrs[4].int_value = mtime_unix_ms;
    if (!native_vm_append_host_node(g_native_active_vm, "Map", "fs_path_stat", attrs, 5U, &node_handle)) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_node(node_handle);
    return AIVM_SYSCALL_OK;
}
