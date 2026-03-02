#include <string.h>

#include "aivm_program.h"
#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static int test_run_nop_halt(void)
{
    AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    if (expect(vm.status == AIVM_VM_STATUS_READY) != 0) {
        return 1;
    }

    aivm_run(&vm);
    if (expect(vm.instruction_pointer == 2U) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }

    return 0;
}

static int test_invalid_opcode_sets_error(void)
{
    AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = (AivmOpcode)99, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_step(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_OPCODE) != 0) {
        return 1;
    }

    return 0;
}

static int test_halt_without_program_is_safe(void)
{
    AivmVm vm;

    aivm_init(&vm, NULL);
    aivm_halt(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_READY) != 0) {
        return 1;
    }

    return 0;
}

static int test_empty_program_halts(void)
{
    AivmVm vm;
    static const AivmProgram program = {
        .instructions = NULL,
        .instruction_count = 0U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }

    return 0;
}

static int test_missing_instruction_buffer_sets_error_detail(void)
{
    AivmVm vm;
    static const AivmProgram program = {
        .instructions = NULL,
        .instruction_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(vm.instruction_pointer == 1U) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Program instruction buffer is null.") == 0) != 0) {
        return 1;
    }

    return 0;
}

int main(void)
{
    if (test_run_nop_halt() != 0) {
        return 1;
    }
    if (test_invalid_opcode_sets_error() != 0) {
        return 1;
    }
    if (test_halt_without_program_is_safe() != 0) {
        return 1;
    }
    if (test_empty_program_halts() != 0) {
        return 1;
    }
    if (test_missing_instruction_buffer_sets_error_detail() != 0) {
        return 1;
    }

    return 0;
}
