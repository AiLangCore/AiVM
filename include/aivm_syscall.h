#ifndef AIVM_SYSCALL_H
#define AIVM_SYSCALL_H

#include <stddef.h>

#include "aivm_syscall_contracts.h"
#include "aivm_types.h"

typedef enum {
    AIVM_SYSCALL_OK = 0,
    AIVM_SYSCALL_ERR_INVALID = -1,
    AIVM_SYSCALL_ERR_NULL_RESULT = -2,
    AIVM_SYSCALL_ERR_NOT_FOUND = -3,
    AIVM_SYSCALL_ERR_CONTRACT = -4,
    AIVM_SYSCALL_ERR_RETURN_TYPE = -5
} AivmSyscallStatus;

typedef int (*AivmSyscallHandler)(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result
);

typedef struct {
    const char* target;
    AivmSyscallHandler handler;
} AivmSyscallBinding;

AivmSyscallStatus aivm_syscall_invoke(
    AivmSyscallHandler handler,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result
);

AivmSyscallStatus aivm_syscall_dispatch(
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result
);

AivmSyscallStatus aivm_syscall_dispatch_checked(
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result
);

AivmSyscallStatus aivm_syscall_dispatch_checked_with_contract(
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result,
    AivmContractStatus* out_contract_status
);

const char* aivm_syscall_status_code(AivmSyscallStatus status);
const char* aivm_syscall_status_message(AivmSyscallStatus status);

#endif
