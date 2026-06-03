#if !defined(_WIN32)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "ailang_package_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

static int write_file(const char* path, const char* text)
{
    FILE* f = fopen(path, "wb");
    size_t len;
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

static int read_file(const char* path, char* out, size_t out_len)
{
    FILE* f;
    long length;
    size_t read_count;
    if (path == NULL || out == NULL || out_len == 0U) {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    length = ftell(f);
    if (length < 0 || (size_t)length >= out_len) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    read_count = fread(out, 1U, (size_t)length, f);
    fclose(f);
    if (read_count != (size_t)length) {
        return 0;
    }
    out[read_count] = '\0';
    return 1;
}

static int mkdir_ok(const char* path)
{
    return MKDIR(path) == 0 || errno == EEXIST;
}

static int run_ok(const char* command)
{
    return command != NULL && system(command) == 0;
}

int main(void)
{
    AilangPackageManagerOptions options;
    char output[4096];
    char error[512];
    char commit[128];
    char registry_record[2048];
    int tool_exit = 0;

#ifdef _WIN32
    (void)run_ok("rmdir /s /q .tmp\\pkg-manager-test >nul 2>nul");
#else
    (void)run_ok("rm -rf .tmp/pkg-manager-test");
#endif
    if (!mkdir_ok(".tmp") ||
        !mkdir_ok(".tmp/pkg-manager-test") ||
        !mkdir_ok(".tmp/pkg-manager-test/registry") ||
        !mkdir_ok(".tmp/pkg-manager-test/registry/packages") ||
        !mkdir_ok(".tmp/pkg-manager-test/project") ||
        !mkdir_ok(".tmp/pkg-manager-test/package-src") ||
        !mkdir_ok(".tmp/pkg-manager-test/package-src/base") ||
        !mkdir_ok(".tmp/pkg-manager-test/package-src/base/src") ||
        !mkdir_ok(".tmp/pkg-manager-test/package-src/pkg") ||
        !mkdir_ok(".tmp/pkg-manager-test/package-src/pkg/tools") ||
        !mkdir_ok(".tmp/pkg-manager-test/package-src/pkg/src")) {
        return 1;
    }
    if (!write_file(
            ".tmp/pkg-manager-test/package-src/base/src/base.aos",
            "Program#base1 {}\n") ||
        !write_file(
            ".tmp/pkg-manager-test/package-src/base/package.toml",
            "schema = \"ailang.package-source.v1\"\n"
            "name = \"base\"\n"
            "version = \"0.1.0\"\n"
            "types = [\"library\"]\n"
            "\n"
            "[libraries.base]\n"
            "namespace = \"base.lib\"\n"
            "entry = \"src/base.aos\"\n"
            "exports = []\n") ||
        !write_file(
            ".tmp/pkg-manager-test/package-src/pkg/src/lib.aos",
            "Program#pkg1 {}\n") ||
        !write_file(
            ".tmp/pkg-manager-test/package-src/pkg/package.toml",
            "schema = \"ailang.package-source.v1\"\n"
            "name = \"demo\"\n"
            "version = \"0.1.0\"\n"
            "types = [\"library\", \"tool\"]\n"
            "\n"
            "[dependencies]\n"
            "base = \"0.1.0\"\n"
            "\n"
            "[libraries.demo]\n"
            "namespace = \"demo.tool\"\n"
            "entry = \"src/lib.aos\"\n"
            "exports = []\n") ||
        !write_file(
            ".tmp/pkg-manager-test/package-src/pkg/tools/demo",
            "#!/bin/sh\n"
            "echo demo-tool \"$@\"\n") ||
        !write_file(
            ".tmp/pkg-manager-test/package-src/pkg/tools/slow",
            "#!/bin/sh\n"
            "sleep 5\n"
            "echo slow-tool\n") ||
        !write_file(
            ".tmp/pkg-manager-test/project/project.aiproj",
            "Program#p1 {\n"
            "  Project#proj1(name=\"demo-app\" entryFile=\"src/app.aos\" entryExport=\"start\")\n"
            "}\n")) {
        return 2;
    }
#ifndef _WIN32
    if (!run_ok("chmod +x .tmp/pkg-manager-test/package-src/pkg/tools/demo .tmp/pkg-manager-test/package-src/pkg/tools/slow")) {
        return 3;
    }
#endif
    if (!run_ok("git -C .tmp/pkg-manager-test/package-src init --quiet") ||
        !run_ok("git -C .tmp/pkg-manager-test/package-src add .") ||
        !run_ok("git -C .tmp/pkg-manager-test/package-src -c user.name=AiLangTest -c user.email=ailang-test@example.invalid commit --quiet -m init") ||
        !run_ok("git -C .tmp/pkg-manager-test/package-src rev-parse HEAD > .tmp/pkg-manager-test/package-commit.txt") ||
        !read_file(".tmp/pkg-manager-test/package-commit.txt", commit, sizeof(commit))) {
        return 4;
    }
    commit[strcspn(commit, "\r\n")] = '\0';
    if (snprintf(
            registry_record,
            sizeof(registry_record),
            "schema = \"ailang.package.v1\"\n"
            "name = \"demo\"\n"
            "repo = \".tmp/pkg-manager-test/package-src\"\n"
            "packageRoot = \"pkg\"\n"
            "types = [\"library\", \"tool\"]\n"
            "defaultVersion = \"0.1.0\"\n"
            "\n"
            "[versions.\"0.1.0\"]\n"
            "ref = \"v0.1.0\"\n"
            "commit = \"%s\"\n",
            commit) >= (int)sizeof(registry_record) ||
        !write_file(".tmp/pkg-manager-test/registry/packages/demo.toml", registry_record)) {
        return 5;
    }
    if (snprintf(
            registry_record,
            sizeof(registry_record),
            "schema = \"ailang.package.v1\"\n"
            "name = \"base\"\n"
            "repo = \".tmp/pkg-manager-test/package-src\"\n"
            "packageRoot = \"base\"\n"
            "types = [\"library\"]\n"
            "defaultVersion = \"0.1.0\"\n"
            "\n"
            "[versions.\"0.1.0\"]\n"
            "ref = \"v0.1.0\"\n"
            "commit = \"%s\"\n",
            commit) >= (int)sizeof(registry_record) ||
        !write_file(".tmp/pkg-manager-test/registry/packages/base.toml", registry_record)) {
        return 21;
    }
    if (snprintf(
            registry_record,
            sizeof(registry_record),
            "schema = \"ailang.package.v1\"\n"
            "name = \"build\"\n"
            "repo = \".tmp/pkg-manager-test/package-src\"\n"
            "packageRoot = \"pkg\"\n"
            "types = [\"tool\"]\n"
            "defaultVersion = \"0.1.0\"\n"
            "\n"
            "[versions.\"0.1.0\"]\n"
            "ref = \"v0.1.0\"\n"
            "commit = \"%s\"\n",
            commit) >= (int)sizeof(registry_record) ||
        !write_file(".tmp/pkg-manager-test/registry/packages/build.toml", registry_record)) {
        return 6;
    }

    memset(&options, 0, sizeof(options));
    options.project_dir = ".tmp/pkg-manager-test/project";
    options.registry_dir = ".tmp/pkg-manager-test/registry";

    if (!ailang_package_manager_list(&options, output, sizeof(output), error, sizeof(error))) {
        return 7;
    }
    if (strstr(output, "demo\n") == NULL) {
        return 8;
    }
    if (ailang_package_manager_add(&options, "build", output, sizeof(output), error, sizeof(error)) ||
        strstr(error, "compiled command 'build'") == NULL) {
        return 9;
    }
    error[0] = '\0';
    if (ailang_package_manager_add(&options, "ailang", output, sizeof(output), error, sizeof(error)) ||
        strstr(error, "provided by the selected SDK") == NULL) {
        return 20;
    }
    if (!read_file(".tmp/pkg-manager-test/project/project.aiproj", output, sizeof(output)) ||
        strstr(output, "Include#dep_build") != NULL) {
        return 10;
    }
    if (!ailang_package_manager_add(&options, "demo", output, sizeof(output), error, sizeof(error)) ||
        strstr(output, "added demo 0.1.0") == NULL) {
        return 11;
    }
    if (!read_file(".tmp/pkg-manager-test/project/project.aiproj", output, sizeof(output)) ||
        strstr(output, "Project#proj1") == NULL ||
        strstr(output, "Include#dep_demo(name=\"demo\" version=\"0.1.0\")") == NULL) {
        return 12;
    }
    if (!ailang_package_manager_list(&options, output, sizeof(output), error, sizeof(error)) ||
        strstr(output, "demo 0.1.0 namespaces=demo.tool") == NULL ||
        strstr(output, "base 0.1.0 namespaces=base.lib") == NULL) {
        return 13;
    }
    if (!read_file(".tmp/pkg-manager-test/project/ailang.lock.toml", output, sizeof(output)) ||
        strstr(output, "namespaces = [\"demo.tool\"]") == NULL ||
        strstr(output, "namespaces = [\"base.lib\"]") == NULL) {
        return 14;
    }
#ifndef _WIN32
    if (!ailang_package_manager_try_run_tool(
            &options,
            "demo",
            0,
            NULL,
            &tool_exit,
            error,
            sizeof(error)) ||
        tool_exit != 0) {
        return 15;
    }
    error[0] = '\0';
#ifdef _WIN32
    _putenv("AILANG_PACKAGE_TOOL_TIMEOUT_SECONDS=1");
#else
    if (setenv("AILANG_PACKAGE_TOOL_TIMEOUT_SECONDS", "1", 1) != 0) {
        return 16;
    }
#endif
    if (ailang_package_manager_try_run_tool(
            &options,
            "slow",
            0,
            NULL,
            &tool_exit,
            error,
            sizeof(error)) ||
        strstr(error, "package tool timed out after 1 seconds: slow") == NULL) {
        return 17;
    }
#endif
    if (!ailang_package_manager_remove(&options, "demo", output, sizeof(output), error, sizeof(error)) ||
        strstr(output, "removed demo") == NULL) {
        return 18;
    }
    if (!read_file(".tmp/pkg-manager-test/project/project.aiproj", output, sizeof(output)) ||
        strstr(output, "Include#dep_demo") != NULL) {
        return 19;
    }
    return 0;
}
