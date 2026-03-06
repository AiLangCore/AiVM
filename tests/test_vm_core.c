#include <string.h>

#include "aivm_program.h"
#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

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

static int test_stub_opcode_sets_error(void)
{
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
    AivmInstruction instructions[(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS - 1U) * 3U + 1U];
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

static int test_gc_policy_triggers_when_interval_and_pressure_align(void)
{
    AivmVm vm;
    AivmInstruction instructions[(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS + 1U) * 3U + 1U];
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
    if (expect(vm.node_gc_compaction_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes == (size_t)AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_count == (size_t)(AIVM_VM_NODE_GC_PRESSURE_THRESHOLD + 1U)) != 0) {
        return 1;
    }

    return 0;
}

static int test_gc_counters_saturate_without_wrapping(void)
{
    AivmVm vm;
    AivmInstruction instructions[(AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS + 1U) * 3U + 1U];
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
    AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.bytes.fromBase64" },
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
        { "sys.bytes.fromBase64", host_core_bytes_small }
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
    AivmVm vm;
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
    vm.string_arena_used = AIVM_VM_STRING_ARENA_CAPACITY;
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
    AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.bytes.fromBase64" },
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
        { "sys.bytes.fromBase64", host_core_bytes_large }
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
    AivmVm vm;
    AivmInstruction instructions[(AIVM_VM_NODE_CAPACITY + 1U) * 2U + 1U];
    AivmValue constants[1];
    AivmProgram program;
    size_t ip = 0U;
    size_t i;

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
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_MEMORY_PRESSURE) != 0) {
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 1U) != 0) {
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

static int test_pressure_counters_remain_zero_on_successful_run(void)
{
    AivmVm vm;
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
    if (test_gc_policy_triggers_when_interval_and_pressure_align() != 0) {
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

    return 0;
}
