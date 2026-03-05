#include <string.h>

#include "aivm_program.h"
#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
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

static int host_str_substring(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (arg_count != 3U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[0].type != AIVM_VAL_STRING || args[1].type != AIVM_VAL_INT || args[2].type != AIVM_VAL_INT) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_string("sub_ok");
    return AIVM_SYSCALL_OK;
}

static int host_str_remove(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (arg_count != 3U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[0].type != AIVM_VAL_STRING || args[1].type != AIVM_VAL_INT || args[2].type != AIVM_VAL_INT) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_string("rem_ok");
    return AIVM_SYSCALL_OK;
}

static int host_str_utf8_byte_count(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (arg_count != 1U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[0].type != AIVM_VAL_STRING) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_int(7);
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
    AivmVm vm;
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
    AivmVm vm;
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Invalid local slot.") == 0) != 0) {
        return 1;
    }

    return 0;
}

static int test_add_int(void)
{
    AivmVm vm;
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
    AivmVm vm;
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "ADD_INT requires int operands.") == 0) != 0) {
        return 1;
    }

    return 0;
}

static int test_jump_skips_instruction(void)
{
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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

static int test_call_ret_collapses_callee_stack_to_single_return(void)
{
    AivmVm vm;
    AivmValue out;
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

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 2U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(out.int_value == 8) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(out.int_value == 11) != 0) {
        return 1;
    }
    return 0;
}

static int test_top_level_ret_halts(void)
{
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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

static int test_tail_call_reuses_frame_in_recursive_loop(void)
{
    AivmVm vm;
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
    for (i = 0U; i < 2000U; i += 1U) {
        if (vm.status != AIVM_VM_STATUS_READY && vm.status != AIVM_VM_STATUS_RUNNING) {
            break;
        }
        aivm_step(&vm);
        if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
            return 1;
        }
        if (expect(vm.call_frame_count <= 1U) != 0) {
            return 1;
        }
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.call_frame_count <= 1U) != 0) {
        return 1;
    }
    return 0;
}

static int test_negative_jump_operand_sets_error(void)
{
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    vm.string_arena_used = AIVM_VM_STRING_ARENA_CAPACITY;
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

    aivm_init(&vm, &program_to_string);
    vm.string_arena_used = AIVM_VM_STRING_ARENA_CAPACITY;
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

    aivm_init(&vm, &program_escape);
    vm.string_arena_used = AIVM_VM_STRING_ARENA_CAPACITY;
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

    return 0;
}

static int test_bytes_arena_overflow_sets_error(void)
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
        { "sys.bytes.fromBase64", host_bytes_large }
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
    AivmVm vm;
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
    AivmVm vm;
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

static int test_call_sys_success_and_void_result(void)
{
    AivmVm vm;
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
    AivmVm vm;
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMS004/AIVMC001: Syscall target was not found.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_string_contracts_success(void)
{
    AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 4 },
        { .opcode = AIVM_OP_CONST, .operand_int = 5 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 6 },
        { .opcode = AIVM_OP_CONST, .operand_int = 7 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.str.substring" },
        { .type = AIVM_VAL_STRING, .string_value = "abcde" },
        { .type = AIVM_VAL_INT, .int_value = 1 },
        { .type = AIVM_VAL_INT, .int_value = 2 },
        { .type = AIVM_VAL_STRING, .string_value = "sys.str.remove" },
        { .type = AIVM_VAL_STRING, .string_value = "vwxyz" },
        { .type = AIVM_VAL_STRING, .string_value = "sys.str.utf8ByteCount" },
        { .type = AIVM_VAL_STRING, .string_value = "a😀bc" }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.str.substring", host_str_substring },
        { "sys.str.remove", host_str_remove },
        { "sys.str.utf8ByteCount", host_str_utf8_byte_count }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 14U,
        .constants = constants,
        .constant_count = 8U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init_with_syscalls(&vm, &program, bindings, 3U);
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 3U) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 7) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("rem_ok")) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_stack_pop(&vm, &out) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(out, aivm_value_string("sub_ok")) == 1) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_string_contract_type_mismatch_sets_error(void)
{
    AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 3 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.str.substring" },
        { .type = AIVM_VAL_STRING, .string_value = "abcde" },
        { .type = AIVM_VAL_STRING, .string_value = "bad_start_type" },
        { .type = AIVM_VAL_INT, .int_value = 2 }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.str.substring", host_str_substring }
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMS004/AIVMC003: Syscall argument type was invalid.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_call_sys_missing_binding_sets_not_found_error(void)
{
    AivmVm vm;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 3 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.str.substring" },
        { .type = AIVM_VAL_STRING, .string_value = "abcde" },
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.str.remove", host_str_remove }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 5U,
        .constants = constants,
        .constant_count = 3U,
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMS003: Syscall target was not found.") == 0) != 0) {
        return 1;
    }
    return 0;
}

