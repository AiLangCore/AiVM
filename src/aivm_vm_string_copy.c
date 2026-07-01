#include "aivm_vm_internal.h"

#include <stdlib.h>
#include <string.h>

static char* lookup_string_in_arena(AivmVm* vm, const char* input)
{
    size_t offset = 0U;
    size_t next_offset;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    if (vm->string_arena_used > 0U &&
        input >= vm->string_arena &&
        input < (vm->string_arena + vm->string_arena_used)) {
        return (char*)input;
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
    free(prefix_copy);
    free(suffix_copy);
    return output;
}
