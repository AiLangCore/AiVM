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

    aivm_init(&vm, NULL);

    if (expect(aivm_frame_push(&vm, 7U, 3U) == 1) != 0) {
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
    if (expect(frame.frame_base == 3U) != 0) {
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
        if (expect(aivm_frame_push(&vm, i, i) == 1) != 0) {
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

    return 0;
}