static int test_async_call_and_await_roundtrip(void)
{
    AivmVm vm;
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

static int test_async_call_invalid_target_sets_error(void)
{
    AivmVm vm;
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

static int test_async_call_target_equal_instruction_count_sets_error(void)
{
    AivmVm vm;
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

static int test_async_call_sys_and_await_roundtrip(void)
{
    AivmVm vm;
    AivmValue out;
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_ASYNC_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_AWAIT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.str.utf8ByteCount" },
        { .type = AIVM_VAL_STRING, .string_value = "a😀bc" }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.str.utf8ByteCount", host_str_utf8_byte_count }
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
    AivmVm vm;
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

static int test_parallel_begin_fork_join_and_cancel(void)
{
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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

static int test_make_node_converts_scalar_children_to_runtime_nodes(void)
{
    AivmVm vm;
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
    AivmVm vm;
    AivmInstruction instructions[1024];
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
    AivmVm vm;
    AivmInstruction instructions[1024];
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
    AivmVm vm;
    AivmInstruction instructions[1024];
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
    if (expect(vm.node_count == expected_node_count) != 0) {
        return 1;
    }
    if (expect(vm.node_allocations_since_gc == expected_alloc_counter) != 0) {
        return 1;
    }
    return 0;
}

static int test_node_capacity_failure_resets_gc_allocation_counter(void)
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
    if (expect(strcmp(aivm_vm_error_detail(&vm), "AIVMM005: node arena capacity exceeded.") == 0) != 0) {
        return 1;
    }
    if (expect(vm.node_gc_compaction_count >= 1U) != 0) {
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
    AivmVm vm;
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

static int test_make_lit_string_requires_string_id(void)
{
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
    AivmInstruction instructions[3];
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
    AivmVm vm;
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
    AivmVm vm;
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
    AivmVm vm;
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

int main(void)
{
    if (test_push_store_load_pop() != 0) {
        return 1;
    }
    if (test_add_int() != 0) {
        return 1;
    }
    if (test_load_local_missing_sets_error() != 0) {
        return 1;
    }
    if (test_add_int_type_mismatch_sets_error() != 0) {
        return 1;
    }
    if (test_jump_skips_instruction() != 0) {
        return 1;
    }
    if (test_jump_if_false_takes_branch() != 0) {
        return 1;
    }
    if (test_jump_if_false_type_mismatch_sets_error() != 0) {
        return 1;
    }
    if (test_call_ret_roundtrip() != 0) {
        return 1;
    }
    if (test_call_ret_collapses_callee_stack_to_single_return() != 0) {
        return 1;
    }
    if (test_top_level_ret_halts() != 0) {
        return 1;
    }
    if (test_top_level_return_alias_halts() != 0) {
        return 1;
    }
    if (test_return_alias_roundtrip() != 0) {
        return 1;
    }
    if (test_call_target_equal_instruction_count_sets_error() != 0) {
        return 1;
    }
    if (test_call_ret_restores_caller_locals_scope() != 0) {
        return 1;
    }
    if (test_tail_call_reuses_frame_in_recursive_loop() != 0) {
        return 1;
    }
    if (test_negative_jump_operand_sets_error() != 0) {
        return 1;
    }
    if (test_eq_int_true_false() != 0) {
        return 1;
    }
    if (test_eq_int_type_mismatch_sets_error() != 0) {
        return 1;
    }
    if (test_eq_value_across_types() != 0) {
        return 1;
    }
    if (test_eq_string_content_and_null_handling() != 0) {
        return 1;
    }
    if (test_eq_stack_underflow_sets_error() != 0) {
        return 1;
    }
    if (test_const_pushes_program_constant() != 0) {
        return 1;
    }
    if (test_str_concat_success() != 0) {
        return 1;
    }
    if (test_str_concat_type_mismatch_sets_error() != 0) {
        return 1;
    }
    if (test_to_string_converts_scalar_values() != 0) {
        return 1;
    }
    if (test_to_string_null_string_sets_error() != 0) {
        return 1;
    }
    if (test_str_escape_escapes_special_chars() != 0) {
        return 1;
    }
    if (test_str_escape_requires_string() != 0) {
        return 1;
    }
    if (test_string_arena_overflow_sets_error() != 0) {
        return 1;
    }
    if (test_bytes_arena_overflow_sets_error() != 0) {
        return 1;
    }
    if (test_str_substring_and_remove_rune_clamp_semantics() != 0) {
        return 1;
    }
    if (test_str_substring_and_remove_type_mismatch() != 0) {
        return 1;
    }
    if (test_call_sys_success_and_void_result() != 0) {
        return 1;
    }
    if (test_call_sys_failure_sets_vm_error() != 0) {
        return 1;
    }
    if (test_call_sys_string_contracts_success() != 0) {
        return 1;
    }
    if (test_call_sys_string_contract_type_mismatch_sets_error() != 0) {
        return 1;
    }
    if (test_call_sys_missing_binding_sets_not_found_error() != 0) {
        return 1;
    }
    if (test_async_call_and_await_roundtrip() != 0) {
        return 1;
    }
    if (test_async_call_invalid_target_sets_error() != 0) {
        return 1;
    }
    if (test_async_call_target_equal_instruction_count_sets_error() != 0) {
        return 1;
    }
    if (test_async_call_sys_and_await_roundtrip() != 0) {
        return 1;
    }
    if (test_await_invalid_handle_sets_error() != 0) {
        return 1;
    }
    if (test_parallel_begin_fork_join_and_cancel() != 0) {
        return 1;
    }
    if (test_parallel_join_mismatch_sets_error() != 0) {
        return 1;
    }
    if (test_parallel_join_resolves_completed_task_handles() != 0) {
        return 1;
    }
    if (test_parallel_fork_requires_context() != 0) {
        return 1;
    }
    if (test_parallel_join_requires_context() != 0) {
        return 1;
    }
    if (test_str_utf8_byte_count() != 0) {
        return 1;
    }
    if (test_str_utf8_byte_count_requires_string() != 0) {
        return 1;
    }
    if (test_node_ops_core_semantics() != 0) {
        return 1;
    }
    if (test_make_node_from_template_and_children() != 0) {
        return 1;
    }
    if (test_make_node_converts_scalar_children_to_runtime_nodes() != 0) {
        return 1;
    }
    if (test_node_compaction_reclaims_unreachable_nodes() != 0) {
        return 1;
    }
    if (test_node_compaction_runs_before_capacity_when_pressure_is_high() != 0) {
        return 1;
    }
    if (test_node_compaction_does_not_run_below_pressure_threshold() != 0) {
        return 1;
    }
    if (test_node_capacity_failure_resets_gc_allocation_counter() != 0) {
        return 1;
    }
    if (test_make_node_requires_node_args() != 0) {
        return 1;
    }
    if (test_make_lit_string_requires_string_id() != 0) {
        return 1;
    }
    if (test_make_block_requires_string_id() != 0) {
        return 1;
    }
    if (test_child_at_out_of_range_sets_error_detail() != 0) {
        return 1;
    }
    if (test_append_child_requires_node_operands() != 0) {
        return 1;
    }
    if (test_node_kind_requires_node_operand() != 0) {
        return 1;
    }
    if (test_attr_key_requires_node_and_index() != 0) {
        return 1;
    }
    if (test_attr_value_kind_requires_node_and_index() != 0) {
        return 1;
    }
    if (test_attr_value_string_requires_node_and_index() != 0) {
        return 1;
    }
    if (test_attr_value_int_requires_node_and_index() != 0) {
        return 1;
    }
    if (test_attr_value_bool_requires_node_and_index() != 0) {
        return 1;
    }
    if (test_make_err_requires_string_operands() != 0) {
        return 1;
    }
    if (test_make_field_string_and_map_roundtrip() != 0) {
        return 1;
    }
    if (test_make_map_requires_int_count() != 0) {
        return 1;
    }

    return 0;
}
