#include "aivm_program.h"
#include "sys/aivm_syscall.h"
#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static int host_remote_call_constant(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (arg_count != 3U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (args[0].type != AIVM_VAL_STRING ||
        args[1].type != AIVM_VAL_STRING ||
        args[2].type != AIVM_VAL_INT) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_int(7);
    return AIVM_SYSCALL_OK;
}

static int run_once_and_assert(const AivmProgram* program, const AivmSyscallBinding* bindings, size_t binding_count)
{
    static AivmVm vm;
    AivmValue out;

    aivm_init_with_syscalls(&vm, program, bindings, binding_count);
    aivm_run(&vm);

    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_NONE) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 2U) != 0) {
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
    if (expect(out.type == AIVM_VAL_INT && out.int_value == 5) != 0) {
        return 1;
    }

    return 0;
}

int main(void)
{
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 2 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 3 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 3 },
        { .opcode = AIVM_OP_CONST, .operand_int = 4 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.remote.call" },
        { .type = AIVM_VAL_STRING, .string_value = "cap.remote" },
        { .type = AIVM_VAL_STRING, .string_value = "echoInt" },
        { .type = AIVM_VAL_INT, .int_value = 7 },
        { .type = AIVM_VAL_INT, .int_value = 5 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 11U,
        .constants = constants,
        .constant_count = 5U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.remote.call", host_remote_call_constant }
    };
    int iteration;

    for (iteration = 0; iteration < 128; iteration += 1) {
        if (run_once_and_assert(&program, bindings, 1U) != 0) {
            return 1;
        }
    }
    return 0;
}
