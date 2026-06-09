#include <stdio.h>
#include <string.h>
#include <time.h>

#include "aivm_program.h"
#include "aivm_vm.h"

static int expect_line(int condition, int line)
{
    if (condition) {
        return 0;
    }
    (void)fprintf(stderr, "expect failed at line %d\n", line);
    return 1;
}

#define expect(condition) expect_line((condition), __LINE__)

typedef int (*AivmTestFn)(void);

static int run_test(const char* name, AivmTestFn fn)
{
    (void)fprintf(stderr, "aivm_test_vm_ops: %s\n", name);
    (void)fflush(stderr);
    return fn();
}

static int host_ui_get_window_size(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_node(640480);
    return AIVM_SYSCALL_OK;
}

static int host_ui_draw_rect(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (arg_count != 6U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[5].type != AIVM_VAL_STRING) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int host_slow_noop(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    clock_t start;
    (void)target;
    (void)args;
    if (arg_count != 0U || result == NULL) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    start = clock();
    while (((double)(clock() - start) / (double)CLOCKS_PER_SEC) < 0.02) {
    }
    *result = aivm_value_int(0);
    return AIVM_SYSCALL_OK;
}

static int host_remote_call_echo_int(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args == NULL || arg_count != 3U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[0].type != AIVM_VAL_STRING ||
        args[1].type != AIVM_VAL_STRING ||
        args[2].type != AIVM_VAL_INT) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_int(args[2].int_value);
    return AIVM_SYSCALL_OK;
}

static int host_bytes_large(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static uint8_t large_bytes[AIVM_VM_BYTES_ARENA_CAPACITY + 1U];
    (void)target;
    (void)args;
    if (arg_count != 1U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[0].type != AIVM_VAL_STRING) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bytes(large_bytes, sizeof(large_bytes));
    return AIVM_SYSCALL_OK;
}

static int test_push_store_load_pop(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 41 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_POP, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 6U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.locals_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.locals[0].type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(vm.locals[0].int_value == 41) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(out.int_value == 41) != 0) {
        return 1;
    }

    return 0;
}

static int test_load_local_missing_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 }
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
    if (expect(vm.error == AIVM_VM_ERR_LOCAL_OUT_OF_RANGE) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "Invalid local slot. op=load index=0") != NULL) != 0) {
        return 1;
    }

    return 0;
}

static int test_add_int(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 2 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 3 },
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(out.int_value == 5) != 0) {
        return 1;
    }

    return 0;
}

static int test_add_int_type_mismatch_sets_error(void)
{
    static AivmVm vm;
    AivmValue non_int;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    (void)aivm_stack_push(&vm, aivm_value_int(1));
    non_int = aivm_value_string("x");
    (void)aivm_stack_push(&vm, non_int);
    aivm_step(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strncmp(aivm_vm_error_detail(&vm), "ADD_INT requires int operands.", 30U) == 0) != 0) {
        return 1;
    }

    return 0;
}

static int test_jump_skips_instruction(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_JUMP, .operand_int = 2 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 111 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 222 },
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
    if (expect(vm.stack_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.int_value == 222) != 0) {
        return 1;
    }
    return 0;
}

static int test_jump_if_false_takes_branch(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_BOOL, .operand_int = 0 },
        { .opcode = AIVM_OP_JUMP_IF_FALSE, .operand_int = 3 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 111 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 333 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 5U,
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.int_value == 333) != 0) {
        return 1;
    }
    return 0;
}

static int test_jump_if_false_type_mismatch_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_JUMP_IF_FALSE, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "JUMP_IF_FALSE requires bool.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_ret_roundtrip(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 7 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
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
    if (expect(vm.call_frame_count == 0U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(out.int_value == 7) != 0) {
        return 1;
    }

    return 0;
}

static int test_call_ret_rejects_extra_callee_stack_values(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 11 },
        { .opcode = AIVM_OP_CALL, .operand_int = 3 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 7 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 8 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 6U,
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
    if (expect(strstr(aivm_vm_error_detail(&vm), "Return restore invalid.") != NULL) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "extraStackValues=1") != NULL) != 0) {
        return 1;
    }
    return 0;
}

static int test_top_level_ret_halts(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
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

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }

    return 0;
}

static int test_top_level_return_alias_halts(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 }
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

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    return 0;
}

static int test_return_alias_roundtrip(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 }
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 9) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_target_equal_instruction_count_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CALL, .operand_int = 1 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Call target out of range.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_ret_restores_caller_locals_scope(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CALL, .operand_int = 7 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 99 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 11U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.locals_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_local_get(&vm, 1U, &out) == 0) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_recursive_tail_call_reuses_current_frame(void)
{
    static AivmVm vm;
    size_t i;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    for (i = 0U; i < 5000U; i += 1U) {
        if (vm.status != AIVM_VM_STATUS_READY && vm.status != AIVM_VM_STATUS_RUNNING) {
            break;
        }
        aivm_step(&vm);
    }
    if (expect(vm.status == AIVM_VM_STATUS_RUNNING) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.call_frame_count == 1U) != 0) {
        return 1;
    }
    return 0;
}

static int test_negative_jump_operand_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_JUMP, .operand_int = -1 },
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
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Negative operand is invalid.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_eq_int_true_false(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_EQ_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 6 },
        { .opcode = AIVM_OP_EQ_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 7U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 2U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_BOOL && out.bool_value == 0) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_BOOL && out.bool_value == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_eq_int_type_mismatch_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_PUSH_BOOL, .operand_int = 1 },
        { .opcode = AIVM_OP_EQ_INT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 3U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "EQ_INT requires int operands.") == 0) != 0) {
        return 1;
    }
    return 0;
}


static int test_eq_value_across_types(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
        { .opcode = AIVM_OP_EQ, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
        { .opcode = AIVM_OP_PUSH_BOOL, .operand_int = 1 },
        { .opcode = AIVM_OP_EQ, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 7U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 2U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_BOOL && out.bool_value == 0) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_BOOL && out.bool_value == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_eq_string_content_and_null_handling(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_EQ, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const char left_hello[] = { 'h', 'e', 'l', 'l', 'o', 0 };
    static const char right_hello[] = { 'h', 'e', 'l', 'l', 'o', 0 };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    (void)aivm_stack_push(&vm, aivm_value_string(left_hello));
    (void)aivm_stack_push(&vm, aivm_value_string(right_hello));
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_BOOL && out.bool_value == 1) != 0) {
        return 1;
    }

    aivm_init(&vm, &program);
    (void)aivm_stack_push(&vm, aivm_value_string((const char*)0));
    (void)aivm_stack_push(&vm, aivm_value_string((const char*)0));
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_BOOL && out.bool_value == 1) != 0) {
        return 1;
    }

    return 0;
}
static int test_eq_stack_underflow_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_EQ, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_STACK_UNDERFLOW) != 0) {
        return 1;
    }
    return 0;
}

static int test_const_pushes_program_constant(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 123 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 123) != 0) {
        return 1;
    }
    return 0;
}

static int test_str_concat_success(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "hello " },
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("hello world")) == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_str_concat_type_mismatch_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "x" },
        { .type = AIVM_VAL_INT, .int_value = 1 }
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
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "STR_CONCAT requires string operands.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_to_string_converts_scalar_values(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_TO_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_TO_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_TO_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_TO_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = -12 },
        { .type = AIVM_VAL_BOOL, .bool_value = 1 },
        { .type = AIVM_VAL_VOID, .int_value = 0 },
        { .type = AIVM_VAL_STRING, .string_value = "x" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 9U,
        .constants = constants,
        .constant_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("x")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("null")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("true")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("-12")) == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_to_string_null_string_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_TO_STRING, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = NULL }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "TO_STRING input string must be non-null.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_str_escape_escapes_special_chars(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_STR_ESCAPE, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "a\\b\"c\nd\re\tf" }
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("a\\\\b\\\"c\\nd\\re\\tf")) == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_str_escape_requires_string(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_STR_ESCAPE, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 7 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "STR_ESCAPE requires string operand.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_string_arena_overflow_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions_concat[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 }
    };
    static const AivmInstruction instructions_to_string[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_TO_STRING, .operand_int = 0 }
    };
    static const AivmInstruction instructions_escape[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_STR_ESCAPE, .operand_int = 0 }
    };
    static const AivmValue constants_concat[] = {
        { .type = AIVM_VAL_STRING, .string_value = "a" },
        { .type = AIVM_VAL_STRING, .string_value = "b" }
    };
    static const AivmValue constants_to_string[] = {
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmValue constants_escape[] = {
        { .type = AIVM_VAL_STRING, .string_value = "x" }
    };
    static const AivmProgram program_concat = {
        .instructions = instructions_concat,
        .instruction_count = 3U,
        .constants = constants_concat,
        .constant_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmProgram program_to_string = {
        .instructions = instructions_to_string,
        .instruction_count = 2U,
        .constants = constants_to_string,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmProgram program_escape = {
        .instructions = instructions_escape,
        .instruction_count = 2U,
        .constants = constants_escape,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program_concat);
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMM001: string arena capacity exceeded.") == 0) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 0U) != 0) {
        return 1;
    }

    aivm_init(&vm, &program_to_string);
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMM001: string arena capacity exceeded.") == 0) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 0U) != 0) {
        return 1;
    }

    aivm_init(&vm, &program_escape);
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMM001: string arena capacity exceeded.") == 0) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 1U) != 0) {
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

static int test_bytes_arena_overflow_sets_error(void)
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
        { "sys.fs.file.read", host_bytes_large }
    };

    aivm_init_with_syscalls(&vm, &program, bindings, 1U);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_MEMORY_PRESSURE) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMM002: bytes arena capacity exceeded.") == 0) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_used == 0U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_high_water == 0U) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 0U) != 0) {
        return 1;
    }
    return 0;
}

