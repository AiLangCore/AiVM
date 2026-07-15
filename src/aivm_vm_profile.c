#include "aivm_vm.h"
#include "sys/aivm_syscall_contracts.h"
#include <string.h>

const char* aivm_runtime_profile_name(AivmRuntimeProfile profile)
{
    switch (profile) {
        case AIVM_RUNTIME_PROFILE_PRODUCTION:
            return "production";
        case AIVM_RUNTIME_PROFILE_DEBUG:
            return "debug";
        case AIVM_RUNTIME_PROFILE_TOOLING:
            return "tooling";
        default:
            return "unknown";
    }
}

int aivm_runtime_profile_from_name(const char* name, AivmRuntimeProfile* out_profile)
{
    if (name == NULL || out_profile == NULL) {
        return 0;
    }
    if (strcmp(name, "production") == 0) {
        *out_profile = AIVM_RUNTIME_PROFILE_PRODUCTION;
        return 1;
    }
    if (strcmp(name, "debug") == 0) {
        *out_profile = AIVM_RUNTIME_PROFILE_DEBUG;
        return 1;
    }
    if (strcmp(name, "tooling") == 0) {
        *out_profile = AIVM_RUNTIME_PROFILE_TOOLING;
        return 1;
    }
    return 0;
}

AivmRuntimeProfile aivm_runtime_default_profile(void)
{
#if defined(AIVM_DEBUG_RUNTIME)
    return AIVM_RUNTIME_PROFILE_DEBUG;
#else
    return AIVM_RUNTIME_PROFILE_PRODUCTION;
#endif
}

AivmRuntimeProfileLimits aivm_runtime_profile_limits(AivmRuntimeProfile profile)
{
    AivmRuntimeProfileLimits limits;
    limits.stack_capacity = AIVM_VM_STACK_CAPACITY;
    limits.call_frame_capacity = AIVM_VM_CALLFRAME_CAPACITY;
    limits.locals_capacity = AIVM_VM_LOCALS_CAPACITY;
    limits.string_arena_capacity = AIVM_VM_STRING_ARENA_CAPACITY;
    limits.bytes_arena_capacity = AIVM_VM_BYTES_ARENA_CAPACITY;
    limits.node_capacity = AIVM_VM_NODE_CAPACITY;
    limits.node_attr_capacity = AIVM_VM_NODE_ATTR_CAPACITY;
    limits.node_child_capacity = AIVM_VM_NODE_CHILD_CAPACITY;
    if (profile == AIVM_RUNTIME_PROFILE_TOOLING) {
        limits.bytes_arena_capacity = AIVM_VM_TOOLING_BYTES_ARENA_CAPACITY;
        limits.node_capacity = AIVM_VM_TOOLING_NODE_CAPACITY;
        limits.node_attr_capacity = AIVM_VM_TOOLING_NODE_ATTR_CAPACITY;
        limits.node_child_capacity = AIVM_VM_TOOLING_NODE_CHILD_CAPACITY;
    }
    limits.task_capacity = AIVM_VM_TASK_CAPACITY;
    limits.par_value_capacity = AIVM_VM_PAR_VALUE_CAPACITY;
    limits.scratch_pair_capacity = AIVM_VM_SCRATCH_PAIR_CAPACITY;
    limits.file_read_bytes = AIVM_VM_FILE_READ_BYTES;
    limits.file_write_bytes = AIVM_VM_FILE_WRITE_BYTES;
    limits.network_read_bytes = AIVM_VM_NETWORK_READ_BYTES;
    limits.network_write_bytes = AIVM_VM_NETWORK_WRITE_BYTES;
    limits.process_count = AIVM_VM_PROCESS_COUNT;
    limits.worker_count = AIVM_VM_WORKER_COUNT;
    limits.ui_window_count = AIVM_VM_UI_WINDOW_COUNT;
    limits.debug_artifact_bytes = AIVM_VM_DEBUG_ARTIFACT_BYTES;
    limits.blob_capacity = AIVM_VM_BLOB_CAPACITY;
    limits.blob_bytes = AIVM_VM_BLOB_BYTES;
    limits.syscall_elapsed_ms = AIVM_VM_SYSCALL_ELAPSED_MS;
    (void)profile;
    return limits;
}

void aivm_set_runtime_profile(AivmVm* vm, AivmRuntimeProfile profile)
{
    AivmRuntimeProfileLimits limits;
    if (vm == NULL) {
        return;
    }
    vm->runtime_profile = profile;
    limits = aivm_runtime_profile_limits(profile);
    vm->bytes_arena_capacity = limits.bytes_arena_capacity;
    vm->node_capacity = limits.node_capacity;
    vm->node_attr_capacity = limits.node_attr_capacity;
    vm->node_child_capacity = limits.node_child_capacity;
    vm->syscall_elapsed_limit_ms = limits.syscall_elapsed_ms;
    if (profile == AIVM_RUNTIME_PROFILE_PRODUCTION) {
        aivm_syscall_policy_allow_production_default(&vm->syscall_policy);
    } else {
        aivm_syscall_policy_allow_all(&vm->syscall_policy);
    }
}

void aivm_set_syscall_policy(AivmVm* vm, const AivmSyscallCapabilityPolicy* policy)
{
    if (vm == NULL || policy == NULL) {
        return;
    }
    vm->syscall_policy = *policy;
}
