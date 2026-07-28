#include "aivm_vm_internal.h"

#include "aivm_worker_runtime.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_batch_u32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) |
        ((uint32_t)bytes[3] << 24U);
}

static void clear_worker_task_group(AivmWorkerTaskGroup* group)
{
    if (group == NULL) {
        return;
    }
    free(group->batch_bytes);
    free(group->payload_offsets);
    free(group->payload_lengths);
    free(group->task_handles);
    memset(group, 0, sizeof(*group));
}

void aivm_vm_cleanup_worker_task_groups(AivmVm* vm)
{
    size_t index;
    if (vm == NULL) {
        return;
    }
    for (index = 0U; index < vm->worker_task_group_count; index += 1U) {
        clear_worker_task_group(&vm->worker_task_groups[index]);
    }
    vm->worker_task_group_count = 0U;
}

static int materialize_worker_task(
    AivmVm* vm,
    AivmWorkerTaskGroup* group,
    size_t logical_index)
{
    int64_t task_handle;
    AivmWorkerRuntimeStatus status;
    if (vm == NULL || group == NULL || logical_index >= group->task_count ||
        group->task_handles[logical_index] != 0) {
        return 0;
    }
    if (!aivm_vm_allocate_worker_task(
        vm, group->worker_catalog_index, &task_handle)) {
        return 0;
    }
    status = aivm_worker_runtime_submit(
        vm->worker_runtime, group->worker_catalog_index,
        (uint64_t)task_handle,
        group->batch_bytes + group->payload_offsets[logical_index],
        group->payload_lengths[logical_index]);
    if (status != AIVM_WORKER_RUNTIME_OK) {
        aivm_vm_discard_worker_task(vm, task_handle);
        aivm_set_vm_error(vm,
            status == AIVM_WORKER_RUNTIME_ERR_MEMORY
                ? AIVM_VM_ERR_MEMORY_PRESSURE : AIVM_VM_ERR_INVALID_PROGRAM,
            "Worker batch materialization failed.");
        return 0;
    }
    group->task_handles[logical_index] = task_handle;
    return 1;
}

int aivm_vm_refill_worker_task_groups(AivmVm* vm)
{
    size_t group_index;
    if (vm == NULL) {
        return 0;
    }
    for (group_index = 0U;
         group_index < vm->worker_task_group_count &&
         vm->completed_task_count < AIVM_VM_TASK_CAPACITY;
         group_index += 1U) {
        AivmWorkerTaskGroup* group = &vm->worker_task_groups[group_index];
        while (group->next_materialize_index < group->task_count &&
               vm->completed_task_count < AIVM_VM_TASK_CAPACITY) {
            size_t logical_index = group->next_materialize_index;
            if (!materialize_worker_task(vm, group, logical_index)) {
                return 0;
            }
            group->next_materialize_index += 1U;
        }
    }
    return 1;
}

static int validate_batch(
    AivmVm* vm,
    AivmValue batch,
    size_t* out_payload_count)
{
    size_t offset = 0U;
    size_t payload_count = 0U;
    while (offset < batch.bytes_value.length) {
        uint32_t payload_length;
        if (batch.bytes_value.length - offset < 4U) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
                "WORKER_RUN_ALL batch framing is truncated.");
            return 0;
        }
        payload_length = read_batch_u32(batch.bytes_value.data + offset);
        offset += 4U;
        if ((size_t)payload_length > batch.bytes_value.length - offset) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
                "WORKER_RUN_ALL batch payload is truncated.");
            return 0;
        }
        offset += (size_t)payload_length;
        payload_count += 1U;
    }
    if (payload_count == 0U) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
            "WORKER_RUN_ALL requires at least one payload.");
        return 0;
    }
    *out_payload_count = payload_count;
    return 1;
}

