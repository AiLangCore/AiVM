#ifndef _WIN32
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#endif

#include "ailang_package_manager.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define AILANG_PM_PATH_SEP '\\'
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#else
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define AILANG_PM_PATH_SEP '/'
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

#define AILANG_PM_TEXT_LIMIT 262144U
#define AILANG_PM_LOCK_LIMIT 131072U
#define AILANG_PM_TOOL_TIMEOUT_SECONDS 30
#define AILANG_PM_TOOL_TIMEOUT_MAX_SECONDS 3600
#define AILANG_PM_MAX_DEPENDENCIES 64
#define AILANG_PM_LOCAL_INCLUDE_MAX_DEPTH 8

typedef struct AilangPackageRecord {
    char name[128];
    char version[64];
    char repo[512];
    char package_root[256];
    char ref[128];
    char commit[128];
    char types[256];
} AilangPackageRecord;

typedef struct AilangPackageDependency {
    char name[128];
    char version[64];
} AilangPackageDependency;

typedef struct AilangPackageSourceMetadata {
    char namespaces[512];
    AilangPackageDependency dependencies[AILANG_PM_MAX_DEPENDENCIES];
    size_t dependency_count;
} AilangPackageSourceMetadata;

typedef struct AilangPackageRestoreQueue {
    AilangPackageDependency entries[AILANG_PM_MAX_DEPENDENCIES];
    size_t count;
} AilangPackageRestoreQueue;

static int pm_is_sdk_owned_name(const char* name);

static int pm_is_absolute_path(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }
#ifdef _WIN32
    return isalpha((unsigned char)path[0]) && path[1] == ':';
#else
    return 0;
#endif
}

static int pm_set_error(char* error, size_t error_len, const char* fmt, ...)
{
    va_list ap;
    if (error != NULL && error_len > 0U) {
        va_start(ap, fmt);
        (void)vsnprintf(error, error_len, fmt, ap);
        va_end(ap);
    }
    return 0;
}

static int pm_set_error_if_empty(char* error, size_t error_len, const char* fmt, ...)
{
    va_list ap;
    if (error != NULL && error_len > 0U && error[0] == '\0') {
        va_start(ap, fmt);
        (void)vsnprintf(error, error_len, fmt, ap);
        va_end(ap);
    }
    return 0;
}

static int pm_append(char* out, size_t out_len, size_t* used, const char* text)
{
    size_t n;
    if (out == NULL || used == NULL || text == NULL) {
        return 0;
    }
    n = strlen(text);
    if (*used + n + 1U > out_len) {
        return 0;
    }
    memcpy(out + *used, text, n);
    *used += n;
    out[*used] = '\0';
    return 1;
}

static int pm_appendf(char* out, size_t out_len, size_t* used, const char* fmt, ...)
{
    va_list ap;
    int n;
    if (out == NULL || used == NULL || fmt == NULL || *used >= out_len) {
        return 0;
    }
    va_start(ap, fmt);
    n = vsnprintf(out + *used, out_len - *used, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= out_len - *used) {
        return 0;
    }
    *used += (size_t)n;
    return 1;
}

