#include "aivm_vm_internal.h"

#include <stdlib.h>
#include <string.h>

static void add_counter_saturating(size_t* counter, size_t delta)
{
    if (counter == NULL) {
        return;
    }
    if (delta > ((size_t)-1 - *counter)) {
        *counter = (size_t)-1;
        return;
    }
    *counter += delta;
}

static int remap_value_node_handle(AivmVm* vm, AivmValue* value, const int64_t* handle_map)
{
    int64_t old_handle;
    if (vm == NULL || value == NULL || handle_map == NULL) {
        return 0;
    }
    if (value->type == AIVM_VAL_PAIR) {
        return value->pair_handle > 0 && value->pair_handle <= (int64_t)vm->scratch_pair_count;
    }
    if (value->type != AIVM_VAL_NODE) {
        return 1;
    }
    old_handle = value->node_handle;
    if (old_handle <= 0 || old_handle > (int64_t)vm->node_capacity) {
        return 0;
    }
    if (handle_map[old_handle] <= 0) {
        return 0;
    }
    value->node_handle = handle_map[old_handle];
    return 1;
}

int aivm_vm_mark_live_node_handles(
    AivmVm* vm,
    uint8_t* live,
    const int64_t* extra_handles,
    size_t extra_handle_count)
{
    int64_t* queue = NULL;
    uint8_t* live_pairs = NULL;
    size_t queue_read = 0U;
    size_t queue_write = 0U;
    size_t i;

    if (vm == NULL || live == NULL) {
        return 0;
    }
    queue = (int64_t*)calloc(vm->node_capacity, sizeof(queue[0]));
    live_pairs = (uint8_t*)calloc(AIVM_VM_SCRATCH_PAIR_CAPACITY, sizeof(live_pairs[0]));
    if (queue == NULL || live_pairs == NULL) {
        free(queue);
        free(live_pairs);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM003: node mark workspace allocation failed.");
        return 0;
    }
    if (!aivm_vm_mark_live_scratch_pair_handles(vm, live_pairs)) {
        goto fail;
    }

    #define ENQUEUE_HANDLE(handle_value) \
        do { \
            int64_t __h = (handle_value); \
            if (__h > 0 && __h <= (int64_t)vm->node_count) { \
                size_t __idx = (size_t)(__h - 1); \
                if (live[__idx] == 0U) { \
                    size_t __next_queue_write; \
                    if (queue_write >= vm->node_capacity) { \
                        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM003: node mark queue capacity exceeded."); \
                        goto fail; \
                    } \
                    if (!aivm_size_add_checked(queue_write, 1U, &__next_queue_write)) { \
                        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM003: node mark queue overflow."); \
                        goto fail; \
                    } \
                    live[__idx] = 1U; \
                    queue[queue_write] = __h; \
                    queue_write = __next_queue_write; \
                } \
            } \
        } while (0)

    ENQUEUE_HANDLE(vm->process_argv_node_handle);
    ENQUEUE_HANDLE(vm->ui_default_window_size_node_handle);
    ENQUEUE_HANDLE(vm->ui_empty_event_node_handle);
    for (i = 0U; i < vm->stack_count; i += 1U) {
        if (vm->stack[i].type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->stack[i].node_handle);
        }
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        if (vm->locals[i].type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->locals[i].node_handle);
        }
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (vm->completed_tasks[i].result.type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->completed_tasks[i].result.node_handle);
        }
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        if (vm->par_values[i].type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->par_values[i].node_handle);
        }
    }
    for (i = 0U; i < vm->scratch_pair_count; i += 1U) {
        if (live_pairs[i] == 0U) {
            continue;
        }
        if (vm->scratch_pairs[i].first.type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->scratch_pairs[i].first.node_handle);
        }
        if (vm->scratch_pairs[i].second.type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->scratch_pairs[i].second.node_handle);
        }
    }
    for (i = 0U; i < AIVM_VM_NODE_BUILDER_CAPACITY; i += 1U) {
        size_t child_index;
        const AivmNodeBuilderRecord* builder = &vm->node_builders[i];
        if (!builder->active) continue;
        for (child_index = 0U; child_index < builder->child_count; child_index += 1U) {
            ENQUEUE_HANDLE(builder->children[child_index]);
        }
    }
    if (extra_handles != NULL) {
        for (i = 0U; i < extra_handle_count; i += 1U) {
            int64_t handle = extra_handles[i];
            if (handle <= 0) {
                continue;
            }
            if (handle > (int64_t)vm->node_count) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid extra node handle during GC mark.");
                goto fail;
            }
            ENQUEUE_HANDLE(handle);
        }
    }

    while (queue_read < queue_write) {
        const AivmNodeRecord* node;
        int64_t handle = queue[queue_read];
        size_t child_index;
        if (!aivm_size_add_checked(queue_read, 1U, &queue_read)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM003: node mark queue overflow.");
            goto fail;
        }
        if (!aivm_vm_lookup_node(vm, handle, &node)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid node handle during GC mark.");
            goto fail;
        }
        for (child_index = 0U; child_index < node->child_count; child_index += 1U) {
            size_t child_slot;
            if (!aivm_size_add_checked(node->child_start, child_index, &child_slot) ||
                child_slot >= vm->node_child_capacity) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid child slot during GC mark.");
                goto fail;
            }
            ENQUEUE_HANDLE(vm->node_children[child_slot]);
        }
    }

    #undef ENQUEUE_HANDLE
    free(queue);
    free(live_pairs);
    return 1;

