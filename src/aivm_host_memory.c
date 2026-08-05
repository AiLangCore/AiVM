#include "aivm_vm_internal.h"
#include "aivm_host_memory.h"

#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif

enum {
    AIVM_HOST_MEMORY_RESERVE_MIN = 512 * 1024 * 1024,
    AIVM_HOST_MEMORY_RESERVE_PERCENT = 10,
    AIVM_HOST_MEMORY_RESUME_PERCENT = 5
};

static int host_memory_snapshot(size_t* total_bytes, size_t* available_bytes)
{
#if defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return 0;
    *total_bytes = (size_t)status.ullTotalPhys;
    *available_bytes = (size_t)status.ullAvailPhys;
    return 1;
#elif defined(__APPLE__)
    uint64_t total = 0U;
    size_t total_size = sizeof(total);
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t stats;
    vm_size_t page_size = 0U;
    if (sysctlbyname("hw.memsize", &total, &total_size, NULL, 0) != 0 ||
        host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS ||
        host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&stats, &count) != KERN_SUCCESS) return 0;
    *total_bytes = (size_t)total;
    *available_bytes = (size_t)((stats.free_count + stats.inactive_count) * (uint64_t)page_size);
    return 1;
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) != 0) return 0;
    *total_bytes = (size_t)info.totalram * (size_t)info.mem_unit;
    *available_bytes = (size_t)(info.freeram + info.bufferram) * (size_t)info.mem_unit;
    return 1;
#else
    (void)total_bytes;
    (void)available_bytes;
    return 0;
#endif
}

int aivm_host_memory_growth_allowed(
    size_t total_bytes,
    size_t available_bytes,
    size_t growth_bytes,
    int was_suspended,
    int* out_suspended)
{
    size_t reserve = total_bytes / 100U * AIVM_HOST_MEMORY_RESERVE_PERCENT;
    size_t resume_margin;
    size_t required;
    if (reserve < AIVM_HOST_MEMORY_RESERVE_MIN) reserve = AIVM_HOST_MEMORY_RESERVE_MIN;
    if (total_bytes > 0U && reserve > total_bytes / 2U) reserve = total_bytes / 2U;
    resume_margin = was_suspended ? total_bytes / 100U * AIVM_HOST_MEMORY_RESUME_PERCENT : 0U;
    if (!aivm_size_add_checked(reserve, resume_margin, &required) ||
        !aivm_size_add_checked(required, growth_bytes, &required) ||
        available_bytes < required) {
        if (out_suspended != NULL) *out_suspended = 1;
        return 0;
    }
    if (out_suspended != NULL) *out_suspended = 0;
    return 1;
}

int aivm_vm_admit_host_memory_growth(AivmVm* vm, size_t growth_bytes)
{
    size_t total_bytes = 0U;
    size_t available_bytes = 0U;
    int suspended = 0;
    if (vm == NULL || vm->runtime_profile != AIVM_RUNTIME_PROFILE_TOOLING) return 1;
    if (!host_memory_snapshot(&total_bytes, &available_bytes)) return 1;
    if (!aivm_host_memory_growth_allowed(total_bytes, available_bytes, growth_bytes,
            vm->host_memory_growth_suspended, &suspended)) {
        vm->host_memory_growth_suspended = suspended;
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE,
            "AIVMM006: tooling arena growth denied to preserve host memory reserve.");
        return 0;
    }
    vm->host_memory_growth_suspended = suspended;
    return 1;
}

int aivm_vm_host_memory_growth_available(AivmVm* vm, size_t growth_bytes)
{
    size_t total_bytes = 0U;
    size_t available_bytes = 0U;
    int suspended = 0;
    if (vm == NULL || vm->runtime_profile != AIVM_RUNTIME_PROFILE_TOOLING) return 0;
    if (!host_memory_snapshot(&total_bytes, &available_bytes)) return 1;
    if (!aivm_host_memory_growth_allowed(total_bytes, available_bytes, growth_bytes,
            vm->host_memory_growth_suspended, &suspended)) {
        vm->host_memory_growth_suspended = suspended;
        return 0;
    }
    vm->host_memory_growth_suspended = suspended;
    return 1;
}

size_t aivm_host_memory_worker_capacity(size_t maximum_workers)
{
    size_t total_bytes = 0U;
    size_t available_bytes = 0U;
    size_t reserve;
    size_t capacity;
    if (maximum_workers == 0U) return 1U;
    if (!host_memory_snapshot(&total_bytes, &available_bytes)) return maximum_workers;
    reserve = total_bytes / 100U * AIVM_HOST_MEMORY_RESERVE_PERCENT;
    if (reserve < AIVM_HOST_MEMORY_RESERVE_MIN) reserve = AIVM_HOST_MEMORY_RESERVE_MIN;
    if (available_bytes <= reserve) return 1U;
    capacity = (available_bytes - reserve) /
        AIVM_VM_TOOLING_BYTES_ARENA_INITIAL_CAPACITY;
    if (capacity == 0U) capacity = 1U;
    return capacity < maximum_workers ? capacity : maximum_workers;
}
