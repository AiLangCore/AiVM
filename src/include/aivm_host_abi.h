#ifndef AIVM_HOST_ABI_H
#define AIVM_HOST_ABI_H

#include <stdint.h>

#include "aivm_runtime.h"
#include "sys/aivm_syscall.h"

#if defined(_WIN32) && defined(AIVM_BUILD_SHARED_LIB)
#if defined(AIVM_CORE_SHARED_IMPL)
#define AIVM_API __declspec(dllexport)
#else
#define AIVM_API __declspec(dllimport)
#endif
#else
#ifndef AIVM_API
#define AIVM_API
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define AIVM_HOST_ABI_VERSION 1U
#define AIVM_HOST_ABI_MIN_COMPATIBLE_VERSION 1U
#define AIVM_HOST_ABI_NAME "aivm-host-abi"

typedef enum {
    AIVM_HOST_ABI_COMPAT_OK = 0,
    AIVM_HOST_ABI_COMPAT_INVALID = 1,
    AIVM_HOST_ABI_COMPAT_CORE_TOO_OLD = 2,
    AIVM_HOST_ABI_COMPAT_CORE_TOO_NEW = 3
} AivmHostAbiCompatibility;

typedef struct {
    uint32_t abi_version;
    uint32_t min_core_abi_version;
    const char* target_id;
    const char* host_name;
    const AivmSyscallBinding* syscall_bindings;
    uint32_t syscall_binding_count;
    AivmRuntimeHostAdapter event_adapter;
} AivmHostAbiDescriptor;

AIVM_API uint32_t aivm_host_abi_version(void);
AIVM_API AivmHostAbiCompatibility aivm_host_abi_check_compatible(
    uint32_t core_abi_version,
    const AivmHostAbiDescriptor* host);
AIVM_API const char* aivm_host_abi_compatibility_code(AivmHostAbiCompatibility status);

#ifdef __cplusplus
}
#endif

#endif
