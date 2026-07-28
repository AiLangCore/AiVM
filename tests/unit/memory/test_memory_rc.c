#include "aivm_program.h"
#include "aivm_vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static int run_string_program(size_t* out_arena_used)
{
    static AivmVm vm;
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

static int test_bytes_safe_point_compaction(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 1U,
        .constants = NULL,
        .constant_count = 0U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const uint8_t live_bytes[] = { 1U, 2U, 3U, 4U };
    const uint8_t* old_pointer;

    aivm_init(&vm, &program);
    memset(vm.bytes_arena, 0xEE, 64U);
    memcpy(&vm.bytes_arena[48], live_bytes, sizeof(live_bytes));
    vm.bytes_arena_used = 64U;
    old_pointer = &vm.bytes_arena[48];
    vm.stack[0] = aivm_value_bytes(old_pointer, sizeof(live_bytes));
    vm.stack[1] = aivm_value_bytes(old_pointer, sizeof(live_bytes));
    vm.stack_count = 2U;

    if (!aivm_collect_safe_point(&vm)) {
        return 1;
    }
    if (expect(vm.bytes_arena_used == sizeof(live_bytes)) != 0 ||
        expect(vm.stack[0].bytes_value.data != old_pointer) != 0 ||
        expect(vm.stack[0].bytes_value.data == vm.stack[1].bytes_value.data) != 0 ||
        expect(memcmp(vm.stack[0].bytes_value.data, live_bytes, sizeof(live_bytes)) == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_string_intern_index_scales_distinct_values(void)
{
    enum { DISTINCT_VALUE_COUNT = 12000 };
    static AivmVm vm;
    AivmInstruction* instructions = NULL;
    AivmValue* constants = NULL;
    char (*texts)[32] = NULL;
    AivmProgram program;
    size_t instruction_index = 0U;
    size_t i;
    AivmValue second;
    AivmValue first;
    int result = 1;

    instructions = (AivmInstruction*)calloc((DISTINCT_VALUE_COUNT * 4U) + 7U, sizeof(instructions[0]));
    constants = (AivmValue*)calloc(DISTINCT_VALUE_COUNT + 3U, sizeof(constants[0]));
    texts = (char (*)[32])calloc(DISTINCT_VALUE_COUNT + 3U, sizeof(texts[0]));
    if (instructions == NULL || constants == NULL || texts == NULL) {
        goto done;
    }
    for (i = 0U; i < DISTINCT_VALUE_COUNT; i += 1U) {
        (void)snprintf(texts[i], sizeof(texts[i]), "intern-value-%zu", i);
        constants[i] = aivm_value_string(texts[i]);
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_CONST, .operand_int = (int64_t)i };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_CONST, .operand_int = DISTINCT_VALUE_COUNT };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_STORE_LOCAL, .operand_int = (int64_t)i };
    }
    constants[DISTINCT_VALUE_COUNT] = aivm_value_string("");
    (void)snprintf(texts[DISTINCT_VALUE_COUNT + 1U], sizeof(texts[DISTINCT_VALUE_COUNT + 1U]), "intern-repeat");
    (void)snprintf(texts[DISTINCT_VALUE_COUNT + 2U], sizeof(texts[DISTINCT_VALUE_COUNT + 2U]), "intern-repeat");
    constants[DISTINCT_VALUE_COUNT + 1U] = aivm_value_string(texts[DISTINCT_VALUE_COUNT + 1U]);
    constants[DISTINCT_VALUE_COUNT + 2U] = aivm_value_string(texts[DISTINCT_VALUE_COUNT + 2U]);
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_CONST, .operand_int = DISTINCT_VALUE_COUNT + 1U };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_CONST, .operand_int = DISTINCT_VALUE_COUNT };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_CONST, .operand_int = DISTINCT_VALUE_COUNT + 2U };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_CONST, .operand_int = DISTINCT_VALUE_COUNT };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_HALT, .operand_int = 0 };
    program = (AivmProgram){
        .instructions = instructions,
        .instruction_count = instruction_index,
        .constants = constants,
        .constant_count = DISTINCT_VALUE_COUNT + 3U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (vm.status == AIVM_VM_STATUS_HALTED &&
        vm.string_intern_complete == 1 &&
        vm.string_intern_count >= DISTINCT_VALUE_COUNT &&
        aivm_stack_pop(&vm, &second) == 1 &&
        aivm_stack_pop(&vm, &first) == 1 &&
        first.type == AIVM_VAL_STRING && second.type == AIVM_VAL_STRING &&
        first.string_value == second.string_value) {
        result = 0;
    } else {
        (void)fprintf(
            stderr,
            "string intern index regression: status=%d complete=%d count=%zu stack=%zu error=%s\n",
            (int)vm.status,
            vm.string_intern_complete,
            vm.string_intern_count,
            vm.stack_count,
            aivm_vm_error_detail(&vm));
    }

done:
    free(instructions);
    free(constants);
    free(texts);
    return result;
}

