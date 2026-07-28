#include "aivm_worker_capacity.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

size_t aivm_worker_available_logical_cpu(void)
{
#if defined(_WIN32)
    DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count > 0U ? (size_t)count : 1U;
#else
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0L ? (size_t)count : 1U;
#endif
}

size_t aivm_worker_active_capacity(size_t profile_ceiling, size_t memory_safe_capacity)
{
    size_t available = aivm_worker_available_logical_cpu();
    size_t cpu_target = (available * 96U) / 100U;
    size_t result;

    if (cpu_target == 0U) {
        cpu_target = 1U;
    }
    result = cpu_target;
    if (profile_ceiling > 0U && result > profile_ceiling) {
        result = profile_ceiling;
    }
    if (memory_safe_capacity > 0U && result > memory_safe_capacity) {
        result = memory_safe_capacity;
    }
    return result > 0U ? result : 1U;
}
