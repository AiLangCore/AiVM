#ifndef AIVM_RUNTIME_H
#define AIVM_RUNTIME_H

#include "aivm_program.h"
#include "sys/aivm_syscall.h"
#include "aivm_vm.h"

int aivm_execute_program(const AivmProgram* program, AivmVm* vm_out);
int aivm_execute_program_with_syscalls(
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    AivmVm* vm_out);
int aivm_execute_program_with_syscalls_and_argv(
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* const* process_argv,
    size_t process_argv_count,
    AivmVm* vm_out);

#endif