static int test_str_substring_and_remove_rune_clamp_semantics(void)
{
    static AivmVm vm;
    AivmValue out;
    static const char emoji_text[] = { 'a', (char)0xF0, (char)0x9F, (char)0x98, (char)0x80, 'b', '\0' };
    static const AivmInstruction instructions_substring[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_STR_SUBSTRING, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmInstruction instructions_remove[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_STR_REMOVE, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmInstruction instructions_clamp[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 4 },
        { .opcode = AIVM_OP_STR_SUBSTRING, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = emoji_text },
        { .type = AIVM_VAL_INT, .int_value = 1 },
        { .type = AIVM_VAL_INT, .int_value = 1 },
        { .type = AIVM_VAL_INT, .int_value = -5 },
        { .type = AIVM_VAL_INT, .int_value = 999 }
    };
    static const AivmProgram substring_program = {
        .instructions = instructions_substring,
        .instruction_count = 5U,
        .constants = constants,
        .constant_count = 5U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmProgram remove_program = {
        .instructions = instructions_remove,
        .instruction_count = 5U,
        .constants = constants,
        .constant_count = 5U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmProgram clamp_program = {
        .instructions = instructions_clamp,
        .instruction_count = 5U,
        .constants = constants,
        .constant_count = 5U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const char emoji_only[] = { (char)0xF0, (char)0x9F, (char)0x98, (char)0x80, '\0' };

    aivm_init(&vm, &substring_program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string(emoji_only)) == 1) != 0) {
        return 1;
    }

    aivm_init(&vm, &remove_program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("ab")) == 1) != 0) {
        return 1;
    }

    aivm_init(&vm, &clamp_program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string(emoji_text)) == 1) != 0) {
        return 1;
    }

    return 0;
}

static int test_str_substring_and_remove_type_mismatch(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_STR_SUBSTRING, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 1 },
        { .type = AIVM_VAL_INT, .int_value = 1 },
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 4U,
        .constants = constants,
        .constant_count = 3U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "STR_SUBSTRING requires (string,int,int).") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_str_substring_reuses_interned_results(void)
{
    static AivmVm vm;
    AivmVm baseline_vm;
    AivmValue second;
    AivmValue first;
    size_t baseline_arena_used;
    static const AivmInstruction baseline_instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_STR_SUBSTRING, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmInstruction repeated_instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_STR_SUBSTRING, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_STR_SUBSTRING, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "abcdef" },
        { .type = AIVM_VAL_INT, .int_value = 1 },
        { .type = AIVM_VAL_INT, .int_value = 2 }
    };
    static const AivmProgram baseline_program = {
        .instructions = baseline_instructions,
        .instruction_count = 5U,
        .constants = constants,
        .constant_count = 3U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmProgram repeated_program = {
        .instructions = repeated_instructions,
        .instruction_count = 9U,
        .constants = constants,
        .constant_count = 3U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&baseline_vm, &baseline_program);
    aivm_run(&baseline_vm);
    if (expect(baseline_vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    baseline_arena_used = baseline_vm.string_arena_used;

    aivm_init(&vm, &repeated_program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 2U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &second) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &first) == 1) != 0) {
        return 1;
    }
    if (expect(first.type == AIVM_VAL_STRING && strcmp(first.string_value, "bc") == 0) != 0) {
        return 1;
    }
    if (expect(second.type == AIVM_VAL_STRING && strcmp(second.string_value, "bc") == 0) != 0) {
        return 1;
    }
    if (expect(first.string_value == second.string_value) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_used == baseline_arena_used) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_success_and_void_result(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 6 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.ui.getWindowSize" },
        { .type = AIVM_VAL_STRING, .string_value = "sys.ui.drawRect" },
        { .type = AIVM_VAL_INT, .int_value = 1 },
        { .type = AIVM_VAL_STRING, .string_value = "#fff" }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.ui.getWindowSize", host_ui_get_window_size },
        { "sys.ui.drawRect", host_ui_draw_rect }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 12U,
        .constants = constants,
        .constant_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init_with_syscalls(&vm, &program, bindings, 2U);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 2U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_VOID) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(out.node_handle == 640480) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_failure_sets_vm_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.missing" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_SYSCALL) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMS004/AIVMC001: Syscall target was not found. target=sys.missing") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_elapsed_limit_sets_vm_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.time.nowUnixMs" }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.time.nowUnixMs", host_slow_noop }
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

    aivm_init_with_syscalls(&vm, &program, bindings, 1U);
    vm.syscall_elapsed_limit_ms = 1U;
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_SYSCALL) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "AIVMS007: Syscall resource limit exceeded.") != NULL) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "target=sys.time.nowUnixMs") != NULL) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_contract_type_mismatch_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CALL, .operand_int = 5 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 2 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 2 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 3 },
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.remote.call" },
        { .type = AIVM_VAL_STRING, .string_value = "cap.remote" },
        { .type = AIVM_VAL_INT, .int_value = 404 },
        { .type = AIVM_VAL_INT, .int_value = 2 }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.remote.call", host_remote_call_echo_int }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 14U,
        .constants = constants,
        .constant_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init_with_syscalls(&vm, &program, bindings, 1U);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_SYSCALL) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "AIVMS004/AIVMC003: Syscall argument type was invalid.") != NULL) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "target=sys.remote.call") != NULL) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "local0=string(\"cap.remote\")") != NULL) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_missing_binding_sets_unbound_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 3 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.remote.call" },
        { .type = AIVM_VAL_STRING, .string_value = "cap.remote" },
        { .type = AIVM_VAL_STRING, .string_value = "echoInt" },
        { .type = AIVM_VAL_INT, .int_value = 7 }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.ui.createWindow", host_remote_call_echo_int }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 5U,
        .constants = constants,
        .constant_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init_with_syscalls(&vm, &program, bindings, 1U);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_SYSCALL) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "AIVMS006: Syscall target is known but has no host binding.") != NULL) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "target=sys.remote.call") != NULL) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_does_not_recover_non_syscall_string_target_from_args(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "not_a_syscall" },
        { .type = AIVM_VAL_STRING, .string_value = "sys.remote.call" }
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

    aivm_init_with_syscalls(&vm, &program, NULL, 0U);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_SYSCALL) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "rawTarget=not_a_syscall") != NULL) != 0) {
        return 1;
    }
    return 0;
}

