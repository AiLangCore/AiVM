#include "aivm_c_api.h"

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
    *result = aivm_value_node(111222);
    return AIVM_SYSCALL_OK;
}

int main(void)
{
    AivmCResult result;
    AivmVm vm;
    AivmProgram reclaim_program;
    size_t i;
    uint32_t abi_version;
    static const AivmInstruction ok_instructions[] = {
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmInstruction bad_opcode[] = {
        { .opcode = (AivmOpcode)99, .operand_int = 0 }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.ui.getWindowSize", host_ui_get_window_size }
    };
    static const uint8_t bad_magic_program[16] = {
        'X', 'I', 'B', 'C',
        1, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };
    static const uint8_t valid_aibc1_program[56] = {
        'A', 'I', 'B', 'C',
        1, 0, 0, 0,
        0, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,   /* section type: instructions */
        28, 0, 0, 0,  /* section size */
        2, 0, 0, 0,   /* instruction_count */
        3, 0, 0, 0,   /* PUSH_INT */
        5, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 0, 0,   /* HALT */
        0, 0, 0, 0, 0, 0, 0, 0
    };
    static const AivmInstruction call_sys_instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue call_sys_constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.ui.getWindowSize" },
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmProgram call_sys_program = {
        .instructions = call_sys_instructions,
        .instruction_count = 4U,
        .constants = call_sys_constants,
        .constant_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmInstruction reclaim_instructions[] = {
        { .opcode = AIVM_OP_ASYNC_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 5 },
        { .opcode = AIVM_OP_RET, .operand_int = 0 }
    };

    abi_version = aivm_c_abi_version();
    if (expect(abi_version == 1U) != 0) {
        return 1;
    }

    result = aivm_c_execute_instructions(ok_instructions, 2U);
    if (expect(result.ok == 1) != 0) {
        return 1;
    }
    if (expect(result.loaded == 1) != 0) {
        return 1;
    }
    if (expect(result.load_status == AIVM_PROGRAM_OK) != 0) {
        return 1;
    }
    if (expect(result.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(result.has_exit_code == 0) != 0) {
        return 1;
    }

    result = aivm_c_execute_instructions(bad_opcode, 1U);
    if (expect(result.ok == 0) != 0) {
        return 1;
    }
    if (expect(result.error == AIVM_VM_ERR_INVALID_OPCODE) != 0) {
        return 1;
    }

    result = aivm_c_execute_instructions(NULL, 0U);
    if (expect(result.ok == 1) != 0) {
        return 1;
    }
    if (expect(result.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(result.has_exit_code == 0) != 0) {
        return 1;
    }

    result = aivm_c_execute_instructions(NULL, 1U);
    if (expect(result.ok == 0) != 0) {
        return 1;
    }
    if (expect(result.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }

    result = aivm_c_execute_instructions_with_constants(
        call_sys_instructions,
        4U,
        call_sys_constants,
        2U);
    if (expect(result.ok == 0) != 0) {
        return 1;
    }
    if (expect(result.error == AIVM_VM_ERR_SYSCALL) != 0) {
        return 1;
    }

    result = aivm_c_execute_instructions_with_syscalls(
        call_sys_instructions,
        4U,
        call_sys_constants,
        2U,
        bindings,
        1U);
    if (expect(result.ok == 1) != 0) {
        return 1;
    }
    if (expect(result.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }

    result = aivm_c_execute_program_with_syscalls(&call_sys_program, bindings, 1U);
    if (expect(result.ok == 1) != 0) {
        return 1;
    }
    if (expect(result.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }

    result = aivm_c_execute_aibc1(valid_aibc1_program, 56U);
    if (expect(result.loaded == 1) != 0) {
        return 1;
    }
    if (expect(result.load_status == AIVM_PROGRAM_OK) != 0) {
        return 1;
    }
    if (expect(result.ok == 1) != 0) {
        return 1;
    }
    if (expect(result.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(result.has_exit_code == 1) != 0) {
        return 1;
    }
    if (expect(result.exit_code == 5) != 0) {
        return 1;
    }

    result = aivm_c_execute_aibc1(bad_magic_program, 16U);
    if (expect(result.loaded == 0) != 0) {
        return 1;
    }
    if (expect(result.load_status == AIVM_PROGRAM_ERR_BAD_MAGIC) != 0) {
        return 1;
    }
    if (expect(result.ok == 0) != 0) {
        return 1;
    }
    if (expect(result.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }

    if (expect(aivm_c_vm_task_reclaim_count(NULL) == 0U) != 0) {
        return 1;
    }
    if (expect(aivm_c_vm_task_reclaim_skip_pinned_count(NULL) == 0U) != 0) {
        return 1;
    }
    if (expect(aivm_c_vm_task_reclaim_exhausted_count(NULL) == 0U) != 0) {
        return 1;
    }

    aivm_program_init(&reclaim_program, reclaim_instructions, 4U);
    aivm_init(&vm, &reclaim_program);
    vm.completed_task_count = AIVM_VM_TASK_CAPACITY;
    vm.next_task_handle = (int64_t)AIVM_VM_TASK_CAPACITY + 1;
    for (i = 0U; i < AIVM_VM_TASK_CAPACITY; i += 1U) {
        vm.completed_tasks[i].state = AIVM_TASK_STATE_COMPLETED;
        vm.completed_tasks[i].handle = (int64_t)i + 1;
        vm.completed_tasks[i].result = aivm_value_int(-((int64_t)i + 1));
    }
    aivm_run(&vm);
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_c_vm_task_reclaim_count(&vm) == 1U) != 0) {
        return 1;
    }
    if (expect(aivm_c_vm_task_reclaim_skip_pinned_count(&vm) == 0U) != 0) {
        return 1;
    }
    if (expect(aivm_c_vm_task_reclaim_exhausted_count(&vm) == 0U) != 0) {
        return 1;
    }

    return 0;
}
