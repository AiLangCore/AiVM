#include "aivm_program.h"
#include "sys/aivm_syscall.h"
#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static int host_utf8_count_constant(
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

static int run_once_and_assert(const AivmProgram* program, const AivmSyscallBinding* bindings, size_t binding_count)
{
    AivmVm vm;
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

static int host_worker_poll_deterministic(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int64_t handle;
    (void)target;

    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    handle = args[0].int_value;
    switch (handle % 5) {
        case 0:
            *result = aivm_value_int(0);
            break;
        case 1:
            *result = aivm_value_int(1);
            break;
        case 2:
            *result = aivm_value_int(-1);
            break;
        case 3:
            *result = aivm_value_int(-2);
            break;
        default:
            *result = aivm_value_int(-3);
            break;
    }
    return AIVM_SYSCALL_OK;
}

static int run_worker_merge_stress(size_t* out_checksum, size_t* out_ready_count)
{
    int64_t handle;
    AivmValue arg;
    AivmValue result;
    AivmSyscallStatus status;
    size_t checksum = 0U;
    size_t ready_count = 0U;
    size_t state_counts[5] = { 0U, 0U, 0U, 0U, 0U };
    static const AivmSyscallBinding bindings[] = {
        { "sys.worker.poll", host_worker_poll_deterministic }
    };

    if (out_checksum == NULL || out_ready_count == NULL) {
        return 1;
    }

    for (handle = 1; handle <= 1500; handle += 1) {
        arg = aivm_value_int(handle);
        status = aivm_syscall_dispatch_checked(bindings, 1U, "sys.worker.poll", &arg, 1U, &result);
        if (status != AIVM_SYSCALL_OK || result.type != AIVM_VAL_INT) {
            return 1;
        }
        if (result.int_value == 0) {
            state_counts[0] += 1U;
            continue;
        }
        if (result.int_value == 1) {
            state_counts[1] += 1U;
        } else if (result.int_value == -1) {
            state_counts[2] += 1U;
        } else if (result.int_value == -2) {
            state_counts[3] += 1U;
        } else if (result.int_value == -3) {
            state_counts[4] += 1U;
        } else {
            return 1;
        }
        ready_count += 1U;
        checksum = (checksum * 257U) + (size_t)handle + (size_t)(result.int_value + 7);
    }

    if (state_counts[0] != 300U || state_counts[1] != 300U || state_counts[2] != 300U ||
        state_counts[3] != 300U || state_counts[4] != 300U) {
        return 1;
    }
    if (ready_count != 1200U) {
        return 1;
    }

    *out_checksum = checksum;
    *out_ready_count = ready_count;
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
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_ADD_INT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.str.utf8ByteCount" },
        { .type = AIVM_VAL_STRING, .string_value = "a😀bc" },
        { .type = AIVM_VAL_INT, .int_value = 5 }
    };
    static const AivmProgram program = {
        .instructions = instructions,
        .instruction_count = 9U,
        .constants = constants,
        .constant_count = 3U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.str.utf8ByteCount", host_utf8_count_constant }
    };
    int iteration;
    size_t merge_checksum_a = 0U;
    size_t merge_checksum_b = 0U;
    size_t ready_count_a = 0U;
    size_t ready_count_b = 0U;

    for (iteration = 0; iteration < 128; iteration += 1) {
        if (run_once_and_assert(&program, bindings, 1U) != 0) {
            return 1;
        }
    }
    if (run_worker_merge_stress(&merge_checksum_a, &ready_count_a) != 0) {
        return 1;
    }
    if (run_worker_merge_stress(&merge_checksum_b, &ready_count_b) != 0) {
        return 1;
    }
    if (expect(ready_count_a == 1200U && ready_count_b == 1200U) != 0) {
        return 1;
    }
    if (expect(merge_checksum_a == merge_checksum_b) != 0) {
        return 1;
    }

    return 0;
}