static int test_nested_call_preserves_argument_without_tail_call_reuse(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_CALL, .operand_int = 3 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CALL, .operand_int = 7 },
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 },
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 10U,
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(out.int_value == 5) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_debug_task_reclaim_stats_intrinsic(void)
{
    static AivmVm vm;
    AivmValue out;
    const AivmNodeRecord* stats_node;
    const AivmNodeAttr* attr0;
    const AivmNodeAttr* attr1;
    const AivmNodeAttr* attr2;
    size_t i;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 4 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.debug.taskReclaimStats" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 6U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_set_runtime_profile(&vm, AIVM_RUNTIME_PROFILE_DEBUG);
    vm.completed_task_count = AIVM_VM_TASK_CAPACITY;
    vm.next_task_handle = (int64_t)AIVM_VM_TASK_CAPACITY + 1;
    for (i = 0U; i < AIVM_VM_TASK_CAPACITY; i += 1U) {
        vm.completed_tasks[i].state = AIVM_TASK_STATE_COMPLETED;
        vm.completed_tasks[i].handle = (int64_t)i + 1;
        vm.completed_tasks[i].result = aivm_value_int(-((int64_t)i + 1));
        vm.completed_tasks[i].worker_context = NULL;
    }

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    stats_node = &vm.nodes[(size_t)(out.node_handle - 1)];
    if (expect(strcmp(stats_node->kind, "DebugTaskReclaimStats") == 0) != 0) {
        return 1;
    }
    if (expect(strncmp(stats_node->id, "debug_task_reclaim_stats_", 25U) == 0) != 0) {
        return 1;
    }
    if (expect(stats_node->attr_count == 3U) != 0) {
        return 1;
    }
    attr0 = &vm.node_attrs[stats_node->attr_start];
    attr1 = &vm.node_attrs[stats_node->attr_start + 1U];
    attr2 = &vm.node_attrs[stats_node->attr_start + 2U];
    if (expect(strcmp(attr0->key, "reclaimed") == 0 && attr0->kind == AIVM_NODE_ATTR_INT && attr0->int_value == 1) != 0) {
        return 1;
    }
    if (expect(strcmp(attr1->key, "skipPinned") == 0 && attr1->kind == AIVM_NODE_ATTR_INT && attr1->int_value == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(attr2->key, "exhausted") == 0 && attr2->kind == AIVM_NODE_ATTR_INT && attr2->int_value == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_debug_task_reclaim_stats_arity_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.debug.taskReclaimStats" },
        { .type = AIVM_VAL_INT, .int_value = 7 }
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
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_SYSCALL) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMS004/AIVMC002: Syscall argument count was invalid.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_and_await_roundtrip(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 4 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 6U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 9) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_starts_pending_worker_task(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 4U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.completed_task_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.completed_tasks[0].state == AIVM_TASK_STATE_PENDING) != 0) {
        return 1;
    }
    if (expect(vm.completed_tasks[0].worker_context != NULL) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == vm.completed_tasks[0].handle) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int test_async_call_copies_string_result_across_worker_boundary(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 4 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "worker-string" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 6U,
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(strcmp(out.string_value, "worker-string") == 0) != 0) {
        return 1;
    }
    if (expect(out.string_value != constants[0].string_value) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_invalid_target_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 999 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 1U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Invalid function index.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_copies_node_result_across_worker_boundary(void)
{
    static AivmVm vm;
    AivmValue out;
    const AivmNodeRecord* node;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 4 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "worker-node" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 7U,
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(out.node_handle > 0 && (size_t)out.node_handle <= vm.node_count) != 0) {
        return 1;
    }
    node = &vm.nodes[(size_t)(out.node_handle - 1)];
    if (expect(strcmp(node->kind, "Block") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(node->id, "worker-node") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_target_equal_instruction_count_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 1 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 1U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Invalid function index.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_task_handle_overflow_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 4U);
    aivm_init(&vm, &program);
    vm.next_task_handle = INT64_MAX;
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Task handle overflow.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_rejects_extra_callee_stack_values(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 3 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 6 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 6U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "Return restore invalid.") != NULL) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_rejects_invalid_call_target_layout(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 7 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 8 },
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 4 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 8U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "Call target local layout invalid.") != NULL) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_reclaims_oldest_task_slot_when_full(void)
{
    static AivmVm vm;
    AivmValue out;
    size_t i;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 4U);
    aivm_init(&vm, &program);
    vm.completed_task_count = AIVM_VM_TASK_CAPACITY;
    vm.next_task_handle = (int64_t)AIVM_VM_TASK_CAPACITY + 1;
    for (i = 0U; i < AIVM_VM_TASK_CAPACITY; i += 1U) {
        vm.completed_tasks[i].state = AIVM_TASK_STATE_COMPLETED;
        vm.completed_tasks[i].handle = (int64_t)i + 1;
        vm.completed_tasks[i].result = aivm_value_int(-((int64_t)i + 1));
        vm.completed_tasks[i].worker_context = NULL;
    }

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.completed_task_count == AIVM_VM_TASK_CAPACITY) != 0) {
        return 1;
    }
    if (expect(vm.completed_tasks[0].handle == 2) != 0) {
        return 1;
    }
    if (expect(vm.completed_tasks[AIVM_VM_TASK_CAPACITY - 1U].handle == (int64_t)AIVM_VM_TASK_CAPACITY + 1) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_skip_pinned_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_exhausted_count == 0U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == (int64_t)AIVM_VM_TASK_CAPACITY + 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_await_evicted_task_handle_sets_error(void)
{
    static AivmVm vm;
    size_t i;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 4 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 6U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    vm.completed_task_count = AIVM_VM_TASK_CAPACITY;
    vm.next_task_handle = (int64_t)AIVM_VM_TASK_CAPACITY + 1;
    for (i = 0U; i < AIVM_VM_TASK_CAPACITY; i += 1U) {
        vm.completed_tasks[i].state = AIVM_TASK_STATE_COMPLETED;
        vm.completed_tasks[i].handle = (int64_t)i + 1;
        vm.completed_tasks[i].result = aivm_value_int(-((int64_t)i + 1));
        vm.completed_tasks[i].worker_context = NULL;
    }

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AWAIT requires valid task handle.") == 0) != 0) {
        return 1;
    }
    aivm_dispose(&vm);
    return 0;
}

static int test_async_call_reclaim_skips_pinned_oldest_handle(void)
{
    static AivmVm vm;
    size_t i;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 4U);
    aivm_init(&vm, &program);
    vm.completed_task_count = AIVM_VM_TASK_CAPACITY;
    vm.next_task_handle = (int64_t)AIVM_VM_TASK_CAPACITY + 1;
    for (i = 0U; i < AIVM_VM_TASK_CAPACITY; i += 1U) {
        vm.completed_tasks[i].state = AIVM_TASK_STATE_COMPLETED;
        vm.completed_tasks[i].handle = (int64_t)i + 1;
        vm.completed_tasks[i].result = aivm_value_int(-((int64_t)i + 1));
        vm.completed_tasks[i].worker_context = NULL;
    }
    vm.stack_count = 1U;
    vm.stack[0] = aivm_value_int(1);

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.completed_task_count == AIVM_VM_TASK_CAPACITY) != 0) {
        return 1;
    }
    if (expect(vm.completed_tasks[0].handle == 1) != 0) {
        return 1;
    }
    if (expect(vm.completed_tasks[1].handle == 3) != 0) {
        return 1;
    }
    if (expect(vm.completed_tasks[AIVM_VM_TASK_CAPACITY - 1U].handle == (int64_t)AIVM_VM_TASK_CAPACITY + 1) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_skip_pinned_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_exhausted_count == 0U) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_full_table_all_pinned_sets_capacity_error(void)
{
    static AivmVm vm;
    size_t i;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;
    aivm_program_init(&program, &instructions[0], 4U);
    aivm_init(&vm, &program);
    vm.completed_task_count = AIVM_VM_TASK_CAPACITY;
    vm.next_task_handle = (int64_t)AIVM_VM_TASK_CAPACITY + 1;
    vm.stack_count = AIVM_VM_TASK_CAPACITY;
    for (i = 0U; i < AIVM_VM_TASK_CAPACITY; i += 1U) {
        vm.completed_tasks[i].state = AIVM_TASK_STATE_COMPLETED;
        vm.completed_tasks[i].handle = (int64_t)i + 1;
        vm.completed_tasks[i].result = aivm_value_int(-((int64_t)i + 1));
        vm.completed_tasks[i].worker_context = NULL;
        vm.stack[i] = aivm_value_int((int64_t)i + 1);
    }

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Task table capacity exceeded.") == 0) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_skip_pinned_count == AIVM_VM_TASK_CAPACITY) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_exhausted_count == 1U) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_sys_and_await_roundtrip(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_ASYNC_CALL_SYS, .operand_int = 3 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.remote.call" },
        { .type = AIVM_VAL_STRING, .string_value = "cap.remote" },
        { .type = AIVM_VAL_STRING, .string_value = "echoInt" },
        { .type = AIVM_VAL_INT, .int_value = 7 }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.remote.call", host_remote_call_echo_int }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 7U,
        .constants = constants,
        .constant_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init_with_syscalls(&vm, &program, bindings, 1U);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 7) != 0) {
        return 1;
    }
    return 0;
}

