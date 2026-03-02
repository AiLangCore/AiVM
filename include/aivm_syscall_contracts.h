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

typedef struct {
    uint32_t id;
    const char* target;
    size_t arg_count;
    AivmValueType arg_types[8];
    AivmValueType return_type;
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
const char* aivm_contract_status_code(AivmContractStatus status);
const char* aivm_contract_status_message(AivmContractStatus status);

#endif
