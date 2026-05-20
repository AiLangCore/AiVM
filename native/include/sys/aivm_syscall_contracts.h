#ifndef AIVM_SYSCALL_CONTRACTS_H
#define AIVM_SYSCALL_CONTRACTS_H

#include <stddef.h>
#include <stdint.h>

#include "aivm_types.h"

typedef enum {
    AIVM_CONTRACT_OK = 0,
    AIVM_CONTRACT_ERR_UNKNOWN_TARGET = 1,
    AIVM_CONTRACT_ERR_ARG_COUNT = 2,
    AIVM_CONTRACT_ERR_ARG_TYPE = 3,
    AIVM_CONTRACT_ERR_UNKNOWN_ID = 4
} AivmContractStatus;

typedef enum {
    AIVM_SYSCALL_CAPABILITY_NONE = 0,
    AIVM_SYSCALL_CAPABILITY_CORE = 1,
    AIVM_SYSCALL_CAPABILITY_CONSOLE = 2,
    AIVM_SYSCALL_CAPABILITY_PROCESS = 3,
    AIVM_SYSCALL_CAPABILITY_PLATFORM = 4,
    AIVM_SYSCALL_CAPABILITY_TIME = 5,
    AIVM_SYSCALL_CAPABILITY_FILESYSTEM = 6,
    AIVM_SYSCALL_CAPABILITY_CRYPTO = 7,
    AIVM_SYSCALL_CAPABILITY_NETWORK = 8,
    AIVM_SYSCALL_CAPABILITY_UI = 9,
    AIVM_SYSCALL_CAPABILITY_WORKER = 10,
    AIVM_SYSCALL_CAPABILITY_REMOTE = 11,
    AIVM_SYSCALL_CAPABILITY_HOST = 12,
    AIVM_SYSCALL_CAPABILITY_IMAGE = 13,
    AIVM_SYSCALL_CAPABILITY_DEBUG = 14
} AivmSyscallCapabilityGroup;

typedef struct {
    uint32_t id;
    const char* target;
    size_t arg_count;
    AivmValueType arg_types[8];
    AivmValueType return_type;
    AivmSyscallCapabilityGroup capability;
} AivmSyscallContract;

AivmContractStatus aivm_syscall_contract_validate(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValueType* out_return_type
);

AivmContractStatus aivm_syscall_contract_validate_id(
    uint32_t id,
    const AivmValue* args,
    size_t arg_count,
    AivmValueType* out_return_type
);

const AivmSyscallContract* aivm_syscall_contract_find_by_target(const char* target);
const AivmSyscallContract* aivm_syscall_contract_find_by_id(uint32_t id);
AivmSyscallCapabilityGroup aivm_syscall_contract_capability(const char* target);
const char* aivm_syscall_capability_name(AivmSyscallCapabilityGroup capability);
int aivm_syscall_capability_from_name(
    const char* name,
    AivmSyscallCapabilityGroup* out_capability
);
int aivm_syscall_contract_is_debug_target(const char* target);
int aivm_syscall_contract_should_bind_in_production(const char* target);
const char* aivm_contract_status_code(AivmContractStatus status);
const char* aivm_contract_status_message(AivmContractStatus status);

#endif