static int test_await_invalid_handle_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 999 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .constants = constants,
        .constant_count = 1U,
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AWAIT requires valid task handle.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_await_pending_task_handle_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    vm.completed_task_count = 1U;
    vm.completed_tasks[0].state = AIVM_TASK_STATE_PENDING;
    vm.completed_tasks[0].handle = 1;
    vm.completed_tasks[0].result = aivm_value_int(42);
    vm.completed_tasks[0].worker_context = NULL;

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AWAIT requires valid task handle.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_await_failed_task_handle_returns_terminal_result(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 1 }
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
    vm.completed_task_count = 1U;
    vm.completed_tasks[0].state = AIVM_TASK_STATE_FAILED;
    vm.completed_tasks[0].handle = 1;
    vm.node_count = 1U;
    vm.nodes[0].kind = "Err";
    vm.nodes[0].id = "err1";
    vm.nodes[0].attr_start = 0U;
    vm.nodes[0].attr_count = 0U;
    vm.nodes[0].child_start = 0U;
    vm.nodes[0].child_count = 0U;
    vm.completed_tasks[0].result = aivm_value_node(1);
    vm.completed_tasks[0].worker_context = NULL;

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_NODE && out.node_handle == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_await_failed_task_non_err_result_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    vm.completed_task_count = 1U;
    vm.completed_tasks[0].state = AIVM_TASK_STATE_FAILED;
    vm.completed_tasks[0].handle = 1;
    vm.completed_tasks[0].result = aivm_value_int(-11);
    vm.completed_tasks[0].worker_context = NULL;

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Terminal failed/canceled task requires Err node result.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_terminal_task_cleanup_stress(void)
{
    enum { terminal_task_count = 64 };
    static AivmVm vm;
    static AivmInstruction instructions[(terminal_task_count * 3) + 1];
    static AivmValue constants[terminal_task_count];
    AivmProgram program;
    size_t ip = 0U;
    size_t i;

    for (i = 0U; i < terminal_task_count; i += 1U) {
        constants[i] = aivm_value_int((int64_t)i + 1);
        instructions[ip].opcode = AIVM_OP_CONST;
        instructions[ip].operand_int = (int64_t)i;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_AWAIT;
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
    program.constant_count = terminal_task_count;

    aivm_init(&vm, &program);
    vm.completed_task_count = terminal_task_count;
    vm.next_task_handle = (int64_t)terminal_task_count + 1;
    vm.node_count = terminal_task_count + 1U;
    for (i = 0U; i < terminal_task_count; i += 1U) {
        vm.completed_tasks[i].state = (i % 2U == 0U) ? AIVM_TASK_STATE_FAILED : AIVM_TASK_STATE_CANCELED;
        vm.completed_tasks[i].handle = (int64_t)i + 1;
        vm.completed_tasks[i].result = aivm_value_node((int64_t)i + 2);
        vm.completed_tasks[i].worker_context = NULL;
        vm.nodes[i + 1U].kind = "Err";
        vm.nodes[i + 1U].id = "terminal_task_err";
        vm.nodes[i + 1U].attr_start = 0U;
        vm.nodes[i + 1U].attr_count = 0U;
        vm.nodes[i + 1U].child_start = 0U;
        vm.nodes[i + 1U].child_count = 0U;
    }

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.completed_task_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_count == terminal_task_count) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_skip_pinned_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.task_reclaim_exhausted_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count >= terminal_task_count) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes >= terminal_task_count - 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_count <= 2U) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 0U) != 0) {
        return 1;
    }
    return 0;
}

