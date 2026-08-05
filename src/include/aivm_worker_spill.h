#ifndef AIVM_WORKER_SPILL_H
#define AIVM_WORKER_SPILL_H

#include <stddef.h>
#include <stdint.h>

typedef struct AivmWorkerSpill AivmWorkerSpill;

int aivm_worker_spill_write(
    const uint8_t* data,
    size_t length,
    AivmWorkerSpill** out_spill);
int aivm_worker_spill_read(
    AivmWorkerSpill* spill,
    uint8_t* destination,
    size_t length);
void aivm_worker_spill_destroy(AivmWorkerSpill* spill);

#endif