static int test_scratch_pair_safe_point_compaction(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 },
        { .opcode = AIVM_OP_POP, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 4 },
        { .opcode = AIVM_OP_CONST, .operand_int = 5 },
        { .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 },
        { .opcode = AIVM_OP_POP, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 10 },
        { .type = AIVM_VAL_INT, .int_value = 11 },
        { .type = AIVM_VAL_INT, .int_value = 20 },
        { .type = AIVM_VAL_INT, .int_value = 21 },
        { .type = AIVM_VAL_INT, .int_value = 30 },
        { .type = AIVM_VAL_INT, .int_value = 31 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = sizeof(instructions) / sizeof(instructions[0]),
        .constants = constants,
        .constant_count = sizeof(constants) / sizeof(constants[0]),
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0 ||
        expect(vm.scratch_pair_count == 3U) != 0 ||
        expect(vm.locals[0].type == AIVM_VAL_PAIR) != 0 ||
        expect(vm.locals[0].pair_handle == 2) != 0) {
        return 1;
    }

    if (!aivm_collect_safe_point(&vm)) {
        return 1;
    }
    if (expect(vm.scratch_pair_count == 1U) != 0 ||
        expect(vm.locals[0].type == AIVM_VAL_PAIR) != 0 ||
        expect(vm.locals[0].pair_handle == 1) != 0 ||
        expect(vm.scratch_pairs[0].first.type == AIVM_VAL_INT) != 0 ||
        expect(vm.scratch_pairs[0].first.int_value == 20) != 0 ||
        expect(vm.scratch_pairs[0].second.type == AIVM_VAL_INT) != 0 ||
        expect(vm.scratch_pairs[0].second.int_value == 21) != 0) {
        return 1;
    }
    return 0;
}

static int test_scratch_pair_capacity_recovers_transient_pairs(void)
{
    static AivmVm vm;
    AivmInstruction* instructions;
    AivmProgram program;
    size_t instruction_count = (AIVM_VM_SCRATCH_PAIR_CAPACITY * 4U) + 4U;
    size_t pair_index;
    size_t instruction_index = 0U;
    AivmValue out;
    int result = 1;

    instructions = (AivmInstruction*)calloc(instruction_count, sizeof(instructions[0]));
    if (instructions == NULL) {
        return 1;
    }
    for (pair_index = 0U; pair_index < AIVM_VM_SCRATCH_PAIR_CAPACITY; pair_index += 1U) {
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = (int64_t)pair_index };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = (int64_t)(pair_index + 1U) };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_POP, .operand_int = 0 };
    }
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = 41 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = 42 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_HALT, .operand_int = 0 };
    program = (AivmProgram){
        .instructions = instructions,
        .instruction_count = instruction_index,
        .constants = NULL,
        .constant_count = 0U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) == 0 &&
        expect(vm.scratch_pair_count == 1U) == 0 &&
        expect(aivm_stack_pop(&vm, &out) == 1) == 0 &&
        expect(out.type == AIVM_VAL_PAIR && out.pair_handle == 1) == 0) {
        result = 0;
    }
    free(instructions);
    return result;
}

