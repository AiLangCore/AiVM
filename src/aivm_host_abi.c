#include "aivm_host_abi.h"

#include <stddef.h>

uint32_t aivm_host_abi_version(void)
{
    return AIVM_HOST_ABI_VERSION;
}

AivmHostAbiCompatibility aivm_host_abi_check_compatible(
    uint32_t core_abi_version,
    const AivmHostAbiDescriptor* host)
{
    if (host == NULL ||
        host->abi_version == 0U ||
        host->min_core_abi_version == 0U ||
        host->target_id == NULL ||
        host->host_name == NULL) {
        return AIVM_HOST_ABI_COMPAT_INVALID;
    }
    if (core_abi_version < host->min_core_abi_version) {
        return AIVM_HOST_ABI_COMPAT_CORE_TOO_OLD;
    }
    if (core_abi_version > host->abi_version) {
        return AIVM_HOST_ABI_COMPAT_CORE_TOO_NEW;
    }
    return AIVM_HOST_ABI_COMPAT_OK;
}

const char* aivm_host_abi_compatibility_code(AivmHostAbiCompatibility status)
{
    switch (status) {
        case AIVM_HOST_ABI_COMPAT_OK:
            return "AIVMHOST000";
        case AIVM_HOST_ABI_COMPAT_INVALID:
            return "AIVMHOST001";
        case AIVM_HOST_ABI_COMPAT_CORE_TOO_OLD:
            return "AIVMHOST002";
        case AIVM_HOST_ABI_COMPAT_CORE_TOO_NEW:
            return "AIVMHOST003";
        default:
            return "AIVMHOST999";
    }
}
