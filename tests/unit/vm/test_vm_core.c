#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "aivm_program.h"
#include "aivm_vm.h"
#include "aivm_host_memory.h"

static int expect_line(int condition, int line)
{
    if (condition) {
        return 0;
    }
    (void)fprintf(stderr, "expect failed at line %d\n", line);
    return 1;
}

#define expect(condition) expect_line((condition), __LINE__)

static int host_core_bytes_small(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static const uint8_t payload[4] = { 1U, 2U, 3U, 4U };
    (void)target;
    if (arg_count != 1U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[0].type != AIVM_VAL_STRING) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bytes(payload, sizeof(payload));
    return AIVM_SYSCALL_OK;
}

static int host_core_bytes_large(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static uint8_t payload[AIVM_VM_BYTES_ARENA_CAPACITY + 1U];
    (void)target;
    if (arg_count != 1U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[0].type != AIVM_VAL_STRING) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bytes(payload, sizeof(payload));
    return AIVM_SYSCALL_OK;
}

static int host_core_bytes_compiler_sized(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static uint8_t payload[8U * 1024U * 1024U];
    (void)target;
    if (arg_count != 1U || args[0].type != AIVM_VAL_STRING) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bytes(payload, sizeof(payload));
    return AIVM_SYSCALL_OK;
}

static int test_run_nop_halt(void)
{
    static AivmVm vm;
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
    static AivmVm vm;
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

static int test_stub_opcode_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_STUB, .operand_int = 0 }
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "STUB opcode is invalid at runtime.") == 0) != 0) {
        return 1;
    }

    return 0;
}

static int test_halt_without_program_is_safe(void)
{
    static AivmVm vm;

    aivm_init(&vm, NULL);
    aivm_halt(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_READY) != 0) {
        return 1;
    }

    return 0;
}

static int test_empty_program_halts(void)
{
    static AivmVm vm;
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
    static AivmVm vm;
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

static int test_gc_policy_constants_are_valid(void)
{
    if (expect(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS > 0) != 0) {
        return 1;
    }
    if (expect(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD > 0) != 0) {
        return 1;
    }
    if (expect(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD < AIVM_VM_NODE_CAPACITY) != 0) {
        return 1;
    }
    if (expect(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD ==
               (AIVM_VM_NODE_CAPACITY * AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR) /
                   AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR) != 0) {
        return 1;
    }
    return 0;
}

static int test_reset_keeps_gc_allocation_counter_deterministic(void)
{
    static AivmVm vm;
    static const char* argv_values[] = { "first", "second" };

    aivm_init_with_syscalls_and_argv(&vm, NULL, NULL, 0U, argv_values, 2U);
    if (expect(vm.status == AIVM_VM_STATUS_READY) != 0) {
        return 1;
    }
    if (expect(vm.node_count >= 3U) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count == 0U) != 0) {
        return 1;
    }
    return 0;
}

static int test_reset_clears_gc_counters_after_allocations(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "tmp" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 3U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc > 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_count > 1U) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_READY) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.node_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_attrs == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_children == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == 0U) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    return 0;
}

static int test_gc_policy_requires_interval_even_under_pressure(void)
{
    static AivmVm vm;
    static AivmInstruction instructions[(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS - 1U) * 3U + 1U];
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "tmp" }
    };
    AivmProgram program;
    const char* argv_values[AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U];
    size_t i;
    size_t ip = 0U;

    for (i = 0U; i < (size_t)(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U); i += 1U) {
        argv_values[i] = "arg";
    }

    for (i = 0U; i < (size_t)(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS - 1U); i += 1U) {
        instructions[ip].opcode = AIVM_OP_CONST;
        instructions[ip].operand_int = 0;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
        instructions[ip].operand_int = 0;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_POP;
        instructions[ip].operand_int = 0;
        ip += 1U;
    }
    instructions[ip].opcode = AIVM_OP_HALT;
    instructions[ip].operand_int = 0;
    ip += 1U;

    memset(&program, 0, sizeof(program));
    program.instructions = instructions;
    program.instruction_count = ip;
    program.constants = constants;
    program.constant_count = 1U;

    aivm_init_with_syscalls_and_argv(
        &vm,
        &program,
        NULL,
        0U,
        argv_values,
        (size_t)(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U));
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == (size_t)(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS - 1U)) != 0) {
        return 1;
    }
    if (expect(vm.node_count == (size_t)(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD + AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS - 1U)) != 0) {
        return 1;
    }

    return 0;
}

