#include "aivm_worker_scheduler.h"

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
typedef volatile LONG ProbeCount;
static size_t probe_count_increment(ProbeCount* value)
{
    return (size_t)InterlockedIncrement(value);
}
static size_t probe_count_load(ProbeCount* value)
{
    return (size_t)InterlockedCompareExchange(value, 0, 0);
}
static void probe_count_store(ProbeCount* value, LONG next)
{
    (void)InterlockedExchange(value, next);
}
static void yield_thread(void) { Sleep(0U); }
#else
#include <stdatomic.h>
#include <sched.h>
typedef atomic_size_t ProbeCount;
static size_t probe_count_increment(ProbeCount* value)
{
    return atomic_fetch_add_explicit(value, 1U, memory_order_release) + 1U;
}
static size_t probe_count_load(ProbeCount* value)
{
    return atomic_load_explicit(value, memory_order_acquire);
}
static void probe_count_store(ProbeCount* value, size_t next)
{
    atomic_store_explicit(value, next, memory_order_release);
}
static void yield_thread(void) { (void)sched_yield(); }
#endif

typedef struct {
    ProbeCount* started;
    ProbeCount* release;
} SchedulerProbe;

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static void run_probe(void* raw_probe)
{
    SchedulerProbe* probe = (SchedulerProbe*)raw_probe;
    (void)probe_count_increment(probe->started);
    while (probe_count_load(probe->release) == 0U) {
        yield_thread();
    }
}

int main(void)
{
    AivmWorkerScheduler* scheduler = NULL;
    SchedulerProbe probes[8];
    ProbeCount started = 0;
    ProbeCount release = 0;
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
    while (probe_count_load(&started) < 4U) {
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

    probe_count_store(&release, 1);
    for (index = 0U; index < 7U; index += 1U) {
        if (expect(aivm_worker_scheduler_await(
            scheduler, (uint64_t)(index + 1U), &status) ==
            AIVM_WORKER_SCHEDULER_OK) != 0 ||
            expect(status == AIVM_WORKER_TASK_COMPLETED) != 0) {
            return 1;
        }
    }
    if (expect(probe_count_load(&started) == 7U) != 0) {
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
