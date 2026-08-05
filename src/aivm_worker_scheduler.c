#include "aivm_worker_scheduler.h"
#include "aivm_host_memory.h"

#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE AivmSchedulerThread;
typedef CRITICAL_SECTION AivmSchedulerMutex;
typedef CONDITION_VARIABLE AivmSchedulerCondition;
#else
#include <pthread.h>
typedef pthread_t AivmSchedulerThread;
typedef pthread_mutex_t AivmSchedulerMutex;
typedef pthread_cond_t AivmSchedulerCondition;
#endif

typedef struct {
    uint64_t submission_id;
    AivmWorkerSchedulerRun run;
    void* context;
    AivmWorkerTaskStatus status;
    int occupied;
} AivmWorkerSchedulerTask;

struct AivmWorkerScheduler {
    size_t active_limit;
    size_t outstanding_limit;
    size_t outstanding_count;
    size_t worker_count;
    int stopping;
    AivmWorkerSchedulerTask* tasks;
    AivmSchedulerThread* workers;
    AivmSchedulerMutex mutex;
    AivmSchedulerCondition changed;
};

static int scheduler_mutex_init(AivmSchedulerMutex* mutex)
{
#if defined(_WIN32)
    InitializeCriticalSection(mutex);
    return 1;
#else
    return pthread_mutex_init(mutex, NULL) == 0;
#endif
}

static void scheduler_mutex_dispose(AivmSchedulerMutex* mutex)
{
#if defined(_WIN32)
    DeleteCriticalSection(mutex);
#else
    (void)pthread_mutex_destroy(mutex);
#endif
}

static void scheduler_lock(AivmSchedulerMutex* mutex)
{
#if defined(_WIN32)
    EnterCriticalSection(mutex);
#else
    (void)pthread_mutex_lock(mutex);
#endif
}

static void scheduler_unlock(AivmSchedulerMutex* mutex)
{
#if defined(_WIN32)
    LeaveCriticalSection(mutex);
#else
    (void)pthread_mutex_unlock(mutex);
#endif
}

static int scheduler_condition_init(AivmSchedulerCondition* condition)
{
#if defined(_WIN32)
    InitializeConditionVariable(condition);
    return 1;
#else
    return pthread_cond_init(condition, NULL) == 0;
#endif
}

static void scheduler_condition_dispose(AivmSchedulerCondition* condition)
{
#if !defined(_WIN32)
    (void)pthread_cond_destroy(condition);
#else
    (void)condition;
#endif
}

static void scheduler_wait(AivmSchedulerCondition* condition, AivmSchedulerMutex* mutex)
{
#if defined(_WIN32)
    (void)SleepConditionVariableCS(condition, mutex, INFINITE);
#else
    (void)pthread_cond_wait(condition, mutex);
#endif
}

static void scheduler_wake_all(AivmSchedulerCondition* condition)
{
#if defined(_WIN32)
    WakeAllConditionVariable(condition);
#else
    (void)pthread_cond_broadcast(condition);
#endif
}

static AivmWorkerSchedulerTask* find_task(
    AivmWorkerScheduler* scheduler,
    uint64_t submission_id)
{
    size_t index;
    for (index = 0U; index < scheduler->outstanding_limit; index += 1U) {
        if (scheduler->tasks[index].occupied != 0 &&
            scheduler->tasks[index].submission_id == submission_id) {
            return &scheduler->tasks[index];
        }
    }
    return NULL;
}

