#ifndef AIVM_WORKER_SCHEDULER_H
#define AIVM_WORKER_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

typedef struct AivmWorkerScheduler AivmWorkerScheduler;
typedef void (*AivmWorkerSchedulerRun)(void* context);

typedef enum {
    AIVM_WORKER_SCHEDULER_OK = 0,
    AIVM_WORKER_SCHEDULER_ERR_ARGUMENT = 1,
    AIVM_WORKER_SCHEDULER_ERR_LIMIT = 2,
    AIVM_WORKER_SCHEDULER_ERR_MEMORY = 3,
    AIVM_WORKER_SCHEDULER_ERR_SYSTEM = 4
} AivmWorkerSchedulerStatus;

typedef enum {
    AIVM_WORKER_TASK_QUEUED = 0,
    AIVM_WORKER_TASK_RUNNING = 1,
    AIVM_WORKER_TASK_COMPLETED = 2,
    AIVM_WORKER_TASK_CANCELED = 3
} AivmWorkerTaskStatus;

/*
 * The outstanding limit is owner-visible admission credit. Background
 * completion does not return credit; only release does. This makes submission
 * acceptance independent of host completion timing.
 */
AivmWorkerSchedulerStatus aivm_worker_scheduler_create(
    size_t active_limit,
    size_t outstanding_limit,
    AivmWorkerScheduler** out_scheduler);
void aivm_worker_scheduler_destroy(AivmWorkerScheduler* scheduler);
AivmWorkerSchedulerStatus aivm_worker_scheduler_submit(
    AivmWorkerScheduler* scheduler,
    uint64_t submission_id,
    AivmWorkerSchedulerRun run,
    void* context);
AivmWorkerSchedulerStatus aivm_worker_scheduler_await(
    AivmWorkerScheduler* scheduler,
    uint64_t submission_id,
    AivmWorkerTaskStatus* out_status);
AivmWorkerSchedulerStatus aivm_worker_scheduler_cancel(
    AivmWorkerScheduler* scheduler,
    uint64_t submission_id,
    int* out_canceled);
AivmWorkerSchedulerStatus aivm_worker_scheduler_release(
    AivmWorkerScheduler* scheduler,
    uint64_t submission_id);
size_t aivm_worker_scheduler_active_limit(const AivmWorkerScheduler* scheduler);
size_t aivm_worker_scheduler_outstanding_count(AivmWorkerScheduler* scheduler);

#endif
