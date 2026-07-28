#ifndef AIVM_WORKER_INVOCATION_H
#define AIVM_WORKER_INVOCATION_H

#include <stddef.h>
#include <stdint.h>

#include "aivm_vm.h"
#include "aivm_worker_program.h"

typedef enum {
    AIVM_WORKER_INVOCATION_PENDING = 0,
    AIVM_WORKER_INVOCATION_COMPLETED = 1,
    AIVM_WORKER_INVOCATION_FAILED = 2,
    AIVM_WORKER_INVOCATION_TRANSPORT_ERROR = 3
} AivmWorkerInvocationStatus;

typedef struct {
    const AivmWorkerProgram* program;
    const AivmSyscallBinding* syscall_bindings;
    size_t syscall_binding_count;
    AivmRuntimeProfile profile;
    uint8_t* payload;
    size_t payload_length;
    uint8_t* result;
    size_t result_length;
    AivmWorkerInvocationStatus status;
    AivmVmError vm_error;
    char error_detail[256];
} AivmWorkerInvocation;

void aivm_worker_invocation_clear(AivmWorkerInvocation* invocation);
void aivm_worker_invocation_release(AivmWorkerInvocation* invocation);
int aivm_worker_invocation_set_payload(
    AivmWorkerInvocation* invocation,
    const uint8_t* payload,
    size_t payload_length);
void aivm_worker_invocation_run(void* raw_invocation);

#endif
