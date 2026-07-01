#include "aivm_vm_internal.h"

void aivm_vm_record_recent_call(
    AivmVm* vm,
    size_t instruction_pointer,
    size_t target,
    size_t arg_count,
    size_t stack_count)
{
    size_t i;
    if (vm == NULL) {
        return;
    }
    for (i = sizeof(vm->recent_calls) / sizeof(vm->recent_calls[0]); i > 1U; i -= 1U) {
        vm->recent_calls[i - 1U] = vm->recent_calls[i - 2U];
    }
    vm->recent_calls[0].instruction_pointer = instruction_pointer;
    vm->recent_calls[0].target = target;
    vm->recent_calls[0].arg_count = arg_count;
    vm->recent_calls[0].stack_count = stack_count;
    if (vm->recent_call_count < (sizeof(vm->recent_calls) / sizeof(vm->recent_calls[0]))) {
        vm->recent_call_count += 1U;
    }
}

void aivm_vm_record_recent_return(
    AivmVm* vm,
    size_t instruction_pointer,
    size_t stack_count,
    size_t pre_restore_stack_count,
    size_t frame_base,
    int has_return_value)
{
    size_t i;
    if (vm == NULL) {
        return;
    }
    for (i = sizeof(vm->recent_returns) / sizeof(vm->recent_returns[0]); i > 1U; i -= 1U) {
        vm->recent_returns[i - 1U] = vm->recent_returns[i - 2U];
    }
    vm->recent_returns[0].instruction_pointer = instruction_pointer;
    vm->recent_returns[0].stack_count = stack_count;
    vm->recent_returns[0].pre_restore_stack_count = pre_restore_stack_count;
    vm->recent_returns[0].frame_base = frame_base;
    vm->recent_returns[0].has_return_value = has_return_value;
    if (vm->recent_return_count < (sizeof(vm->recent_returns) / sizeof(vm->recent_returns[0]))) {
        vm->recent_return_count += 1U;
    }
}

void aivm_vm_record_recent_opcode(
    AivmVm* vm,
    size_t instruction_pointer,
    int opcode,
    size_t stack_count)
{
    size_t i;
    if (vm == NULL) {
        return;
    }
    for (i = sizeof(vm->recent_opcodes) / sizeof(vm->recent_opcodes[0]); i > 1U; i -= 1U) {
        vm->recent_opcodes[i - 1U] = vm->recent_opcodes[i - 2U];
    }
    vm->recent_opcodes[0].instruction_pointer = instruction_pointer;
    vm->recent_opcodes[0].opcode = opcode;
    vm->recent_opcodes[0].stack_count = stack_count;
    if (vm->recent_opcode_count < (sizeof(vm->recent_opcodes) / sizeof(vm->recent_opcodes[0]))) {
        vm->recent_opcode_count += 1U;
    }
}
