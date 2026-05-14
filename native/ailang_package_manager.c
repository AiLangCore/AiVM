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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define AILANG_PM_PATH_SEP '/'
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

#define AILANG_PM_TEXT_LIMIT 262144U
#define AILANG_PM_LOCK_LIMIT 131072U

typedef struct AilangPackageRecord {
    char name[128];
    char version[64];
    char repo[512];
    char package_root[256];
    char ref[128];
    char commit[128];
    char types[256];
} AilangPackageRecord;

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
    struct stat st;
    return path != NULL && stat(path, &st) == 0 &&
#ifdef _WIN32
        ((st.st_mode & _S_IFREG) != 0);
#else
        S_ISREG(st.st_mode);
#endif
}

static int pm_directory_exists(const char* path)
{
    struct stat st;
    return path != NULL && stat(path, &st) == 0 &&
#ifdef _WIN32
        ((st.st_mode & _S_IFDIR) != 0);
#else
        S_ISDIR(st.st_mode);
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

static int pm_resolve_install_root(const AilangPackageManagerOptions* options, char* out, size_t out_len)
{
    const char* env;
    const char* home;
    int n;
    if (options != NULL && options->install_root != NULL && options->install_root[0] != '\0') {
        return snprintf(out, out_len, "%s", options->install_root) >= 0 &&
               strlen(options->install_root) < out_len;
    }
    env = getenv("AILANG_INSTALL_ROOT");
    if (env != NULL && env[0] != '\0') {
        return snprintf(out, out_len, "%s", env) >= 0 && strlen(env) < out_len;
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
        return 1;
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

static int pm_parse_include(const char* cursor, char* name, size_t name_len, char* version, size_t version_len)
{
    const char* name_pos;
    const char* version_pos;
    const char* start;
    const char* end;
    size_t n;
    if (cursor == NULL || name == NULL || version == NULL) {
        return 0;
    }
    name_pos = strstr(cursor, "name=\"");
    if (name_pos == NULL) {
        return 0;
    }
    start = name_pos + strlen("name=\"");
    end = strchr(start, '"');
    if (end == NULL) {
        return 0;
    }
    n = (size_t)(end - start);
    if (n + 1U > name_len) {
        return 0;
    }
    memcpy(name, start, n);
    name[n] = '\0';
    version[0] = '\0';
    version_pos = strstr(cursor, "version=\"");
    if (version_pos != NULL) {
        start = version_pos + strlen("version=\"");
        end = strchr(start, '"');
        if (end != NULL) {
            n = (size_t)(end - start);
            if (n + 1U > version_len) {
                return 0;
            }
            memcpy(version, start, n);
            version[n] = '\0';
        }
    }
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
            if (pm_toml_get_string(p, "name", name, sizeof(name))) {
                (void)pm_toml_get_string(p, "version", version, sizeof(version));
                if (!pm_appendf(output, output_len, &used, "%s%s%s\n", name, version[0] == '\0' ? "" : " ", version)) {
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
    const char* p;
    int restored = 0;
    if (output != NULL && output_len > 0U) {
        output[0] = '\0';
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
    p = manifest;
    while ((p = strstr(p, "Include")) != NULL) {
        char include_name[128];
        char include_version[64];
        char package_dir[PATH_MAX];
        char conflict[256];
        AilangPackageRecord record;
        if (!pm_parse_include(p, include_name, sizeof(include_name), include_version, sizeof(include_version))) {
            free(manifest);
            return pm_set_error(error, error_len, "invalid Include package declaration");
        }
        if (!pm_load_record(registry, include_name, include_version, &record, error, error_len)) {
            free(manifest);
            return 0;
        }
        if (pm_tool_conflict(options, &record, conflict, sizeof(conflict))) {
            free(manifest);
            return pm_set_error(error, error_len, "package tool conflicts with %s", conflict);
        }
        if (!pm_join_path(package_cache, record.name, package_dir, sizeof(package_dir)) ||
            !pm_clone_checkout(&record, package_dir)) {
            free(manifest);
            return pm_set_error(error, error_len, "package clone/checkout failed: %s", record.name);
        }
        if (!pm_append(&lock_text[0], sizeof(lock_text), &lock_used, "[[package]]\n") ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "name = \"%s\"\n", record.name) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "version = \"%s\"\n", record.version) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "repo = \"%s\"\n", record.repo) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "packageRoot = \"%s\"\n", record.package_root) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "commit = \"%s\"\n", record.commit) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "path = \".ailang/packages/%s\"\n", record.name) ||
            !pm_appendf(&lock_text[0], sizeof(lock_text), &lock_used, "types = [%s]\n\n", record.types)) {
            free(manifest);
            return pm_set_error(error, error_len, "lockfile output overflow");
        }
        restored += 1;
        p += strlen("Include");
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

int ailang_package_manager_register(AilangNativeBridge* bridge, char* error, size_t error_len)
{
    return ailang_native_bridge_register(bridge, "package.list", bridge_package_list, NULL, error, error_len) &&
           ailang_native_bridge_register(bridge, "package.restore", bridge_package_restore, NULL, error, error_len);
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
    AilangNativeBridge bridge;
    AilangNativeValue arg;
    AilangNativeValue result;
    char project_dir[PATH_MAX];
    char error[512];
    const char* fn_name;
    if (argc < 3 || argv == NULL || argv[2] == NULL) {
        fprintf(stderr, "Usage: ailang package <list|restore> [project-dir]\n");
        return 2;
    }
    if (strcmp(argv[2], "list") == 0) {
        fn_name = "package.list";
    } else if (strcmp(argv[2], "restore") == 0) {
        fn_name = "package.restore";
    } else {
        fprintf(stderr, "Err#err1(code=PKG000 message=\"Unknown package command.\" nodeId=package)\n");
        return 2;
    }
    if (!pm_resolve_cli_project_dir(argc, argv, 3, project_dir, sizeof(project_dir))) {
        fprintf(stderr, "Err#err1(code=PKG001 message=\"Package command path overflow.\" nodeId=package)\n");
        return 2;
    }
    ailang_native_bridge_init(&bridge);
    if (!ailang_package_manager_register(&bridge, error, sizeof(error))) {
        fprintf(stderr, "Err#err1(code=PKG001 message=\"%s\" nodeId=package)\n", error);
        return 2;
    }
    ailang_native_value_string(&arg, project_dir);
    if (!ailang_native_bridge_call(&bridge, fn_name, &arg, 1U, &result, error, sizeof(error))) {
        fprintf(stderr, "Err#err1(code=PKG001 message=\"%s\" nodeId=package)\n", error);
        return 2;
    }
    if (result.type == AILANG_NATIVE_STRING && result.as.string_value != NULL) {
        fputs(result.as.string_value, stdout);
    }
    return 0;
}
