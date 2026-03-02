#include "aivm_program.h"
#include "aivm_runtime.h"

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
    *result = aivm_value_node(320200);
    return AIVM_SYSCALL_OK;
}

int main(void)
{
    AivmVm vm;
    static const AivmInstruction instructions_ok[] = {
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program_ok = {
        .instructions = instructions_ok,
        .instruction_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmInstruction instructions_err[] = {
        { .opcode = (AivmOpcode)99, .operand_int = 0 }
    };
    static const AivmProgram program_err = {
        .instructions = instructions_err,
        .instruction_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmInstruction instructions_sys[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants_sys[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.ui_getWindowSize" },
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.ui_getWindowSize", host_ui_get_window_size }
    };
    static const AivmProgram program_sys = {
        .instructions = instructions_sys,
        .instruction_count = 4U,
        .constants = constants_sys,
        .constant_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    if (expect(aivm_execute_program(&program_ok, &vm) == 1) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }

    if (expect(aivm_execute_program(&program_err, &vm) == 0) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }

    if (expect(aivm_execute_program(NULL, &vm) == 0) != 0) {
        return 1;
    }

    if (expect(aivm_execute_program_with_syscalls(&program_sys, bindings, 1U, &vm) == 1) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }

    return 0;
}
