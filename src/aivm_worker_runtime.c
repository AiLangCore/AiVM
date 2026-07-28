#include "aivm_worker_runtime.h"

#include "aivm_worker_capacity.h"
#include "aivm_worker_invocation.h"
#include "aivm_worker_program.h"

#include <stdlib.h>

typedef struct {
    uint64_t submission_id;
    AivmWorkerInvocation invocation;
    int occupied;
} AivmWorkerRuntimeTask;

struct AivmWorkerRuntime {
    const AivmProgram* owner_program;
    AivmSyscallCapabilityPolicy parent_policy;
    const AivmSyscallBinding* syscall_bindings;
    size_t syscall_binding_count;
    AivmRuntimeProfile profile;
    size_t outstanding_limit;
    size_t active_limit;
    AivmWorkerProgram* programs;
    size_t program_count;
    AivmWorkerRuntimeTask* tasks;
    AivmWorkerScheduler* scheduler;
};

static AivmWorkerRuntimeTask* find_runtime_task(
    AivmWorkerRuntime* runtime,
    uint64_t submission_id)
{
    size_t index;
    for (index = 0U; index < runtime->outstanding_limit; index += 1U) {
        if (runtime->tasks[index].occupied != 0 &&
            runtime->tasks[index].submission_id == submission_id) {
            return &runtime->tasks[index];
        }
    }
    return NULL;
}

static AivmWorkerRuntimeStatus prepare_program(
    AivmWorkerRuntime* runtime,
    size_t catalog_index)
{
    AivmWorkerProgramStatus status;
    if (runtime->programs[catalog_index].loaded != NULL) {
        return AIVM_WORKER_RUNTIME_OK;
    }
    status = aivm_worker_program_prepare(
        &runtime->owner_program->worker_catalog.entries[catalog_index],
        &runtime->parent_policy,
        &runtime->programs[catalog_index]);
    switch (status) {
        case AIVM_WORKER_PROGRAM_OK:
            return AIVM_WORKER_RUNTIME_OK;
        case AIVM_WORKER_PROGRAM_ERR_CAPABILITY:
            return AIVM_WORKER_RUNTIME_ERR_CAPABILITY;
        case AIVM_WORKER_PROGRAM_ERR_MEMORY:
            return AIVM_WORKER_RUNTIME_ERR_MEMORY;
        case AIVM_WORKER_PROGRAM_ERR_ARTIFACT:
        case AIVM_WORKER_PROGRAM_ERR_SIGNATURE:
            return AIVM_WORKER_RUNTIME_ERR_PROGRAM;
        default:
            return AIVM_WORKER_RUNTIME_ERR_ARGUMENT;
    }
}

AivmWorkerRuntimeStatus aivm_worker_runtime_create(
    const AivmProgram* owner_program,
    const AivmSyscallCapabilityPolicy* parent_policy,
    const AivmSyscallBinding* syscall_bindings,
    size_t syscall_binding_count,
    AivmRuntimeProfile profile,
    size_t outstanding_limit,
    AivmWorkerRuntime** out_runtime)
{
    AivmWorkerRuntime* runtime;
    AivmRuntimeProfileLimits limits;
    AivmWorkerSchedulerStatus scheduler_status;
    if (owner_program == NULL || parent_policy == NULL || out_runtime == NULL ||
        outstanding_limit == 0U ||
        (syscall_binding_count > 0U && syscall_bindings == NULL)) {
        return AIVM_WORKER_RUNTIME_ERR_ARGUMENT;
    }
    *out_runtime = NULL;
    runtime = (AivmWorkerRuntime*)calloc(1U, sizeof(*runtime));
    if (runtime == NULL) {
        return AIVM_WORKER_RUNTIME_ERR_MEMORY;
    }
    runtime->program_count = owner_program->worker_catalog.count;
    runtime->programs = (AivmWorkerProgram*)calloc(
        runtime->program_count == 0U ? 1U : runtime->program_count,
        sizeof(*runtime->programs));
    runtime->tasks = (AivmWorkerRuntimeTask*)calloc(
        outstanding_limit, sizeof(*runtime->tasks));
    if (runtime->programs == NULL || runtime->tasks == NULL) {
        free(runtime->programs);
        free(runtime->tasks);
        free(runtime);
        return AIVM_WORKER_RUNTIME_ERR_MEMORY;
    }
    runtime->owner_program = owner_program;
    runtime->parent_policy = *parent_policy;
    runtime->syscall_bindings = syscall_bindings;
    runtime->syscall_binding_count = syscall_binding_count;
    runtime->profile = profile;
    runtime->outstanding_limit = outstanding_limit;
    limits = aivm_runtime_profile_limits(profile);
    runtime->active_limit = aivm_worker_active_capacity(
        limits.worker_count, outstanding_limit);
    scheduler_status = aivm_worker_scheduler_create(
        runtime->active_limit, outstanding_limit, &runtime->scheduler);
    if (scheduler_status != AIVM_WORKER_SCHEDULER_OK) {
        free(runtime->programs);
        free(runtime->tasks);
        free(runtime);
        return scheduler_status == AIVM_WORKER_SCHEDULER_ERR_MEMORY
            ? AIVM_WORKER_RUNTIME_ERR_MEMORY : AIVM_WORKER_RUNTIME_ERR_SYSTEM;
    }
    *out_runtime = runtime;
    return AIVM_WORKER_RUNTIME_OK;
}

