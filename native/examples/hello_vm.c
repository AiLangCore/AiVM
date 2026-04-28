#include <stdio.h>

#include "aivm_program.h"
#include "aivm_vm.h"

int main(void)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    AivmVm vm;

    aivm_init(&vm, &program);
    aivm_run(&vm);

    printf("AiVM.C run complete. instruction_pointer=%zu\n", vm.instruction_pointer);
    return 0;
}
