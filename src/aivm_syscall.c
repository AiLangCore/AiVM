#include "aivm_syscall.h"

#include <string.h>

#include "aivm_syscall_contracts.h"

AivmSyscallStatus aivm_syscall_invoke(
    AivmSyscallHandler handler,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }

    if (handler == NULL || target == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }

    return (AivmSyscallStatus)handler(target, args, arg_count, result);
}

AivmSyscallStatus aivm_syscall_dispatch(
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t index;

    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }

    if (bindings == NULL || target == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }

    for (index = 0U; index < binding_count; index += 1U) {
        if (bindings[index].target == NULL || bindings[index].handler == NULL) {
            continue;
        }

        if (strcmp(bindings[index].target, target) == 0) {
            return aivm_syscall_invoke(bindings[index].handler, target, args, arg_count, result);
        }
    }

    result->type = AIVM_VAL_VOID;
    return AIVM_SYSCALL_ERR_NOT_FOUND;
}

AivmSyscallStatus aivm_syscall_dispatch_checked_with_contract(
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result,
    AivmContractStatus* out_contract_status)
{
    AivmValueType expected_return_type = AIVM_VAL_VOID;
    AivmContractStatus contract_status;
    AivmSyscallStatus invoke_status;

    if (result == NULL) {
        if (out_contract_status != NULL) {
            *out_contract_status = AIVM_CONTRACT_OK;
        }
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }

    contract_status = aivm_syscall_contract_validate(target, args, arg_count, &expected_return_type);
    if (contract_status != AIVM_CONTRACT_OK) {
        if (out_contract_status != NULL) {
            *out_contract_status = contract_status;
        }
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (out_contract_status != NULL) {
        *out_contract_status = AIVM_CONTRACT_OK;
    }

    invoke_status = aivm_syscall_dispatch(bindings, binding_count, target, args, arg_count, result);
    if (invoke_status != AIVM_SYSCALL_OK) {
        return invoke_status;
    }

    if (result->type != expected_return_type) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_RETURN_TYPE;
    }

    return AIVM_SYSCALL_OK;
}

AivmSyscallStatus aivm_syscall_dispatch_checked(
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    return aivm_syscall_dispatch_checked_with_contract(
        bindings,
        binding_count,
        target,
        args,
        arg_count,
        result,
        NULL);
}

const char* aivm_syscall_status_code(AivmSyscallStatus status)
{
    switch (status) {
        case AIVM_SYSCALL_OK:
            return "AIVMS000";
        case AIVM_SYSCALL_ERR_INVALID:
            return "AIVMS001";
        case AIVM_SYSCALL_ERR_NULL_RESULT:
            return "AIVMS002";
        case AIVM_SYSCALL_ERR_NOT_FOUND:
            return "AIVMS003";
        case AIVM_SYSCALL_ERR_CONTRACT:
            return "AIVMS004";
        case AIVM_SYSCALL_ERR_RETURN_TYPE:
            return "AIVMS005";
        default:
            return "AIVMS999";
    }
}

const char* aivm_syscall_status_message(AivmSyscallStatus status)
{
    switch (status) {
        case AIVM_SYSCALL_OK:
            return "Syscall dispatch succeeded.";
        case AIVM_SYSCALL_ERR_INVALID:
            return "Syscall dispatch input was invalid.";
        case AIVM_SYSCALL_ERR_NULL_RESULT:
            return "Syscall dispatch result pointer was null.";
        case AIVM_SYSCALL_ERR_NOT_FOUND:
            return "Syscall target was not found.";
        case AIVM_SYSCALL_ERR_CONTRACT:
            return "Syscall arguments violated contract.";
        case AIVM_SYSCALL_ERR_RETURN_TYPE:
            return "Syscall return type violated contract.";
        default:
            return "Unknown syscall dispatch status.";
    }
}
