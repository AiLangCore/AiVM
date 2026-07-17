#include "aivm_vm_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t arena_grow_limit(size_t current, size_t step, size_t max_value)
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

static int ensure_string_arena_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->string_arena_limit && vm->string_arena_limit < vm->string_arena_capacity) {
        vm->string_arena_limit = arena_grow_limit(vm->string_arena_limit, AIVM_VM_STRING_ARENA_GROWTH_STEP, vm->string_arena_capacity);
    }
    return needed <= vm->string_arena_limit;
}

static int ensure_bytes_arena_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->bytes_arena_limit && vm->bytes_arena_limit < vm->bytes_arena_capacity) {
        vm->bytes_arena_limit = arena_grow_limit(vm->bytes_arena_limit, AIVM_VM_BYTES_ARENA_GROWTH_STEP, vm->bytes_arena_capacity);
    }
    return needed <= vm->bytes_arena_limit;
}

int aivm_pointer_in_string_arena(const AivmVm* vm, const char* text)
{
    if (vm == NULL || text == NULL || vm->string_arena_used == 0U) {
        return 0;
    }
    return text >= vm->string_arena &&
           text < (vm->string_arena + vm->string_arena_used);
}

char* aivm_string_arena_alloc(AivmVm* vm, size_t size)
{
    char* start;
    size_t needed = 0U;
    if (vm == NULL) {
        return NULL;
    }
    if (!aivm_size_add_checked(vm->string_arena_used, size, &needed)) {
        aivm_counter_increment_saturating(&vm->string_arena_pressure_count);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
        return NULL;
    }
    if (needed > vm->string_arena_limit) {
        if (!aivm_compact_string_arena(vm)) {
            aivm_counter_increment_saturating(&vm->string_arena_pressure_count);
            if (vm->status != AIVM_VM_STATUS_ERROR) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
            }
            return NULL;
        }
        if (!aivm_size_add_checked(vm->string_arena_used, size, &needed)) {
            aivm_counter_increment_saturating(&vm->string_arena_pressure_count);
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
            return NULL;
        }
    }
    if (needed > vm->string_arena_limit &&
        !ensure_string_arena_capacity(vm, needed)) {
        aivm_counter_increment_saturating(&vm->string_arena_pressure_count);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
        return NULL;
    }

    start = &vm->string_arena[vm->string_arena_used];
    vm->string_arena_used = needed;
    if (vm->string_arena_used > vm->string_arena_high_water) {
        vm->string_arena_high_water = vm->string_arena_used;
    }
    return start;
}

uint8_t* aivm_bytes_arena_alloc(AivmVm* vm, size_t size)
{
    uint8_t* start;
    size_t needed = 0U;
    if (vm == NULL) {
        return NULL;
    }
    if (!aivm_size_add_checked(vm->bytes_arena_used, size, &needed)) {
        aivm_counter_increment_saturating(&vm->bytes_arena_pressure_count);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM002: bytes arena capacity exceeded.");
        return NULL;
    }
    if (needed > vm->bytes_arena_limit &&
        !ensure_bytes_arena_capacity(vm, needed)) {
        aivm_counter_increment_saturating(&vm->bytes_arena_pressure_count);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM002: bytes arena capacity exceeded.");
        return NULL;
    }
    start = &vm->bytes_arena[vm->bytes_arena_used];
    vm->bytes_arena_used = needed;
    if (vm->bytes_arena_used > vm->bytes_arena_high_water) {
        vm->bytes_arena_high_water = vm->bytes_arena_used;
    }
    return start;
}

typedef struct {
    const uint8_t* old_data;
    size_t length;
    const uint8_t* new_data;
} AivmBytesRelocation;

static int pointer_in_bytes_arena(const AivmVm* vm, const uint8_t* data, size_t length)
{
    uintptr_t arena_start;
    uintptr_t arena_end;
    uintptr_t data_start;
    if (vm == NULL || data == NULL || vm->bytes_arena_used == 0U) {
        return 0;
    }
    arena_start = (uintptr_t)vm->bytes_arena;
    arena_end = arena_start + vm->bytes_arena_used;
    data_start = (uintptr_t)data;
    return data_start >= arena_start && data_start <= arena_end && length <= (size_t)(arena_end - data_start);
}

