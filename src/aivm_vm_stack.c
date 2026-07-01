#include "aivm_vm_internal.h"

static size_t stack_grow_limit(size_t current, size_t step, size_t max_value)
{
    size_t next;
    if (current >= max_value) {
        return max_value;
    }
    if (!aivm_size_add_checked(current, step, &next) || next > max_value) {
        return max_value;
    }
    return next;
}

static int ensure_stack_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->stack_limit && vm->stack_limit < AIVM_VM_STACK_CAPACITY) {
        vm->stack_limit = stack_grow_limit(vm->stack_limit, AIVM_VM_STACK_GROWTH_STEP, AIVM_VM_STACK_CAPACITY);
    }
    return needed <= vm->stack_limit;
}

static int ensure_call_frame_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->call_frame_limit && vm->call_frame_limit < AIVM_VM_CALLFRAME_CAPACITY) {
        vm->call_frame_limit = stack_grow_limit(vm->call_frame_limit, AIVM_VM_CALLFRAME_GROWTH_STEP, AIVM_VM_CALLFRAME_CAPACITY);
    }
    return needed <= vm->call_frame_limit;
}

static int ensure_locals_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->locals_limit && vm->locals_limit < AIVM_VM_LOCALS_CAPACITY) {
        vm->locals_limit = stack_grow_limit(vm->locals_limit, AIVM_VM_LOCALS_GROWTH_STEP, AIVM_VM_LOCALS_CAPACITY);
    }
    return needed <= vm->locals_limit;
}

int aivm_stack_push(AivmVm* vm, AivmValue value)
{
    size_t needed = 0U;
    if (vm == NULL) {
        return 0;
    }

    if (!aivm_size_add_checked(vm->stack_count, 1U, &needed) ||
        !ensure_stack_capacity(vm, needed)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_STACK_OVERFLOW, "Stack overflow.");
        return 0;
    }

    vm->stack[vm->stack_count] = value;
    vm->stack_count = needed;
    return 1;
}

int aivm_stack_pop(AivmVm* vm, AivmValue* out_value)
{
    if (vm == NULL || out_value == NULL) {
        return 0;
    }

    if (vm->stack_count == 0U) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_STACK_UNDERFLOW, "Stack underflow.");
        return 0;
    }

    vm->stack_count -= 1U;
    *out_value = vm->stack[vm->stack_count];
    return 1;
}

int aivm_frame_push(AivmVm* vm, size_t return_instruction_pointer, size_t frame_base)
{
    size_t needed = 0U;
    if (vm == NULL) {
        return 0;
    }
    if (!aivm_vm_validate_call_local_state(vm, "frame-push")) {
        return 0;
    }
    if (frame_base > vm->stack_count) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call frame base exceeds stack depth.");
        return 0;
    }

    if (!aivm_size_add_checked(vm->call_frame_count, 1U, &needed) ||
        !ensure_call_frame_capacity(vm, needed)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_FRAME_OVERFLOW, "Call-frame overflow.");
        return 0;
    }

    vm->call_frames[vm->call_frame_count].return_instruction_pointer = return_instruction_pointer;
    vm->call_frames[vm->call_frame_count].frame_base = frame_base;
    vm->call_frames[vm->call_frame_count].locals_base = vm->locals_count;
    vm->call_frame_count = needed;
    return 1;
}

int aivm_frame_pop(AivmVm* vm, AivmCallFrame* out_frame)
{
    if (vm == NULL || out_frame == NULL) {
        return 0;
    }
    if (!aivm_vm_validate_call_local_state(vm, "frame-pop")) {
        return 0;
    }

    if (vm->call_frame_count == 0U) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_FRAME_UNDERFLOW, "Call-frame underflow.");
        return 0;
    }

    vm->call_frame_count -= 1U;
    *out_frame = vm->call_frames[vm->call_frame_count];
    if (!aivm_vm_validate_frame_record(vm, out_frame, "frame-pop")) {
        return 0;
    }
    return 1;
}

int aivm_local_set(AivmVm* vm, size_t index, AivmValue value)
{
    size_t base = 0U;
    size_t absolute_index;
    size_t needed = 0U;
    if (vm == NULL) {
        return 0;
    }
    if (!aivm_vm_validate_call_local_state(vm, "local-set")) {
        return 0;
    }

    if (vm->call_frame_count > 0U) {
        base = vm->call_frames[vm->call_frame_count - 1U].locals_base;
    }
    if (base >= AIVM_VM_LOCALS_CAPACITY || index >= (AIVM_VM_LOCALS_CAPACITY - base)) {
        aivm_vm_set_local_out_of_range_error(vm, "store", index, base);
        return 0;
    }
    if (!aivm_size_add_checked(base, index, &absolute_index) ||
        !aivm_size_add_checked(absolute_index, 1U, &needed) ||
        !ensure_locals_capacity(vm, needed)) {
        aivm_vm_set_local_out_of_range_error(vm, "store", index, base);
        return 0;
    }
    vm->locals[absolute_index] = value;
    if (absolute_index >= vm->locals_count) {
        vm->locals_count = needed;
    }
    return 1;
}

int aivm_local_get(const AivmVm* vm, size_t index, AivmValue* out_value)
{
    size_t base = 0U;
    size_t absolute_index;
    if (vm == NULL || out_value == NULL) {
        return 0;
    }
    if (((AivmVm*)vm)->stack_count > ((AivmVm*)vm)->stack_limit ||
        ((AivmVm*)vm)->call_frame_count > ((AivmVm*)vm)->call_frame_limit ||
        ((AivmVm*)vm)->locals_count > ((AivmVm*)vm)->locals_limit) {
        return 0;
    }

    if (vm->call_frame_count > 0U) {
        base = vm->call_frames[vm->call_frame_count - 1U].locals_base;
        if (vm->call_frames[vm->call_frame_count - 1U].frame_base > vm->stack_count ||
            base > vm->locals_count) {
            return 0;
        }
    }
    if (base >= AIVM_VM_LOCALS_CAPACITY || index >= (AIVM_VM_LOCALS_CAPACITY - base)) {
        return 0;
    }
    if (!aivm_size_add_checked(base, index, &absolute_index)) {
        return 0;
    }
    if (absolute_index >= vm->locals_count) {
        return 0;
    }

    *out_value = vm->locals[absolute_index];
    return 1;
}