static int pm_file_exists(const char* path)
{
#ifdef _WIN32
    DWORD attrs;
    if (path == NULL) {
        return 0;
    }
    attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static int pm_directory_exists(const char* path)
{
#ifdef _WIN32
    DWORD attrs;
    if (path == NULL) {
        return 0;
    }
    attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static int pm_join_path(const char* left, const char* right, char* out, size_t out_len)
{
    int n;
    if (left == NULL || right == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (left[0] == '\0') {
        n = snprintf(out, out_len, "%s", right);
    } else {
        n = snprintf(out, out_len, "%s%c%s", left, AILANG_PM_PATH_SEP, right);
    }
    return n >= 0 && (size_t)n < out_len;
}

static int pm_mkdir_one(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (pm_directory_exists(path)) {
        return 1;
    }
#ifdef _WIN32
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

static int pm_mkdirs(const char* path)
{
    char tmp[PATH_MAX];
    size_t len;
    size_t i;
    if (path == NULL) {
        return 0;
    }
    len = strlen(path);
    if (len == 0U || len >= sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, path, len + 1U);
    for (i = 1U; i < len; i += 1U) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char saved = tmp[i];
            tmp[i] = '\0';
            if (tmp[0] != '\0' && !pm_mkdir_one(tmp)) {
                tmp[i] = saved;
                return 0;
            }
            tmp[i] = saved;
        }
    }
    return pm_mkdir_one(tmp);
}

static int pm_write_text(const char* path, const char* text)
{
    FILE* f;
    size_t len;
    if (path == NULL || text == NULL) {
        return 0;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    len = strlen(text);
    if (len > 0U && fwrite(text, 1U, len, f) != len) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static int pm_read_text_limited(const char* path, char** out_text)
{
    FILE* f;
    long length;
    size_t read_count;
    char* text;
    if (path == NULL || out_text == NULL) {
        return 0;
    }
    *out_text = NULL;
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    length = ftell(f);
    if (length < 0 || (size_t)length > AILANG_PM_TEXT_LIMIT) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    text = (char*)malloc((size_t)length + 1U);
    if (text == NULL) {
        fclose(f);
        return 0;
    }
    read_count = fread(text, 1U, (size_t)length, f);
    fclose(f);
    if (read_count != (size_t)length) {
        free(text);
        return 0;
    }
    text[read_count] = '\0';
    *out_text = text;
    return 1;
}

static int pm_copy_text(char* out, size_t out_len, const char* text)
{
    if (out == NULL || out_len == 0U || text == NULL) {
        return 0;
    }
    if (snprintf(out, out_len, "%s", text) < 0 || strlen(text) >= out_len) {
        return 0;
    }
    return 1;
}

static int pm_sanitize_id(const char* input, char* out, size_t out_len)
{
    size_t i;
    size_t w = 0U;
    if (input == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    for (i = 0U; input[i] != '\0'; i += 1U) {
        char c = input[i];
        if (isalnum((unsigned char)c)) {
            c = (char)tolower((unsigned char)c);
        } else {
            c = '_';
        }
        if (w + 1U >= out_len) {
            return 0;
        }
        out[w++] = c;
    }
    if (w == 0U) {
        return 0;
    }
    out[w] = '\0';
    return 1;
}

static int pm_toml_get_string(const char* text, const char* key, char* out, size_t out_len)
{
    char needle[128];
    const char* pos;
    const char* start;
    const char* end;
    size_t n;
    if (text == NULL || key == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (snprintf(needle, sizeof(needle), "%s = \"", key) >= (int)sizeof(needle)) {
        return 0;
    }
    pos = strstr(text, needle);
    if (pos == NULL) {
        if (snprintf(needle, sizeof(needle), "%s=\"", key) >= (int)sizeof(needle)) {
            return 0;
        }
        pos = strstr(text, needle);
    }
    if (pos == NULL) {
        return 0;
    }
    start = pos + strlen(needle);
    end = strchr(start, '"');
    if (end == NULL) {
        return 0;
    }
    n = (size_t)(end - start);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return 1;
}

static int pm_toml_get_array(const char* text, const char* key, char* out, size_t out_len)
{
    char needle[128];
    const char* pos;
    const char* start;
    const char* end;
    size_t n;
    if (text == NULL || key == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (snprintf(needle, sizeof(needle), "%s = [", key) >= (int)sizeof(needle)) {
        return 0;
    }
    pos = strstr(text, needle);
    if (pos == NULL) {
        if (snprintf(needle, sizeof(needle), "%s=[", key) >= (int)sizeof(needle)) {
            return 0;
        }
        pos = strstr(text, needle);
    }
    if (pos == NULL) {
        out[0] = '\0';
        return 1;
    }
    start = pos + strlen(needle);
    end = strchr(start, ']');
    if (end == NULL) {
        return 0;
    }
    n = (size_t)(end - start);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return 1;
}

static int pm_toml_array_to_display(const char* array_text, char* out, size_t out_len)
{
    size_t i;
    size_t used = 0U;
    int in_quote = 0;
    if (array_text == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    out[0] = '\0';
    for (i = 0U; array_text[i] != '\0'; i += 1U) {
        char c = array_text[i];
        if (c == '"') {
            in_quote = !in_quote;
            continue;
        }
        if (!in_quote && isspace((unsigned char)c)) {
            continue;
        }
        if (!in_quote && c == ',') {
            if (!pm_append(out, out_len, &used, ",")) {
                return 0;
            }
            continue;
        }
        if (used + 2U > out_len) {
            return 0;
        }
        out[used++] = c;
        out[used] = '\0';
    }
    return 1;
}

static int pm_toml_get_version_string(
    const char* text,
    const char* version,
    const char* key,
    char* out,
    size_t out_len)
{
    char section[160];
    const char* pos;
    if (snprintf(section, sizeof(section), "[versions.\"%s\"]", version) >= (int)sizeof(section)) {
        return 0;
    }
    pos = strstr(text, section);
    return pos != NULL && pm_toml_get_string(pos, key, out, out_len);
}

static int pm_toml_get_section_string(
    const char* text,
    const char* section,
    const char* key,
    char* out,
    size_t out_len)
{
    char header[192];
    const char* pos;
    const char* next;
    size_t section_len;
    char* copy;
    int ok;
    if (text == NULL || section == NULL || key == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (snprintf(header, sizeof(header), "[%s]", section) >= (int)sizeof(header)) {
        return 0;
    }
    pos = strstr(text, header);
    if (pos == NULL) {
        return 0;
    }
    next = strstr(pos + strlen(header), "\n[");
    section_len = (next == NULL) ? strlen(pos) : (size_t)(next - pos);
    copy = (char*)malloc(section_len + 1U);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, pos, section_len);
    copy[section_len] = '\0';
    ok = pm_toml_get_string(copy, key, out, out_len);
    free(copy);
    return ok;
}

static int pm_project_config_get_string(const char* project_dir, const char* key, char* out, size_t out_len)
{
    static const char* files[] = { "config.toml", "config.local.toml" };
    size_t i;
    int found = 0;
    if (project_dir == NULL || key == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    for (i = 0U; i < sizeof(files) / sizeof(files[0]); i += 1U) {
        char path[PATH_MAX];
        char* text = NULL;
        char value[PATH_MAX];
        if (!pm_join_path(project_dir, files[i], path, sizeof(path)) ||
            !pm_file_exists(path) ||
            !pm_read_text_limited(path, &text)) {
            continue;
        }
        if (pm_toml_get_string(text, key, value, sizeof(value))) {
            (void)snprintf(out, out_len, "%s", value);
            found = 1;
        }
        free(text);
    }
    return found && strlen(out) < out_len;
}

static int pm_project_config_get_package_path(
    const char* project_dir,
    const char* package_name,
    char* out,
    size_t out_len)
{
    static const char* files[] = { "config.toml", "config.local.toml" };
    size_t i;
    int found = 0;
    if (project_dir == NULL || package_name == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    for (i = 0U; i < sizeof(files) / sizeof(files[0]); i += 1U) {
        char path[PATH_MAX];
        char* text = NULL;
        char section[192];
        char value[PATH_MAX];
        if (snprintf(section, sizeof(section), "packages.%s", package_name) >= (int)sizeof(section) ||
            !pm_join_path(project_dir, files[i], path, sizeof(path)) ||
            !pm_file_exists(path) ||
            !pm_read_text_limited(path, &text)) {
            continue;
        }
        if (pm_toml_get_section_string(text, section, "path", value, sizeof(value))) {
            (void)snprintf(out, out_len, "%s", value);
            found = 1;
        }
        free(text);
    }
    return found && strlen(out) < out_len;
}

static int pm_resolve_project_local_path(
    const char* project_dir,
    const char* configured_path,
    char* out,
    size_t out_len)
{
    if (project_dir == NULL || configured_path == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    if (pm_is_absolute_path(configured_path)) {
        return snprintf(out, out_len, "%s", configured_path) >= 0 && strlen(configured_path) < out_len;
    }
    return pm_join_path(project_dir, configured_path, out, out_len);
}

static int pm_is_dotted_namespace(const char* text)
{
    int saw_dot = 0;
    int expect_segment_start = 1;
    size_t i;
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    for (i = 0U; text[i] != '\0'; i += 1U) {
        unsigned char c = (unsigned char)text[i];
        if (expect_segment_start) {
            if (!islower(c)) {
                return 0;
            }
            expect_segment_start = 0;
        } else if (c == '.') {
            saw_dot = 1;
            expect_segment_start = 1;
        } else if (!islower(c) && !isdigit(c)) {
            return 0;
        }
    }
    return saw_dot && !expect_segment_start;
}

static int pm_is_package_name(const char* text)
{
    int expect_segment_start = 1;
    size_t i;
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    for (i = 0U; text[i] != '\0'; i += 1U) {
        unsigned char c = (unsigned char)text[i];
        if (expect_segment_start) {
            if (!islower(c)) {
                return 0;
            }
            expect_segment_start = 0;
        } else if (c == '-') {
            expect_segment_start = 1;
        } else if (!islower(c) && !isdigit(c)) {
            return 0;
        }
    }
    return !expect_segment_start;
}

static int pm_collect_source_dependencies(
    const char* descriptor,
    AilangPackageSourceMetadata* metadata,
    char* error,
    size_t error_len)
{
    const char* section;
    const char* cursor;
    if (descriptor == NULL || metadata == NULL) {
        return pm_set_error(error, error_len, "invalid package dependency metadata request");
    }
    section = strstr(descriptor, "[dependencies]");
    if (section == NULL) {
        return 1;
    }
    cursor = strchr(section, '\n');
    if (cursor == NULL) {
        return 1;
    }
    cursor += 1;
    while (*cursor != '\0') {
        const char* line_start = cursor;
        const char* line_end = strchr(line_start, '\n');
        const char* eq;
        const char* key_start;
        const char* key_end;
        const char* value_start;
        const char* value_end;
        char name[128];
        char version[64];
        size_t n;
        if (line_end == NULL) {
            line_end = line_start + strlen(line_start);
        }
        cursor = *line_end == '\n' ? line_end + 1 : line_end;
        key_start = line_start;
        while (key_start < line_end && isspace((unsigned char)*key_start)) {
            key_start += 1;
        }
        if (key_start == line_end || *key_start == '#') {
            continue;
        }
        if (*key_start == '[') {
            break;
        }
        eq = memchr(key_start, '=', (size_t)(line_end - key_start));
        if (eq == NULL) {
            return pm_set_error(error, error_len, "invalid package dependency declaration");
        }
        key_end = eq;
        while (key_end > key_start && isspace((unsigned char)key_end[-1])) {
            key_end -= 1;
        }
        n = (size_t)(key_end - key_start);
        if (n == 0U || n + 1U > sizeof(name)) {
            return pm_set_error(error, error_len, "package dependency name is too long");
        }
        memcpy(name, key_start, n);
        name[n] = '\0';
        if (!pm_is_package_name(name) || strcmp(name, "ailang") == 0) {
            return pm_set_error(error, error_len, "invalid package dependency name: %s", name);
        }
        value_start = eq + 1;
        while (value_start < line_end && isspace((unsigned char)*value_start)) {
            value_start += 1;
        }
        if (value_start >= line_end || *value_start != '"') {
            return pm_set_error(error, error_len, "package dependency must use an exact quoted version: %s", name);
        }
        value_start += 1;
        value_end = memchr(value_start, '"', (size_t)(line_end - value_start));
        if (value_end == NULL) {
            return pm_set_error(error, error_len, "invalid package dependency version: %s", name);
        }
        n = (size_t)(value_end - value_start);
        if (n == 0U || n + 1U > sizeof(version)) {
            return pm_set_error(error, error_len, "package dependency version is too long: %s", name);
        }
        memcpy(version, value_start, n);
        version[n] = '\0';
        if (metadata->dependency_count >= AILANG_PM_MAX_DEPENDENCIES) {
            return pm_set_error(error, error_len, "too many package dependencies");
        }
        (void)snprintf(
            metadata->dependencies[metadata->dependency_count].name,
            sizeof(metadata->dependencies[metadata->dependency_count].name),
            "%s",
            name);
        (void)snprintf(
            metadata->dependencies[metadata->dependency_count].version,
            sizeof(metadata->dependencies[metadata->dependency_count].version),
            "%s",
            version);
        metadata->dependency_count += 1U;
    }
    return 1;
}

static int pm_collect_source_namespaces(
    const char* descriptor,
    AilangPackageSourceMetadata* metadata,
    char* error,
    size_t error_len)
{
    const char* cursor;
    size_t namespace_count = 0U;
    size_t entry_count = 0U;
    size_t used = 0U;
    if (descriptor == NULL || metadata == NULL) {
        return pm_set_error(error, error_len, "invalid package source metadata request");
    }
    metadata->namespaces[0] = '\0';
    metadata->dependency_count = 0U;
    cursor = descriptor;
    while ((cursor = strstr(cursor, "entry = \"")) != NULL) {
        entry_count += 1U;
        cursor += strlen("entry = \"");
    }
    cursor = descriptor;
    while ((cursor = strstr(cursor, "namespace = \"")) != NULL) {
        const char* start = cursor + strlen("namespace = \"");
        const char* end = strchr(start, '"');
        char namespace_value[128];
        size_t n;
        if (end == NULL) {
            return pm_set_error(error, error_len, "invalid package namespace declaration");
        }
        n = (size_t)(end - start);
        if (n + 1U > sizeof(namespace_value)) {
            return pm_set_error(error, error_len, "package namespace is too long");
        }
        memcpy(namespace_value, start, n);
        namespace_value[n] = '\0';
        if (!pm_is_dotted_namespace(namespace_value)) {
            return pm_set_error(error, error_len, "invalid package namespace: %s", namespace_value);
        }
        if (metadata->namespaces[0] != '\0' &&
            strstr(metadata->namespaces, namespace_value) != NULL) {
            return pm_set_error(error, error_len, "duplicate package namespace: %s", namespace_value);
        }
        if (namespace_count > 0U && !pm_append(metadata->namespaces, sizeof(metadata->namespaces), &used, ", ")) {
            return pm_set_error(error, error_len, "package namespace list overflow");
        }
        if (!pm_append(metadata->namespaces, sizeof(metadata->namespaces), &used, "\"") ||
            !pm_append(metadata->namespaces, sizeof(metadata->namespaces), &used, namespace_value) ||
            !pm_append(metadata->namespaces, sizeof(metadata->namespaces), &used, "\"")) {
            return pm_set_error(error, error_len, "package namespace list overflow");
        }
        namespace_count += 1U;
        cursor = end + 1;
    }
    if (entry_count > 0U && namespace_count != entry_count) {
        return pm_set_error(error, error_len, "package library entries must declare namespaces");
    }
    return pm_collect_source_dependencies(descriptor, metadata, error, error_len);
}

static int pm_load_package_source_metadata(
    const char* package_dir,
    const AilangPackageRecord* record,
    AilangPackageSourceMetadata* metadata,
    char* error,
    size_t error_len)
{
    char package_root[PATH_MAX];
    char descriptor_path[PATH_MAX];
    char* descriptor = NULL;
    int ok;
    if (package_dir == NULL || record == NULL || metadata == NULL) {
        return pm_set_error(error, error_len, "invalid package source metadata request");
    }
    if (!pm_join_path(package_dir, record->package_root, package_root, sizeof(package_root)) ||
        !pm_join_path(package_root, "package.toml", descriptor_path, sizeof(descriptor_path))) {
        return pm_set_error(error, error_len, "package source descriptor path overflow");
    }
    if (!pm_read_text_limited(descriptor_path, &descriptor)) {
        return pm_set_error(error, error_len, "package source descriptor not found: %s", record->name);
    }
    ok = pm_collect_source_namespaces(descriptor, metadata, error, error_len);
    free(descriptor);
    return ok;
}

static int pm_namespace_seen(const char* registry, const char* namespace_value)
{
    char needle[160];
    if (registry == NULL || namespace_value == NULL || namespace_value[0] == '\0') {
        return 0;
    }
    if (snprintf(needle, sizeof(needle), "\n%s\n", namespace_value) >= (int)sizeof(needle)) {
        return 0;
    }
    return strstr(registry, needle) != NULL;
}

static int pm_restore_queue_index(const AilangPackageRestoreQueue* queue, const char* name)
{
    size_t i;
    if (queue == NULL || name == NULL) {
        return -1;
    }
    for (i = 0U; i < queue->count; i += 1U) {
        if (strcmp(queue->entries[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int pm_restore_queue_add(
    AilangPackageRestoreQueue* queue,
    const char* name,
    const char* version,
    char* error,
    size_t error_len)
{
    int existing;
    const char* requested_version = version == NULL ? "" : version;
    if (queue == NULL || name == NULL || name[0] == '\0') {
        return pm_set_error(error, error_len, "invalid package dependency request");
    }
    existing = pm_restore_queue_index(queue, name);
    if (existing >= 0) {
        const char* existing_version = queue->entries[existing].version;
        if (existing_version[0] != '\0' && requested_version[0] != '\0' &&
            strcmp(existing_version, requested_version) != 0) {
            return pm_set_error(
                error,
                error_len,
                "package dependency version conflict: %s %s vs %s",
                name,
                existing_version,
                requested_version);
        }
        if (existing_version[0] == '\0' && requested_version[0] != '\0') {
            (void)snprintf(queue->entries[existing].version, sizeof(queue->entries[existing].version), "%s", requested_version);
        }
        return 1;
    }
    if (queue->count >= AILANG_PM_MAX_DEPENDENCIES) {
        return pm_set_error(error, error_len, "too many package dependencies");
    }
    (void)snprintf(queue->entries[queue->count].name, sizeof(queue->entries[queue->count].name), "%s", name);
    (void)snprintf(queue->entries[queue->count].version, sizeof(queue->entries[queue->count].version), "%s", requested_version);
    queue->count += 1U;
    return 1;
}

static int pm_first_version(const char* text, char* out, size_t out_len)
{
    const char* marker;
    const char* end;
    size_t n;
    marker = strstr(text, "[versions.\"");
    if (marker == NULL) {
        return 0;
    }
    marker += strlen("[versions.\"");
    end = strchr(marker, '"');
    if (end == NULL) {
        return 0;
    }
    n = (size_t)(end - marker);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out, marker, n);
    out[n] = '\0';
    return 1;
}

static int pm_shell_quote(const char* value, char* out, size_t out_len)
{
    size_t i;
    size_t w = 0U;
    if (value == NULL || out == NULL || out_len < 3U) {
        return 0;
    }
#ifdef _WIN32
    out[w++] = '"';
    for (i = 0U; value[i] != '\0'; i += 1U) {
        if (value[i] == '"') {
            if (w + 2U >= out_len) return 0;
            out[w++] = '\\';
        } else if (w + 1U >= out_len) {
            return 0;
        }
        out[w++] = value[i];
    }
    if (w + 2U > out_len) return 0;
    out[w++] = '"';
    out[w] = '\0';
#else
    out[w++] = '\'';
    for (i = 0U; value[i] != '\0'; i += 1U) {
        if (value[i] == '\'') {
            if (w + 4U >= out_len) return 0;
            out[w++] = '\'';
            out[w++] = '\\';
            out[w++] = '\'';
            out[w++] = '\'';
        } else {
            if (w + 1U >= out_len) return 0;
            out[w++] = value[i];
        }
    }
    if (w + 2U > out_len) return 0;
    out[w++] = '\'';
    out[w] = '\0';
#endif
    return 1;
}

static int pm_run(const char* command)
{
    return command != NULL && system(command) == 0;
}

static unsigned int pm_tool_timeout_seconds(void)
{
    const char* env = getenv("AILANG_PACKAGE_TOOL_TIMEOUT_SECONDS");
    char* end = NULL;
    unsigned long value;
    if (env == NULL || env[0] == '\0') {
        return AILANG_PM_TOOL_TIMEOUT_SECONDS;
    }
    value = strtoul(env, &end, 10);
    if (end == env || *end != '\0' || value == 0UL) {
        return AILANG_PM_TOOL_TIMEOUT_SECONDS;
    }
    if (value > AILANG_PM_TOOL_TIMEOUT_MAX_SECONDS) {
        return AILANG_PM_TOOL_TIMEOUT_MAX_SECONDS;
    }
    return (unsigned int)value;
}

static int pm_run_bounded_command(
    const char* command,
    unsigned int timeout_seconds,
    int* exit_code,
    int* timed_out)
{
    if (exit_code == NULL || timed_out == NULL || command == NULL || command[0] == '\0') {
        return 0;
    }
    *timed_out = 0;
#ifdef _WIN32
    {
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        DWORD wait_result;
        DWORD child_exit = 1U;
        char command_line[PATH_MAX * 4];
        if (strlen(command) >= sizeof(command_line)) {
            return 0;
        }
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        (void)snprintf(command_line, sizeof(command_line), "%s", command);
        if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
            return 0;
        }
        wait_result = WaitForSingleObject(process.hProcess, timeout_seconds * 1000U);
        if (wait_result == WAIT_TIMEOUT) {
            *timed_out = 1;
            TerminateProcess(process.hProcess, 124U);
            WaitForSingleObject(process.hProcess, INFINITE);
            *exit_code = 124;
        } else if (wait_result == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &child_exit)) {
            *exit_code = (int)child_exit;
        } else {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 0;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }
#else
    {
        pid_t pid;
        time_t start;
        int status = 0;
        pid = fork();
        if (pid < 0) {
            return 0;
        }
        if (pid == 0) {
            (void)setpgid(0, 0);
            execl("/bin/sh", "sh", "-c", command, (char*)NULL);
            _exit(127);
        }
        (void)setpgid(pid, pid);
        start = time(NULL);
        for (;;) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                break;
            }
            if (waited < 0) {
                return 0;
            }
            if (start != (time_t)-1 && time(NULL) - start >= (time_t)timeout_seconds) {
                *timed_out = 1;
                (void)kill(-pid, SIGKILL);
                (void)kill(pid, SIGKILL);
                (void)waitpid(pid, &status, 0);
                *exit_code = 124;
                return 1;
            }
            sleep(1U);
        }
        if (WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            *exit_code = 128 + WTERMSIG(status);
        } else {
            *exit_code = 1;
        }
        return 1;
    }
#endif
}

static int pm_run_tool_command(
    const char* tool_name,
    const char* tool_path,
    int arg_count,
    char** args,
    int* exit_code,
    char* error,
    size_t error_len)
{
    char command[PATH_MAX * 4];
    char quoted[PATH_MAX * 2];
    size_t used = 0U;
    int i;
    int timed_out = 0;
    unsigned int timeout_seconds = pm_tool_timeout_seconds();
    if (tool_path == NULL || exit_code == NULL || !pm_shell_quote(tool_path, quoted, sizeof(quoted))) {
        return 0;
    }
    command[0] = '\0';
    if (!pm_append(command, sizeof(command), &used, quoted)) {
        return 0;
    }
    for (i = 0; i < arg_count; i += 1) {
        if (args == NULL || args[i] == NULL || !pm_shell_quote(args[i], quoted, sizeof(quoted)) ||
            !pm_append(command, sizeof(command), &used, " ") ||
            !pm_append(command, sizeof(command), &used, quoted)) {
            return 0;
        }
    }
    if (!pm_run_bounded_command(command, timeout_seconds, exit_code, &timed_out)) {
        return 0;
    }
    if (timed_out) {
        return pm_set_error(
            error,
            error_len,
            "package tool timed out after %u seconds: %s",
            timeout_seconds,
            tool_name == NULL ? "<unknown>" : tool_name);
    }
    return 1;
}

static int pm_resolve_install_root(const AilangPackageManagerOptions* options, char* out, size_t out_len)
{
    const char* env;
    const char* home;
    const char* project_dir;
    int n;
    if (options != NULL && options->install_root != NULL && options->install_root[0] != '\0') {
        return snprintf(out, out_len, "%s", options->install_root) >= 0 &&
               strlen(options->install_root) < out_len;
    }
    env = getenv("AILANG_INSTALL_ROOT");
    if (env != NULL && env[0] != '\0') {
        return snprintf(out, out_len, "%s", env) >= 0 && strlen(env) < out_len;
    }
    project_dir = (options != NULL && options->project_dir != NULL && options->project_dir[0] != '\0') ?
        options->project_dir :
        NULL;
    if (project_dir != NULL && pm_project_config_get_string(project_dir, "installRoot", out, out_len)) {
        return 1;
    }
    home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return snprintf(out, out_len, ".ailang") >= 0 && strlen(".ailang") < out_len;
    }
    n = snprintf(out, out_len, "%s%c.ailang", home, AILANG_PM_PATH_SEP);
    return n >= 0 && (size_t)n < out_len;
}

static int pm_resolve_registry(const AilangPackageManagerOptions* options, char* out, size_t out_len)
{
    const char* env;
    const char* project_dir;
    char install_root[PATH_MAX];
    char registries[PATH_MAX];
    char quoted_out[PATH_MAX * 2];
    char command[PATH_MAX * 3];
    if (options != NULL && options->registry_dir != NULL && options->registry_dir[0] != '\0') {
        return snprintf(out, out_len, "%s", options->registry_dir) >= 0 &&
               strlen(options->registry_dir) < out_len;
    }
    env = getenv("AILANG_PACKAGE_REGISTRY");
    if (env != NULL && env[0] != '\0') {
        return snprintf(out, out_len, "%s", env) >= 0 && strlen(env) < out_len;
    }
    project_dir = (options != NULL && options->project_dir != NULL && options->project_dir[0] != '\0') ?
        options->project_dir :
        NULL;
    if (project_dir != NULL && pm_project_config_get_string(project_dir, "packageRegistry", out, out_len)) {
        return 1;
    }
    if (pm_directory_exists("../ailang-packages")) {
        return snprintf(out, out_len, "../ailang-packages") >= 0 &&
               strlen("../ailang-packages") < out_len;
    }
    if (pm_directory_exists("ailang-packages")) {
        return snprintf(out, out_len, "ailang-packages") >= 0 &&
               strlen("ailang-packages") < out_len;
    }
    if (!pm_resolve_install_root(options, install_root, sizeof(install_root)) ||
        !pm_join_path(install_root, "registries", registries, sizeof(registries)) ||
        !pm_join_path(registries, "ailang-packages", out, out_len) ||
        !pm_mkdirs(registries)) {
        return 0;
    }
    if (pm_directory_exists(out)) {
        if (!pm_shell_quote(out, quoted_out, sizeof(quoted_out))) {
            return 0;
        }
        if (snprintf(
                command,
                sizeof(command),
                "git -C %s fetch --depth 1 --quiet origin main && git -C %s reset --hard --quiet origin/main",
                quoted_out,
                quoted_out) >= (int)sizeof(command)) {
            return 0;
        }
        return pm_run(command);
    }
    if (!pm_shell_quote(out, quoted_out, sizeof(quoted_out))) {
        return 0;
    }
    if (snprintf(
            command,
            sizeof(command),
            "git clone --depth 1 https://github.com/AiLangCore/ailang-packages.git %s",
            quoted_out) >= (int)sizeof(command)) {
        return 0;
    }
    return pm_run(command) && pm_directory_exists(out);
}

static int pm_load_record(
    const char* registry,
    const char* name,
    const char* version,
    AilangPackageRecord* record,
    char* error,
    size_t error_len)
{
    char packages[PATH_MAX];
    char filename[256];
    char path[PATH_MAX];
    char* text = NULL;
    if (registry == NULL || name == NULL || record == NULL) {
        return pm_set_error(error, error_len, "invalid package record request");
    }
    memset(record, 0, sizeof(*record));
    if (snprintf(filename, sizeof(filename), "%s.toml", name) >= (int)sizeof(filename) ||
        !pm_join_path(registry, "packages", packages, sizeof(packages)) ||
        !pm_join_path(packages, filename, path, sizeof(path)) ||
        !pm_read_text_limited(path, &text)) {
        return pm_set_error(error, error_len, "package not found in registry: %s", name);
    }
    if (!pm_toml_get_string(text, "name", record->name, sizeof(record->name)) ||
        !pm_toml_get_string(text, "repo", record->repo, sizeof(record->repo)) ||
        !pm_toml_get_string(text, "packageRoot", record->package_root, sizeof(record->package_root)) ||
        !pm_toml_get_array(text, "types", record->types, sizeof(record->types))) {
        free(text);
        return pm_set_error(error, error_len, "invalid package registry record: %s", name);
    }
    if (version != NULL && version[0] != '\0') {
        (void)snprintf(record->version, sizeof(record->version), "%s", version);
    } else if (!pm_toml_get_string(text, "defaultVersion", record->version, sizeof(record->version)) &&
               !pm_first_version(text, record->version, sizeof(record->version))) {
        free(text);
        return pm_set_error(error, error_len, "package has no version: %s", name);
    }
    if (!pm_toml_get_version_string(text, record->version, "ref", record->ref, sizeof(record->ref)) ||
        !pm_toml_get_version_string(text, record->version, "commit", record->commit, sizeof(record->commit))) {
        free(text);
        return pm_set_error(error, error_len, "package version not found: %s@%s", name, record->version);
    }
    free(text);
    return 1;
}

static int pm_parse_include_attr(const char* cursor, const char* key, char* out, size_t out_len)
{
    char needle[64];
    const char* pos;
    const char* close;
    const char* start;
    const char* end;
    size_t n;
    if (cursor == NULL || key == NULL || out == NULL || out_len == 0U ||
        snprintf(needle, sizeof(needle), "%s=\"", key) >= (int)sizeof(needle)) {
        return 0;
    }
    close = strchr(cursor, ')');
    if (close == NULL) {
        return 0;
    }
    pos = strstr(cursor, needle);
    if (pos == NULL || pos >= close) {
        return 0;
    }
    start = pos + strlen(needle);
    end = strchr(start, '"');
    if (end == NULL || end > close) {
        return 0;
    }
    n = (size_t)(end - start);
    if (n + 1U > out_len) {
        return 0;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return 1;
}

static int pm_parse_include_ex(
    const char* cursor,
    char* name,
    size_t name_len,
    char* version,
    size_t version_len,
    char* path,
    size_t path_len)
{
    if (cursor == NULL || name == NULL || version == NULL) {
        return 0;
    }
    if (!pm_parse_include_attr(cursor, "name", name, name_len)) {
        return 0;
    }
    version[0] = '\0';
    (void)pm_parse_include_attr(cursor, "version", version, version_len);
    if (path != NULL && path_len > 0U) {
        path[0] = '\0';
        (void)pm_parse_include_attr(cursor, "path", path, path_len);
    }
    return 1;
}

static int pm_parse_include(const char* cursor, char* name, size_t name_len, char* version, size_t version_len)
{
    return pm_parse_include_ex(cursor, name, name_len, version, version_len, NULL, 0U);
}

static int pm_parse_package_spec(const char* spec, char* name, size_t name_len, char* version, size_t version_len)
{
    const char* at;
    size_t name_size;
    if (spec == NULL || spec[0] == '\0' || name == NULL || version == NULL) {
        return 0;
    }
    at = strrchr(spec, '@');
    if (at != NULL && at != spec && at[1] != '\0') {
        name_size = (size_t)(at - spec);
        if (name_size + 1U > name_len || strlen(at + 1) + 1U > version_len) {
            return 0;
        }
        memcpy(name, spec, name_size);
        name[name_size] = '\0';
        (void)snprintf(version, version_len, "%s", at + 1);
    } else {
        if (strlen(spec) + 1U > name_len) {
            return 0;
        }
        (void)snprintf(name, name_len, "%s", spec);
        version[0] = '\0';
    }
    return name[0] != '\0';
}

static int pm_scan_manifest_includes_for_restore(
    const char* project_dir,
    const char* manifest,
    AilangPackageRestoreQueue* queue,
    unsigned int depth,
    char* error,
    size_t error_len)
{
    const char* p;
    if (project_dir == NULL || manifest == NULL || queue == NULL) {
        return pm_set_error(error, error_len, "invalid package restore include scan");
    }
    if (depth > AILANG_PM_LOCAL_INCLUDE_MAX_DEPTH) {
        return pm_set_error(error, error_len, "local package include graph is too deep");
    }
    p = manifest;
    while ((p = strstr(p, "Include")) != NULL) {
        char include_name[128];
        char include_version[64];
        char include_path[PATH_MAX];
        if (!pm_parse_include_ex(
                p,
                include_name,
                sizeof(include_name),
                include_version,
                sizeof(include_version),
                include_path,
                sizeof(include_path))) {
            return pm_set_error(error, error_len, "invalid Include package declaration");
        }
        if (include_path[0] != '\0') {
            char local_project_dir[PATH_MAX];
            char local_manifest_path[PATH_MAX];
            char* local_manifest = NULL;
            int ok;
            if (include_path[0] == '/' || include_path[0] == '\\') {
                if (snprintf(local_project_dir, sizeof(local_project_dir), "%s", include_path) >=
                    (int)sizeof(local_project_dir)) {
                    return pm_set_error(error, error_len, "local package include path overflow");
                }
            } else if (!pm_join_path(project_dir, include_path, local_project_dir, sizeof(local_project_dir))) {
                return pm_set_error(error, error_len, "local package include path overflow");
            }
            if (!pm_join_path(local_project_dir, "project.aiproj", local_manifest_path, sizeof(local_manifest_path))) {
                return pm_set_error(error, error_len, "local package manifest path overflow");
            }
            if (pm_file_exists(local_manifest_path)) {
                if (!pm_read_text_limited(local_manifest_path, &local_manifest)) {
                    return pm_set_error(error, error_len, "could not read local package manifest: %s", include_name);
                }
                ok = pm_scan_manifest_includes_for_restore(
                    local_project_dir,
                    local_manifest,
                    queue,
                    depth + 1U,
                    error,
                    error_len);
                free(local_manifest);
                if (!ok) {
                    return 0;
                }
            }
        } else {
            if (pm_is_sdk_owned_name(include_name)) {
                return pm_set_error(error, error_len, "ailang is provided by the selected SDK, not package restore");
            }
            if (!pm_restore_queue_add(queue, include_name, include_version, error, error_len)) {
                return 0;
            }
        }
        p += strlen("Include");
    }
    return 1;
}

static int pm_is_sdk_owned_name(const char* name)
{
    return name != NULL && strcmp(name, "ailang") == 0;
}

static int pm_manifest_has_include(const char* manifest, const char* package_name)
{
    const char* p = manifest;
    while (p != NULL && (p = strstr(p, "Include")) != NULL) {
        char name[128];
        char version[64];
        if (pm_parse_include(p, name, sizeof(name), version, sizeof(version)) &&
            strcmp(name, package_name) == 0) {
            return 1;
        }
        p += strlen("Include");
    }
    return 0;
}

static const char* pm_find_matching_delim(const char* open, char left, char right)
{
    const char* p;
    int depth = 0;
    int in_string = 0;
    if (open == NULL || *open != left) {
        return NULL;
    }
    for (p = open; *p != '\0'; p += 1) {
        if (*p == '"' && (p == open || p[-1] != '\\')) {
            in_string = !in_string;
        }
        if (in_string) {
            continue;
        }
        if (*p == left) {
            depth += 1;
        } else if (*p == right) {
            depth -= 1;
            if (depth == 0) {
                return p;
            }
        }
    }
    return NULL;
}

static const char* pm_find_project_attr_close(const char* manifest)
{
    const char* project;
    const char* attrs;
    project = strstr(manifest, "Project");
    if (project == NULL) {
        return NULL;
    }
    attrs = strchr(project, '(');
    return pm_find_matching_delim(attrs, '(', ')');
}

static int pm_append_include_to_manifest(const char* manifest, const AilangPackageRecord* record, char** out_text)
{
    const char* attr_close;
    const char* insert_at;
    const char* suffix_at;
    char id[160];
    char include_line[384];
    size_t prefix_len;
    size_t manifest_len;
    size_t line_len;
    size_t extra_len = 0U;
    int has_project_block = 0;
    char* next;
    if (manifest == NULL || record == NULL || out_text == NULL) {
        return 0;
    }
    *out_text = NULL;
    attr_close = pm_find_project_attr_close(manifest);
    if (attr_close == NULL || !pm_sanitize_id(record->name, id, sizeof(id))) {
        return 0;
    }
    if (snprintf(
            include_line,
            sizeof(include_line),
            "    Include#dep_%s(name=\"%s\" version=\"%s\")\n",
            id,
            record->name,
            record->version) >= (int)sizeof(include_line)) {
        return 0;
    }
    manifest_len = strlen(manifest);
    insert_at = attr_close + 1;
    while (*insert_at != '\0' && isspace((unsigned char)*insert_at)) {
        insert_at += 1;
    }
    if (*insert_at == '{') {
        const char* project_end = pm_find_matching_delim(insert_at, '{', '}');
        if (project_end == NULL) {
            return 0;
        }
        insert_at = project_end;
        suffix_at = project_end;
        has_project_block = 1;
    } else {
        suffix_at = insert_at;
        insert_at = attr_close + 1;
        extra_len = strlen(" {\n  }\n");
    }
    prefix_len = (size_t)(insert_at - manifest);
    line_len = strlen(include_line);
    next = (char*)malloc(manifest_len + line_len + extra_len + 2U);
    if (next == NULL) {
        return 0;
    }
    memcpy(next, manifest, prefix_len);
    if (has_project_block) {
        if (prefix_len > 0U && next[prefix_len - 1U] != '\n') {
            next[prefix_len++] = '\n';
        }
        memcpy(next + prefix_len, include_line, line_len);
        memcpy(next + prefix_len + line_len, suffix_at, strlen(suffix_at) + 1U);
    } else {
        memcpy(next + prefix_len, " {\n", 3U);
        prefix_len += 3U;
        memcpy(next + prefix_len, include_line, line_len);
        prefix_len += line_len;
        memcpy(next + prefix_len, "  }\n", 4U);
        prefix_len += 4U;
        memcpy(next + prefix_len, suffix_at, strlen(suffix_at) + 1U);
    }
    *out_text = next;
    return 1;
}

static int pm_remove_include_from_manifest(const char* manifest, const char* package_name, char** out_text, int* removed)
{
    const char* line_start;
    const char* cursor;
    size_t output_len;
    size_t used = 0U;
    char* next;
    if (manifest == NULL || package_name == NULL || out_text == NULL || removed == NULL) {
        return 0;
    }
    output_len = strlen(manifest) + 1U;
    next = (char*)malloc(output_len);
    if (next == NULL) {
        return 0;
    }
    next[0] = '\0';
    *removed = 0;
    cursor = manifest;
    while (*cursor != '\0') {
        const char* line_end = strchr(cursor, '\n');
        size_t line_len = line_end == NULL ? strlen(cursor) : (size_t)(line_end - cursor) + 1U;
        line_start = cursor;
        if (strstr(line_start, "Include") != NULL) {
            char line[1024];
            char name[128];
            char version[64];
            size_t copy_len = line_len >= sizeof(line) ? sizeof(line) - 1U : line_len;
            memcpy(line, line_start, copy_len);
            line[copy_len] = '\0';
            if (pm_parse_include(line, name, sizeof(name), version, sizeof(version)) &&
                strcmp(name, package_name) == 0) {
                *removed += 1;
                cursor += line_len;
                continue;
            }
        }
        if (used + line_len + 1U > output_len) {
            free(next);
            return 0;
        }
        memcpy(next + used, line_start, line_len);
        used += line_len;
        next[used] = '\0';
        cursor += line_len;
    }
    *out_text = next;
    return 1;
}

static int pm_contains_token(const char* types, const char* token)
{
    return types != NULL && token != NULL && strstr(types, token) != NULL;
}

static int pm_is_compiled_command(const char* name)
{
    static const char* commands[] = {
        "run", "build", "init", "template", "agent", "package", "clean",
        "project", "version", "help", "debug", "repl", "bench", "publish",
        "serve"
    };
    size_t i;
    for (i = 0U; i < (sizeof(commands) / sizeof(commands[0])); i += 1U) {
        if (strcmp(name, commands[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int pm_tool_conflict(
    const AilangPackageManagerOptions* options,
    const AilangPackageRecord* record,
    char* detail,
    size_t detail_len)
{
    char install_root[PATH_MAX];
    char global_tools[PATH_MAX];
    char global_tool[PATH_MAX];
    char local_root[PATH_MAX];
    char local_tools[PATH_MAX];
    char local_tool[PATH_MAX];
    const char* project_dir;
    if (record == NULL || !pm_contains_token(record->types, "tool")) {
        return 0;
    }
    if (pm_is_compiled_command(record->name)) {
        (void)snprintf(detail, detail_len, "compiled command '%s'", record->name);
        return 1;
    }
    if (pm_resolve_install_root(options, install_root, sizeof(install_root)) &&
        pm_join_path(install_root, "tools", global_tools, sizeof(global_tools)) &&
        pm_join_path(global_tools, record->name, global_tool, sizeof(global_tool)) &&
        pm_file_exists(global_tool)) {
        (void)snprintf(detail, detail_len, "global tool '%s'", record->name);
        return 1;
    }
    project_dir = (options != NULL && options->project_dir != NULL) ? options->project_dir : ".";
    if (pm_join_path(project_dir, ".ailang", local_root, sizeof(local_root)) &&
        pm_join_path(local_root, "tools", local_tools, sizeof(local_tools)) &&
        pm_join_path(local_tools, record->name, local_tool, sizeof(local_tool)) &&
        pm_file_exists(local_tool)) {
        (void)snprintf(detail, detail_len, "local tool '%s'", record->name);
        return 1;
    }
    return 0;
}

static int pm_clone_checkout(const AilangPackageRecord* record, const char* package_dir)
{
    char quoted_repo[PATH_MAX * 2];
    char quoted_dir[PATH_MAX * 2];
    char quoted_commit[256];
    char command[PATH_MAX * 5];
    if (!pm_shell_quote(record->repo, quoted_repo, sizeof(quoted_repo)) ||
        !pm_shell_quote(package_dir, quoted_dir, sizeof(quoted_dir)) ||
        !pm_shell_quote(record->commit, quoted_commit, sizeof(quoted_commit))) {
        return 0;
    }
    if (!pm_directory_exists(package_dir)) {
        if (snprintf(command, sizeof(command), "git clone %s %s", quoted_repo, quoted_dir) >= (int)sizeof(command) ||
            !pm_run(command)) {
            return 0;
        }
    }
    if (snprintf(command, sizeof(command), "git -C %s fetch --tags --quiet", quoted_dir) >= (int)sizeof(command) ||
        !pm_run(command)) {
        return 0;
    }
    if (snprintf(command, sizeof(command), "git -C %s checkout --quiet %s", quoted_dir, quoted_commit) >= (int)sizeof(command)) {
        return 0;
    }
    return pm_run(command);
}

int ailang_package_manager_list(
    const AilangPackageManagerOptions* options,
    char* output,
    size_t output_len,
    char* error,
    size_t error_len)
{
    const char* project_dir = (options != NULL && options->project_dir != NULL) ? options->project_dir : ".";
    char lock_path[PATH_MAX];
    char registry[PATH_MAX];
    char packages[PATH_MAX];
    size_t used = 0U;
    if (output == NULL || output_len == 0U) {
        return pm_set_error(error, error_len, "missing package list output buffer");
    }
    output[0] = '\0';
    if (!pm_join_path(project_dir, "ailang.lock.toml", lock_path, sizeof(lock_path))) {
        return pm_set_error(error, error_len, "package list path overflow");
    }
    if (pm_file_exists(lock_path)) {
        char* text = NULL;
        const char* p;
        if (!pm_read_text_limited(lock_path, &text)) {
            return pm_set_error(error, error_len, "could not read package lockfile");
        }
        p = text;
        while ((p = strstr(p, "[[package]]")) != NULL) {
            char name[128] = "";
            char version[64] = "";
            char namespaces[512] = "";
            char namespaces_display[512] = "";
            if (pm_toml_get_string(p, "name", name, sizeof(name))) {
                (void)pm_toml_get_string(p, "version", version, sizeof(version));
                (void)pm_toml_get_array(p, "namespaces", namespaces, sizeof(namespaces));
                if (namespaces[0] != '\0' &&
                    !pm_toml_array_to_display(namespaces, namespaces_display, sizeof(namespaces_display))) {
                    free(text);
                    return pm_set_error(error, error_len, "package namespace list output overflow");
                }
                if (!pm_appendf(
                        output,
                        output_len,
                        &used,
                        "%s%s%s%s%s\n",
                        name,
                        version[0] == '\0' ? "" : " ",
                        version,
                        namespaces_display[0] == '\0' ? "" : " namespaces=",
                        namespaces_display)) {
                    free(text);
                    return pm_set_error(error, error_len, "package list output overflow");
                }
            }
            p += strlen("[[package]]");
        }
        free(text);
        return 1;
    }
    if (!pm_resolve_registry(options, registry, sizeof(registry)) ||
        !pm_join_path(registry, "packages", packages, sizeof(packages))) {
        return pm_set_error(error, error_len, "could not resolve package registry");
    }
#ifdef _WIN32
    {
        char pattern[PATH_MAX];
        WIN32_FIND_DATAA data;
        HANDLE h;
        if (!pm_join_path(packages, "*.toml", pattern, sizeof(pattern))) {
            return pm_set_error(error, error_len, "registry path overflow");
        }
        h = FindFirstFileA(pattern, &data);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                size_t len = strlen(data.cFileName);
                if (len > 5U && strcmp(data.cFileName + len - 5U, ".toml") == 0) {
                    if (!pm_appendf(output, output_len, &used, "%.*s\n", (int)(len - 5U), data.cFileName)) {
                        FindClose(h);
                        return pm_set_error(error, error_len, "package list output overflow");
                    }
                }
            } while (FindNextFileA(h, &data));
            FindClose(h);
        }
    }
#else
    {
        DIR* dir = opendir(packages);
        struct dirent* ent;
        if (dir == NULL) {
            return 1;
        }
        while ((ent = readdir(dir)) != NULL) {
            size_t len = strlen(ent->d_name);
            if (len > 5U && strcmp(ent->d_name + len - 5U, ".toml") == 0) {
                if (!pm_appendf(output, output_len, &used, "%.*s\n", (int)(len - 5U), ent->d_name)) {
                    closedir(dir);
                    return pm_set_error(error, error_len, "package list output overflow");
                }
            }
        }
        closedir(dir);
    }
#endif
    return 1;
}

int ailang_package_manager_restore(
    const AilangPackageManagerOptions* options,
    char* output,
    size_t output_len,
    char* error,
    size_t error_len)
{
    const char* project_dir = (options != NULL && options->project_dir != NULL) ? options->project_dir : ".";
    char manifest_path[PATH_MAX];
    char registry[PATH_MAX];
    char local_root[PATH_MAX];
    char package_cache[PATH_MAX];
    char lock_path[PATH_MAX];
    char lock_text[AILANG_PM_LOCK_LIMIT];
    size_t lock_used = 0U;
    char* manifest = NULL;
    AilangPackageRestoreQueue queue;
    size_t queue_index;
    char restored_namespaces[1024];
    size_t restored_namespaces_used = 0U;
    int restored = 0;
    if (output != NULL && output_len > 0U) {
        output[0] = '\0';
    }
    restored_namespaces[0] = '\0';
    if (!pm_append(restored_namespaces, sizeof(restored_namespaces), &restored_namespaces_used, "\n")) {
        return pm_set_error(error, error_len, "package namespace registry overflow");
    }
    if (!pm_join_path(project_dir, "project.aiproj", manifest_path, sizeof(manifest_path)) ||
        !pm_join_path(project_dir, ".ailang", local_root, sizeof(local_root)) ||
        !pm_join_path(local_root, "packages", package_cache, sizeof(package_cache)) ||
        !pm_join_path(project_dir, "ailang.lock.toml", lock_path, sizeof(lock_path))) {
        return pm_set_error(error, error_len, "package restore path overflow");
    }
    if (!pm_file_exists(manifest_path)) {
        return pm_set_error(error, error_len, "project.aiproj not found");
    }
    if (!pm_resolve_registry(options, registry, sizeof(registry)) ||
        !pm_read_text_limited(manifest_path, &manifest) ||
        !pm_mkdirs(package_cache)) {
        free(manifest);
        return pm_set_error(error, error_len, "package restore setup failed");
    }
    lock_text[0] = '\0';
    if (!pm_append(&lock_text[0], sizeof(lock_text), &lock_used, "schema = \"ailang.lock.v1\"\n") ||
        !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "registry = \"%s\"\n\n", registry)) {
        free(manifest);
        return pm_set_error(error, error_len, "lockfile output overflow");
    }
    memset(&queue, 0, sizeof(queue));
    if (!pm_scan_manifest_includes_for_restore(project_dir, manifest, &queue, 0U, error, error_len)) {
        free(manifest);
        return 0;
    }
    for (queue_index = 0U; queue_index < queue.count; queue_index += 1U) {
        const char* include_name = queue.entries[queue_index].name;
        const char* include_version = queue.entries[queue_index].version;
        char package_dir[PATH_MAX];
        char lock_package_path[PATH_MAX];
        char configured_package_path[PATH_MAX];
        char configured_package_dir[PATH_MAX];
        char conflict[256];
        AilangPackageRecord record;
        AilangPackageSourceMetadata source_metadata;
        int use_configured_package = 0;
        if (!pm_load_record(registry, include_name, include_version, &record, error, error_len)) {
            free(manifest);
            return 0;
        }
        if (queue.entries[queue_index].version[0] == '\0') {
            (void)snprintf(queue.entries[queue_index].version, sizeof(queue.entries[queue_index].version), "%s", record.version);
        } else if (strcmp(queue.entries[queue_index].version, record.version) != 0) {
            free(manifest);
            return pm_set_error(
                error,
                error_len,
                "package dependency version conflict: %s %s vs %s",
                record.name,
                queue.entries[queue_index].version,
                record.version);
        }
        configured_package_path[0] = '\0';
        configured_package_dir[0] = '\0';
        if (pm_project_config_get_package_path(project_dir, record.name, configured_package_path, sizeof(configured_package_path))) {
            if (!pm_resolve_project_local_path(
                    project_dir,
                    configured_package_path,
                    configured_package_dir,
                    sizeof(configured_package_dir))) {
                free(manifest);
                return pm_set_error(error, error_len, "local package override path overflow: %s", record.name);
            }
            use_configured_package = 1;
        }
        if (!use_configured_package && pm_tool_conflict(options, &record, conflict, sizeof(conflict))) {
            free(manifest);
            return pm_set_error(error, error_len, "package tool conflicts with %s", conflict);
        }
        if (use_configured_package) {
            if (!pm_directory_exists(configured_package_dir)) {
                free(manifest);
                return pm_set_error(error, error_len, "local package override not found: %s", record.name);
            }
            if (snprintf(package_dir, sizeof(package_dir), "%s", configured_package_dir) >= (int)sizeof(package_dir) ||
                snprintf(lock_package_path, sizeof(lock_package_path), "%s", configured_package_path) >= (int)sizeof(lock_package_path)) {
                free(manifest);
                return pm_set_error(error, error_len, "local package override path overflow: %s", record.name);
            }
        } else {
            if (!pm_join_path(package_cache, record.name, package_dir, sizeof(package_dir)) ||
                !pm_clone_checkout(&record, package_dir)) {
                free(manifest);
                return pm_set_error(error, error_len, "package clone/checkout failed: %s", record.name);
            }
            if (snprintf(lock_package_path, sizeof(lock_package_path), ".ailang/packages/%s", record.name) >=
                (int)sizeof(lock_package_path)) {
                free(manifest);
                return pm_set_error(error, error_len, "package lock path overflow: %s", record.name);
            }
        }
        if (!pm_load_package_source_metadata(package_dir, &record, &source_metadata, error, error_len)) {
            free(manifest);
            return 0;
        }
        {
            size_t dep_index;
            for (dep_index = 0U; dep_index < source_metadata.dependency_count; dep_index += 1U) {
                if (!pm_restore_queue_add(
                        &queue,
                        source_metadata.dependencies[dep_index].name,
                        source_metadata.dependencies[dep_index].version,
                        error,
                        error_len)) {
                    free(manifest);
                    return 0;
                }
            }
        }
        if (source_metadata.namespaces[0] != '\0') {
            const char* ns_cursor = source_metadata.namespaces;
            while ((ns_cursor = strchr(ns_cursor, '"')) != NULL) {
                const char* ns_start = ns_cursor + 1;
                const char* ns_end = strchr(ns_start, '"');
                char namespace_value[128];
                size_t n;
                if (ns_end == NULL) {
                    free(manifest);
                    return pm_set_error(error, error_len, "invalid package namespace list");
                }
                n = (size_t)(ns_end - ns_start);
                if (n + 1U > sizeof(namespace_value)) {
                    free(manifest);
                    return pm_set_error(error, error_len, "package namespace is too long");
                }
                memcpy(namespace_value, ns_start, n);
                namespace_value[n] = '\0';
                if (pm_namespace_seen(restored_namespaces, namespace_value)) {
                    free(manifest);
                    return pm_set_error(error, error_len, "duplicate package namespace: %s", namespace_value);
                }
                if (!pm_append(restored_namespaces, sizeof(restored_namespaces), &restored_namespaces_used, namespace_value) ||
                    !pm_append(restored_namespaces, sizeof(restored_namespaces), &restored_namespaces_used, "\n")) {
                    free(manifest);
                    return pm_set_error(error, error_len, "package namespace registry overflow");
                }
                ns_cursor = ns_end + 1;
            }
        }
        if (!pm_append(&lock_text[0], sizeof(lock_text), &lock_used, "[[package]]\n") ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "name = \"%s\"\n", record.name) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "version = \"%s\"\n", record.version) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "repo = \"%s\"\n", record.repo) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "packageRoot = \"%s\"\n", record.package_root) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "commit = \"%s\"\n", record.commit) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "path = \"%s\"\n", lock_package_path) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "types = [%s]\n", record.types) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "namespaces = [%s]\n\n", source_metadata.namespaces)) {
            free(manifest);
            return pm_set_error(error, error_len, "lockfile output overflow");
        }
        restored += 1;
    }
    free(manifest);
    if (restored == 0 && !pm_append(&lock_text[0], sizeof(lock_text), &lock_used, "# no packages\n")) {
        return pm_set_error(error, error_len, "lockfile output overflow");
    }
    if (!pm_write_text(lock_path, lock_text)) {
        return pm_set_error(error, error_len, "could not write ailang.lock.toml");
    }
    if (output != NULL && output_len > 0U) {
        (void)snprintf(output, output_len, "Ok#ok1(type=int value=%d)\n", restored);
    }
    return 1;
}

int ailang_package_manager_add(
    const AilangPackageManagerOptions* options,
    const char* package_spec,
    char* output,
    size_t output_len,
    char* error,
    size_t error_len)
{
    const char* project_dir = (options != NULL && options->project_dir != NULL) ? options->project_dir : ".";
    char manifest_path[PATH_MAX];
    char registry[PATH_MAX];
    char name[128];
    char version[64];
    char restore_output[AILANG_NATIVE_BRIDGE_MAX_STRING];
    char* manifest = NULL;
    char* next_manifest = NULL;
    AilangPackageRecord record;
    char conflict[256];
    if (output != NULL && output_len > 0U) {
        output[0] = '\0';
    }
    if (!pm_parse_package_spec(package_spec, name, sizeof(name), version, sizeof(version))) {
        return pm_set_error(error, error_len, "invalid package spec");
    }
    if (pm_is_sdk_owned_name(name)) {
        return pm_set_error(error, error_len, "ailang is provided by the selected SDK, not package restore");
    }
    if (!pm_join_path(project_dir, "project.aiproj", manifest_path, sizeof(manifest_path)) ||
        !pm_resolve_registry(options, registry, sizeof(registry)) ||
        !pm_read_text_limited(manifest_path, &manifest)) {
        return pm_set_error(error, error_len, "package add setup failed");
    }
    if (pm_manifest_has_include(manifest, name)) {
        free(manifest);
        return pm_set_error(error, error_len, "package already included: %s", name);
    }
    if (!pm_load_record(registry, name, version, &record, error, error_len)) {
        free(manifest);
        return 0;
    }
    if (!pm_project_config_get_package_path(project_dir, record.name, conflict, sizeof(conflict)) &&
        pm_tool_conflict(options, &record, conflict, sizeof(conflict))) {
        free(manifest);
        return pm_set_error(error, error_len, "package tool conflicts with %s", conflict);
    }
    if (
        !pm_append_include_to_manifest(manifest, &record, &next_manifest) ||
        !pm_write_text(manifest_path, next_manifest)) {
        free(manifest);
        free(next_manifest);
        return error != NULL && error[0] != '\0' ? 0 : pm_set_error(error, error_len, "package add failed");
    }
    free(manifest);
    free(next_manifest);
    if (!ailang_package_manager_restore(options, restore_output, sizeof(restore_output), error, error_len)) {
        return 0;
    }
    if (output != NULL && output_len > 0U) {
        (void)snprintf(output, output_len, "Ok#ok1(type=string value=\"added %s %s\")\n", record.name, record.version);
    }
    return 1;
}

int ailang_package_manager_remove(
    const AilangPackageManagerOptions* options,
    const char* package_name,
    char* output,
    size_t output_len,
    char* error,
    size_t error_len)
{
    const char* project_dir = (options != NULL && options->project_dir != NULL) ? options->project_dir : ".";
    char manifest_path[PATH_MAX];
    char restore_output[AILANG_NATIVE_BRIDGE_MAX_STRING];
    char* manifest = NULL;
    char* next_manifest = NULL;
    int removed = 0;
    if (output != NULL && output_len > 0U) {
        output[0] = '\0';
    }
    if (package_name == NULL || package_name[0] == '\0') {
        return pm_set_error(error, error_len, "missing package name");
    }
    if (!pm_join_path(project_dir, "project.aiproj", manifest_path, sizeof(manifest_path)) ||
        !pm_read_text_limited(manifest_path, &manifest)) {
        return pm_set_error(error, error_len, "package remove setup failed");
    }
    if (!pm_remove_include_from_manifest(manifest, package_name, &next_manifest, &removed)) {
        free(manifest);
        return pm_set_error(error, error_len, "package remove failed");
    }
    free(manifest);
    if (removed == 0) {
        free(next_manifest);
        return pm_set_error(error, error_len, "package not included: %s", package_name);
    }
    if (!pm_write_text(manifest_path, next_manifest)) {
        free(next_manifest);
        return pm_set_error(error, error_len, "could not write project.aiproj");
    }
    free(next_manifest);
    if (!ailang_package_manager_restore(options, restore_output, sizeof(restore_output), error, error_len)) {
        return 0;
    }
    if (output != NULL && output_len > 0U) {
        (void)snprintf(output, output_len, "Ok#ok1(type=string value=\"removed %s\")\n", package_name);
    }
    return 1;
}

static int pm_getcwd(char* out, size_t out_len)
{
    if (out == NULL || out_len == 0U) {
        return 0;
    }
#ifdef _WIN32
    return _getcwd(out, (int)out_len) != NULL;
#else
    return getcwd(out, out_len) != NULL;
#endif
}

static int pm_dirname_in_place(char* path)
{
    size_t len;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    len = strlen(path);
    while (len > 1U && (path[len - 1U] == '/' || path[len - 1U] == '\\')) {
        path[len - 1U] = '\0';
        len -= 1U;
    }
    while (len > 0U && path[len - 1U] != '/' && path[len - 1U] != '\\') {
        len -= 1U;
    }
    if (len == 0U) {
        return 0;
    }
    if (len == 1U) {
        path[1] = '\0';
    } else {
        path[len - 1U] = '\0';
    }
    return 1;
}

static int pm_find_project_dir(const AilangPackageManagerOptions* options, char* out, size_t out_len)
{
    char current[PATH_MAX];
    if (options != NULL && options->project_dir != NULL && options->project_dir[0] != '\0') {
        return pm_copy_text(out, out_len, options->project_dir);
    }
    if (!pm_getcwd(current, sizeof(current))) {
        return 0;
    }
    for (;;) {
        char manifest[PATH_MAX];
        char lockfile[PATH_MAX];
        if ((pm_join_path(current, "project.aiproj", manifest, sizeof(manifest)) && pm_file_exists(manifest)) ||
            (pm_join_path(current, "ailang.lock.toml", lockfile, sizeof(lockfile)) && pm_file_exists(lockfile))) {
            return pm_copy_text(out, out_len, current);
        }
        if (!pm_dirname_in_place(current)) {
            break;
        }
    }
    return pm_copy_text(out, out_len, ".");
}

static int pm_find_tool_in_package_root(const char* package_root, const char* tool_name, char* out, size_t out_len)
{
    static const char* prefixes[] = { "tools", "bin", "scripts", "" };
    size_t i;
    for (i = 0U; i < (sizeof(prefixes) / sizeof(prefixes[0])); i += 1U) {
        char dir[PATH_MAX];
        char candidate[PATH_MAX];
        if (prefixes[i][0] == '\0') {
            if (!pm_join_path(package_root, tool_name, candidate, sizeof(candidate))) {
                continue;
            }
        } else if (!pm_join_path(package_root, prefixes[i], dir, sizeof(dir)) ||
                   !pm_join_path(dir, tool_name, candidate, sizeof(candidate))) {
            continue;
        }
        if (pm_file_exists(candidate)) {
            return pm_copy_text(out, out_len, candidate);
        }
#ifdef _WIN32
        {
            char exe_candidate[PATH_MAX];
            if (snprintf(exe_candidate, sizeof(exe_candidate), "%s.exe", candidate) < (int)sizeof(exe_candidate) &&
                pm_file_exists(exe_candidate)) {
                return pm_copy_text(out, out_len, exe_candidate);
            }
        }
#endif
    }
    return 0;
}

static int pm_find_local_tool(const char* project_dir, const char* tool_name, char* out, size_t out_len)
{
    char lock_path[PATH_MAX];
    char* text = NULL;
    const char* p;
    if (!pm_join_path(project_dir, "ailang.lock.toml", lock_path, sizeof(lock_path)) ||
        !pm_read_text_limited(lock_path, &text)) {
        return 0;
    }
    p = text;
    while ((p = strstr(p, "[[package]]")) != NULL) {
        char package_path[PATH_MAX];
        char package_root[PATH_MAX];
        char local_path[PATH_MAX];
        char root_path[PATH_MAX];
        if (pm_toml_get_string(p, "path", package_path, sizeof(package_path)) &&
            pm_toml_get_string(p, "packageRoot", package_root, sizeof(package_root)) &&
            pm_resolve_project_local_path(project_dir, package_path, local_path, sizeof(local_path)) &&
            pm_join_path(local_path, package_root, root_path, sizeof(root_path)) &&
            pm_find_tool_in_package_root(root_path, tool_name, out, out_len)) {
            free(text);
            return 1;
        }
        p += strlen("[[package]]");
    }
    free(text);
    return 0;
}

int ailang_package_manager_try_run_tool(
    const AilangPackageManagerOptions* options,
    const char* tool_name,
    int arg_count,
    char** args,
    int* exit_code,
    char* error,
    size_t error_len)
{
    char install_root[PATH_MAX];
    char global_tools[PATH_MAX];
    char tool_path[PATH_MAX];
    char project_dir[PATH_MAX];
    char configured_package_path[PATH_MAX];
    char configured_package_dir[PATH_MAX];
    if (exit_code != NULL) {
        *exit_code = 0;
    }
    if (tool_name == NULL || tool_name[0] == '\0' || exit_code == NULL) {
        return pm_set_error(error, error_len, "invalid tool request");
    }
    if (pm_find_project_dir(options, project_dir, sizeof(project_dir)) &&
        pm_project_config_get_package_path(project_dir, tool_name, configured_package_path, sizeof(configured_package_path)) &&
        pm_resolve_project_local_path(project_dir, configured_package_path, configured_package_dir, sizeof(configured_package_dir)) &&
        pm_find_tool_in_package_root(configured_package_dir, tool_name, tool_path, sizeof(tool_path))) {
        return pm_run_tool_command(tool_name, tool_path, arg_count, args, exit_code, error, error_len) ? 1 :
            pm_set_error_if_empty(error, error_len, "failed to run configured local tool: %s", tool_name);
    }
    if (pm_resolve_install_root(options, install_root, sizeof(install_root)) &&
        pm_join_path(install_root, "tools", global_tools, sizeof(global_tools)) &&
        pm_join_path(global_tools, tool_name, tool_path, sizeof(tool_path)) &&
        pm_file_exists(tool_path)) {
        return pm_run_tool_command(tool_name, tool_path, arg_count, args, exit_code, error, error_len) ? 1 :
            pm_set_error_if_empty(error, error_len, "failed to run global tool: %s", tool_name);
    }
#ifdef _WIN32
    if (pm_resolve_install_root(options, install_root, sizeof(install_root)) &&
        pm_join_path(install_root, "tools", global_tools, sizeof(global_tools)) &&
        snprintf(tool_path, sizeof(tool_path), "%s%c%s.exe", global_tools, AILANG_PM_PATH_SEP, tool_name) < (int)sizeof(tool_path) &&
        pm_file_exists(tool_path)) {
        return pm_run_tool_command(tool_name, tool_path, arg_count, args, exit_code, error, error_len) ? 1 :
            pm_set_error_if_empty(error, error_len, "failed to run global tool: %s", tool_name);
    }
#endif
    if (pm_find_project_dir(options, project_dir, sizeof(project_dir)) &&
        pm_find_local_tool(project_dir, tool_name, tool_path, sizeof(tool_path))) {
        return pm_run_tool_command(tool_name, tool_path, arg_count, args, exit_code, error, error_len) ? 1 :
            pm_set_error_if_empty(error, error_len, "failed to run local tool: %s", tool_name);
    }
    return 0;
}

static int bridge_package_list(
    void* context,
    const AilangNativeValue* args,
    size_t arg_count,
    AilangNativeValue* result,
    char* error,
    size_t error_len)
{
    static char output[AILANG_NATIVE_BRIDGE_MAX_STRING];
    AilangPackageManagerOptions options;
    (void)context;
    memset(&options, 0, sizeof(options));
    options.project_dir = ".";
    if (arg_count > 0U && args != NULL && args[0].type == AILANG_NATIVE_STRING) {
        options.project_dir = args[0].as.string_value;
    }
    if (!ailang_package_manager_list(&options, output, sizeof(output), error, error_len)) {
        return 0;
    }
    ailang_native_value_string(result, output);
    return 1;
}

static int bridge_package_restore(
    void* context,
    const AilangNativeValue* args,
    size_t arg_count,
    AilangNativeValue* result,
    char* error,
    size_t error_len)
{
    static char output[AILANG_NATIVE_BRIDGE_MAX_STRING];
    AilangPackageManagerOptions options;
    (void)context;
    memset(&options, 0, sizeof(options));
    options.project_dir = ".";
    if (arg_count > 0U && args != NULL && args[0].type == AILANG_NATIVE_STRING) {
        options.project_dir = args[0].as.string_value;
    }
    if (!ailang_package_manager_restore(&options, output, sizeof(output), error, error_len)) {
        return 0;
    }
    ailang_native_value_string(result, output);
    return 1;
}

static int bridge_package_add(
    void* context,
    const AilangNativeValue* args,
    size_t arg_count,
    AilangNativeValue* result,
    char* error,
    size_t error_len)
{
    static char output[AILANG_NATIVE_BRIDGE_MAX_STRING];
    AilangPackageManagerOptions options;
    const char* project_dir = ".";
    const char* package_spec;
    (void)context;
    if (arg_count < 1U || args == NULL || args[0].type != AILANG_NATIVE_STRING) {
        return pm_set_error(error, error_len, "missing package spec");
    }
    package_spec = args[0].as.string_value;
    if (arg_count > 1U && args[1].type == AILANG_NATIVE_STRING) {
        project_dir = args[1].as.string_value;
    }
    memset(&options, 0, sizeof(options));
    options.project_dir = project_dir;
    if (!ailang_package_manager_add(&options, package_spec, output, sizeof(output), error, error_len)) {
        return 0;
    }
    ailang_native_value_string(result, output);
    return 1;
}

static int bridge_package_remove(
    void* context,
    const AilangNativeValue* args,
    size_t arg_count,
    AilangNativeValue* result,
    char* error,
    size_t error_len)
{
    static char output[AILANG_NATIVE_BRIDGE_MAX_STRING];
    AilangPackageManagerOptions options;
    const char* project_dir = ".";
    const char* package_name;
    (void)context;
    if (arg_count < 1U || args == NULL || args[0].type != AILANG_NATIVE_STRING) {
        return pm_set_error(error, error_len, "missing package name");
    }
    package_name = args[0].as.string_value;
    if (arg_count > 1U && args[1].type == AILANG_NATIVE_STRING) {
        project_dir = args[1].as.string_value;
    }
    memset(&options, 0, sizeof(options));
    options.project_dir = project_dir;
    if (!ailang_package_manager_remove(&options, package_name, output, sizeof(output), error, error_len)) {
        return 0;
    }
    ailang_native_value_string(result, output);
    return 1;
}

int ailang_package_manager_register(AilangNativeBridge* bridge, char* error, size_t error_len)
{
    return ailang_native_bridge_register(bridge, "package.list", bridge_package_list, NULL, error, error_len) &&
           ailang_native_bridge_register(bridge, "package.restore", bridge_package_restore, NULL, error, error_len) &&
           ailang_native_bridge_register(bridge, "package.add", bridge_package_add, NULL, error, error_len) &&
           ailang_native_bridge_register(bridge, "package.remove", bridge_package_remove, NULL, error, error_len);
}

static int pm_resolve_cli_project_dir(int argc, char** argv, int start, char* out, size_t out_len)
{
    int i;
    const char* project_dir = ".";
    for (i = start; i < argc; i += 1) {
        if (argv[i] != NULL && argv[i][0] != '-') {
            project_dir = argv[i];
            break;
        }
    }
    return snprintf(out, out_len, "%s", project_dir) >= 0 && strlen(project_dir) < out_len;
}

int ailang_package_manager_cli(int argc, char** argv)
{
    AilangPackageManagerOptions options;
    char project_dir[PATH_MAX];
    char output[AILANG_NATIVE_BRIDGE_MAX_STRING];
    char error[512];
    if (argc < 3 || argv == NULL || argv[2] == NULL) {
        fprintf(stderr, "Usage: ailang package <list|restore|add|remove> [args]\n");
        return 2;
    }
    memset(&options, 0, sizeof(options));
    error[0] = '\0';
    if (strcmp(argv[2], "list") == 0 || strcmp(argv[2], "restore") == 0) {
        if (!pm_resolve_cli_project_dir(argc, argv, 3, project_dir, sizeof(project_dir))) {
            fprintf(stderr, "Err#err1(code=PKG001 message=\"Package command path overflow.\" nodeId=package)\n");
            return 2;
        }
        options.project_dir = project_dir;
        if (strcmp(argv[2], "list") == 0) {
            if (!ailang_package_manager_list(&options, output, sizeof(output), error, sizeof(error))) {
                fprintf(stderr, "Err#err1(code=PKG001 message=\"%s\" nodeId=package)\n", error);
                return 2;
            }
        } else if (!ailang_package_manager_restore(&options, output, sizeof(output), error, sizeof(error))) {
            fprintf(stderr, "Err#err1(code=PKG001 message=\"%s\" nodeId=package)\n", error);
            return 2;
        }
        fputs(output, stdout);
        return 0;
    }
    if (strcmp(argv[2], "add") == 0 || strcmp(argv[2], "remove") == 0) {
        if (argc < 4 || argv[3] == NULL) {
            fprintf(stderr, "Err#err1(code=PKG001 message=\"Missing package name.\" nodeId=package)\n");
            return 2;
        }
        if (!pm_resolve_cli_project_dir(argc, argv, 4, project_dir, sizeof(project_dir))) {
            fprintf(stderr, "Err#err1(code=PKG001 message=\"Package command path overflow.\" nodeId=package)\n");
            return 2;
        }
        options.project_dir = project_dir;
        if (strcmp(argv[2], "add") == 0) {
            if (!ailang_package_manager_add(&options, argv[3], output, sizeof(output), error, sizeof(error))) {
                fprintf(stderr, "Err#err1(code=PKG001 message=\"%s\" nodeId=package)\n", error);
                return 2;
            }
        } else if (!ailang_package_manager_remove(&options, argv[3], output, sizeof(output), error, sizeof(error))) {
            fprintf(stderr, "Err#err1(code=PKG001 message=\"%s\" nodeId=package)\n", error);
            return 2;
        }
        fputs(output, stdout);
        return 0;
    }
    fprintf(stderr, "Err#err1(code=PKG000 message=\"Unknown package command.\" nodeId=package)\n");
    return 2;
}