static int compact_relocate_value_bytes(
    AivmVm* vm,
    AivmValue* value,
    uint8_t* new_arena,
    size_t* new_used,
    AivmBytesRelocation* relocations,
    size_t* relocation_count,
    size_t relocation_capacity)
{
    size_t i;
    size_t next_used;
    uint8_t* destination;
    if (vm == NULL || value == NULL || new_arena == NULL || new_used == NULL ||
        relocations == NULL || relocation_count == NULL) {
        return 0;
    }
    if (value->type != AIVM_VAL_BYTES || value->bytes_value.data == NULL ||
        !pointer_in_bytes_arena(vm, value->bytes_value.data, value->bytes_value.length)) {
        return 1;
    }
    for (i = 0U; i < *relocation_count; i += 1U) {
        if (relocations[i].old_data == value->bytes_value.data &&
            relocations[i].length == value->bytes_value.length) {
            value->bytes_value.data = relocations[i].new_data;
            return 1;
        }
    }
    if (*relocation_count >= relocation_capacity ||
        !aivm_size_add_checked(*new_used, value->bytes_value.length, &next_used) ||
        next_used > vm->bytes_arena_capacity) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM002: bytes arena capacity exceeded during compaction.");
        return 0;
    }
    destination = &new_arena[*new_used];
    if (value->bytes_value.length > 0U) {
        memcpy(destination, value->bytes_value.data, value->bytes_value.length);
    }
    relocations[*relocation_count].old_data = value->bytes_value.data;
    relocations[*relocation_count].length = value->bytes_value.length;
    relocations[*relocation_count].new_data = destination;
    *relocation_count += 1U;
    *new_used = next_used;
    value->bytes_value.data = destination;
    return 1;
}

int aivm_compact_bytes_arena(AivmVm* vm)
{
    uint8_t* live_pairs = NULL;
    uint8_t* old_arena;
    uint8_t* new_arena = NULL;
    AivmBytesRelocation* relocations = NULL;
    size_t relocation_capacity = 0U;
    size_t relocation_count = 0U;
    size_t new_used = 0U;
    size_t pair_value_capacity = 0U;
    size_t i;
    if (vm == NULL) {
        return 0;
    }
    if (vm->bytes_arena_used == 0U) {
        return 1;
    }
    if (!aivm_size_add_checked(vm->stack_count, vm->locals_count, &relocation_capacity) ||
        !aivm_size_add_checked(relocation_capacity, vm->completed_task_count, &relocation_capacity) ||
        !aivm_size_add_checked(relocation_capacity, vm->par_value_count, &relocation_capacity) ||
        !aivm_size_add_checked(vm->scratch_pair_count, vm->scratch_pair_count, &pair_value_capacity) ||
        !aivm_size_add_checked(relocation_capacity, pair_value_capacity, &relocation_capacity)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM002: bytes compaction root count overflow.");
        return 0;
    }
    if (relocation_capacity == 0U) {
        vm->bytes_arena_used = 0U;
        vm->bytes_arena_gc_threshold = vm->bytes_arena_capacity < AIVM_VM_BYTES_ARENA_INITIAL_CAPACITY
            ? vm->bytes_arena_capacity
            : AIVM_VM_BYTES_ARENA_INITIAL_CAPACITY;
        return 1;
    }
    live_pairs = (uint8_t*)calloc(AIVM_VM_SCRATCH_PAIR_CAPACITY, sizeof(live_pairs[0]));
    new_arena = (uint8_t*)calloc(vm->bytes_arena_capacity, sizeof(new_arena[0]));
    relocations = (AivmBytesRelocation*)calloc(relocation_capacity, sizeof(relocations[0]));
    if (live_pairs == NULL || new_arena == NULL || relocations == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM002: bytes arena compaction workspace allocation failed.");
        goto fail;
    }
    if (!aivm_vm_mark_live_scratch_pair_handles(vm, live_pairs)) {
        goto fail;
    }
#define RELOCATE_BYTES(value_ptr) \
    do { \
        if (!compact_relocate_value_bytes( \
                vm, (value_ptr), new_arena, &new_used, relocations, &relocation_count, relocation_capacity)) { \
            goto fail; \
        } \
    } while (0)
    for (i = 0U; i < vm->stack_count; i += 1U) {
        RELOCATE_BYTES(&vm->stack[i]);
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        RELOCATE_BYTES(&vm->locals[i]);
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        RELOCATE_BYTES(&vm->completed_tasks[i].result);
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        RELOCATE_BYTES(&vm->par_values[i]);
    }
    for (i = 0U; i < vm->scratch_pair_count; i += 1U) {
        if (live_pairs[i] != 0U) {
            RELOCATE_BYTES(&vm->scratch_pairs[i].first);
            RELOCATE_BYTES(&vm->scratch_pairs[i].second);
        }
    }
#undef RELOCATE_BYTES
    old_arena = vm->bytes_arena;
    vm->bytes_arena = new_arena;
    vm->bytes_arena_storage_capacity = vm->bytes_arena_capacity;
    vm->bytes_arena_used = new_used;
    vm->bytes_arena_gc_threshold = arena_grow_limit(
        new_used,
        AIVM_VM_BYTES_ARENA_INITIAL_CAPACITY,
        vm->bytes_arena_capacity);
    free(old_arena);
    free(live_pairs);
    free(relocations);
    return 1;

fail:
#undef RELOCATE_BYTES
    free(live_pairs);
    free(new_arena);
    free(relocations);
    return 0;
}
