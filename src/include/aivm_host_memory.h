#ifndef AIVM_HOST_MEMORY_H
#define AIVM_HOST_MEMORY_H

#include <stddef.h>

int aivm_host_memory_growth_allowed(
    size_t total_bytes,
    size_t available_bytes,
    size_t growth_bytes,
    int was_suspended,
    int* out_suspended);

/* Physical scheduling hint only; never changes task admission or observation. */
size_t aivm_host_memory_worker_capacity(size_t maximum_workers);

#endif