void aivm_worker_runtime_destroy(AivmWorkerRuntime* runtime)
{
    size_t index;
    if (runtime == NULL) {
        return;
    }
    aivm_worker_scheduler_destroy(runtime->scheduler);
    for (index = 0U; index < runtime->outstanding_limit; index += 1U) {
        if (runtime->tasks[index].occupied != 0) {
            aivm_worker_invocation_release(&runtime->tasks[index].invocation);
        }
    }
    for (index = 0U; index < runtime->program_count; index += 1U) {
        aivm_worker_program_release(&runtime->programs[index]);
    }
    free(runtime->tasks);
    free(runtime->programs);
    free(runtime);
}

AivmWorkerRuntimeStatus aivm_worker_runtime_submit(
    AivmWorkerRuntime* runtime,
    size_t worker_catalog_index,
    uint64_t submission_id,
    const uint8_t* payload,
    size_t payload_length)
{
    size_t index;
    AivmWorkerRuntimeTask* task = NULL;
    AivmWorkerRuntimeStatus prepare_status;
    AivmWorkerSchedulerStatus scheduler_status;
    AivmRuntimeProfileLimits limits;
    if (runtime == NULL || worker_catalog_index >= runtime->program_count ||
        (payload_length > 0U && payload == NULL)) {
        return AIVM_WORKER_RUNTIME_ERR_ARGUMENT;
    }
    limits = aivm_runtime_profile_limits(runtime->profile);
    if (payload_length > limits.bytes_arena_capacity) {
        return AIVM_WORKER_RUNTIME_ERR_LIMIT;
    }
    if (find_runtime_task(runtime, submission_id) != NULL) {
        return AIVM_WORKER_RUNTIME_ERR_LIMIT;
    }
    prepare_status = prepare_program(runtime, worker_catalog_index);
    if (prepare_status != AIVM_WORKER_RUNTIME_OK) {
        return prepare_status;
    }
    for (index = 0U; index < runtime->outstanding_limit; index += 1U) {
        if (runtime->tasks[index].occupied == 0) {
            task = &runtime->tasks[index];
            break;
        }
    }
    if (task == NULL) {
        return AIVM_WORKER_RUNTIME_ERR_LIMIT;
    }
    aivm_worker_invocation_clear(&task->invocation);
    task->submission_id = submission_id;
    task->invocation.program = &runtime->programs[worker_catalog_index];
    task->invocation.syscall_bindings = runtime->syscall_bindings;
    task->invocation.syscall_binding_count = runtime->syscall_binding_count;
    task->invocation.profile = runtime->profile;
    if (!aivm_worker_invocation_set_payload(
        &task->invocation, payload, payload_length)) {
        return AIVM_WORKER_RUNTIME_ERR_MEMORY;
    }
    task->occupied = 1;
    scheduler_status = aivm_worker_scheduler_submit(
        runtime->scheduler, submission_id,
        aivm_worker_invocation_run, &task->invocation);
    if (scheduler_status != AIVM_WORKER_SCHEDULER_OK) {
        task->occupied = 0;
        aivm_worker_invocation_release(&task->invocation);
        return scheduler_status == AIVM_WORKER_SCHEDULER_ERR_LIMIT
            ? AIVM_WORKER_RUNTIME_ERR_LIMIT : AIVM_WORKER_RUNTIME_ERR_SYSTEM;
    }
    return AIVM_WORKER_RUNTIME_OK;
}