static int test_gc_policy_does_not_compact_observational_pressure(void)
{
    static AivmVm vm;
    static AivmInstruction instructions[(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS + 1U) * 3U + 1U];
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "tmp" }
    };
    AivmProgram program;
    const char* argv_values[AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U];
    size_t i;
    size_t ip = 0U;

    for (i = 0U; i < (size_t)(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U); i += 1U) {
        argv_values[i] = "arg";
    }

    for (i = 0U; i < (size_t)(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS + 1U); i += 1U) {
        instructions[ip].opcode = AIVM_OP_CONST;
        instructions[ip].operand_int = 0;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
        instructions[ip].operand_int = 0;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_POP;
        instructions[ip].operand_int = 0;
        ip += 1U;
    }
    instructions[ip].opcode = AIVM_OP_HALT;
    instructions[ip].operand_int = 0;
    ip += 1U;

    memset(&program, 0, sizeof(program));
    program.instructions = instructions;
    program.instruction_count = ip;
    program.constants = constants;
    program.constant_count = 1U;

    aivm_init_with_syscalls_and_argv(
        &vm,
        &program,
        NULL,
        0U,
        argv_values,
        (size_t)(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U));
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc >= (size_t)AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS) != 0) {
        return 1;
    }
    if (expect(vm.node_count > (size_t)AIVM_VM_NODE_GC_PRESSURE_THRESHOLD) != 0) {
        return 1;
    }

    return 0;
}

static int test_gc_counters_saturate_without_wrapping(void)
{
    static AivmVm vm;
    static AivmInstruction instructions[(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS + 1U) * 3U + 1U];
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "tmp" }
    };
    AivmProgram program;
    const char* argv_values[AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U];
    size_t i;
    size_t ip = 0U;

    for (i = 0U; i < (size_t)(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U); i += 1U) {
        argv_values[i] = "arg";
    }

    for (i = 0U; i < (size_t)(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS + 1U); i += 1U) {
        instructions[ip].opcode = AIVM_OP_CONST;
        instructions[ip].operand_int = 0;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
        instructions[ip].operand_int = 0;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_POP;
        instructions[ip].operand_int = 0;
        ip += 1U;
    }
    instructions[ip].opcode = AIVM_OP_HALT;
    instructions[ip].operand_int = 0;
    ip += 1U;

    memset(&program, 0, sizeof(program));
    program.instructions = instructions;
    program.instruction_count = ip;
    program.constants = constants;
    program.constant_count = 1U;

    aivm_init_with_syscalls_and_argv(
        &vm,
        &program,
        NULL,
        0U,
        argv_values,
        (size_t)(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U));

    vm.node_gc_attempt_count = (size_t)-1;
    vm.node_gc_compaction_count = (size_t)-1;
    vm.node_gc_reclaimed_nodes = (size_t)-1;
    vm.node_gc_reclaimed_attrs = (size_t)-1;
    vm.node_gc_reclaimed_children = (size_t)-1;

    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count == (size_t)-1) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count == (size_t)-1) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes == (size_t)-1) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_attrs == (size_t)-1) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_children == (size_t)-1) != 0) {
        return 1;
    }

    return 0;
}

static int test_reset_clears_bytes_arena_after_syscall_materialization(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.fs.file.read" },
        { .type = AIVM_VAL_STRING, .string_value = "ignored" }
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
    static const AivmSyscallBinding bindings[] = {
        { "sys.fs.file.read", host_core_bytes_small }
    };

    aivm_init_with_syscalls(&vm, &program, bindings, 1U);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_used == 4U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_high_water == 4U) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_READY) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_used == 0U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_high_water == 0U) != 0) {
        return 1;
    }
    return 0;
}

static int test_reset_clears_pressure_counters_after_string_failure(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "a" },
        { .type = AIVM_VAL_STRING, .string_value = "b" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 3U,
        .constants = constants,
        .constant_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    memset(vm.string_arena, 'x', AIVM_VM_STRING_ARENA_CAPACITY - 1U);
    vm.string_arena[AIVM_VM_STRING_ARENA_CAPACITY - 1U] = '\0';
    vm.string_arena_used = AIVM_VM_STRING_ARENA_CAPACITY;
    vm.stack_count = 1U;
    vm.stack[0] = aivm_value_string(vm.string_arena);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_MEMORY_PRESSURE) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 1U) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_READY) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    return 0;
}

