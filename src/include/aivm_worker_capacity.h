#ifndef AIVM_WORKER_CAPACITY_H
#define AIVM_WORKER_CAPACITY_H

#include <stddef.h>

/*
 * Mechanical host-capacity discovery for the VM-owned worker scheduler.
 * Returned capacity affects performance only, never language semantics.
 */
size_t aivm_worker_available_logical_cpu(void);
size_t aivm_worker_active_capacity(size_t profile_ceiling, size_t memory_safe_capacity);

#endif