static int test_parallel_join_resolves_canceled_task_handles(void)
{
    static AivmVm vm;
    AivmValue out;
    const AivmNodeRecord* block_node;
    const AivmNodeRecord* child;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PAR_BEGIN, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_PAR_FORK, .operand_int = 0 },
        { .opcode = AIVM_OP_PAR_JOIN, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 5U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    vm.completed_task_count = 1U;
    vm.completed_tasks[0].state = AIVM_TASK_STATE_CANCELED;
    vm.completed_tasks[0].handle = 1;
    vm.node_count = 1U;
    vm.nodes[0].kind = "Err";
    vm.nodes[0].id = "err_cancel";
    vm.nodes[0].attr_start = 0U;
    vm.nodes[0].attr_count = 0U;
    vm.nodes[0].child_start = 0U;
    vm.nodes[0].child_count = 0U;
    vm.completed_tasks[0].result = aivm_value_node(1);
    vm.completed_tasks[0].worker_context = NULL;

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    block_node = &vm.nodes[(size_t)(out.node_handle - 1)];
    if (expect(block_node->child_count == 1U) != 0) {
        return 1;
    }
    child = &vm.nodes[(size_t)(vm.node_children[block_node->child_start] - 1)];
    if (expect(strcmp(child->kind, "Err") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_parallel_join_failed_task_non_err_result_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PAR_BEGIN, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_PAR_FORK, .operand_int = 0 },
        { .opcode = AIVM_OP_PAR_JOIN, .operand_int = 1 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 4U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    vm.completed_task_count = 1U;
    vm.completed_tasks[0].state = AIVM_TASK_STATE_FAILED;
    vm.completed_tasks[0].handle = 1;
    vm.completed_tasks[0].result = aivm_value_int(-99);
    vm.completed_tasks[0].worker_context = NULL;

    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Terminal failed/canceled task requires Err node result.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_parallel_begin_fork_join_and_cancel(void)
{
    static AivmVm vm;
    AivmValue out;
    const AivmNodeRecord* block_node;
    const AivmNodeRecord* child0;
    const AivmNodeRecord* child1;
    const AivmNodeAttr* child0_attr;
    const AivmNodeAttr* child1_attr;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PAR_BEGIN, .operand_int = 2 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 41 },
        { .opcode = AIVM_OP_PAR_FORK, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_PAR_FORK, .operand_int = 0 },
        { .opcode = AIVM_OP_PAR_JOIN, .operand_int = 2 },
        { .opcode = AIVM_OP_PAR_CANCEL, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    AivmProgram program;

    aivm_program_init(&program, instructions, 8U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(out.node_handle > 0) != 0) {
        return 1;
    }
    block_node = &vm.nodes[(size_t)(out.node_handle - 1)];
    if (expect(strcmp(block_node->kind, "Block") == 0) != 0) {
        return 1;
    }
    if (expect(strncmp(block_node->id, "par_", 4U) == 0) != 0) {
        return 1;
    }
    if (expect(block_node->child_count == 2U) != 0) {
        return 1;
    }
    child0 = &vm.nodes[(size_t)(vm.node_children[block_node->child_start] - 1)];
    child1 = &vm.nodes[(size_t)(vm.node_children[block_node->child_start + 1U] - 1)];
    if (expect(strcmp(child0->kind, "Lit") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(child1->kind, "Lit") == 0) != 0) {
        return 1;
    }
    child0_attr = &vm.node_attrs[child0->attr_start];
    child1_attr = &vm.node_attrs[child1->attr_start];
    if (expect(child0_attr->kind == AIVM_NODE_ATTR_INT && child0_attr->int_value == 41) != 0) {
        return 1;
    }
    if (expect(child1_attr->kind == AIVM_NODE_ATTR_INT && child1_attr->int_value == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_parallel_join_mismatch_sets_error(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PAR_BEGIN, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 10 },
        { .opcode = AIVM_OP_PAR_FORK, .operand_int = 0 },
        { .opcode = AIVM_OP_PAR_JOIN, .operand_int = 2 }
    };
    AivmProgram program;

    aivm_program_init(&program, instructions, 4U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "PAR_JOIN branch count mismatch.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_parallel_join_resolves_completed_task_handles(void)
{
    static AivmVm vm;
    AivmValue out;
    const AivmNodeRecord* block_node;
    const AivmNodeRecord* child;
    const AivmNodeAttr* child_attr;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PAR_BEGIN, .operand_int = 1 },
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 5 },
        { .opcode = AIVM_OP_PAR_FORK, .operand_int = 0 },
        { .opcode = AIVM_OP_PAR_JOIN, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 77 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    AivmProgram program;

    aivm_program_init(&program, instructions, 7U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    block_node = &vm.nodes[(size_t)(out.node_handle - 1)];
    if (expect(strcmp(block_node->kind, "Block") == 0) != 0) {
        return 1;
    }
    if (expect(block_node->child_count == 1U) != 0) {
        return 1;
    }
    child = &vm.nodes[(size_t)(vm.node_children[block_node->child_start] - 1)];
    if (expect(strcmp(child->kind, "Lit") == 0) != 0) {
        return 1;
    }
    child_attr = &vm.node_attrs[child->attr_start];
    if (expect(child_attr->kind == AIVM_NODE_ATTR_INT && child_attr->int_value == 77) != 0) {
        return 1;
    }
    return 0;
}

static int test_parallel_fork_requires_context(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 10 },
        { .opcode = AIVM_OP_PAR_FORK, .operand_int = 0 }
    };
    AivmProgram program;

    aivm_program_init(&program, instructions, 2U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "PAR_FORK requires active Par context.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_parallel_join_requires_context(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PAR_JOIN, .operand_int = 0 }
    };
    AivmProgram program;

    aivm_program_init(&program, instructions, 1U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "PAR_JOIN requires active Par context.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_str_utf8_byte_count(void)
{
    static AivmVm vm;
    AivmValue out;
    static const char emoji_text[] = { (char)0xF0, (char)0x9F, (char)0x98, (char)0x80, '\0' };
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_STR_UTF8_BYTE_COUNT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = emoji_text }
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 4) != 0) {
        return 1;
    }
    return 0;
}

static int test_str_utf8_byte_count_requires_string(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_STR_UTF8_BYTE_COUNT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_INT, .int_value = 9 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "STR_UTF8_BYTE_COUNT requires string operand.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_node_ops_core_semantics(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_NODE_KIND, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_NODE_ID, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_MAKE_LIT_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 4 },
        { .opcode = AIVM_OP_MAKE_LIT_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 2 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_APPEND_CHILD, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 2 },
        { .opcode = AIVM_OP_APPEND_CHILD, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CHILD_COUNT, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 5 },
        { .opcode = AIVM_OP_CONST, .operand_int = 6 },
        { .opcode = AIVM_OP_CONST, .operand_int = 7 },
        { .opcode = AIVM_OP_CONST, .operand_int = 8 },
        { .opcode = AIVM_OP_MAKE_ERR, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 3 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 3 },
        { .opcode = AIVM_OP_ATTR_COUNT, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 9 },
        { .opcode = AIVM_OP_ATTR_KEY, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 9 },
        { .opcode = AIVM_OP_ATTR_VALUE_KIND, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 9 },
        { .opcode = AIVM_OP_ATTR_VALUE_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 10 },
        { .opcode = AIVM_OP_ATTR_VALUE_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 10 },
        { .opcode = AIVM_OP_ATTR_VALUE_BOOL, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "blk1" },
        { .type = AIVM_VAL_STRING, .string_value = "lit_s" },
        { .type = AIVM_VAL_STRING, .string_value = "hello" },
        { .type = AIVM_VAL_STRING, .string_value = "lit_i" },
        { .type = AIVM_VAL_INT, .int_value = 42 },
        { .type = AIVM_VAL_STRING, .string_value = "err1" },
        { .type = AIVM_VAL_STRING, .string_value = "VAL999" },
        { .type = AIVM_VAL_STRING, .string_value = "boom" },
        { .type = AIVM_VAL_STRING, .string_value = "n42" },
        { .type = AIVM_VAL_INT, .int_value = 0 },
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 49U,
        .constants = constants,
        .constant_count = 11U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }

    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(out.type == AIVM_VAL_BOOL && out.bool_value == 0) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(out.type == AIVM_VAL_INT && out.int_value == 0) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("VAL999")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("identifier")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("code")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(out.type == AIVM_VAL_INT && out.int_value == 3) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(out.type == AIVM_VAL_INT && out.int_value == 2) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("blk1")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("Block")) == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_make_node_from_template_and_children(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_MAKE_LIT_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_NODE, .operand_int = 0 },
        { .opcode = AIVM_OP_CHILD_COUNT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "tmpl" },
        { .type = AIVM_VAL_STRING, .string_value = "c1" },
        { .type = AIVM_VAL_STRING, .string_value = "value" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 13U,
        .constants = constants,
        .constant_count = 3U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_make_node_empty_from_kind_and_id(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_NODE_EMPTY, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_NODE_KIND, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_NODE_ID, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "Program" },
        { .type = AIVM_VAL_STRING, .string_value = "p1" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 9U,
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("p1")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("Program")) == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_string_compaction_preserves_live_node_strings(void)
{
    static AivmVm vm;
    AivmValue out;
    char* arena_before_compaction;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_NODE_EMPTY, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_STR_CONCAT, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_NODE_KIND, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_NODE_ID, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "Program" },
        { .type = AIVM_VAL_STRING, .string_value = "p1" },
        { .type = AIVM_VAL_STRING, .string_value = "force" },
        { .type = AIVM_VAL_STRING, .string_value = "compaction" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 13U,
        .constants = constants,
        .constant_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    while (vm.status != AIVM_VM_STATUS_HALTED &&
           vm.status != AIVM_VM_STATUS_ERROR &&
           vm.instruction_pointer < 4U) {
        aivm_step(&vm);
    }
    if (expect(vm.status == AIVM_VM_STATUS_RUNNING) != 0) {
        return 1;
    }
    arena_before_compaction = vm.string_arena;
    vm.string_arena_limit = vm.string_arena_used + 1U;
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.string_arena != arena_before_compaction) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("p1")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("Program")) == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_append_attr_adds_lit_attr_to_node(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_NODE_EMPTY, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_MAKE_LIT_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_APPEND_ATTR, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_ATTR_COUNT, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_ATTR_VALUE_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "Export" },
        { .type = AIVM_VAL_STRING, .string_value = "e1" },
        { .type = AIVM_VAL_STRING, .string_value = "name" },
        { .type = AIVM_VAL_STRING, .string_value = "start" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 14U,
        .constants = constants,
        .constant_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(aivm_value_equals(out, aivm_value_string("start")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0 ||
        expect(out.type == AIVM_VAL_INT && out.int_value == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_make_node_converts_scalar_children_to_runtime_nodes(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 42 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_NODE, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CHILD_AT, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_ATTR_VALUE_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "tmpl" },
        { .type = AIVM_VAL_INT, .int_value = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 12U,
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
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 42) != 0) {
        return 1;
    }
    return 0;
}

static int test_node_compaction_reclaims_unreachable_nodes(void)
{
    static AivmVm vm;
    static AivmInstruction instructions[(AIVM_VM_NODE_CAPACITY + 32U) * 3U + 3U];
    AivmValue constants[1];
    AivmProgram program;
    size_t ip = 0U;
    size_t i;
    size_t transient_nodes = AIVM_VM_NODE_CAPACITY + 32U;

    constants[0] = aivm_value_string("tmp");
    for (i = 0U; i < transient_nodes; i += 1U) {
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
    instructions[ip].opcode = AIVM_OP_CONST;
    instructions[ip].operand_int = 0;
    ip += 1U;
    instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
    instructions[ip].operand_int = 0;
    ip += 1U;
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

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.node_count <= AIVM_VM_NODE_CAPACITY) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count > 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count >= vm.node_gc_compaction_count) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes > 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_high_water >= vm.node_count) != 0) {
        return 1;
    }
    return 0;
}

static int test_node_compaction_runs_before_capacity_when_pressure_is_high(void)
{
    static AivmVm vm;
    static AivmInstruction instructions[(AIVM_VM_NODE_CAPACITY - 8U) * 3U + 3U];
    AivmValue constants[1];
    AivmProgram program;
    size_t ip = 0U;
    size_t i;
    size_t transient_nodes = AIVM_VM_NODE_CAPACITY - 8U;
    size_t expected_alloc_counter_after_compaction =
        transient_nodes - (size_t)AIVM_VM_NODE_GC_PRESSURE_THRESHOLD + 2U;

    constants[0] = aivm_value_string("tmp");
    for (i = 0U; i < transient_nodes; i += 1U) {
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
    instructions[ip].opcode = AIVM_OP_CONST;
    instructions[ip].operand_int = 0;
    ip += 1U;
    instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
    instructions[ip].operand_int = 0;
    ip += 1U;
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

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count > 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count >= vm.node_gc_compaction_count) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count >= 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes > 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc > 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == expected_alloc_counter_after_compaction) != 0) {
        return 1;
    }
    if (expect(vm.node_high_water < AIVM_VM_NODE_CAPACITY) != 0) {
        return 1;
    }
    return 0;
}

static int test_node_compaction_does_not_run_below_pressure_threshold(void)
{
    static AivmVm vm;
    static AivmInstruction instructions[((size_t)AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 2U) * 3U + 3U];
    AivmValue constants[1];
    AivmProgram program;
    size_t ip = 0U;
    size_t i;
    size_t transient_nodes = (size_t)AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 2U;
    size_t expected_node_count = (size_t)AIVM_VM_NODE_GC_PRESSURE_THRESHOLD;
    size_t expected_alloc_counter = (size_t)AIVM_VM_NODE_GC_PRESSURE_THRESHOLD - 1U;

    constants[0] = aivm_value_string("tmp");
    for (i = 0U; i < transient_nodes; i += 1U) {
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
    instructions[ip].opcode = AIVM_OP_CONST;
    instructions[ip].operand_int = 0;
    ip += 1U;
    instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
    instructions[ip].operand_int = 0;
    ip += 1U;
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
    if (expect(vm.node_count == expected_node_count) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == expected_alloc_counter) != 0) {
        return 1;
    }
    return 0;
}