static int test_reset_clears_pressure_counters_after_bytes_failure(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.fs.file.read" },
        { .type = AIVM_VAL_STRING, .string_value = "ignored" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 3U,
        .constants = constants,
        .constant_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.fs.file.read", host_core_bytes_large }
    };

    aivm_init_with_syscalls(&vm, &program, bindings, 1U);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_MEMORY_PRESSURE) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 1U) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_READY) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    return 0;
}

static int test_reset_clears_pressure_counters_after_node_failure(void)
{
    static AivmVm vm;
    AivmInstruction* instructions;
    AivmValue constants[1];
    AivmProgram program;
    size_t ip = 0U;
    size_t i;

    instructions = (AivmInstruction*)calloc((AIVM_VM_NODE_CAPACITY + 1U) * 2U + 1U, sizeof(AivmInstruction));
    if (instructions == NULL) {
        return 1;
    }
    constants[0] = aivm_value_string("tmp");
    for (i = 0U; i < (size_t)(AIVM_VM_NODE_CAPACITY + 1U); i += 1U) {
        instructions[ip].opcode = AIVM_OP_CONST;
        instructions[ip].operand_int = 0;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
        instructions[ip].operand_int = 0;
        ip += 1U;
    }
    instructions[ip].opcode = AIVM_OP_HALT;
    instructions[ip].operand_int = 0;
    ip += 1U;

    memset(&program, 0, sizeof(program));
    program.instructions = instructions;
    program.instruction_count = ip;
    program.constants = constants;
    program.constant_count = 1U;

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        free(instructions);
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_MEMORY_PRESSURE) != 0) {
        free(instructions);
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 1U) != 0) {
        free(instructions);
        return 1;
    }

    aivm_reset_state(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_READY) != 0) {
        free(instructions);
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 0U) != 0) {
        free(instructions);
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 0U) != 0) {
        free(instructions);
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 0U) != 0) {
        free(instructions);
        return 1;
    }
    free(instructions);
    return 0;
}

static int test_pressure_counters_remain_zero_on_successful_run(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 2 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    return 0;
}

static int test_tooling_profile_allocates_tooling_node_arenas(void)
{
    static AivmVm production_vm;
    static AivmVm tooling_vm;
    AivmRuntimeProfileLimits tooling_limits = aivm_runtime_profile_limits(AIVM_RUNTIME_PROFILE_TOOLING);

    aivm_init_with_profile(&production_vm, NULL, AIVM_RUNTIME_PROFILE_PRODUCTION);
    aivm_init_with_profile(&tooling_vm, NULL, AIVM_RUNTIME_PROFILE_TOOLING);

    if (expect(production_vm.node_capacity == AIVM_VM_NODE_CAPACITY) != 0 ||
        expect(production_vm.node_attr_capacity == AIVM_VM_NODE_ATTR_CAPACITY) != 0 ||
        expect(production_vm.node_child_capacity == AIVM_VM_NODE_CHILD_CAPACITY) != 0 ||
        expect(production_vm.string_arena_capacity == AIVM_VM_STRING_ARENA_CAPACITY) != 0 ||
        expect(tooling_vm.runtime_profile == AIVM_RUNTIME_PROFILE_TOOLING) != 0 ||
        expect(tooling_vm.node_capacity == tooling_limits.node_capacity) != 0 ||
        expect(tooling_vm.node_attr_capacity == tooling_limits.node_attr_capacity) != 0 ||
        expect(tooling_vm.node_child_capacity == tooling_limits.node_child_capacity) != 0 ||
        expect(tooling_vm.string_arena_capacity == tooling_limits.string_arena_capacity) != 0 ||
        expect(tooling_vm.string_arena_storage_capacity == tooling_limits.string_arena_capacity) != 0 ||
        expect(production_vm.bytes_arena_capacity == AIVM_VM_BYTES_ARENA_CAPACITY) != 0 ||
        expect(tooling_vm.bytes_arena_capacity == tooling_limits.bytes_arena_capacity) != 0 ||
        expect(tooling_limits.bytes_arena_capacity == 64U * 1024U * 1024U) != 0 ||
        expect(tooling_vm.bytes_arena_capacity > production_vm.bytes_arena_capacity) != 0 ||
        expect(tooling_vm.string_arena_capacity > production_vm.string_arena_capacity) != 0 ||
        expect(tooling_vm.node_capacity > production_vm.node_capacity) != 0) {
        return 1;
    }

    /* Reusing a VM for tooling must replace storage before parser values are rebuilt. */
    aivm_init_with_profile(&production_vm, NULL, AIVM_RUNTIME_PROFILE_TOOLING);
    if (expect(production_vm.string_arena_capacity == tooling_limits.string_arena_capacity) != 0 ||
        expect(production_vm.string_arena_storage_capacity == tooling_limits.string_arena_capacity) != 0 ||
        expect(production_vm.node_capacity == tooling_limits.node_capacity) != 0) {
        return 1;
    }

    aivm_dispose(&production_vm);
    aivm_dispose(&tooling_vm);
    return 0;
}

