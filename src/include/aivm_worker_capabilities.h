#ifndef AIVM_WORKER_CAPABILITIES_H
#define AIVM_WORKER_CAPABILITIES_H

#include <stdint.h>

enum {
    AIVM_WORKER_CAPABILITY_FILESYSTEM = 1U,
    AIVM_WORKER_CAPABILITY_PROCESS = 2U,
    AIVM_WORKER_CAPABILITY_NETWORK = 4U,
    AIVM_WORKER_CAPABILITY_ENVIRONMENT = 8U,
    AIVM_WORKER_CAPABILITY_CLOCK = 16U,
    AIVM_WORKER_CAPABILITY_RANDOM = 32U,
    AIVM_WORKER_CAPABILITY_UI = 64U,
    AIVM_WORKER_CAPABILITY_DEBUG = 128U,
    AIVM_WORKER_CAPABILITY_REMOTE = 256U,
    AIVM_WORKER_CAPABILITY_STANDARD_STREAMS = 512U
};

/*
 * Converts the stable WorkerCatalog ABI mask into AiVM's internal syscall
 * capability representation. The two masks are deliberately not conflated.
 */
uint64_t aivm_worker_capability_syscall_mask(uint32_t worker_mask);
int aivm_worker_capabilities_supported(uint32_t worker_mask);

#endif