static int test_safe_point_compaction_reclaims_below_pressure_threshold(void)
{
    static AivmVm vm;
    static AivmInstruction instructions[128U * 3U + 3U];
    AivmValue constants[1];
    AivmProgram program;
    AivmValue out;
    const AivmNodeRecord* node;
    size_t ip = 0U;
    size_t i;
    size_t transient_nodes = 128U;

    constants[0] = aivm_value_string("tmp");
    for (i = 0U; i < transient_nodes; i += 1U) {
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
    instructions[ip].opcode = AIVM_OP_CONST;
    instructions[ip].operand_int = 0;
    ip += 1U;
    instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
    instructions[ip].operand_int = 0;
    ip += 1U;
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

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_count == transient_nodes + 2U) != 0) {
        return 1;
    }
    if (expect(aivm_collect_safe_point(&vm) == 1) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes == transient_nodes) != 0) {
        return 1;
    }
    if (expect(vm.node_count >= 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == 0U) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_NODE && out.node_handle == 2) != 0) {
        return 1;
    }
    node = &vm.nodes[(size_t)(out.node_handle - 1)];
    if (expect(strcmp(node->kind, "Block") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(node->id, "tmp") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_return_boundary_runs_safe_point_compaction(void)
{
    static AivmVm vm;
    static AivmInstruction instructions[1100U * 3U + 7U];
    AivmValue constants[1];
    AivmProgram program;
    AivmValue out;
    const AivmNodeRecord* node;
    size_t ip = 0U;
    size_t function_target;
    size_t i;
    size_t transient_nodes = 1100U;

    constants[0] = aivm_value_string("tmp");
    instructions[ip].opcode = AIVM_OP_CALL;
    instructions[ip].operand_int = 2;
    ip += 1U;
    instructions[ip].opcode = AIVM_OP_HALT;
    instructions[ip].operand_int = 0;
    ip += 1U;
    function_target = ip;
    for (i = 0U; i < transient_nodes; i += 1U) {
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
    instructions[ip].opcode = AIVM_OP_CONST;
    instructions[ip].operand_int = 0;
    ip += 1U;
    instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
    instructions[ip].operand_int = 0;
    ip += 1U;
    instructions[ip].opcode = AIVM_OP_RET;
    instructions[ip].operand_int = 0;
    ip += 1U;

    memset(&program, 0, sizeof(program));
    program.instructions = instructions;
    program.instruction_count = ip;
    program.constants = constants;
    program.constant_count = 1U;

    if (expect(function_target == 2U) != 0) {
        return 1;
    }
    aivm_init(&vm, &program);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes == transient_nodes) != 0) {
        return 1;
    }
    if (expect(vm.node_count == 2U) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == 0U) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_NODE && out.node_handle == 2) != 0) {
        return 1;
    }
    node = &vm.nodes[(size_t)(out.node_handle - 1)];
    if (expect(strcmp(node->kind, "Block") == 0) != 0) {
        return 1;
    }
    if (expect(strcmp(node->id, "tmp") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_node_compaction_runs_on_child_pressure_before_node_threshold(void)
{
    static AivmVm vm;
    size_t persistent_children = 17U;
    size_t transient_maps = ((size_t)AIVM_VM_NODE_CHILD_GC_PRESSURE_THRESHOLD / persistent_children) + 1U;
    static AivmInstruction instructions[17U * 3U + ((((size_t)AIVM_VM_NODE_CHILD_GC_PRESSURE_THRESHOLD / 17U) + 1U) * 20U) + 1U];
    AivmValue constants[1];
    AivmProgram program;
    size_t ip = 0U;
    size_t i;
    size_t j;

    constants[0] = aivm_value_string("child");
    for (i = 0U; i < persistent_children; i += 1U) {
        instructions[ip].opcode = AIVM_OP_CONST;
        instructions[ip].operand_int = 0;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_MAKE_BLOCK;
        instructions[ip].operand_int = 0;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_STORE_LOCAL;
        instructions[ip].operand_int = (int32_t)i;
        ip += 1U;
    }
    for (i = 0U; i < transient_maps; i += 1U) {
        for (j = 0U; j < persistent_children; j += 1U) {
            instructions[ip].opcode = AIVM_OP_LOAD_LOCAL;
            instructions[ip].operand_int = (int32_t)j;
            ip += 1U;
        }
        instructions[ip].opcode = AIVM_OP_PUSH_INT;
        instructions[ip].operand_int = (int32_t)persistent_children;
        ip += 1U;
        instructions[ip].opcode = AIVM_OP_MAKE_MAP;
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

    aivm_init(&vm, &program);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count > 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count >= vm.node_gc_compaction_count) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes > 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_high_water < (size_t)AIVM_VM_NODE_GC_PRESSURE_THRESHOLD) != 0) {
        return 1;
    }
    return 0;
}

static int test_node_capacity_failure_resets_gc_allocation_counter(void)
{
    static AivmVm vm;
    static AivmInstruction instructions[(AIVM_VM_NODE_CAPACITY + 1U) * 2U + 1U];
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMM005: node arena capacity exceeded.") == 0) != 0) {
        return 1;
    }
    if (expect(vm.string_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.bytes_arena_pressure_count == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_arena_pressure_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count >= 1U) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_attempt_count >= vm.node_gc_compaction_count) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_reclaimed_nodes == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == 0U) != 0) {
        return 1;
    }
    if (expect(vm.node_count <= AIVM_VM_NODE_CAPACITY) != 0) {
        return 1;
    }
    return 0;
}

static int test_make_node_requires_node_args(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_NODE, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "tmpl" },
        { .type = AIVM_VAL_STRING, .string_value = "not_node" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 5U,
        .constants = constants,
        .constant_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "MAKE_NODE requires (node,int>=0).") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_make_node_empty_requires_string_args(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_NODE_EMPTY, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "p1" }
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
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "MAKE_NODE_EMPTY requires (string,string).") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_append_attr_requires_single_attr_node(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_NODE_EMPTY, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_APPEND_ATTR, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "Export" },
        { .type = AIVM_VAL_STRING, .string_value = "e1" },
        { .type = AIVM_VAL_STRING, .string_value = "not_attr" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 6U,
        .constants = constants,
        .constant_count = 3U,
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "APPEND_ATTR requires single-attr node and capacity.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_make_lit_string_requires_string_id(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_LIT_STRING, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "value" }
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
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "MAKE_LIT_STRING requires (string,string).") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_make_block_requires_string_id(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 }
    };
    AivmProgram program;

    aivm_program_init(&program, instructions, 2U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "MAKE_BLOCK requires string id.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_child_at_out_of_range_sets_error_detail(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CHILD_AT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "n1" },
        { .type = AIVM_VAL_INT, .int_value = 0 }
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
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "CHILD_AT index out of range.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_append_child_requires_node_operands(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_APPEND_CHILD, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "n1" },
        { .type = AIVM_VAL_STRING, .string_value = "not_a_node" }
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
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "APPEND_CHILD requires (node,node).") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_node_kind_requires_node_operand(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 9 },
        { .opcode = AIVM_OP_NODE_KIND, .operand_int = 0 }
    };
    AivmProgram program;

    aivm_program_init(&program, instructions, 2U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "NODE_KIND requires node operand.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_attr_key_requires_node_and_index(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_ATTR_KEY, .operand_int = 0 }
    };
    AivmProgram program;

    aivm_program_init(&program, instructions, 3U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "ATTR_KEY requires (node,int).") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int run_attr_requires_node_and_index_test(AivmOpcode opcode, const char* expected_message)
{
    static AivmVm vm;
    static AivmInstruction instructions[3];
    AivmProgram program;

    instructions[0].opcode = AIVM_OP_PUSH_INT;
    instructions[0].operand_int = 1;
    instructions[1].opcode = AIVM_OP_PUSH_INT;
    instructions[1].operand_int = 0;
    instructions[2].opcode = opcode;
    instructions[2].operand_int = 0;

    aivm_program_init(&program, instructions, 3U);
    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), expected_message) == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_attr_value_kind_requires_node_and_index(void)
{
    return run_attr_requires_node_and_index_test(
        AIVM_OP_ATTR_VALUE_KIND,
        "ATTR_VALUE_KIND requires (node,int).");
}

static int test_attr_value_string_requires_node_and_index(void)
{
    return run_attr_requires_node_and_index_test(
        AIVM_OP_ATTR_VALUE_STRING,
        "ATTR_VALUE_STRING requires (node,int).");
}

