#include "aivm_program.h"
#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static int run_node_graph_program(size_t* out_node_count)
{
    AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_APPEND_CHILD, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "root" },
        { .type = AIVM_VAL_STRING, .string_value = "leaf" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 6U,
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
    if (expect(vm.stack[0].type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(vm.node_count >= 3U) != 0) {
        return 1;
    }
    *out_node_count = vm.node_count;
    return 0;
}

int main(void)
{
    size_t first = 0U;
    size_t second = 0U;
    size_t baseline = 0U;
    size_t current = 0U;
    size_t i = 0U;

    if (run_node_graph_program(&first) != 0) {
        return 1;
    }
    if (run_node_graph_program(&second) != 0) {
        return 1;
    }

    /* Deterministic reset point: node arena usage is stable across runs. */
    if (expect(first == second) != 0) {
        return 1;
    }

    baseline = first;
    for (i = 0U; i < 1000U; i += 1U) {
        if (run_node_graph_program(&current) != 0) {
            return 1;
        }
        if (expect(current == baseline) != 0) {
            return 1;
        }
    }

    return 0;
}