static int test_tooling_profile_materializes_large_bytes(void)
{
    static AivmVm vm;
    AivmValue result;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.fs.file.read" },
        { .type = AIVM_VAL_STRING, .string_value = "ignored" }
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
    static const AivmSyscallBinding bindings[] = {
        { "sys.fs.file.read", host_core_bytes_compiler_sized }
    };

    aivm_init_with_syscalls_and_argv_profile(
        &vm,
        &program,
        bindings,
        1U,
        NULL,
        0U,
        AIVM_RUNTIME_PROFILE_TOOLING);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0 ||
        expect(vm.bytes_arena_pressure_count == 0U) != 0 ||
        expect(aivm_stack_pop(&vm, &result) == 1) != 0 ||
        expect(result.type == AIVM_VAL_BYTES) != 0 ||
        expect(result.bytes_value.length == 8U * 1024U * 1024U) != 0) {
        aivm_dispose(&vm);
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int test_host_memory_growth_reserve_and_hysteresis(void)
{
    int suspended = 0;
    size_t gib = 1024U * 1024U * 1024U;
    if (expect(aivm_host_memory_growth_allowed(16U * gib, 4U * gib, 16U * 1024U, 0, &suspended) == 1) != 0 ||
        expect(suspended == 0) != 0 ||
        expect(aivm_host_memory_growth_allowed(16U * gib, 1U * gib, 16U * 1024U, 0, &suspended) == 0) != 0 ||
        expect(suspended == 1) != 0 ||
        expect(aivm_host_memory_growth_allowed(16U * gib, 2U * gib, 16U * 1024U, 1, &suspended) == 0) != 0 ||
        expect(suspended == 1) != 0 ||
        expect(aivm_host_memory_growth_allowed(16U * gib, 3U * gib, 16U * 1024U, 1, &suspended) == 1) != 0 ||
        expect(suspended == 0) != 0) {
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
    if (test_stub_opcode_sets_error() != 0) {
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
    if (test_gc_policy_constants_are_valid() != 0) {
        return 1;
    }
    if (test_reset_keeps_gc_allocation_counter_deterministic() != 0) {
        return 1;
    }
    if (test_reset_clears_gc_counters_after_allocations() != 0) {
        return 1;
    }
    if (test_gc_policy_requires_interval_even_under_pressure() != 0) {
        return 1;
    }
    if (test_gc_policy_does_not_compact_observational_pressure() != 0) {
        return 1;
    }
    if (test_gc_counters_saturate_without_wrapping() != 0) {
        return 1;
    }
    if (test_reset_clears_bytes_arena_after_syscall_materialization() != 0) {
        return 1;
    }
    if (test_reset_clears_pressure_counters_after_string_failure() != 0) {
        return 1;
    }
    if (test_reset_clears_pressure_counters_after_bytes_failure() != 0) {
        return 1;
    }
    if (test_reset_clears_pressure_counters_after_node_failure() != 0) {
        return 1;
    }
    if (test_pressure_counters_remain_zero_on_successful_run() != 0) {
        return 1;
    }
    if (test_tooling_profile_allocates_tooling_node_arenas() != 0) {
        return 1;
    }
    if (test_tooling_profile_materializes_large_bytes() != 0) {
        return 1;
    }
    if (test_host_memory_growth_reserve_and_hysteresis() != 0) {
        return 1;
    }

    return 0;
}
