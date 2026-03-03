#include "aivm_runtime.h"

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
