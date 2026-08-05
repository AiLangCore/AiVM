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
    size_t bytes_capacity;
    size_t string_capacity;
    uint8_t* resized_bytes;
    char* replacement_strings;
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
    if (vm->string_arena_capacity == 0U) {
        vm->string_arena_capacity = AIVM_VM_STRING_ARENA_CAPACITY;
    }
    string_capacity = vm->string_arena_capacity;
    if (vm->string_arena == NULL) {
        vm->string_arena = (char*)calloc(string_capacity, sizeof(vm->string_arena[0]));
        if (vm->string_arena != NULL) {
            vm->string_arena_storage_capacity = string_capacity;
        }
    } else if (vm->string_arena_storage_capacity < string_capacity) {
        /* Reset follows immediately; replacing avoids invalidating live pointers with realloc. */
        replacement_strings = (char*)calloc(string_capacity, sizeof(vm->string_arena[0]));
        if (replacement_strings != NULL) {
            free(vm->string_arena);
            vm->string_arena = replacement_strings;
            vm->string_arena_storage_capacity = string_capacity;
        }
    }
    if (vm->bytes_arena_capacity == 0U && vm->bytes_arena_adaptive == 0) {
        vm->bytes_arena_capacity = AIVM_VM_BYTES_ARENA_CAPACITY;
    }
    bytes_capacity = vm->bytes_arena_adaptive != 0
        ? AIVM_VM_TOOLING_BYTES_ARENA_INITIAL_CAPACITY
        : vm->bytes_arena_capacity;
    if (vm->bytes_arena == NULL) {
        vm->bytes_arena = (uint8_t*)calloc(bytes_capacity, sizeof(vm->bytes_arena[0]));
        if (vm->bytes_arena != NULL) {
            vm->bytes_arena_storage_capacity = bytes_capacity;
        }
    } else if (vm->bytes_arena_storage_capacity < bytes_capacity) {
        resized_bytes = (uint8_t*)realloc(vm->bytes_arena, bytes_capacity * sizeof(vm->bytes_arena[0]));
        if (resized_bytes != NULL) {
            vm->bytes_arena = resized_bytes;
            vm->bytes_arena_storage_capacity = bytes_capacity;
        }
    }
    if (vm->nodes == NULL) {
        vm->nodes = (AivmNodeRecord*)calloc(vm->node_capacity, sizeof(vm->nodes[0]));
    }
    if (vm->node_attrs == NULL) {
        vm->node_attrs = (AivmNodeAttr*)calloc(vm->node_attr_capacity, sizeof(vm->node_attrs[0]));
    }
    if (vm->node_children == NULL) {
        vm->node_children = (int64_t*)calloc(vm->node_child_capacity, sizeof(vm->node_children[0]));
    }
    if (vm->scratch_pairs == NULL) {
        vm->scratch_pairs = (AivmScratchPair*)calloc(AIVM_VM_SCRATCH_PAIR_CAPACITY, sizeof(vm->scratch_pairs[0]));
    }
    if (vm->stack == NULL ||
        vm->locals == NULL ||
        vm->string_arena == NULL ||
        vm->string_arena_storage_capacity < string_capacity ||
        vm->bytes_arena == NULL ||
        vm->bytes_arena_storage_capacity < bytes_capacity ||
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
