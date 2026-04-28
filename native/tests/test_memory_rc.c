#include "aivm_program.h"
#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static int run_string_program(size_t* out_arena_used)
{
    AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "hello-" },
        { .type = AIVM_VAL_STRING, .string_value = "world" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 4U,
        .constants = constants,
        .constant_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.stack[0].type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_used > 0U) != 0) {
        return 1;
    }
    *out_arena_used = vm.string_arena_used;
    return 0;
}

int main(void)
{
    size_t first = 0U;
    size_t second = 0U;
    size_t baseline = 0U;
    size_t current = 0U;
    size_t i = 0U;

    if (run_string_program(&first) != 0) {
        return 1;
    }
    if (run_string_program(&second) != 0) {
        return 1;
    }

    /* Deterministic explicit reset point: arena usage should not grow across runs. */
    if (expect(first == second) != 0) {
        return 1;
    }

    baseline = first;
    for (i = 0U; i < 1000U; i += 1U) {
        if (run_string_program(&current) != 0) {
            return 1;
        }
        if (expect(current == baseline) != 0) {
            return 1;
        }
    }

    return 0;
}