static int test_scratch_pair_compacts_deep_live_chain(void)
{
    static AivmVm vm;
    AivmInstruction* instructions;
    AivmProgram program;
    size_t instruction_count = 4U + ((AIVM_VM_SCRATCH_PAIR_CAPACITY - 1U) * 4U) + 1U;
    size_t pair_index;
    size_t instruction_index = 0U;
    AivmValue out;
    int result = 1;

    instructions = (AivmInstruction*)calloc(instruction_count, sizeof(instructions[0]));
    if (instructions == NULL) {
        return 1;
    }
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = 2 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 };
    for (pair_index = 1U; pair_index < AIVM_VM_SCRATCH_PAIR_CAPACITY; pair_index += 1U) {
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = (int64_t)(pair_index + 2U) };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 };
    }
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_HALT, .operand_int = 0 };
    program = (AivmProgram){
        .instructions = instructions,
        .instruction_count = instruction_index,
        .constants = NULL,
        .constant_count = 0U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) == 0 &&
        expect(vm.scratch_pair_count == AIVM_VM_SCRATCH_PAIR_CAPACITY) == 0 &&
        expect(aivm_collect_safe_point(&vm) == 1) == 0 &&
        expect(vm.scratch_pair_count == AIVM_VM_SCRATCH_PAIR_CAPACITY) == 0 &&
        expect(vm.locals[0].type == AIVM_VAL_PAIR) == 0 &&
        expect(aivm_stack_push(&vm, vm.locals[0]) == 1) == 0 &&
        expect(aivm_stack_pop(&vm, &out) == 1) == 0 &&
        expect(out.type == AIVM_VAL_PAIR && out.pair_handle == AIVM_VM_SCRATCH_PAIR_CAPACITY) == 0) {
        result = 0;
    }
    free(instructions);
    return result;
}

static int test_scratch_pair_capacity_preserves_pair_operands(void)
{
    static AivmVm vm;
    AivmInstruction* instructions;
    AivmProgram program;
    size_t instruction_count = (AIVM_VM_SCRATCH_PAIR_CAPACITY * 4U) + 7U;
    size_t pair_index;
    size_t instruction_index = 0U;
    AivmValue out;
    int result = 1;

    instructions = (AivmInstruction*)calloc(instruction_count, sizeof(instructions[0]));
    if (instructions == NULL) {
        return 1;
    }
    for (pair_index = 0U; pair_index < (AIVM_VM_SCRATCH_PAIR_CAPACITY - 1U); pair_index += 1U) {
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = (int64_t)pair_index };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = (int64_t)(pair_index + 1U) };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 };
        instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_POP, .operand_int = 0 };
    }
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = 41 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = 42 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PUSH_INT, .operand_int = 43 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PAIR_FIRST, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_PAIR_FIRST, .operand_int = 0 };
    instructions[instruction_index++] = (AivmInstruction){ .opcode = AIVM_OP_HALT, .operand_int = 0 };
    program = (AivmProgram){
        .instructions = instructions,
        .instruction_count = instruction_index,
        .constants = NULL,
        .constant_count = 0U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) == 0 &&
        expect(vm.scratch_pair_count == 2U) == 0 &&
        expect(aivm_stack_pop(&vm, &out) == 1) == 0 &&
        expect(out.type == AIVM_VAL_INT && out.int_value == 41) == 0) {
        result = 0;
    }
    free(instructions);
    return result;
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
    if (test_bytes_safe_point_compaction() != 0) {
        return 1;
    }
    if (test_string_intern_index_scales_distinct_values() != 0) {
        return 1;
    }
    if (test_scratch_pair_safe_point_compaction() != 0) {
        return 1;
    }
    if (test_scratch_pair_capacity_recovers_transient_pairs() != 0) {
        return 1;
    }
    if (test_scratch_pair_compacts_deep_live_chain() != 0) {
        return 1;
    }
    if (test_scratch_pair_capacity_preserves_pair_operands() != 0) {
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
