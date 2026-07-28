#include "aivm_vm_internal.h"

#include <stdlib.h>

static int next_capacity(size_t current, size_t needed, size_t* out)
{
    size_t next = current;
    if (out == NULL) return 0;
    if (next == 0U) next = 1U;
    while (next < needed) {
        if (next > (size_t)-1 / 2U) {
            next = needed;
            break;
        }
        next *= 2U;
    }
    *out = next;
    return 1;
}

int aivm_vm_try_grow_node_arenas(
    AivmVm* vm,
    size_t needed_node_count,
    size_t needed_attr_count,
    size_t needed_child_count)
{
    size_t node_capacity;
    size_t attr_capacity;
    size_t child_capacity;
    size_t growth_bytes = 0U;
    size_t part = 0U;
    AivmNodeRecord* nodes;
    AivmNodeAttr* attrs;
    int64_t* children;

    if (vm == NULL || vm->runtime_profile != AIVM_RUNTIME_PROFILE_TOOLING) return 0;
    if (!next_capacity(vm->node_capacity, needed_node_count, &node_capacity) ||
        !next_capacity(vm->node_attr_capacity, needed_attr_count, &attr_capacity) ||
        !next_capacity(vm->node_child_capacity, needed_child_count, &child_capacity)) return 0;
    if (node_capacity == vm->node_capacity &&
        attr_capacity == vm->node_attr_capacity &&
        child_capacity == vm->node_child_capacity) return 1;

    if (node_capacity > vm->node_capacity) {
        if (node_capacity - vm->node_capacity > (size_t)-1 / sizeof(vm->nodes[0])) return 0;
        part = (node_capacity - vm->node_capacity) * sizeof(vm->nodes[0]);
        if (!aivm_size_add_checked(growth_bytes, part, &growth_bytes)) return 0;
    }
    if (attr_capacity > vm->node_attr_capacity) {
        if (attr_capacity - vm->node_attr_capacity > (size_t)-1 / sizeof(vm->node_attrs[0])) return 0;
        part = (attr_capacity - vm->node_attr_capacity) * sizeof(vm->node_attrs[0]);
        if (!aivm_size_add_checked(growth_bytes, part, &growth_bytes)) return 0;
    }
    if (child_capacity > vm->node_child_capacity) {
        if (child_capacity - vm->node_child_capacity > (size_t)-1 / sizeof(vm->node_children[0])) return 0;
        part = (child_capacity - vm->node_child_capacity) * sizeof(vm->node_children[0]);
        if (!aivm_size_add_checked(growth_bytes, part, &growth_bytes)) return 0;
    }
    if (!aivm_vm_host_memory_growth_available(vm, growth_bytes)) return 0;

    nodes = vm->nodes;
    attrs = vm->node_attrs;
    children = vm->node_children;
    if (node_capacity > vm->node_capacity) {
        nodes = (AivmNodeRecord*)realloc(vm->nodes, node_capacity * sizeof(vm->nodes[0]));
        if (nodes == NULL) return 0;
        vm->nodes = nodes;
        vm->node_capacity = node_capacity;
    }
    if (attr_capacity > vm->node_attr_capacity) {
        attrs = (AivmNodeAttr*)realloc(vm->node_attrs, attr_capacity * sizeof(vm->node_attrs[0]));
        if (attrs == NULL) return 0;
        vm->node_attrs = attrs;
        vm->node_attr_capacity = attr_capacity;
    }
    if (child_capacity > vm->node_child_capacity) {
        children = (int64_t*)realloc(vm->node_children, child_capacity * sizeof(vm->node_children[0]));
        if (children == NULL) return 0;
        vm->node_children = children;
        vm->node_child_capacity = child_capacity;
    }
    return 1;
}

int aivm_vm_try_grow_pressure_node_arenas(AivmVm* vm)
{
    size_t needed_nodes;
    size_t needed_attrs;
    size_t needed_children;
    if (vm == NULL || vm->runtime_profile != AIVM_RUNTIME_PROFILE_TOOLING) return 0;
    needed_nodes = vm->node_count >=
            (vm->node_capacity / AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR) *
                AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR
        ? (vm->node_capacity == (size_t)-1 ? vm->node_capacity : vm->node_capacity + 1U)
        : vm->node_count;
    needed_attrs = vm->node_attr_count >=
            (vm->node_attr_capacity / AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR) *
                AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR
        ? (vm->node_attr_capacity == (size_t)-1 ? vm->node_attr_capacity : vm->node_attr_capacity + 1U)
        : vm->node_attr_count;
    needed_children = vm->node_child_count >=
            (vm->node_child_capacity / AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR) *
                AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR
        ? (vm->node_child_capacity == (size_t)-1 ? vm->node_child_capacity : vm->node_child_capacity + 1U)
        : vm->node_child_count;
    return aivm_vm_try_grow_node_arenas(vm, needed_nodes, needed_attrs, needed_children);
}
