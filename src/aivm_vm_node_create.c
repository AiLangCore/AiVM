#include "aivm_vm_internal.h"

#include <stdlib.h>
#include <string.h>

static char* snapshot_node_input_copy(const char* input, size_t length)
{
    char* copy;
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

static int snapshot_node_input_string(
    AivmVm* vm,
    const char* input,
    const char** out_source,
    char** out_temp_copy)
{
    size_t length = 0U;
    size_t next_length = 0U;
    if (out_source != NULL) {
        *out_source = NULL;
    }
    if (out_temp_copy != NULL) {
        *out_temp_copy = NULL;
    }
    if (vm == NULL || input == NULL || out_source == NULL || out_temp_copy == NULL) {
        return 0;
    }
    while (input[length] != '\0') {
        if (!aivm_size_add_checked(length, 1U, &next_length)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string snapshot length overflow.");
            return 0;
        }
        length = next_length;
    }
    if (!aivm_pointer_in_string_arena(vm, input)) {
        *out_source = input;
        return 1;
    }
    *out_temp_copy = snapshot_node_input_copy(input, length);
    if (*out_temp_copy == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string snapshot allocation failed.");
        return 0;
    }
    *out_source = *out_temp_copy;
    return 1;
}

static int prepare_compaction_scratch(
    AivmVm* vm,
    int64_t** remapped_children,
    int64_t** handle_map,
    size_t child_count)
{
    if (vm == NULL || remapped_children == NULL || handle_map == NULL) {
        return 0;
    }
    if (*handle_map == NULL) {
        *handle_map = (int64_t*)calloc(vm->node_capacity + 1U, sizeof((*handle_map)[0]));
    } else {
        memset(*handle_map, 0, (vm->node_capacity + 1U) * sizeof((*handle_map)[0]));
    }
    if (child_count > 0U && *remapped_children == NULL) {
        *remapped_children = (int64_t*)calloc(child_count, sizeof((*remapped_children)[0]));
    }
    if (*handle_map == NULL || (child_count > 0U && *remapped_children == NULL)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node compaction scratch allocation failed.");
        return 0;
    }
    return 1;
}

int aivm_vm_create_node_record(
    AivmVm* vm,
    const char* kind,
    const char* id,
    const AivmNodeAttr* attrs,
    size_t attr_count,
    const int64_t* children,
    size_t child_count,
    int64_t* out_handle)
{
    AivmNodeRecord* node;
    int64_t* remapped_children = NULL;
    const int64_t* effective_children = children;
    int64_t* handle_map = NULL;
    AivmNodeAttr* stable_attrs = NULL;
    char** attr_key_copies = NULL;
    char** attr_value_copies = NULL;
    const char* kind_source = NULL;
    const char* id_source = NULL;
    char* kind_copy = NULL;
    char* id_copy = NULL;
    size_t needed_attr_count = 0U;
    size_t needed_child_count = 0U;
    size_t needed_node_count = 0U;
    size_t string_reserve = 0U;
    size_t i;
    if (vm == NULL || kind == NULL || id == NULL || out_handle == NULL) {
        return 0;
    }
    if (attr_count > 0U && attrs == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Node attrs must be non-null when attr_count is non-zero.");
        return 0;
    }
    if (attr_count > 0U) {
        stable_attrs = (AivmNodeAttr*)calloc(attr_count, sizeof(stable_attrs[0]));
        attr_key_copies = (char**)calloc(attr_count, sizeof(attr_key_copies[0]));
        attr_value_copies = (char**)calloc(attr_count, sizeof(attr_value_copies[0]));
    }
    if (attr_count > 0U && (stable_attrs == NULL || attr_key_copies == NULL || attr_value_copies == NULL)) {
        free(remapped_children);
        free(handle_map);
        free(stable_attrs);
        free(attr_key_copies);
        free(attr_value_copies);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node allocation scratch allocation failed.");
        return 0;
    }
    if (!snapshot_node_input_string(vm, kind, &kind_source, &kind_copy) ||
        !snapshot_node_input_string(vm, id, &id_source, &id_copy)) {
        goto fail;
    }
    for (i = 0U; i < attr_count; i += 1U) {
        stable_attrs[i] = attrs[i];
        if (!snapshot_node_input_string(
                vm,
                attrs[i].key == NULL ? "" : attrs[i].key,
                &stable_attrs[i].key,
                &attr_key_copies[i])) {
            goto fail;
        }
        if (attrs[i].kind == AIVM_NODE_ATTR_IDENTIFIER || attrs[i].kind == AIVM_NODE_ATTR_STRING) {
            if (!snapshot_node_input_string(
                    vm,
                    attrs[i].string_value == NULL ? "" : attrs[i].string_value,
                    &stable_attrs[i].string_value,
                    &attr_value_copies[i])) {
                goto fail;
            }
        }
    }
    if (!aivm_size_add_checked(strlen(kind_source), 1U, &string_reserve) ||
        !aivm_size_add_checked(string_reserve, strlen(id_source), &string_reserve) ||
        !aivm_size_add_checked(string_reserve, 1U, &string_reserve)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: node string reservation overflow.");
        goto fail;
    }
    for (i = 0U; i < attr_count; i += 1U) {
        size_t attr_bytes = 0U;
        if (!aivm_size_add_checked(strlen(stable_attrs[i].key), 1U, &attr_bytes) ||
            !aivm_size_add_checked(string_reserve, attr_bytes, &string_reserve)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: node string reservation overflow.");
            goto fail;
        }
        if (stable_attrs[i].kind == AIVM_NODE_ATTR_IDENTIFIER || stable_attrs[i].kind == AIVM_NODE_ATTR_STRING) {
            if (!aivm_size_add_checked(strlen(stable_attrs[i].string_value), 1U, &attr_bytes) ||
                !aivm_size_add_checked(string_reserve, attr_bytes, &string_reserve)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: node string reservation overflow.");
                goto fail;
            }
        }
    }
    if (!aivm_string_arena_reserve(vm, string_reserve)) {
        goto fail;
    }
    if (aivm_vm_should_attempt_proactive_node_gc(vm, attr_count, child_count)) {
        if (!prepare_compaction_scratch(vm, &remapped_children, &handle_map, child_count)) {
            goto fail;
        }
        if (!aivm_vm_compact_node_arenas_with_map(vm, children, child_count, handle_map)) {
            goto fail;
        }
        if (!aivm_vm_remap_child_handles_for_compaction(vm, remapped_children, children, child_count, handle_map)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid child handle remap during proactive node GC.");
            goto fail;
        }
        if (child_count > 0U) {
            effective_children = remapped_children;
        }
        vm->node_allocations_since_gc = 0U;
    }
    if (!aivm_size_add_checked(vm->node_attr_count, attr_count, &needed_attr_count) ||
        !aivm_size_add_checked(vm->node_child_count, child_count, &needed_child_count) ||
        !aivm_size_add_checked(vm->node_count, 1U, &needed_node_count)) {
        aivm_counter_increment_saturating(&vm->node_arena_pressure_count);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node arena capacity exceeded.");
        goto fail;
    }
    if (needed_node_count > vm->node_capacity ||
        needed_attr_count > vm->node_attr_capacity ||
        needed_child_count > vm->node_child_capacity) {
        if (!prepare_compaction_scratch(vm, &remapped_children, &handle_map, child_count)) {
            goto fail;
        }
        if (!aivm_vm_compact_node_arenas_with_map(vm, effective_children, child_count, handle_map)) {
            goto fail;
        }
        if (!aivm_vm_remap_child_handles_for_compaction(vm, remapped_children, effective_children, child_count, handle_map)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid child handle remap during node GC.");
            goto fail;
        }
        if (child_count > 0U) {
            effective_children = remapped_children;
        }
        vm->node_allocations_since_gc = 0U;
        if (!aivm_size_add_checked(vm->node_attr_count, attr_count, &needed_attr_count) ||
            !aivm_size_add_checked(vm->node_child_count, child_count, &needed_child_count) ||
            !aivm_size_add_checked(vm->node_count, 1U, &needed_node_count) ||
            needed_node_count > vm->node_capacity ||
            needed_attr_count > vm->node_attr_capacity ||
            needed_child_count > vm->node_child_capacity) {
            aivm_counter_increment_saturating(&vm->node_arena_pressure_count);
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node arena capacity exceeded.");
            goto fail;
        }
    }

    node = &vm->nodes[vm->node_count];
    node->kind = aivm_vm_copy_string_to_arena(vm, kind_source);
    node->id = aivm_vm_copy_string_to_arena(vm, id_source);
    if (node->kind == NULL || node->id == NULL) {
        goto fail;
    }
    node->attr_start = vm->node_attr_count;
    node->attr_count = attr_count;
    node->child_start = vm->node_child_count;
    node->child_count = child_count;

    for (i = 0U; i < attr_count; i += 1U) {
        AivmNodeAttr attr = stable_attrs[i];
        size_t attr_slot = 0U;
        AivmNodeAttr* out_attr;
        if (!aivm_size_add_checked(vm->node_attr_count, i, &attr_slot) ||
            attr_slot >= vm->node_attr_capacity) {
            aivm_counter_increment_saturating(&vm->node_arena_pressure_count);
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node arena capacity exceeded.");
            goto fail;
        }
        out_attr = &vm->node_attrs[attr_slot];
        out_attr->key = aivm_vm_copy_string_to_arena(vm, attr.key);
        out_attr->kind = attr.kind;
        if (out_attr->key == NULL) {
            goto fail;
        }
        if (attr.kind == AIVM_NODE_ATTR_IDENTIFIER || attr.kind == AIVM_NODE_ATTR_STRING) {
            out_attr->string_value = aivm_vm_copy_string_to_arena(vm, attr.string_value == NULL ? "" : attr.string_value);
            if (out_attr->string_value == NULL) {
                goto fail;
            }
        } else if (attr.kind == AIVM_NODE_ATTR_INT) {
            out_attr->int_value = attr.int_value;
        } else {
            out_attr->bool_value = attr.bool_value != 0 ? 1 : 0;
        }
    }

    for (i = 0U; i < child_count; i += 1U) {
        size_t child_slot = 0U;
        if (!aivm_size_add_checked(vm->node_child_count, i, &child_slot) ||
            child_slot >= vm->node_child_capacity) {
            aivm_counter_increment_saturating(&vm->node_arena_pressure_count);
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node arena capacity exceeded.");
            goto fail;
        }
        vm->node_children[child_slot] = effective_children[i];
    }

    vm->node_attr_count = needed_attr_count;
    vm->node_child_count = needed_child_count;
    vm->node_count = needed_node_count;
    if (vm->node_count > vm->node_high_water) {
        vm->node_high_water = vm->node_count;
    }
    if (vm->node_attr_count > vm->node_attr_high_water) {
        vm->node_attr_high_water = vm->node_attr_count;
    }
    if (vm->node_child_count > vm->node_child_high_water) {
        vm->node_child_high_water = vm->node_child_count;
    }
    {
        size_t updated_allocations_since_gc;
        if (aivm_size_add_checked(vm->node_allocations_since_gc, 1U, &updated_allocations_since_gc)) {
            vm->node_allocations_since_gc = updated_allocations_since_gc;
        }
    }
    *out_handle = (int64_t)vm->node_count;
    for (i = 0U; i < attr_count; i += 1U) {
        free(attr_key_copies[i]);
        free(attr_value_copies[i]);
    }
    free(kind_copy);
    free(id_copy);
    free(stable_attrs);
    free(attr_key_copies);
    free(attr_value_copies);
    free(remapped_children);
    free(handle_map);
    return 1;

fail:
    for (i = 0U; i < attr_count; i += 1U) {
        if (attr_key_copies != NULL) {
            free(attr_key_copies[i]);
        }
        if (attr_value_copies != NULL) {
            free(attr_value_copies[i]);
        }
    }
    free(kind_copy);
    free(id_copy);
    free(stable_attrs);
    free(attr_key_copies);
    free(attr_value_copies);
    free(remapped_children);
    free(handle_map);
    return 0;
}
