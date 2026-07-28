#ifndef AIVM_SYSCALL_H
#define AIVM_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

#include "sys/aivm_syscall_contracts.h"
#include "aivm_types.h"

typedef enum {
    AIVM_SYSCALL_OK = 0,
    AIVM_SYSCALL_ERR_INVALID = -1,
    AIVM_SYSCALL_ERR_NULL_RESULT = -2,
    AIVM_SYSCALL_ERR_NOT_FOUND = -3,
    AIVM_SYSCALL_ERR_CONTRACT = -4,
    AIVM_SYSCALL_ERR_RETURN_TYPE = -5,
    AIVM_SYSCALL_ERR_UNBOUND = -6,
    AIVM_SYSCALL_ERR_RESOURCE_LIMIT = -7,
    AIVM_SYSCALL_ERR_CAPABILITY_DENIED = -8
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

typedef struct {
    uint64_t allowed_capability_mask;
} AivmSyscallCapabilityPolicy;

uint64_t aivm_syscall_capability_mask(AivmSyscallCapabilityGroup capability);
void aivm_syscall_policy_allow_none(AivmSyscallCapabilityPolicy* policy);
void aivm_syscall_policy_allow_all(AivmSyscallCapabilityPolicy* policy);
void aivm_syscall_policy_allow_production_default(AivmSyscallCapabilityPolicy* policy);
void aivm_syscall_policy_allow_group(
    AivmSyscallCapabilityPolicy* policy,
    AivmSyscallCapabilityGroup capability
);
void aivm_syscall_policy_deny_group(
    AivmSyscallCapabilityPolicy* policy,
    AivmSyscallCapabilityGroup capability
);
int aivm_syscall_policy_allows(
    const AivmSyscallCapabilityPolicy* policy,
    AivmSyscallCapabilityGroup capability
);

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

AivmSyscallStatus aivm_syscall_dispatch_checked_with_policy(
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const AivmSyscallCapabilityPolicy* policy,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result,
    AivmContractStatus* out_contract_status
);

const char* aivm_syscall_status_code(AivmSyscallStatus status);
const char* aivm_syscall_status_message(AivmSyscallStatus status);

#endif
