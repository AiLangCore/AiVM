#include "aivm_worker_scheduler.h"

#include <stdatomic.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
static void yield_thread(void) { Sleep(0U); }
#else
#include <sched.h>
static void yield_thread(void) { (void)sched_yield(); }
#endif

typedef struct {
    atomic_size_t* started;
    atomic_int* release;
} SchedulerProbe;

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static void run_probe(void* raw_probe)
{
    SchedulerProbe* probe = (SchedulerProbe*)raw_probe;
    (void)atomic_fetch_add_explicit(probe->started, 1U, memory_order_release);
    while (atomic_load_explicit(probe->release, memory_order_acquire) == 0) {
        yield_thread();
    }
}

int main(void)
{
    AivmWorkerScheduler* scheduler = NULL;
    SchedulerProbe probes[8];
    atomic_size_t started = 0U;
    atomic_int release = 0;
    AivmWorkerTaskStatus status;
    size_t index;

    if (expect(aivm_worker_scheduler_create(4U, 7U, &scheduler) ==
        AIVM_WORKER_SCHEDULER_OK) != 0) {
        return 1;
    }
    if (expect(aivm_worker_scheduler_active_limit(scheduler) == 4U) != 0) {
        return 1;
    }
    for (index = 0U; index < 8U; index += 1U) {
        probes[index].started = &started;
        probes[index].release = &release;
    }
    for (index = 0U; index < 7U; index += 1U) {
        if (expect(aivm_worker_scheduler_submit(
            scheduler, (uint64_t)(index + 1U), run_probe, &probes[index]) ==
            AIVM_WORKER_SCHEDULER_OK) != 0) {
            return 1;
        }
    }
    while (atomic_load_explicit(&started, memory_order_acquire) < 4U) {
        yield_thread();
    }
    if (expect(aivm_worker_scheduler_outstanding_count(scheduler) == 7U) != 0) {
        return 1;
    }
    if (expect(aivm_worker_scheduler_submit(
        scheduler, 8U, run_probe, &probes[7]) ==
        AIVM_WORKER_SCHEDULER_ERR_LIMIT) != 0) {
        return 1;
    }

    atomic_store_explicit(&release, 1, memory_order_release);
    for (index = 0U; index < 7U; index += 1U) {
        if (expect(aivm_worker_scheduler_await(
            scheduler, (uint64_t)(index + 1U), &status) ==
            AIVM_WORKER_SCHEDULER_OK) != 0 ||
            expect(status == AIVM_WORKER_TASK_COMPLETED) != 0) {
            return 1;
        }
    }
    if (expect(atomic_load_explicit(&started, memory_order_acquire) == 7U) != 0) {
        return 1;
    }
    /* Completion alone does not alter owner-visible admission credit. */
    if (expect(aivm_worker_scheduler_submit(
        scheduler, 8U, run_probe, &probes[7]) ==
        AIVM_WORKER_SCHEDULER_ERR_LIMIT) != 0) {
        return 1;
    }
    if (expect(aivm_worker_scheduler_release(scheduler, 1U) ==
        AIVM_WORKER_SCHEDULER_OK) != 0) {
        return 1;
    }
    if (expect(aivm_worker_scheduler_submit(
        scheduler, 8U, run_probe, &probes[7]) ==
        AIVM_WORKER_SCHEDULER_OK) != 0) {
        return 1;
    }
    if (expect(aivm_worker_scheduler_await(scheduler, 8U, &status) ==
        AIVM_WORKER_SCHEDULER_OK) != 0 ||
        expect(status == AIVM_WORKER_TASK_COMPLETED) != 0) {
        return 1;
    }

    for (index = 2U; index <= 8U; index += 1U) {
        if (expect(aivm_worker_scheduler_release(scheduler, (uint64_t)index) ==
            AIVM_WORKER_SCHEDULER_OK) != 0) {
            return 1;
        }
    }
    if (expect(aivm_worker_scheduler_outstanding_count(scheduler) == 0U) != 0) {
        return 1;
    }
    aivm_worker_scheduler_destroy(scheduler);
    printf("aivm worker scheduler tests passed\n");
    return 0;
}
