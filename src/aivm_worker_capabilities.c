#include "aivm_worker_capabilities.h"

#include "sys/aivm_syscall.h"

static uint64_t include_capability(
    uint32_t worker_mask,
    uint32_t worker_bit,
    AivmSyscallCapabilityGroup syscall_group)
{
    return (worker_mask & worker_bit) != 0U
        ? aivm_syscall_capability_mask(syscall_group) : 0U;
}

uint64_t aivm_worker_capability_syscall_mask(uint32_t worker_mask)
{
    uint64_t result = 0U;
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_FILESYSTEM, AIVM_SYSCALL_CAPABILITY_FILESYSTEM);
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_PROCESS, AIVM_SYSCALL_CAPABILITY_PROCESS);
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_NETWORK, AIVM_SYSCALL_CAPABILITY_NETWORK);
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_ENVIRONMENT, AIVM_SYSCALL_CAPABILITY_HOST);
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_CLOCK, AIVM_SYSCALL_CAPABILITY_TIME);
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_RANDOM, AIVM_SYSCALL_CAPABILITY_CRYPTO);
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_UI, AIVM_SYSCALL_CAPABILITY_UI);
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_DEBUG, AIVM_SYSCALL_CAPABILITY_DEBUG);
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_REMOTE, AIVM_SYSCALL_CAPABILITY_REMOTE);
    result |= include_capability(worker_mask,
        AIVM_WORKER_CAPABILITY_STANDARD_STREAMS, AIVM_SYSCALL_CAPABILITY_CONSOLE);
    return result;
}
