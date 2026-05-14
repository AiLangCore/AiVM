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

static int mkdir_ok(const char* path)
{
    return MKDIR(path) == 0 || errno == EEXIST;
}

int main(void)
{
    AilangPackageManagerOptions options;
    char output[4096];
    char error[512];

    if (!mkdir_ok(".tmp") ||
        !mkdir_ok(".tmp/pkg-manager-test") ||
        !mkdir_ok(".tmp/pkg-manager-test/registry") ||
        !mkdir_ok(".tmp/pkg-manager-test/registry/packages") ||
        !mkdir_ok(".tmp/pkg-manager-test/project")) {
        return 1;
    }
    if (!write_file(
            ".tmp/pkg-manager-test/registry/packages/demo.toml",
            "schema = \"ailang.package.v1\"\n"
            "name = \"demo\"\n"
            "repo = \"https://example.invalid/demo.git\"\n"
            "packageRoot = \".\"\n"
            "types = [\"library\"]\n"
            "\n"
            "[versions.\"0.1.0\"]\n"
            "ref = \"v0.1.0\"\n"
            "commit = \"abc123\"\n") ||
        !write_file(
            ".tmp/pkg-manager-test/project/project.aiproj",
            "Program#p1 {\n"
            "  Project#proj1(name=\"demo-app\" entryFile=\"src/app.aos\" entryExport=\"start\") {\n"
            "    Include#dep1(name=\"demo\" version=\"0.1.0\")\n"
            "  }\n"
            "}\n")) {
        return 2;
    }

    memset(&options, 0, sizeof(options));
    options.project_dir = ".tmp/pkg-manager-test/project";
    options.registry_dir = ".tmp/pkg-manager-test/registry";

    if (!ailang_package_manager_list(&options, output, sizeof(output), error, sizeof(error))) {
        return 3;
    }
    if (strstr(output, "demo\n") == NULL) {
        return 4;
    }
    return 0;
}