fail:
    #undef ENQUEUE_HANDLE
    free(queue);
    free(live_pairs);
    return 0;
}

int aivm_vm_compact_node_arenas_with_map(
    AivmVm* vm,
    const int64_t* extra_handles,
    size_t extra_handle_count,
    int64_t* out_handle_map)
{
    uint8_t* live = NULL;
    int64_t* handle_map = NULL;
    uint8_t* live_pairs = NULL;
    AivmNodeRecord* new_nodes = NULL;
    AivmNodeAttr* new_attrs = NULL;
    int64_t* new_children = NULL;
    size_t new_node_count = 0U;
    size_t new_attr_count = 0U;
    size_t new_child_count = 0U;
    size_t old_node_count;
    size_t old_attr_count;
    size_t old_child_count;
    size_t i;

    if (vm == NULL) {
        return 0;
    }
    aivm_counter_increment_saturating(&vm->node_gc_attempt_count);
    if (vm->node_count == 0U) {
        return 1;
    }
    live = (uint8_t*)calloc(vm->node_capacity, sizeof(uint8_t));
    handle_map = (int64_t*)calloc(vm->node_capacity + 1U, sizeof(int64_t));
    live_pairs = (uint8_t*)calloc(AIVM_VM_SCRATCH_PAIR_CAPACITY, sizeof(uint8_t));
    new_nodes = (AivmNodeRecord*)calloc(vm->node_capacity, sizeof(AivmNodeRecord));
    new_attrs = (AivmNodeAttr*)calloc(vm->node_attr_capacity, sizeof(AivmNodeAttr));
    new_children = (int64_t*)calloc(vm->node_child_capacity, sizeof(int64_t));
    if (live == NULL || handle_map == NULL || live_pairs == NULL || new_nodes == NULL || new_attrs == NULL || new_children == NULL) {
        free(live);
        free(handle_map);
        free(live_pairs);
        free(new_nodes);
        free(new_attrs);
        free(new_children);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node compaction allocation failed.");
        return 0;
    }
    old_node_count = vm->node_count;
    old_attr_count = vm->node_attr_count;
    old_child_count = vm->node_child_count;

    if (!aivm_vm_mark_live_node_handles(vm, live, extra_handles, extra_handle_count)) {
        goto fail;
    }
    if (!aivm_vm_mark_live_scratch_pair_handles(vm, live_pairs)) {
        goto fail;
    }

    for (i = 0U; i < vm->node_count; i += 1U) {
        if (live[i] != 0U) {
            size_t old_handle_index;
            size_t compacted_handle;
            if (!aivm_size_add_checked(i, 1U, &old_handle_index) ||
                !aivm_size_add_checked(new_node_count, 1U, &compacted_handle)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node compaction handle overflow.");
                goto fail;
            }
            handle_map[old_handle_index] = (int64_t)compacted_handle;
            new_node_count = compacted_handle;
        }
    }

    for (i = 0U; i < vm->node_count; i += 1U) {
        const AivmNodeRecord* old_node;
        AivmNodeRecord* out_node;
        size_t attr_i;
        size_t child_i;

        if (live[i] == 0U) {
            continue;
        }
        old_node = &vm->nodes[i];
        {
            size_t old_handle_index;
            int64_t compacted_handle;
            if (!aivm_size_add_checked(i, 1U, &old_handle_index)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node compaction handle overflow.");
                goto fail;
            }
            compacted_handle = handle_map[old_handle_index];
            if (compacted_handle <= 0) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Dangling live node handle during node GC.");
                goto fail;
            }
            out_node = &new_nodes[(size_t)(compacted_handle - 1)];
        }
        *out_node = *old_node;
        out_node->attr_start = new_attr_count;
        out_node->child_start = new_child_count;

        {
            size_t needed_attr_count;
            size_t needed_child_count;
            if (!aivm_size_add_checked(new_attr_count, old_node->attr_count, &needed_attr_count) ||
                !aivm_size_add_checked(new_child_count, old_node->child_count, &needed_child_count) ||
                needed_attr_count > vm->node_attr_capacity ||
                needed_child_count > vm->node_child_capacity) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node compaction capacity exceeded.");
                goto fail;
            }
        }

        for (attr_i = 0U; attr_i < old_node->attr_count; attr_i += 1U) {
            size_t new_attr_slot = 0U;
            size_t old_attr_slot = 0U;
            if (!aivm_size_add_checked(new_attr_count, attr_i, &new_attr_slot) ||
                !aivm_size_add_checked(old_node->attr_start, attr_i, &old_attr_slot) ||
                new_attr_slot >= vm->node_attr_capacity ||
                old_attr_slot >= vm->node_attr_capacity) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node attr slot overflow during node GC.");
                goto fail;
            }
            new_attrs[new_attr_slot] = vm->node_attrs[old_attr_slot];
        }
        for (child_i = 0U; child_i < old_node->child_count; child_i += 1U) {
            size_t old_child_slot = 0U;
            size_t new_child_slot = 0U;
            int64_t old_child;
            if (!aivm_size_add_checked(old_node->child_start, child_i, &old_child_slot) ||
                !aivm_size_add_checked(new_child_count, child_i, &new_child_slot) ||
                old_child_slot >= vm->node_child_capacity ||
                new_child_slot >= vm->node_child_capacity) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node child slot overflow during node GC.");
                goto fail;
            }
            old_child = vm->node_children[old_child_slot];
            if (old_child <= 0 || old_child > (int64_t)vm->node_capacity || handle_map[old_child] <= 0) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Dangling child handle during node GC.");
                goto fail;
            }
            new_children[new_child_slot] = handle_map[old_child];
        }
        if (!aivm_size_add_checked(new_attr_count, old_node->attr_count, &new_attr_count) ||
            !aivm_size_add_checked(new_child_count, old_node->child_count, &new_child_count)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node compaction capacity exceeded.");
            goto fail;
        }
    }

    memcpy(vm->nodes, new_nodes, vm->node_capacity * sizeof(vm->nodes[0]));
    memcpy(vm->node_attrs, new_attrs, vm->node_attr_capacity * sizeof(vm->node_attrs[0]));
    memcpy(vm->node_children, new_children, vm->node_child_capacity * sizeof(vm->node_children[0]));
    vm->node_count = new_node_count;
    vm->node_attr_count = new_attr_count;
    vm->node_child_count = new_child_count;
    aivm_counter_increment_saturating(&vm->node_gc_compaction_count);
    add_counter_saturating(&vm->node_gc_reclaimed_nodes, old_node_count - new_node_count);
    add_counter_saturating(&vm->node_gc_reclaimed_attrs, old_attr_count - new_attr_count);
    add_counter_saturating(&vm->node_gc_reclaimed_children, old_child_count - new_child_count);

    for (i = 0U; i < vm->stack_count; i += 1U) {
        if (!remap_value_node_handle(vm, &vm->stack[i], handle_map)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid stack node handle during node GC.");
            goto fail;
        }
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        if (!remap_value_node_handle(vm, &vm->locals[i], handle_map)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid local node handle during node GC.");
            goto fail;
        }
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (!remap_value_node_handle(vm, &vm->completed_tasks[i].result, handle_map)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid completed-task node handle during node GC.");
            goto fail;
        }
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        if (!remap_value_node_handle(vm, &vm->par_values[i], handle_map)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid parallel-value node handle during node GC.");
            goto fail;
        }
    }
    for (i = 0U; i < vm->scratch_pair_count; i += 1U) {
        if (live_pairs[i] == 0U) {
            continue;
        }
        if (!remap_value_node_handle(vm, &vm->scratch_pairs[i].first, handle_map) ||
            !remap_value_node_handle(vm, &vm->scratch_pairs[i].second, handle_map)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid scratch-pair node handle during node GC.");
            goto fail;
        }
    }
    for (i = 0U; i < AIVM_VM_NODE_BUILDER_CAPACITY; i += 1U) {
        AivmNodeBuilderRecord* builder = &vm->node_builders[i];
        if (builder->active &&
            !aivm_vm_remap_child_handles_for_compaction(
                vm,
                builder->children,
                builder->children,
                builder->child_count,
                handle_map)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid node-builder child handle during node GC.");
            goto fail;
        }
    }
    if (vm->process_argv_node_handle > 0) {
        if (vm->process_argv_node_handle > (int64_t)vm->node_capacity ||
            handle_map[vm->process_argv_node_handle] <= 0) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid process argv node handle during node GC.");
            goto fail;
        }
        vm->process_argv_node_handle = handle_map[vm->process_argv_node_handle];
    }
    if (vm->ui_default_window_size_node_handle > 0) {
        if (vm->ui_default_window_size_node_handle > (int64_t)vm->node_capacity ||
            handle_map[vm->ui_default_window_size_node_handle] <= 0) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid ui window size node handle during node GC.");
            goto fail;
        }
        vm->ui_default_window_size_node_handle = handle_map[vm->ui_default_window_size_node_handle];
    }
    if (vm->ui_empty_event_node_handle > 0) {
        if (vm->ui_empty_event_node_handle > (int64_t)vm->node_capacity ||
            handle_map[vm->ui_empty_event_node_handle] <= 0) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid ui event node handle during node GC.");
            goto fail;
        }
        vm->ui_empty_event_node_handle = handle_map[vm->ui_empty_event_node_handle];
    }
    if (out_handle_map != NULL) {
        memcpy(out_handle_map, handle_map, (vm->node_capacity + 1U) * sizeof(handle_map[0]));
    }
    free(live);
    free(handle_map);
    free(live_pairs);
    free(new_nodes);
    free(new_attrs);
    free(new_children);
    return 1;