AivmWorkerRuntimeStatus aivm_worker_runtime_await(
    AivmWorkerRuntime* runtime,
    uint64_t submission_id,
    AivmWorkerRuntimeResult* out_result)
{
    AivmWorkerRuntimeTask* task;
    AivmWorkerTaskStatus scheduler_status;
    if (runtime == NULL || out_result == NULL) {
        return AIVM_WORKER_RUNTIME_ERR_ARGUMENT;
    }
    task = find_runtime_task(runtime, submission_id);
    if (task == NULL ||
        aivm_worker_scheduler_await(
            runtime->scheduler, submission_id, &scheduler_status) !=
            AIVM_WORKER_SCHEDULER_OK) {
        return AIVM_WORKER_RUNTIME_ERR_ARGUMENT;
    }
    if (scheduler_status == AIVM_WORKER_TASK_CANCELED) {
        out_result->data = NULL;
        out_result->length = 0U;
        out_result->status = AIVM_WORKER_RUNTIME_CANCELED;
        out_result->vm_error = AIVM_VM_ERR_NONE;
        out_result->error_detail = "Worker task canceled.";
        return AIVM_WORKER_RUNTIME_CANCELED;
    }
    out_result->data = task->invocation.result;
    out_result->length = task->invocation.result_length;
    out_result->vm_error = task->invocation.vm_error;
    out_result->error_detail = task->invocation.error_detail;
    if (task->invocation.status == AIVM_WORKER_INVOCATION_COMPLETED) {
        out_result->status = AIVM_WORKER_RUNTIME_OK;
        return AIVM_WORKER_RUNTIME_OK;
    }
    out_result->status =
        task->invocation.status == AIVM_WORKER_INVOCATION_TRANSPORT_ERROR
        ? AIVM_WORKER_RUNTIME_ERR_TRANSPORT
        : AIVM_WORKER_RUNTIME_ERR_EXECUTION;
    return out_result->status;
}

AivmWorkerRuntimeStatus aivm_worker_runtime_cancel(
    AivmWorkerRuntime* runtime,
    uint64_t submission_id,
    int* out_canceled)
{
    if (runtime == NULL || out_canceled == NULL) {
        return AIVM_WORKER_RUNTIME_ERR_ARGUMENT;
    }
    return aivm_worker_scheduler_cancel(
        runtime->scheduler, submission_id, out_canceled) ==
        AIVM_WORKER_SCHEDULER_OK
        ? AIVM_WORKER_RUNTIME_OK : AIVM_WORKER_RUNTIME_ERR_ARGUMENT;
}

AivmWorkerRuntimeStatus aivm_worker_runtime_release(
    AivmWorkerRuntime* runtime,
    uint64_t submission_id)
{
    AivmWorkerRuntimeTask* task;
    if (runtime == NULL) {
        return AIVM_WORKER_RUNTIME_ERR_ARGUMENT;
    }
    task = find_runtime_task(runtime, submission_id);
    if (task == NULL ||
        aivm_worker_scheduler_release(runtime->scheduler, submission_id) !=
            AIVM_WORKER_SCHEDULER_OK) {
        return AIVM_WORKER_RUNTIME_ERR_ARGUMENT;
    }
    aivm_worker_invocation_release(&task->invocation);
    task->occupied = 0;
    return AIVM_WORKER_RUNTIME_OK;
}

size_t aivm_worker_runtime_active_limit(const AivmWorkerRuntime* runtime)
{
    return runtime != NULL ? runtime->active_limit : 0U;
}