static int test_attr_value_int_requires_node_and_index(void)
{
    return run_attr_requires_node_and_index_test(
        AIVM_OP_ATTR_VALUE_INT,
        "ATTR_VALUE_INT requires (node,int).");
}

static int test_attr_value_bool_requires_node_and_index(void)
{
    return run_attr_requires_node_and_index_test(
        AIVM_OP_ATTR_VALUE_BOOL,
        "ATTR_VALUE_BOOL requires (node,int).");
}

static int test_make_err_requires_string_operands(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_MAKE_ERR, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "err1" },
        { .type = AIVM_VAL_STRING, .string_value = "VM001" },
        { .type = AIVM_VAL_INT, .int_value = 42 },
        { .type = AIVM_VAL_STRING, .string_value = "node0" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 5U,
        .constants = constants,
        .constant_count = 4U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "MAKE_ERR requires (string,string,string,string).") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_make_field_string_and_map_roundtrip(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },  /* lit id */
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },  /* lit value */
        { .opcode = AIVM_OP_MAKE_LIT_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },

        { .opcode = AIVM_OP_CONST, .operand_int = 2 },  /* field key */
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_FIELD_STRING, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 1 },

        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_MAP, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_CHILD_AT, .operand_int = 0 },   /* Field node */
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_ATTR_VALUE_STRING, .operand_int = 0 }, /* key */
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "id1" },
        { .type = AIVM_VAL_STRING, .string_value = "v1" },
        { .type = AIVM_VAL_STRING, .string_value = "name" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 16U,
        .constants = constants,
        .constant_count = 3U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("name")) == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_make_map_requires_int_count(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_MAP, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "not-int" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .constants = constants,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "MAKE_MAP requires int child count.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_scratch_pair_roundtrip(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 41 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 42 },
        { .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PAIR_FIRST, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PAIR_SECOND, .operand_int = 0 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 10U,
        .constants = NULL,
        .constant_count = 0U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    AivmValue out;

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.scratch_pair_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 83) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_copies_pair_result_across_worker_boundary(void)
{
    static AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 8 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PAIR_FIRST, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PAIR_SECOND, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 11 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 12 },
        { .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 12U,
        .constants = NULL,
        .constant_count = 0U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.scratch_pair_count == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 12) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 11) != 0) {
        return 1;
    }
    return 0;
}

static int test_scratch_pair_roots_node_through_compaction(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 7 },
        { .opcode = AIVM_OP_MAKE_PAIR, .operand_int = 0 },
        { .opcode = AIVM_OP_STORE_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_MAKE_BLOCK, .operand_int = 0 },
        { .opcode = AIVM_OP_POP, .operand_int = 0 },
        { .opcode = AIVM_OP_LOAD_LOCAL, .operand_int = 0 },
        { .opcode = AIVM_OP_PAIR_FIRST, .operand_int = 0 },
        { .opcode = AIVM_OP_NODE_ID, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "kept" },
        { .type = AIVM_VAL_STRING, .string_value = "discarded" }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 12U,
        .constants = constants,
        .constant_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    AivmValue out;

    aivm_init(&vm, &program);
    aivm_step(&vm);
    aivm_step(&vm);
    aivm_step(&vm);
    aivm_step(&vm);
    aivm_step(&vm);
    aivm_step(&vm);
    aivm_step(&vm);
    aivm_step(&vm);
    if (expect(aivm_collect_safe_point(&vm) == 1) != 0) {
        return 1;
    }
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_STRING && strcmp(out.string_value, "kept") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_pair_first_requires_pair(void)
{
    static AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_PAIR_FIRST, .operand_int = 0 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 2U,
        .constants = NULL,
        .constant_count = 0U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, &program);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_TYPE_MISMATCH) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "PAIR_FIRST requires pair operand.") == 0) != 0) {
        return 1;
    }
    return 0;
}

