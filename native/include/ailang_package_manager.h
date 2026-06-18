#ifndef AILANG_PACKAGE_MANAGER_H
#define AILANG_PACKAGE_MANAGER_H

#include <stddef.h>

#include "ailang_native_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AilangPackageManagerOptions {
    const char* project_dir;
    const char* registry_dir;
    const char* install_root;
} AilangPackageManagerOptions;

int ailang_package_manager_register(AilangNativeBridge* bridge, char* error, size_t error_len);

int ailang_package_manager_list(
    const AilangPackageManagerOptions* options,
    char* output,
    size_t output_len,
    char* error,
    size_t error_len);

int ailang_package_manager_restore(
    const AilangPackageManagerOptions* options,
    char* output,
    size_t output_len,
    char* error,
    size_t error_len);

int ailang_package_manager_add(
    const AilangPackageManagerOptions* options,
    const char* package_spec,
    char* output,
    size_t output_len,
    char* error,
    size_t error_len);

int ailang_package_manager_remove(
    const AilangPackageManagerOptions* options,
    const char* package_name,
    char* output,
    size_t output_len,
    char* error,
    size_t error_len);

int ailang_package_manager_try_run_tool(
    const AilangPackageManagerOptions* options,
    const char* tool_name,
    int arg_count,
    char** args,
    int* exit_code,
    char* error,
    size_t error_len);

int ailang_package_manager_try_run_interactive_tool(
    const AilangPackageManagerOptions* options,
    const char* tool_name,
    int arg_count,
    char** args,
    int* exit_code,
    char* error,
    size_t error_len);

int ailang_package_manager_find_package_root(
    const AilangPackageManagerOptions* options,
    const char* package_name,
    char* output,
    size_t output_len,
    char* error,
    size_t error_len);

int ailang_package_manager_cli(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif
