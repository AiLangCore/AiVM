#include "aivm_vm_internal.h"

#include <stdlib.h>
#include <string.h>

static uint64_t string_hash(const char* input, size_t length)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;

    for (i = 0U; i < length; i += 1U) {
        hash ^= (uint8_t)input[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash == 0U ? UINT64_C(1) : hash;
}

static size_t string_intern_slot(uint64_t hash, size_t capacity)
{
    return (size_t)(hash & (uint64_t)(capacity - 1U));
}

static int string_intern_insert(
    AivmStringInternEntry* entries,
    size_t capacity,
    uint64_t hash,
    size_t offset,
    size_t length)
{
    size_t slot;
    size_t probes;

    if (entries == NULL || capacity == 0U) {
        return 0;
    }
    slot = string_intern_slot(hash, capacity);
    for (probes = 0U; probes < capacity; probes += 1U) {
        AivmStringInternEntry* entry = &entries[slot];
        if (entry->hash == 0U) {
            entry->hash = hash;
            entry->offset = offset;
            entry->length = length;
            return 1;
        }
        slot = (slot + 1U) & (capacity - 1U);
    }
    return 0;
}

static int string_intern_resize(AivmVm* vm, size_t requested_capacity)
{
    AivmStringInternEntry* entries;
    size_t capacity = 1024U;
    size_t i;

    if (vm == NULL) {
        return 0;
    }
    while (capacity < requested_capacity) {
        if (capacity > ((size_t)-1 / 2U)) {
            return 0;
        }
        capacity *= 2U;
    }
    entries = (AivmStringInternEntry*)calloc(capacity, sizeof(entries[0]));
    if (entries == NULL) {
        return 0;
    }
    for (i = 0U; i < vm->string_intern_capacity; i += 1U) {
        const AivmStringInternEntry* old_entry = &vm->string_intern_entries[i];
        if (old_entry->hash != 0U &&
            !string_intern_insert(entries, capacity, old_entry->hash, old_entry->offset, old_entry->length)) {
            free(entries);
            return 0;
        }
    }
    free(vm->string_intern_entries);
    vm->string_intern_entries = entries;
    vm->string_intern_capacity = capacity;
    return 1;
}

void aivm_vm_reset_string_intern_index(AivmVm* vm)
{
    if (vm == NULL) {
        return;
    }
    if (vm->string_intern_entries != NULL && vm->string_intern_capacity > 0U) {
        memset(vm->string_intern_entries, 0, vm->string_intern_capacity * sizeof(vm->string_intern_entries[0]));
    }
    vm->string_intern_count = 0U;
    vm->string_intern_complete = 1;
}

void aivm_vm_rebuild_string_intern_index(AivmVm* vm)
{
    size_t offset = 0U;
    size_t count = 0U;

    if (vm == NULL) {
        return;
    }
    while (offset < vm->string_arena_used) {
        size_t length = strlen(&vm->string_arena[offset]);
        if (length >= vm->string_arena_used - offset) {
            vm->string_intern_complete = 0;
            return;
        }
        count += 1U;
        offset += length + 1U;
    }
    aivm_vm_reset_string_intern_index(vm);
    if (count == 0U) {
        return;
    }
    if (!string_intern_resize(vm, count * 2U)) {
        vm->string_intern_complete = 0;
        return;
    }
    offset = 0U;
    while (offset < vm->string_arena_used) {
        size_t length = strlen(&vm->string_arena[offset]);
        uint64_t hash = string_hash(&vm->string_arena[offset], length);
        if (!string_intern_insert(vm->string_intern_entries, vm->string_intern_capacity, hash, offset, length)) {
            vm->string_intern_complete = 0;
            return;
        }
        vm->string_intern_count += 1U;
        offset += length + 1U;
    }
}

static char* lookup_string_in_index(AivmVm* vm, const char* input, size_t length)
{
    uint64_t hash;
    size_t slot;
    size_t probes;

    if (vm == NULL || input == NULL || !vm->string_intern_complete || vm->string_intern_capacity == 0U) {
        return NULL;
    }
    hash = string_hash(input, length);
    slot = string_intern_slot(hash, vm->string_intern_capacity);
    for (probes = 0U; probes < vm->string_intern_capacity; probes += 1U) {
        const AivmStringInternEntry* entry = &vm->string_intern_entries[slot];
        if (entry->hash == 0U) {
            return NULL;
        }
        if (entry->hash == hash && entry->length == length &&
            memcmp(&vm->string_arena[entry->offset], input, length) == 0) {
            return &vm->string_arena[entry->offset];
        }
        slot = (slot + 1U) & (vm->string_intern_capacity - 1U);
    }
    return NULL;
}

static void remember_string_in_index(AivmVm* vm, const char* input, size_t length)
{
    size_t offset;
    uint64_t hash;

    if (vm == NULL || input == NULL || !vm->string_intern_complete) {
        return;
    }
    if (vm->string_intern_capacity == 0U ||
        (vm->string_intern_count + 1U) * 10U >= vm->string_intern_capacity * 7U) {
        size_t requested = vm->string_intern_capacity == 0U ? 1024U : vm->string_intern_capacity * 2U;
        if (!string_intern_resize(vm, requested)) {
            vm->string_intern_complete = 0;
            return;
        }
    }
    offset = (size_t)(input - vm->string_arena);
    hash = string_hash(input, length);
    if (string_intern_insert(vm->string_intern_entries, vm->string_intern_capacity, hash, offset, length)) {
        vm->string_intern_count += 1U;
    } else {
        vm->string_intern_complete = 0;
    }
}

static char* lookup_string_in_arena(AivmVm* vm, const char* input)
{
    size_t offset = 0U;
    size_t next_offset;
    size_t length;
    char* indexed;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    if (vm->string_arena_used > 0U &&
        input >= vm->string_arena &&
        input < (vm->string_arena + vm->string_arena_used)) {
        return (char*)input;
    }
    length = strlen(input);
    indexed = lookup_string_in_index(vm, input, length);
    if (indexed != NULL || vm->string_intern_complete) {
        return indexed;
    }
    while (offset < vm->string_arena_used) {
        const char* candidate = &vm->string_arena[offset];
        if (strcmp(candidate, input) == 0) {
            return (char*)candidate;
        }
        while (offset < vm->string_arena_used && vm->string_arena[offset] != '\0') {
            if (!aivm_size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
        if (offset < vm->string_arena_used) {
            if (!aivm_size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
    }
    return NULL;
}

static char* lookup_string_range_in_arena(AivmVm* vm, const char* input, size_t length)
{
    size_t offset = 0U;
    size_t next_offset;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    {
        char* indexed = lookup_string_in_index(vm, input, length);
        if (indexed != NULL || vm->string_intern_complete) {
            return indexed;
        }
    }
    while (offset < vm->string_arena_used) {
        char* candidate = &vm->string_arena[offset];
        size_t candidate_length = strlen(candidate);
        if (candidate_length == length && memcmp(candidate, input, length) == 0) {
            return candidate;
        }
        if (!aivm_size_add_checked(offset, candidate_length, &offset)) {
            return NULL;
        }
        if (offset < vm->string_arena_used) {
            if (!aivm_size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
    }
    return NULL;
}

static char* alloc_temp_string_copy(const char* input, size_t length)
{
    char* copy = NULL;
    size_t bytes_needed = 0U;
    if (input == NULL) {
        return NULL;
    }
    if (!aivm_size_add_checked(length, 1U, &bytes_needed)) {
        return NULL;
    }
    copy = (char*)malloc(bytes_needed);
    if (copy == NULL) {
        return NULL;
    }
    if (length > 0U) {
        memcpy(copy, input, length);
    }
    copy[length] = '\0';
    return copy;
}

const char* aivm_vm_snapshot_arena_backed_string(
    AivmVm* vm,
    const char* input,
    size_t length,
    char** out_temp_copy)
{
    if (out_temp_copy != NULL) {
        *out_temp_copy = NULL;
    }
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    if (!aivm_pointer_in_string_arena(vm, input)) {
        return input;
    }
    if (out_temp_copy == NULL) {
        return NULL;
    }
    *out_temp_copy = alloc_temp_string_copy(input, length);
    if (*out_temp_copy == NULL) {
        return NULL;
    }
    return *out_temp_copy;
}

char* aivm_vm_copy_string_to_arena(AivmVm* vm, const char* input)
{
    size_t length = 0U;
    size_t bytes_needed = 0U;
    size_t i;
    size_t next_length;
    char* output;
    char* source_copy = NULL;
    const char* source = input;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    output = lookup_string_in_arena(vm, input);
    if (output != NULL) {
        return output;
    }
    while (input[length] != '\0') {
        if (!aivm_size_add_checked(length, 1U, &next_length)) {
            return NULL;
        }
        length = next_length;
    }
    if (!aivm_size_add_checked(length, 1U, &bytes_needed)) {
        return NULL;
    }
    source = aivm_vm_snapshot_arena_backed_string(vm, input, length, &source_copy);
    if (source == NULL) {
        return NULL;
    }
    output = aivm_string_arena_alloc(vm, bytes_needed);
    if (output == NULL) {
        free(source_copy);
        return NULL;
    }
    for (i = 0U; i < length; i += 1U) {
        output[i] = source[i];
    }
    output[length] = '\0';
    remember_string_in_index(vm, output, length);
    free(source_copy);
    return output;
}

char* aivm_vm_copy_string_range_to_arena(AivmVm* vm, const char* input, size_t length)
{
    char* output;
    size_t i;
    size_t bytes_needed = 0U;
    char* source_copy = NULL;
    const char* source = input;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    output = lookup_string_range_in_arena(vm, input, length);
    if (output != NULL) {
        return output;
    }
    if (!aivm_size_add_checked(length, 1U, &bytes_needed)) {
        return NULL;
    }
    source = aivm_vm_snapshot_arena_backed_string(vm, input, length, &source_copy);
    if (source == NULL) {
        return NULL;
    }
    output = aivm_string_arena_alloc(vm, bytes_needed);
    if (output == NULL) {
        free(source_copy);
        return NULL;
    }
    for (i = 0U; i < length; i += 1U) {
        output[i] = source[i];
    }
    output[length] = '\0';
    remember_string_in_index(vm, output, length);
    free(source_copy);
    return output;
}

char* aivm_vm_copy_string_splice_to_arena(
    AivmVm* vm,
    const char* prefix,
    size_t prefix_length,
    const char* suffix,
    size_t suffix_length)
{
    size_t offset = 0U;
    size_t next_offset;
    size_t total_length;
    size_t bytes_needed = 0U;
    char* output;
    size_t i;
    char* prefix_copy = NULL;
    char* suffix_copy = NULL;
    const char* prefix_source = prefix;
    const char* suffix_source = suffix;
    if (vm == NULL || prefix == NULL || suffix == NULL) {
        return NULL;
    }
    if (!aivm_size_add_checked(prefix_length, suffix_length, &total_length) ||
        !aivm_size_add_checked(total_length, 1U, &bytes_needed)) {
        return NULL;
    }
    while (offset < vm->string_arena_used) {
        char* candidate = &vm->string_arena[offset];
        size_t candidate_length = strlen(candidate);
        if (candidate_length == total_length &&
            memcmp(candidate, prefix, prefix_length) == 0 &&
            memcmp(candidate + prefix_length, suffix, suffix_length) == 0) {
            return candidate;
        }
        if (!aivm_size_add_checked(offset, candidate_length, &offset)) {
            return NULL;
        }
        if (offset < vm->string_arena_used) {
            if (!aivm_size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
    }
    prefix_source = aivm_vm_snapshot_arena_backed_string(vm, prefix, prefix_length, &prefix_copy);
    if (prefix_source == NULL) {
        return NULL;
    }
    suffix_source = aivm_vm_snapshot_arena_backed_string(vm, suffix, suffix_length, &suffix_copy);
    if (suffix_source == NULL) {
        free(prefix_copy);
        return NULL;
    }
    output = aivm_string_arena_alloc(vm, bytes_needed);
    if (output == NULL) {
        free(prefix_copy);
        free(suffix_copy);
        return NULL;
    }
    for (i = 0U; i < prefix_length; i += 1U) {
        output[i] = prefix_source[i];
    }
    for (i = 0U; i < suffix_length; i += 1U) {
        output[prefix_length + i] = suffix_source[i];
    }
    output[total_length] = '\0';
    remember_string_in_index(vm, output, total_length);
    free(prefix_copy);
    free(suffix_copy);
    return output;
}