int main(void)
{
    if (run_test("test_push_store_load_pop", test_push_store_load_pop) != 0) {
        return 1;
    }
    if (run_test("test_add_int", test_add_int) != 0) {
        return 1;
    }
    if (run_test("test_load_local_missing_sets_error", test_load_local_missing_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_add_int_type_mismatch_sets_error", test_add_int_type_mismatch_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_jump_skips_instruction", test_jump_skips_instruction) != 0) {
        return 1;
    }
    if (run_test("test_jump_if_false_takes_branch", test_jump_if_false_takes_branch) != 0) {
        return 1;
    }
    if (run_test("test_jump_if_false_type_mismatch_sets_error", test_jump_if_false_type_mismatch_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_call_ret_roundtrip", test_call_ret_roundtrip) != 0) {
        return 1;
    }
    if (run_test("test_call_ret_rejects_extra_callee_stack_values", test_call_ret_rejects_extra_callee_stack_values) != 0) {
        return 1;
    }
    if (run_test("test_top_level_ret_halts", test_top_level_ret_halts) != 0) {
        return 1;
    }
    if (run_test("test_top_level_return_alias_halts", test_top_level_return_alias_halts) != 0) {
        return 1;
    }
    if (run_test("test_return_alias_roundtrip", test_return_alias_roundtrip) != 0) {
        return 1;
    }
    if (run_test("test_call_target_equal_instruction_count_sets_error", test_call_target_equal_instruction_count_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_call_ret_restores_caller_locals_scope", test_call_ret_restores_caller_locals_scope) != 0) {
        return 1;
    }
    if (run_test("test_recursive_tail_call_reuses_current_frame", test_recursive_tail_call_reuses_current_frame) != 0) {
        return 1;
    }
    if (run_test("test_negative_jump_operand_sets_error", test_negative_jump_operand_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_eq_int_true_false", test_eq_int_true_false) != 0) {
        return 1;
    }
    if (run_test("test_eq_int_type_mismatch_sets_error", test_eq_int_type_mismatch_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_eq_value_across_types", test_eq_value_across_types) != 0) {
        return 1;
    }
    if (run_test("test_eq_string_content_and_null_handling", test_eq_string_content_and_null_handling) != 0) {
        return 1;
    }
    if (run_test("test_eq_stack_underflow_sets_error", test_eq_stack_underflow_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_const_pushes_program_constant", test_const_pushes_program_constant) != 0) {
        return 1;
    }
    if (run_test("test_str_concat_success", test_str_concat_success) != 0) {
        return 1;
    }
    if (run_test("test_str_concat_type_mismatch_sets_error", test_str_concat_type_mismatch_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_to_string_converts_scalar_values", test_to_string_converts_scalar_values) != 0) {
        return 1;
    }
    if (run_test("test_to_string_null_string_sets_error", test_to_string_null_string_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_str_escape_escapes_special_chars", test_str_escape_escapes_special_chars) != 0) {
        return 1;
    }
    if (run_test("test_str_escape_requires_string", test_str_escape_requires_string) != 0) {
        return 1;
    }
    if (run_test("test_string_arena_overflow_sets_error", test_string_arena_overflow_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_bytes_arena_overflow_sets_error", test_bytes_arena_overflow_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_str_substring_and_remove_rune_clamp_semantics", test_str_substring_and_remove_rune_clamp_semantics) != 0) {
        return 1;
    }
    if (run_test("test_str_substring_and_remove_type_mismatch", test_str_substring_and_remove_type_mismatch) != 0) {
        return 1;
    }
    if (run_test("test_str_substring_reuses_interned_results", test_str_substring_reuses_interned_results) != 0) {
        return 1;
    }
    if (run_test("test_call_sys_success_and_void_result", test_call_sys_success_and_void_result) != 0) {
        return 1;
    }
    if (run_test("test_call_sys_failure_sets_vm_error", test_call_sys_failure_sets_vm_error) != 0) {
        return 1;
    }
    if (run_test("test_call_sys_elapsed_limit_sets_vm_error", test_call_sys_elapsed_limit_sets_vm_error) != 0) {
        return 1;
    }
    if (run_test("test_call_sys_contract_type_mismatch_sets_error", test_call_sys_contract_type_mismatch_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_call_sys_missing_binding_sets_unbound_error", test_call_sys_missing_binding_sets_unbound_error) != 0) {
        return 1;
    }
    if (run_test("test_call_sys_does_not_recover_non_syscall_string_target_from_args", test_call_sys_does_not_recover_non_syscall_string_target_from_args) != 0) {
        return 1;
    }
    if (run_test("test_nested_call_preserves_argument_without_tail_call_reuse", test_nested_call_preserves_argument_without_tail_call_reuse) != 0) {
        return 1;
    }
    if (run_test("test_call_sys_debug_task_reclaim_stats_intrinsic", test_call_sys_debug_task_reclaim_stats_intrinsic) != 0) {
        return 1;
    }
    if (run_test("test_call_sys_debug_task_reclaim_stats_arity_error", test_call_sys_debug_task_reclaim_stats_arity_error) != 0) {
        return 1;
    }
    if (run_test("test_async_call_and_await_roundtrip", test_async_call_and_await_roundtrip) != 0) {
        return 1;
    }
    if (run_test("test_async_call_starts_pending_worker_task", test_async_call_starts_pending_worker_task) != 0) {
        return 1;
    }
    if (run_test("test_async_call_copies_string_result_across_worker_boundary", test_async_call_copies_string_result_across_worker_boundary) != 0) {
        return 1;
    }
    if (run_test("test_async_call_invalid_target_sets_error", test_async_call_invalid_target_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_async_call_target_equal_instruction_count_sets_error", test_async_call_target_equal_instruction_count_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_async_call_task_handle_overflow_sets_error", test_async_call_task_handle_overflow_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_async_call_rejects_extra_callee_stack_values", test_async_call_rejects_extra_callee_stack_values) != 0) {
        return 1;
    }
    if (run_test("test_async_call_rejects_invalid_call_target_layout", test_async_call_rejects_invalid_call_target_layout) != 0) {
        return 1;
    }
    if (run_test("test_async_call_copies_node_result_across_worker_boundary", test_async_call_copies_node_result_across_worker_boundary) != 0) {
        return 1;
    }
    if (run_test("test_async_call_reclaims_oldest_task_slot_when_full", test_async_call_reclaims_oldest_task_slot_when_full) != 0) {
        return 1;
    }
    if (run_test("test_await_evicted_task_handle_sets_error", test_await_evicted_task_handle_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_async_call_reclaim_skips_pinned_oldest_handle", test_async_call_reclaim_skips_pinned_oldest_handle) != 0) {
        return 1;
    }
    if (run_test("test_async_call_full_table_all_pinned_sets_capacity_error", test_async_call_full_table_all_pinned_sets_capacity_error) != 0) {
        return 1;
    }
    if (run_test("test_async_call_sys_and_await_roundtrip", test_async_call_sys_and_await_roundtrip) != 0) {
        return 1;
    }
    if (run_test("test_await_invalid_handle_sets_error", test_await_invalid_handle_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_await_pending_task_handle_sets_error", test_await_pending_task_handle_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_await_failed_task_handle_returns_terminal_result", test_await_failed_task_handle_returns_terminal_result) != 0) {
        return 1;
    }
    if (run_test("test_await_failed_task_non_err_result_sets_error", test_await_failed_task_non_err_result_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_terminal_task_cleanup_stress", test_terminal_task_cleanup_stress) != 0) {
        return 1;
    }
    if (run_test("test_parallel_begin_fork_join_and_cancel", test_parallel_begin_fork_join_and_cancel) != 0) {
        return 1;
    }
    if (run_test("test_parallel_join_mismatch_sets_error", test_parallel_join_mismatch_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_parallel_join_resolves_completed_task_handles", test_parallel_join_resolves_completed_task_handles) != 0) {
        return 1;
    }
    if (run_test("test_parallel_join_resolves_canceled_task_handles", test_parallel_join_resolves_canceled_task_handles) != 0) {
        return 1;
    }
    if (run_test("test_parallel_join_failed_task_non_err_result_sets_error", test_parallel_join_failed_task_non_err_result_sets_error) != 0) {
        return 1;
    }
    if (run_test("test_parallel_fork_requires_context", test_parallel_fork_requires_context) != 0) {
        return 1;
    }
    if (run_test("test_parallel_join_requires_context", test_parallel_join_requires_context) != 0) {
        return 1;
    }
    if (run_test("test_str_utf8_byte_count", test_str_utf8_byte_count) != 0) {
        return 1;
    }
    if (run_test("test_str_utf8_byte_count_requires_string", test_str_utf8_byte_count_requires_string) != 0) {
        return 1;
    }
    if (run_test("test_node_ops_core_semantics", test_node_ops_core_semantics) != 0) {
        return 1;
    }
    if (run_test("test_make_node_from_template_and_children", test_make_node_from_template_and_children) != 0) {
        return 1;
    }
    if (run_test("test_make_node_empty_from_kind_and_id", test_make_node_empty_from_kind_and_id) != 0) {
        return 1;
    }
    if (run_test("test_string_compaction_preserves_live_node_strings", test_string_compaction_preserves_live_node_strings) != 0) {
        return 1;
    }
    if (run_test("test_append_attr_adds_lit_attr_to_node", test_append_attr_adds_lit_attr_to_node) != 0) {
        return 1;
    }
    if (run_test("test_make_node_converts_scalar_children_to_runtime_nodes", test_make_node_converts_scalar_children_to_runtime_nodes) != 0) {
        return 1;
    }
    if (run_test("test_node_compaction_reclaims_unreachable_nodes", test_node_compaction_reclaims_unreachable_nodes) != 0) {
        return 1;
    }
    if (run_test("test_node_compaction_runs_before_capacity_when_pressure_is_high", test_node_compaction_runs_before_capacity_when_pressure_is_high) != 0) {
        return 1;
    }
    if (run_test("test_node_compaction_does_not_run_below_pressure_threshold", test_node_compaction_does_not_run_below_pressure_threshold) != 0) {
        return 1;
    }
    if (run_test("test_safe_point_compaction_reclaims_below_pressure_threshold", test_safe_point_compaction_reclaims_below_pressure_threshold) != 0) {
        return 1;
    }
    if (run_test("test_return_boundary_runs_safe_point_compaction", test_return_boundary_runs_safe_point_compaction) != 0) {
        return 1;
    }
    if (run_test("test_node_compaction_runs_on_child_pressure_before_node_threshold", test_node_compaction_runs_on_child_pressure_before_node_threshold) != 0) {
        return 1;
    }
    if (run_test("test_node_capacity_failure_resets_gc_allocation_counter", test_node_capacity_failure_resets_gc_allocation_counter) != 0) {
        return 1;
    }
    if (run_test("test_make_node_requires_node_args", test_make_node_requires_node_args) != 0) {
        return 1;
    }
    if (run_test("test_make_node_empty_requires_string_args", test_make_node_empty_requires_string_args) != 0) {
        return 1;
    }
    if (run_test("test_append_attr_requires_single_attr_node", test_append_attr_requires_single_attr_node) != 0) {
        return 1;
    }
    if (run_test("test_make_lit_string_requires_string_id", test_make_lit_string_requires_string_id) != 0) {
        return 1;
    }
    if (run_test("test_make_block_requires_string_id", test_make_block_requires_string_id) != 0) {
        return 1;
    }
    if (run_test("test_child_at_out_of_range_sets_error_detail", test_child_at_out_of_range_sets_error_detail) != 0) {
        return 1;
    }
    if (run_test("test_append_child_requires_node_operands", test_append_child_requires_node_operands) != 0) {
        return 1;
    }
    if (run_test("test_node_kind_requires_node_operand", test_node_kind_requires_node_operand) != 0) {
        return 1;
    }
    if (run_test("test_attr_key_requires_node_and_index", test_attr_key_requires_node_and_index) != 0) {
        return 1;
    }
    if (run_test("test_attr_value_kind_requires_node_and_index", test_attr_value_kind_requires_node_and_index) != 0) {
        return 1;
    }
    if (run_test("test_attr_value_string_requires_node_and_index", test_attr_value_string_requires_node_and_index) != 0) {
        return 1;
    }
    if (run_test("test_attr_value_int_requires_node_and_index", test_attr_value_int_requires_node_and_index) != 0) {
        return 1;
    }
    if (run_test("test_attr_value_bool_requires_node_and_index", test_attr_value_bool_requires_node_and_index) != 0) {
        return 1;
    }
    if (run_test("test_make_err_requires_string_operands", test_make_err_requires_string_operands) != 0) {
        return 1;
    }
    if (run_test("test_make_field_string_and_map_roundtrip", test_make_field_string_and_map_roundtrip) != 0) {
        return 1;
    }
    if (run_test("test_make_map_requires_int_count", test_make_map_requires_int_count) != 0) {
        return 1;
    }
    if (run_test("test_scratch_pair_roundtrip", test_scratch_pair_roundtrip) != 0) {
        return 1;
    }
    if (run_test("test_async_call_copies_pair_result_across_worker_boundary", test_async_call_copies_pair_result_across_worker_boundary) != 0) {
        return 1;
    }
    if (run_test("test_scratch_pair_roots_node_through_compaction", test_scratch_pair_roots_node_through_compaction) != 0) {
        return 1;
    }
    if (run_test("test_pair_first_requires_pair", test_pair_first_requires_pair) != 0) {
        return 1;
    }

    return 0;
}
