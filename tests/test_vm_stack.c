#include "aivm_vm.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmVm vm;
    AivmValue value;
    AivmValue out;
    size_t i;

    aivm_init(&vm, NULL);

    value = aivm_value_int(7);
    if (expect(aivm_stack_push(&vm, value) == 1) != 0) {
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
    if (expect(out.int_value == 7) != 0) {
        return 1;
    }

    if (expect(aivm_stack_pop(&vm, &out) == 0) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_STACK_UNDERFLOW) != 0) {
        return 1;
    }

    aivm_reset_state(&vm);
    value = aivm_value_int(1);
    for (i = 0U; i < AIVM_VM_STACK_CAPACITY; i += 1U) {
        if (expect(aivm_stack_push(&vm, value) == 1) != 0) {
            return 1;
        }
    }

    if (expect(aivm_stack_push(&vm, value) == 0) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }
    if (expect(vm.error == AIVM_VM_ERR_STACK_OVERFLOW) != 0) {
        return 1;
    }

    return 0;
}
