#ifndef AIVM_WORKER_PROGRAM_H
#define AIVM_WORKER_PROGRAM_H

#include <stddef.h>
#include <stdint.h>

#include "aivm_program.h"
#include "sys/aivm_syscall.h"

typedef struct {
    AivmProgram* loaded;
    size_t function_target;
    uint64_t required_capabilities;
} AivmWorkerProgram;

typedef enum {
    AIVM_WORKER_PROGRAM_OK = 0,
    AIVM_WORKER_PROGRAM_ERR_ARGUMENT = 1,
    AIVM_WORKER_PROGRAM_ERR_CAPABILITY = 2,
    AIVM_WORKER_PROGRAM_ERR_MEMORY = 3,
    AIVM_WORKER_PROGRAM_ERR_ARTIFACT = 4,
    AIVM_WORKER_PROGRAM_ERR_SIGNATURE = 5
} AivmWorkerProgramStatus;

void aivm_worker_program_clear(AivmWorkerProgram* program);
void aivm_worker_program_release(AivmWorkerProgram* program);
AivmWorkerProgramStatus aivm_worker_program_prepare(
    const AivmWorkerCatalogEntry* entry,
    const AivmSyscallCapabilityPolicy* parent_policy,
    AivmWorkerProgram* out_program);

#endif
