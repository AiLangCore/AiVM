#include <string.h>

#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmVm vm;
    AivmCallFrame frame;
    AivmValue value;
    AivmValue out;
    size_t i;
    static const AivmInstruction bad_return_instructions[] = {
        { .opcode = AIVM_OP_CALL, .operand_int = 2 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 1 },
        { .opcode = AIVM_OP_PUSH_INT, .operand_int = 2 },
        { .opcode = AIVM_OP_RETURN, .operand_int = 0 }
    };
    static const AivmProgram bad_return_program = {
        .instructions = bad_return_instructions,
        .instruction_count = 5U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };

    aivm_init(&vm, NULL);

    if (expect(aivm_frame_push(&vm, 7U, 0U) == 1) != 0) {
        return 1;
    }
    if (expect(vm.call_frame_count == 1U) != 0) {
        return 1;
    }

    if (expect(aivm_frame_pop(&vm, &frame) == 1) != 0) {
        return 1;
    }
    if (expect(frame.return_instruction_pointer == 7U) != 0) {
        return 1;
    }
    if (expect(frame.frame_base == 0U) != 0) {
        return 1;
    }
    if (expect(frame.locals_base == 0U) != 0) {
        return 1;
    }

    if (expect(aivm_frame_pop(&vm, &frame) == 0) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_FRAME_UNDERFLOW) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    for (i = 0U; i < AIVM_VM_CALLFRAME_CAPACITY; i += 1U) {
        if (expect(aivm_frame_push(&vm, i, 0U) == 1) != 0) {
            return 1;
        }
    }
    if (expect(aivm_frame_push(&vm, 0U, 0U) == 0) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_FRAME_OVERFLOW) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    value = aivm_value_int(1234);
    if (expect(aivm_local_set(&vm, 0U, value) == 1) != 0) {
        return 1;
    }
    if (expect(vm.locals_count == 1U) != 0) {
        return 1;
    }

    if (expect(aivm_local_get(&vm, 0U, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(out.int_value == 1234) != 0) {
        return 1;
    }

    if (expect(aivm_local_get(&vm, 9U, &out) == 0) != 0) {
        return 1;
    }

    if (expect(aivm_local_set(&vm, AIVM_VM_LOCALS_CAPACITY, value) == 0) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_LOCAL_OUT_OF_RANGE) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    if (expect(aivm_local_set(&vm, 0U, aivm_value_int(11)) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_frame_push(&vm, 1U, 0U) == 1) != 0) {
        return 1;
    }
    if (expect(aivm_local_set(&vm, 0U, aivm_value_int(22)) == 1) != 0) {
        return 1;
    }
    if (expect(vm.locals_count == 2U) != 0) {
        return 1;
    }
    if (expect(aivm_local_get(&vm, 0U, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT) != 0 || expect(out.int_value == 22) != 0) {
        return 1;
    }
    if (expect(aivm_frame_pop(&vm, &frame) == 1) != 0) {
        return 1;
    }
    vm.locals_count = frame.locals_base;
    if (expect(aivm_local_get(&vm, 0U, &out) == 1) != 0) {
        return 1;
    }
    if (expect(out.type == AIVM_VAL_INT) != 0 || expect(out.int_value == 11) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    vm.stack_count = 1U;
    if (expect(aivm_frame_push(&vm, 1U, 2U) == 0) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_vm_error_detail(&vm), "Call frame base exceeds stack depth.") == 0) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    if (expect(aivm_frame_push(&vm, 1U, 0U) == 1) != 0) {
        return 1;
    }
    vm.call_frames[0].locals_base = 5U;
    vm.locals_count = 0U;
    if (expect(aivm_local_set(&vm, 0U, aivm_value_int(33)) == 0) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "VM frame invariant failed. op=local-set") != NULL) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    if (expect(aivm_frame_push(&vm, 1U, 0U) == 1) != 0) {
        return 1;
    }
    vm.call_frames[0].frame_base = 5U;
    if (expect(aivm_frame_pop(&vm, &frame) == 0) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_INVALID_PROGRAM) != 0) {
        return 1;
    }
    if (expect(strstr(aivm_vm_error_detail(&vm), "VM frame invariant failed. op=frame-pop") != NULL) != 0) {
        return 1;
    }

    aivm_init(&vm, &bad_return_program);
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
