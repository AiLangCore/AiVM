#include "aivm_runtime.h"

AivmRuntimeHostEventStatus aivm_runtime_host_enqueue_event(
    const AivmRuntimeHostAdapter* adapter,
    const char* event_name,
    AivmValue payload)
{
    if (adapter == NULL || adapter->enqueue == NULL || event_name == NULL || event_name[0] == '\0') {
        return AIVM_RUNTIME_HOST_EVENT_INVALID;
    }

    if (adapter->enqueue(adapter->context, event_name, payload) != 0) {
        return AIVM_RUNTIME_HOST_EVENT_REJECTED;
    }

    return AIVM_RUNTIME_HOST_EVENT_OK;
}

AivmRuntimeHostEventStatus aivm_runtime_host_drain_events(
    const AivmRuntimeHostAdapter* adapter,
    size_t max_events,
    size_t* out_drained_count)
{
    if (out_drained_count == NULL) {
        return AIVM_RUNTIME_HOST_EVENT_INVALID;
    }
    *out_drained_count = 0U;
    if (adapter == NULL || adapter->drain == NULL || max_events == 0U) {
        return AIVM_RUNTIME_HOST_EVENT_INVALID;
    }

    if (adapter->drain(adapter->context, max_events, out_drained_count) != 0) {
        *out_drained_count = 0U;
        return AIVM_RUNTIME_HOST_EVENT_REJECTED;
    }
    if (*out_drained_count > max_events) {
        *out_drained_count = 0U;
        return AIVM_RUNTIME_HOST_EVENT_INVALID;
    }

    return AIVM_RUNTIME_HOST_EVENT_OK;
}

int aivm_execute_program(const AivmProgram* program, AivmVm* vm_out)
{
    return aivm_execute_program_with_syscalls(program, NULL, 0U, vm_out);
}

int aivm_execute_program_with_syscalls(
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    AivmVm* vm_out)
{
    return aivm_execute_program_with_syscalls_and_argv(
        program,
        bindings,
        binding_count,
        NULL,
        0U,
        vm_out);
}

int aivm_execute_program_with_syscalls_and_argv(
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* const* process_argv,
    size_t process_argv_count,
    AivmVm* vm_out)
{
    if (program == NULL || vm_out == NULL) {
        return 0;
    }

    aivm_init_with_syscalls_and_argv(vm_out, program, bindings, binding_count, process_argv, process_argv_count);
    aivm_run(vm_out);

    if (vm_out->status == AIVM_VM_STATUS_ERROR) {
        return 0;
    }

    return 1;
}