static AivmWorkerSchedulerTask* find_queued_task(AivmWorkerScheduler* scheduler)
{
    size_t index;
    size_t running_count = 0U;
    size_t memory_capacity;
    AivmWorkerSchedulerTask* selected = NULL;
    for (index = 0U; index < scheduler->outstanding_limit; index += 1U) {
        if (scheduler->tasks[index].occupied != 0 &&
            scheduler->tasks[index].status == AIVM_WORKER_TASK_RUNNING) {
            running_count += 1U;
        }
    }
    memory_capacity = aivm_host_memory_worker_capacity(scheduler->active_limit);
    if (running_count >= memory_capacity) return NULL;
    for (index = 0U; index < scheduler->outstanding_limit; index += 1U) {
        AivmWorkerSchedulerTask* candidate = &scheduler->tasks[index];
        if (candidate->occupied == 0 || candidate->status != AIVM_WORKER_TASK_QUEUED) {
            continue;
        }
        if (selected == NULL || candidate->submission_id < selected->submission_id) {
            selected = candidate;
        }
    }
    return selected;
}

#if defined(_WIN32)
static DWORD WINAPI scheduler_worker_main(LPVOID raw_scheduler)
#else
static void* scheduler_worker_main(void* raw_scheduler)
#endif
{
    AivmWorkerScheduler* scheduler = (AivmWorkerScheduler*)raw_scheduler;
    for (;;) {
        AivmWorkerSchedulerTask* task;
        AivmWorkerSchedulerRun run;
        void* context;
        uint64_t submission_id;
        scheduler_lock(&scheduler->mutex);
        task = find_queued_task(scheduler);
        while (task == NULL && scheduler->stopping == 0) {
            scheduler_wait(&scheduler->changed, &scheduler->mutex);
            task = find_queued_task(scheduler);
        }
        if (scheduler->stopping != 0) {
            scheduler_unlock(&scheduler->mutex);
            break;
        }
        task->status = AIVM_WORKER_TASK_RUNNING;
        submission_id = task->submission_id;
        run = task->run;
        context = task->context;
        scheduler_unlock(&scheduler->mutex);

        run(context);

        scheduler_lock(&scheduler->mutex);
        task = find_task(scheduler, submission_id);
        if (task != NULL && task->status == AIVM_WORKER_TASK_RUNNING) {
            task->status = AIVM_WORKER_TASK_COMPLETED;
        }
        scheduler_wake_all(&scheduler->changed);
        scheduler_unlock(&scheduler->mutex);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int start_worker(
    AivmWorkerScheduler* scheduler,
    AivmSchedulerThread* out_thread)
{
#if defined(_WIN32)
    *out_thread = CreateThread(
        NULL, 16U * 1024U * 1024U,
        scheduler_worker_main, scheduler, 0U, NULL);
    return *out_thread != NULL;
#else
    pthread_attr_t attributes;
    int created;
    if (pthread_attr_init(&attributes) != 0) {
        return 0;
    }
    if (pthread_attr_setstacksize(&attributes, 16U * 1024U * 1024U) != 0) {
        (void)pthread_attr_destroy(&attributes);
        return 0;
    }
    created = pthread_create(
        out_thread, &attributes, scheduler_worker_main, scheduler) == 0;
    (void)pthread_attr_destroy(&attributes);
    return created;
#endif
}

static void join_worker(AivmSchedulerThread thread)
{
#if defined(_WIN32)
    (void)WaitForSingleObject(thread, INFINITE);
    (void)CloseHandle(thread);
#else
    (void)pthread_join(thread, NULL);
#endif
}

AivmWorkerSchedulerStatus aivm_worker_scheduler_create(
    size_t active_limit,
    size_t outstanding_limit,
    AivmWorkerScheduler** out_scheduler)
{
    AivmWorkerScheduler* scheduler;
    size_t index;
    if (out_scheduler == NULL || active_limit == 0U ||
        outstanding_limit == 0U || active_limit > outstanding_limit) {
        return AIVM_WORKER_SCHEDULER_ERR_ARGUMENT;
    }
    *out_scheduler = NULL;
    scheduler = (AivmWorkerScheduler*)calloc(1U, sizeof(*scheduler));
    if (scheduler == NULL) {
        return AIVM_WORKER_SCHEDULER_ERR_MEMORY;
    }
    scheduler->tasks = (AivmWorkerSchedulerTask*)calloc(
        outstanding_limit, sizeof(*scheduler->tasks));
    scheduler->workers = (AivmSchedulerThread*)calloc(
        active_limit, sizeof(*scheduler->workers));
    if (scheduler->tasks == NULL || scheduler->workers == NULL) {
        free(scheduler->tasks);
        free(scheduler->workers);
        free(scheduler);
        return AIVM_WORKER_SCHEDULER_ERR_MEMORY;
    }
    scheduler->active_limit = active_limit;
    scheduler->outstanding_limit = outstanding_limit;
    if (!scheduler_mutex_init(&scheduler->mutex)) {
        free(scheduler->tasks);
        free(scheduler->workers);
        free(scheduler);
        return AIVM_WORKER_SCHEDULER_ERR_SYSTEM;
    }
    if (!scheduler_condition_init(&scheduler->changed)) {
        scheduler_mutex_dispose(&scheduler->mutex);
        free(scheduler->tasks);
        free(scheduler->workers);
        free(scheduler);
        return AIVM_WORKER_SCHEDULER_ERR_SYSTEM;
    }
    for (index = 0U; index < active_limit; index += 1U) {
        if (!start_worker(scheduler, &scheduler->workers[index])) {
            scheduler_lock(&scheduler->mutex);
            scheduler->stopping = 1;
            scheduler_wake_all(&scheduler->changed);
            scheduler_unlock(&scheduler->mutex);
            while (scheduler->worker_count > 0U) {
                scheduler->worker_count -= 1U;
                join_worker(scheduler->workers[scheduler->worker_count]);
            }
            scheduler_condition_dispose(&scheduler->changed);
            scheduler_mutex_dispose(&scheduler->mutex);
            free(scheduler->tasks);
            free(scheduler->workers);
            free(scheduler);
            return AIVM_WORKER_SCHEDULER_ERR_SYSTEM;
        }
        scheduler->worker_count += 1U;
    }
    *out_scheduler = scheduler;
    return AIVM_WORKER_SCHEDULER_OK;
}

void aivm_worker_scheduler_destroy(AivmWorkerScheduler* scheduler)
{
    if (scheduler == NULL) {
        return;
    }
    scheduler_lock(&scheduler->mutex);
    scheduler->stopping = 1;
    scheduler_wake_all(&scheduler->changed);
    scheduler_unlock(&scheduler->mutex);
    while (scheduler->worker_count > 0U) {
        scheduler->worker_count -= 1U;
        join_worker(scheduler->workers[scheduler->worker_count]);
    }
    scheduler_condition_dispose(&scheduler->changed);
    scheduler_mutex_dispose(&scheduler->mutex);
    free(scheduler->tasks);
    free(scheduler->workers);
    free(scheduler);
}

AivmWorkerSchedulerStatus aivm_worker_scheduler_submit(
    AivmWorkerScheduler* scheduler,
    uint64_t submission_id,
    AivmWorkerSchedulerRun run,
    void* context)
{
    size_t index;
    AivmWorkerSchedulerTask* task = NULL;
    if (scheduler == NULL || run == NULL) {
        return AIVM_WORKER_SCHEDULER_ERR_ARGUMENT;
    }
    scheduler_lock(&scheduler->mutex);
    if (scheduler->outstanding_count >= scheduler->outstanding_limit ||
        find_task(scheduler, submission_id) != NULL) {
        scheduler_unlock(&scheduler->mutex);
        return AIVM_WORKER_SCHEDULER_ERR_LIMIT;
    }
    for (index = 0U; index < scheduler->outstanding_limit; index += 1U) {
        if (scheduler->tasks[index].occupied == 0) {
            task = &scheduler->tasks[index];
            break;
        }
    }
    if (task == NULL) {
        scheduler_unlock(&scheduler->mutex);
        return AIVM_WORKER_SCHEDULER_ERR_LIMIT;
    }
    task->submission_id = submission_id;
    task->run = run;
    task->context = context;
    task->status = AIVM_WORKER_TASK_QUEUED;
    task->occupied = 1;
    scheduler->outstanding_count += 1U;
    scheduler_wake_all(&scheduler->changed);
    scheduler_unlock(&scheduler->mutex);
    return AIVM_WORKER_SCHEDULER_OK;
}

AivmWorkerSchedulerStatus aivm_worker_scheduler_await(
    AivmWorkerScheduler* scheduler,
    uint64_t submission_id,
    AivmWorkerTaskStatus* out_status)
{
    AivmWorkerSchedulerTask* task;
    if (scheduler == NULL || out_status == NULL) {
        return AIVM_WORKER_SCHEDULER_ERR_ARGUMENT;
    }
    scheduler_lock(&scheduler->mutex);
    task = find_task(scheduler, submission_id);
    while (task != NULL &&
           task->status != AIVM_WORKER_TASK_COMPLETED &&
           task->status != AIVM_WORKER_TASK_CANCELED) {
        scheduler_wait(&scheduler->changed, &scheduler->mutex);
        task = find_task(scheduler, submission_id);
    }
    if (task == NULL) {
        scheduler_unlock(&scheduler->mutex);
        return AIVM_WORKER_SCHEDULER_ERR_ARGUMENT;
    }
    *out_status = task->status;
    scheduler_unlock(&scheduler->mutex);
    return AIVM_WORKER_SCHEDULER_OK;
}

AivmWorkerSchedulerStatus aivm_worker_scheduler_cancel(
    AivmWorkerScheduler* scheduler,
    uint64_t submission_id,
    int* out_canceled)
{
    AivmWorkerSchedulerTask* task;
    if (scheduler == NULL || out_canceled == NULL) {
        return AIVM_WORKER_SCHEDULER_ERR_ARGUMENT;
    }
    scheduler_lock(&scheduler->mutex);
    task = find_task(scheduler, submission_id);
    if (task == NULL) {
        scheduler_unlock(&scheduler->mutex);
        return AIVM_WORKER_SCHEDULER_ERR_ARGUMENT;
    }
    *out_canceled = task->status == AIVM_WORKER_TASK_QUEUED;
    if (*out_canceled != 0) {
        task->status = AIVM_WORKER_TASK_CANCELED;
        scheduler_wake_all(&scheduler->changed);
    }
    scheduler_unlock(&scheduler->mutex);
    return AIVM_WORKER_SCHEDULER_OK;
}

AivmWorkerSchedulerStatus aivm_worker_scheduler_release(
    AivmWorkerScheduler* scheduler,
    uint64_t submission_id)
{
    AivmWorkerSchedulerTask* task;
    if (scheduler == NULL) {
        return AIVM_WORKER_SCHEDULER_ERR_ARGUMENT;
    }
    scheduler_lock(&scheduler->mutex);
    task = find_task(scheduler, submission_id);
    if (task == NULL ||
        (task->status != AIVM_WORKER_TASK_COMPLETED &&
         task->status != AIVM_WORKER_TASK_CANCELED)) {
        scheduler_unlock(&scheduler->mutex);
        return AIVM_WORKER_SCHEDULER_ERR_ARGUMENT;
    }
    task->occupied = 0;
    task->run = NULL;
    task->context = NULL;
    scheduler->outstanding_count -= 1U;
    scheduler_unlock(&scheduler->mutex);
    return AIVM_WORKER_SCHEDULER_OK;
}

size_t aivm_worker_scheduler_active_limit(const AivmWorkerScheduler* scheduler)
{
    return scheduler != NULL ? scheduler->active_limit : 0U;
}

size_t aivm_worker_scheduler_outstanding_count(AivmWorkerScheduler* scheduler)
{
    size_t count;
    if (scheduler == NULL) {
        return 0U;
    }
    scheduler_lock(&scheduler->mutex);
    count = scheduler->outstanding_count;
    scheduler_unlock(&scheduler->mutex);
    return count;
}