int aivm_vm_submit_worker_tasks(AivmVm* vm, size_t transport_version)
{
    AivmValue batch;
    AivmValue worker_ref;
    size_t catalog_index;
    size_t offset;
    size_t payload_count;
    AivmWorkerTaskGroup* group;
    int64_t group_handle;
    if (vm == NULL || transport_version != 1U ||
        !aivm_stack_pop(vm, &batch) ||
        !aivm_stack_pop(vm, &worker_ref)) {
        if (vm != NULL) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
                "WORKER_RUN_ALL requires transport version 1.");
        }
        return 0;
    }
    if (worker_ref.type != AIVM_VAL_WORKER_REF ||
        worker_ref.worker_ref_handle <= 0 ||
        batch.type != AIVM_VAL_BYTES) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH,
            "WORKER_RUN_ALL requires WorkerRef and canonical batch bytes.");
        return 0;
    }
    if (!validate_batch(vm, batch, &payload_count)) {
        return 0;
    }
    if (vm->worker_task_group_count >= AIVM_VM_TASK_CAPACITY ||
        vm->next_worker_task_group_handle == INT64_MAX) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
            "WORKER_RUN_ALL exceeds the deterministic workload-group bound.");
        return 0;
    }
    catalog_index = (size_t)(worker_ref.worker_ref_handle - 1);
    if (vm->program == NULL ||
        catalog_index >= vm->program->worker_catalog.count ||
        !aivm_vm_ensure_worker_runtime(vm)) {
        return 0;
    }
    group_handle = vm->next_worker_task_group_handle;
    group = &vm->worker_task_groups[vm->worker_task_group_count];
    memset(group, 0, sizeof(*group));
    group->batch_bytes = (uint8_t*)malloc(batch.bytes_value.length);
    group->payload_offsets = (size_t*)calloc(payload_count, sizeof(size_t));
    group->payload_lengths = (size_t*)calloc(payload_count, sizeof(size_t));
    group->task_handles = (int64_t*)calloc(payload_count, sizeof(int64_t));
    if ((batch.bytes_value.length > 0U && group->batch_bytes == NULL) ||
        group->payload_offsets == NULL || group->payload_lengths == NULL ||
        group->task_handles == NULL) {
        clear_worker_task_group(group);
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE,
            "WORKER_RUN_ALL could not retain bounded workload metadata.");
        return 0;
    }
    memcpy(group->batch_bytes, batch.bytes_value.data, batch.bytes_value.length);
    group->handle = group_handle;
    group->worker_catalog_index = catalog_index;
    group->task_count = payload_count;
    group->batch_length = batch.bytes_value.length;
    offset = 0U;
    payload_count = 0U;
    while (offset < group->batch_length) {
        uint32_t payload_length = read_batch_u32(group->batch_bytes + offset);
        offset += 4U;
        group->payload_offsets[payload_count] = offset;
        group->payload_lengths[payload_count] = (size_t)payload_length;
        offset += (size_t)payload_length;
        payload_count += 1U;
    }
    vm->next_worker_task_group_handle += 1;
    vm->worker_task_group_count += 1U;
    if (!aivm_vm_refill_worker_task_groups(vm)) {
        return 0;
    }
    return aivm_stack_push(vm, aivm_value_worker_tasks(group_handle));
}

int aivm_vm_worker_task_at(AivmVm* vm)
{
    AivmValue index_value;
    AivmValue tasks_value;
    size_t group_index;
    AivmWorkerTaskGroup* group = NULL;
    if (vm == NULL || !aivm_stack_pop(vm, &index_value) ||
        !aivm_stack_pop(vm, &tasks_value)) {
        return 0;
    }
    if (tasks_value.type != AIVM_VAL_WORKER_TASKS ||
        index_value.type != AIVM_VAL_INT || index_value.int_value < 0) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH,
            "WORKER_TASK_AT requires WorkerTasks and a non-negative index.");
        return 0;
    }
    for (group_index = 0U; group_index < vm->worker_task_group_count;
         group_index += 1U) {
        if (vm->worker_task_groups[group_index].handle ==
            tasks_value.worker_tasks_handle) {
            group = &vm->worker_task_groups[group_index];
            break;
        }
    }
    if (group == NULL || (size_t)index_value.int_value >= group->task_count) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
            "WORKER_TASK_AT index is outside the ordered workload.");
        return 0;
    }
    if (group->task_handles[(size_t)index_value.int_value] == 0) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
            "WORKER_TASK_AT index is outside the materialized canonical window.");
        return 0;
    }
    return aivm_stack_push(vm, aivm_value_task(
        group->task_handles[(size_t)index_value.int_value]));
}
