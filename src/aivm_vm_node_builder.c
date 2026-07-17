#include "aivm_vm_internal.h"

#include <stdlib.h>
#include <string.h>

static char* copy_text(const char* input)
{
    size_t length;
    char* copy;
    if (input == NULL) return NULL;
    length = strlen(input);
    copy = (char*)malloc(length + 1U);
    if (copy == NULL) return NULL;
    memcpy(copy, input, length + 1U);
    return copy;
}

static void dispose_builder(AivmNodeBuilderRecord* builder)
{
    size_t i;
    if (builder == NULL) return;
    free(builder->kind);
    free(builder->id);
    for (i = 0U; i < builder->attr_count; i += 1U) {
        free((char*)builder->attrs[i].key);
        if (builder->attrs[i].kind == AIVM_NODE_ATTR_STRING) {
            free((char*)builder->attrs[i].string_value);
        }
    }
    free(builder->attrs);
    free(builder->children);
    memset(builder, 0, sizeof(*builder));
}

static AivmNodeBuilderRecord* lookup_builder(AivmVm* vm, int64_t handle)
{
    if (vm == NULL || handle <= 0 || handle > (int64_t)AIVM_VM_NODE_BUILDER_CAPACITY) return NULL;
    if (!vm->node_builders[handle - 1].active) return NULL;
    return &vm->node_builders[handle - 1];
}

static int grow_array(void** data, size_t item_size, size_t* capacity, size_t required)
{
    size_t next = *capacity == 0U ? 4U : *capacity;
    void* replacement;
    while (next < required) {
        if (next > ((size_t)-1) / 2U) return 0;
        next *= 2U;
    }
    replacement = realloc(*data, next * item_size);
    if (replacement == NULL) return 0;
    *data = replacement;
    *capacity = next;
    return 1;
}

void aivm_vm_reset_node_builders(AivmVm* vm)
{
    size_t i;
    if (vm == NULL) return;
    for (i = 0U; i < AIVM_VM_NODE_BUILDER_CAPACITY; i += 1U) dispose_builder(&vm->node_builders[i]);
}

int aivm_vm_node_builder_new(AivmVm* vm, const char* kind, const char* id, int64_t* out_handle)
{
    size_t i;
    AivmNodeBuilderRecord* builder;
    if (vm == NULL || kind == NULL || id == NULL || out_handle == NULL) return 0;
    for (i = 0U; i < AIVM_VM_NODE_BUILDER_CAPACITY; i += 1U) {
        if (!vm->node_builders[i].active) break;
    }
    if (i == AIVM_VM_NODE_BUILDER_CAPACITY) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "NODE_BUILDER_NEW capacity exceeded.");
        return 0;
    }
    builder = &vm->node_builders[i];
    builder->kind = copy_text(kind);
    builder->id = copy_text(id);
    if (builder->kind == NULL || builder->id == NULL) {
        dispose_builder(builder);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "NODE_BUILDER_NEW allocation failed.");
        return 0;
    }
    builder->active = 1;
    *out_handle = (int64_t)(i + 1U);
    return 1;
}

int aivm_vm_node_builder_append_child(AivmVm* vm, int64_t builder_handle, int64_t child_handle)
{
    AivmNodeBuilderRecord* builder = lookup_builder(vm, builder_handle);
    const AivmNodeRecord* child = NULL;
    if (builder == NULL || !aivm_vm_lookup_node(vm, child_handle, &child)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "NODE_BUILDER_APPEND_CHILD requires (builder,node).");
        return 0;
    }
    (void)child;
    if (builder->child_count == builder->child_capacity &&
        !grow_array((void**)&builder->children, sizeof(builder->children[0]), &builder->child_capacity, builder->child_count + 1U)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "NODE_BUILDER_APPEND_CHILD allocation failed.");
        return 0;
    }
    builder->children[builder->child_count++] = child_handle;
    return 1;
}

int aivm_vm_node_builder_append_attr(AivmVm* vm, int64_t builder_handle, int64_t attr_handle)
{
    AivmNodeBuilderRecord* builder = lookup_builder(vm, builder_handle);
    const AivmNodeRecord* attr = NULL;
    AivmNodeAttr value;
    size_t attr_slot;
    if (builder == NULL || !aivm_vm_lookup_node(vm, attr_handle, &attr) || attr->attr_count != 1U ||
        attr->attr_start >= vm->node_attr_count) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "NODE_BUILDER_APPEND_ATTR requires (builder,single-attr-node).");
        return 0;
    }
    attr_slot = attr->attr_start;
    value = vm->node_attrs[attr_slot];
    value.key = copy_text(attr->id);
    if (value.kind == AIVM_NODE_ATTR_STRING) value.string_value = copy_text(value.string_value);
    if (value.key == NULL || (value.kind == AIVM_NODE_ATTR_STRING && value.string_value == NULL)) {
        free((char*)value.key);
        free((char*)value.string_value);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "NODE_BUILDER_APPEND_ATTR allocation failed.");
        return 0;
    }
    if (builder->attr_count == builder->attr_capacity &&
        !grow_array((void**)&builder->attrs, sizeof(builder->attrs[0]), &builder->attr_capacity, builder->attr_count + 1U)) {
        free((char*)value.key);
        if (value.kind == AIVM_NODE_ATTR_STRING) free((char*)value.string_value);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "NODE_BUILDER_APPEND_ATTR allocation failed.");
        return 0;
    }
    builder->attrs[builder->attr_count++] = value;
    return 1;
}

int aivm_vm_node_builder_finish(AivmVm* vm, int64_t builder_handle, int64_t* out_node_handle)
{
    AivmNodeBuilderRecord* builder = lookup_builder(vm, builder_handle);
    int result;
    if (builder == NULL || out_node_handle == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "NODE_BUILDER_FINISH requires builder operand.");
        return 0;
    }
    result = aivm_vm_create_node_record(vm, builder->kind, builder->id, builder->attrs, builder->attr_count,
        builder->children, builder->child_count, out_node_handle);
    if (result) dispose_builder(builder);
    return result;
}
