#include "aivm_vm.h"

void aivm_set_vm_error(AivmVm* vm, AivmVmError error, const char* detail)
{
    if (vm == NULL) {
        return;
    }
    vm->error = error;
    vm->status = AIVM_VM_STATUS_ERROR;
    vm->error_detail = detail;
}

const char* aivm_vm_error_code(AivmVmError error)
{
    switch (error) {
        case AIVM_VM_ERR_NONE:
            return "AIVM000";
        case AIVM_VM_ERR_INVALID_OPCODE:
            return "AIVM001";
        case AIVM_VM_ERR_STACK_OVERFLOW:
            return "AIVM002";
        case AIVM_VM_ERR_STACK_UNDERFLOW:
            return "AIVM003";
        case AIVM_VM_ERR_FRAME_OVERFLOW:
            return "AIVM004";
        case AIVM_VM_ERR_FRAME_UNDERFLOW:
            return "AIVM005";
        case AIVM_VM_ERR_LOCAL_OUT_OF_RANGE:
            return "AIVM006";
        case AIVM_VM_ERR_TYPE_MISMATCH:
            return "AIVM007";
        case AIVM_VM_ERR_INVALID_PROGRAM:
            return "AIVM008";
        case AIVM_VM_ERR_STRING_OVERFLOW:
            return "AIVM009";
        case AIVM_VM_ERR_SYSCALL:
            return "AIVM010";
        case AIVM_VM_ERR_MEMORY_PRESSURE:
            return "AIVM011";
        default:
            return "AIVM999";
    }
}

const char* aivm_vm_error_message(AivmVmError error)
{
    switch (error) {
        case AIVM_VM_ERR_NONE:
            return "No error.";
        case AIVM_VM_ERR_INVALID_OPCODE:
            return "Unsupported opcode.";
        case AIVM_VM_ERR_STACK_OVERFLOW:
            return "Stack overflow.";
        case AIVM_VM_ERR_STACK_UNDERFLOW:
            return "Stack underflow.";
        case AIVM_VM_ERR_FRAME_OVERFLOW:
            return "Call frame overflow.";
        case AIVM_VM_ERR_FRAME_UNDERFLOW:
            return "Call frame underflow.";
        case AIVM_VM_ERR_LOCAL_OUT_OF_RANGE:
            return "Local index out of range.";
        case AIVM_VM_ERR_TYPE_MISMATCH:
            return "Type mismatch.";
        case AIVM_VM_ERR_INVALID_PROGRAM:
            return "Invalid program state.";
        case AIVM_VM_ERR_STRING_OVERFLOW:
            return "VM string arena overflow.";
        case AIVM_VM_ERR_SYSCALL:
            return "Syscall dispatch failed.";
        case AIVM_VM_ERR_MEMORY_PRESSURE:
            return "VM memory pressure limit exceeded.";
        default:
            return "Unknown VM error.";
    }
}

const char* aivm_vm_error_detail(const AivmVm* vm)
{
    if (vm == NULL || vm->error_detail == NULL) {
        return "";
    }
    return vm->error_detail;
}
