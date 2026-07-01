#include "aivm_vm_internal.h"
#include <stdlib.h>

int aivm_size_add_checked(size_t a, size_t b, size_t* out)
{
    if (out == NULL) {
        return 0;
    }
    if (a > ((size_t)-1 - b)) {
        return 0;
    }
    *out = a + b;
    return 1;
}

int aivm_vm_ensure_storage(AivmVm* vm)
{
    if (vm == NULL) {
        return 0;
    }
    vm->storage_magic = AIVM_VM_STORAGE_MAGIC;
    if (vm->stack == NULL) {
        vm->stack = (AivmValue*)calloc(AIVM_VM_STACK_CAPACITY, sizeof(vm->stack[0]));
    }
    if (vm->locals == NULL) {
        vm->locals = (AivmValue*)calloc(AIVM_VM_LOCALS_CAPACITY, sizeof(vm->locals[0]));
    }
    if (vm->string_arena == NULL) {
        vm->string_arena = (char*)calloc(AIVM_VM_STRING_ARENA_CAPACITY, sizeof(vm->string_arena[0]));
    }
    if (vm->bytes_arena == NULL) {
        vm->bytes_arena = (uint8_t*)calloc(AIVM_VM_BYTES_ARENA_CAPACITY, sizeof(vm->bytes_arena[0]));
    }
    if (vm->nodes == NULL) {
        vm->nodes = (AivmNodeRecord*)calloc(AIVM_VM_NODE_CAPACITY, sizeof(vm->nodes[0]));
    }
    if (vm->node_attrs == NULL) {
        vm->node_attrs = (AivmNodeAttr*)calloc(AIVM_VM_NODE_ATTR_CAPACITY, sizeof(vm->node_attrs[0]));
    }
    if (vm->node_children == NULL) {
        vm->node_children = (int64_t*)calloc(AIVM_VM_NODE_CHILD_CAPACITY, sizeof(vm->node_children[0]));
    }
    if (vm->scratch_pairs == NULL) {
        vm->scratch_pairs = (AivmScratchPair*)calloc(AIVM_VM_SCRATCH_PAIR_CAPACITY, sizeof(vm->scratch_pairs[0]));
    }
    if (vm->stack == NULL ||
        vm->locals == NULL ||
        vm->string_arena == NULL ||
        vm->bytes_arena == NULL ||
        vm->nodes == NULL ||
        vm->node_attrs == NULL ||
        vm->node_children == NULL ||
        vm->scratch_pairs == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: VM storage allocation failed.");
        return 0;
    }
    return 1;
}

void aivm_counter_increment_saturating(size_t* counter)
{
    size_t next_value;
    if (counter == NULL) {
        return;
    }
    if (aivm_size_add_checked(*counter, 1U, &next_value)) {
        *counter = next_value;
    } else {
        *counter = (size_t)-1;
    }
}