fail:
    free(live);
    free(handle_map);
    free(live_pairs);
    free(new_nodes);
    free(new_attrs);
    free(new_children);
    return 0;
}

int aivm_vm_remap_child_handles_for_compaction(
    AivmVm* vm,
    int64_t* remapped_children,
    const int64_t* children,
    size_t child_count,
    const int64_t* handle_map)
{
    size_t i;
    (void)vm;
    if (child_count == 0U) {
        return 1;
    }
    if (remapped_children == NULL || children == NULL || handle_map == NULL) {
        return 0;
    }
    for (i = 0U; i < child_count; i += 1U) {
        int64_t handle = children[i];
        if (handle <= 0 || handle > (int64_t)vm->node_capacity || handle_map[handle] <= 0) {
            return 0;
        }
        remapped_children[i] = handle_map[handle];
    }
    return 1;
}

int aivm_vm_should_attempt_proactive_node_gc(
    const AivmVm* vm,
    size_t incoming_attr_count,
    size_t incoming_child_count)
{
    size_t needed_attr_count = 0U;
    size_t needed_child_count = 0U;
    if (vm == NULL) {
        return 0;
    }
    if (vm->node_allocations_since_gc < AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS) {
        return 0;
    }
    if (!aivm_size_add_checked(vm->node_attr_count, incoming_attr_count, &needed_attr_count) ||
        !aivm_size_add_checked(vm->node_child_count, incoming_child_count, &needed_child_count)) {
        return 1;
    }
    /* A pressure threshold is observational, not a reason to repeatedly
     * compact a live arena. Capacity checks in node creation perform the
     * deterministic compaction attempt when space is actually required; safe
     * points cover normal reclamation between compiler phases. */
    return vm->node_count >= vm->node_capacity ||
           needed_attr_count >= vm->node_attr_capacity ||
           needed_child_count >= vm->node_child_capacity;
}

int aivm_vm_should_attempt_return_safe_point(const AivmVm* vm)
{
    size_t node_threshold;
    size_t attr_threshold;
    size_t child_threshold;
    if (vm == NULL) {
        return 0;
    }
    if (vm->node_allocations_since_gc < AIVM_VM_NODE_GC_RETURN_SAFEPOINT_ALLOCATIONS) {
        return 0;
    }
    if (vm->runtime_profile != AIVM_RUNTIME_PROFILE_TOOLING) {
        return 1;
    }
    node_threshold =
        (vm->node_capacity / AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR) *
        AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR;
    attr_threshold =
        (vm->node_attr_capacity / AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR) *
        AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR;
    child_threshold =
        (vm->node_child_capacity / AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR) *
        AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR;
    return vm->node_count >= node_threshold ||
           vm->node_attr_count >= attr_threshold ||
           vm->node_child_count >= child_threshold;
}

int aivm_vm_should_attempt_bytes_return_safe_point(const AivmVm* vm)
{
    return vm != NULL &&
           vm->bytes_arena_gc_threshold < vm->bytes_arena_capacity &&
           vm->bytes_arena_used >= vm->bytes_arena_gc_threshold;
}
