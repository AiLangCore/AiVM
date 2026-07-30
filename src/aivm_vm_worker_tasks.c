#include "aivm_vm_internal.h"

#include "aivm_worker_runtime.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static AivmCompletedTask* find_owner_task(AivmVm* vm, int64_t handle)
{
    size_t index;
    for (index = 0U; index < vm->completed_task_count; index += 1U) {
        if (vm->completed_tasks[index].handle == handle) {
            return &vm->completed_tasks[index];
        }
    }
    return NULL;
}

static void set_worker_task_error(
    AivmVm* vm,
    AivmVmError error,
    const char* detail)
{
    const char* source = detail;
    if (vm == NULL) {
        return;
    }
    if (source == NULL || source[0] == '\0') {
        source = "Worker execution failed.";
    }
    (void)snprintf(vm->error_detail_storage,
        sizeof(vm->error_detail_storage), "%s", source);
    aivm_set_vm_error(vm, error, vm->error_detail_storage);
}

int aivm_vm_ensure_worker_runtime(AivmVm* vm)
{
    AivmWorkerRuntimeStatus status;
    if (vm->worker_runtime != NULL) {
        return 1;
    }
    status = aivm_worker_runtime_create(
        vm->program, &vm->syscall_policy,
        vm->syscall_bindings, vm->syscall_binding_count,
        vm->runtime_profile, AIVM_VM_TASK_CAPACITY,
        &vm->worker_runtime);
    if (status != AIVM_WORKER_RUNTIME_OK) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
            "Worker runtime initialization failed.");
        return 0;
    }
    return 1;
}

static int create_canceled_result(AivmVm* vm, AivmValue* out_result)
{
    AivmNodeAttr attrs[2];
    int64_t handle;
    attrs[0].key = "code";
    attrs[0].kind = AIVM_NODE_ATTR_IDENTIFIER;
    attrs[0].string_value = "TASK_CANCELED";
    attrs[1].key = "message";
    attrs[1].kind = AIVM_NODE_ATTR_STRING;
    attrs[1].string_value = "Worker task canceled.";
    if (!aivm_vm_create_node_record(
        vm, "Err", "worker_task_canceled",
        attrs, 2U, NULL, 0U, &handle)) {
        return 0;
    }
    *out_result = aivm_value_node(handle);
    return 1;
}

void aivm_vm_cleanup_worker_runtime(AivmVm* vm)
{
    if (vm == NULL || vm->worker_runtime == NULL) {
        return;
    }
    aivm_worker_runtime_destroy(vm->worker_runtime);
    vm->worker_runtime = NULL;
}

int aivm_vm_push_worker_ref(AivmVm* vm, size_t catalog_index)
{
    if (vm == NULL || vm->program == NULL ||
        catalog_index >= vm->program->worker_catalog.count ||
        catalog_index >= (size_t)INT64_MAX) {
        if (vm != NULL) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
                "WORKER_REF requires a valid WorkerCatalog index.");
        }
        return 0;
    }
    return aivm_stack_push(
        vm, aivm_value_worker_ref((int64_t)catalog_index + 1));
}

int aivm_vm_submit_worker_task(AivmVm* vm)
{
    AivmValue payload;
    AivmValue worker_ref;
    size_t catalog_index;
    int64_t handle;
    AivmWorkerRuntimeStatus status;
    if (vm == NULL ||
        !aivm_stack_pop(vm, &payload) ||
        !aivm_stack_pop(vm, &worker_ref)) {
        return 0;
    }
    if (worker_ref.type != AIVM_VAL_WORKER_REF ||
        worker_ref.worker_ref_handle <= 0 ||
        payload.type != AIVM_VAL_BYTES) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH,
            "WORKER_RUN requires WorkerRef and bytes.");
        return 0;
    }
    catalog_index = (size_t)(worker_ref.worker_ref_handle - 1);
    if (vm->program == NULL ||
        catalog_index >= vm->program->worker_catalog.count ||
        !aivm_vm_ensure_worker_runtime(vm) ||
        !aivm_vm_allocate_worker_task(vm, catalog_index, &handle)) {
        return 0;
    }
    status = aivm_worker_runtime_submit(
        vm->worker_runtime, catalog_index, (uint64_t)handle,
        payload.bytes_value.data, payload.bytes_value.length);
    if (status != AIVM_WORKER_RUNTIME_OK) {
        aivm_vm_discard_worker_task(vm, handle);
        aivm_set_vm_error(vm,
            status == AIVM_WORKER_RUNTIME_ERR_MEMORY
                ? AIVM_VM_ERR_MEMORY_PRESSURE : AIVM_VM_ERR_INVALID_PROGRAM,
            status == AIVM_WORKER_RUNTIME_ERR_CAPABILITY
                ? "Worker capability denied."
                : "Worker task admission failed.");
        return 0;
    }
    return aivm_stack_push(vm, aivm_value_task(handle));
}

int aivm_vm_complete_worker_task(AivmVm* vm, AivmCompletedTask* task)
{
    AivmWorkerRuntimeResult result;
    AivmWorkerRuntimeStatus status;
    uint8_t* destination;
    if (vm == NULL || task == NULL || vm->worker_runtime == NULL) {
        return 0;
    }
    status = aivm_worker_runtime_await(
        vm->worker_runtime, (uint64_t)task->handle, &result);
    if (status == AIVM_WORKER_RUNTIME_CANCELED) {
        if (!create_canceled_result(vm, &task->result)) {
            return 0;
        }
        task->state = AIVM_TASK_STATE_CANCELED;
        (void)aivm_worker_runtime_release(
            vm->worker_runtime, (uint64_t)task->handle);
        return 1;
    }
    if (status != AIVM_WORKER_RUNTIME_OK) {
        set_worker_task_error(vm,
            status == AIVM_WORKER_RUNTIME_ERR_TRANSPORT
                ? AIVM_VM_ERR_TYPE_MISMATCH : AIVM_VM_ERR_INVALID_PROGRAM,
            result.error_detail);
        (void)aivm_worker_runtime_release(
            vm->worker_runtime, (uint64_t)task->handle);
        return 0;
    }
    destination = aivm_bytes_arena_alloc(vm, result.length);
    if (destination == NULL) {
        (void)aivm_worker_runtime_release(
            vm->worker_runtime, (uint64_t)task->handle);
        return 0;
    }
    if (result.length > 0U) {
        memcpy(destination, result.data, result.length);
    }
    task->result = aivm_value_bytes(destination, result.length);
    task->state = AIVM_TASK_STATE_COMPLETED;
    (void)aivm_worker_runtime_release(
        vm->worker_runtime, (uint64_t)task->handle);
    return 1;
}

int aivm_vm_cancel_task(AivmVm* vm)
{
    AivmValue task_value;
    AivmCompletedTask* task;
    int canceled = 0;
    if (vm == NULL || !aivm_stack_pop(vm, &task_value)) {
        return 0;
    }
    if (task_value.type != AIVM_VAL_TASK) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH,
            "TASK_CANCEL requires Task.");
        return 0;
    }
    task = find_owner_task(vm, task_value.task_handle);
    if (task == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
            "TASK_CANCEL requires a valid Task.");
        return 0;
    }
    if (task->is_worker_task != 0 && task->state == AIVM_TASK_STATE_PENDING &&
        vm->worker_runtime != NULL) {
        if (aivm_worker_runtime_cancel(
            vm->worker_runtime, (uint64_t)task->handle, &canceled) !=
            AIVM_WORKER_RUNTIME_OK) {
            return 0;
        }
    }
    return aivm_stack_push(vm, aivm_value_bool(canceled));
}
