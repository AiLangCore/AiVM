#include <string.h>

#include "aivm_program.h"
#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmVm vm;
    static const AivmInstruction async_call_instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = -1 }
    };
    AivmProgram async_call_program;

    if (expect(strcmp(aivm_vm_error_code(AIVM_VM_ERR_NONE), "AIVM000") == 0) != 0) {
        return 1;
    }

    if (expect(strcmp(aivm_vm_error_code(AIVM_VM_ERR_INVALID_OPCODE), "AIVM001") == 0) != 0) {
        return 1;
    }

    if (expect(strcmp(aivm_vm_error_code((AivmVmError)99), "AIVM999") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_code(AIVM_VM_ERR_TYPE_MISMATCH), "AIVM007") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_code(AIVM_VM_ERR_INVALID_PROGRAM), "AIVM008") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_code(AIVM_VM_ERR_STRING_OVERFLOW), "AIVM009") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_code(AIVM_VM_ERR_SYSCALL), "AIVM010") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_code(AIVM_VM_ERR_MEMORY_PRESSURE), "AIVM011") == 0) != 0) {
        return 1;
    }

    if (expect(strcmp(aivm_vm_error_message(AIVM_VM_ERR_NONE), "No error.") == 0) != 0) {
        return 1;
    }

    if (expect(strcmp(aivm_vm_error_message(AIVM_VM_ERR_INVALID_OPCODE), "Unsupported opcode.") == 0) != 0) {
        return 1;
    }

    if (expect(strcmp(aivm_vm_error_message((AivmVmError)99), "Unknown VM error.") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_message(AIVM_VM_ERR_TYPE_MISMATCH), "Type mismatch.") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_message(AIVM_VM_ERR_INVALID_PROGRAM), "Invalid program state.") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_message(AIVM_VM_ERR_STRING_OVERFLOW), "VM string arena overflow.") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_message(AIVM_VM_ERR_SYSCALL), "Syscall dispatch failed.") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_message(AIVM_VM_ERR_MEMORY_PRESSURE), "VM memory pressure limit exceeded.") == 0) != 0) {
        return 1;
    }

    if (expect(strcmp(aivm_vm_error_detail(NULL), "") == 0) != 0) {
        return 1;
    }

    aivm_program_init(&async_call_program, async_call_instructions, 1U);
    aivm_init(&vm, &async_call_program);
    if (expect(strcmp(aivm_vm_error_detail(&vm), "") == 0) != 0) {
        return 1;
    }
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Negative operand is invalid.") == 0) != 0) {
        return 1;
    }

    aivm_init(&vm, &async_call_program);
    if (expect(strcmp(aivm_vm_error_detail(&vm), "") == 0) != 0) {
        return 1;
    }

    return 0;
}
