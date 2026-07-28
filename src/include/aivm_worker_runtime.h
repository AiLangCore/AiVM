#ifndef AIVM_WORKER_RUNTIME_H
#define AIVM_WORKER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "aivm_program.h"
#include "aivm_vm.h"
#include "aivm_worker_scheduler.h"

typedef struct AivmWorkerRuntime AivmWorkerRuntime;

typedef enum {
    AIVM_WORKER_RUNTIME_OK = 0,
    AIVM_WORKER_RUNTIME_ERR_ARGUMENT = 1,
    AIVM_WORKER_RUNTIME_ERR_LIMIT = 2,
    AIVM_WORKER_RUNTIME_ERR_CATALOG = 3,
    AIVM_WORKER_RUNTIME_ERR_CAPABILITY = 4,
    AIVM_WORKER_RUNTIME_ERR_PROGRAM = 5,
    AIVM_WORKER_RUNTIME_ERR_EXECUTION = 6,
    AIVM_WORKER_RUNTIME_ERR_TRANSPORT = 7,
    AIVM_WORKER_RUNTIME_ERR_MEMORY = 8,
    AIVM_WORKER_RUNTIME_ERR_SYSTEM = 9,
    AIVM_WORKER_RUNTIME_CANCELED = 10
} AivmWorkerRuntimeStatus;

typedef struct {
    const uint8_t* data;
    size_t length;
    AivmWorkerRuntimeStatus status;
    AivmVmError vm_error;
    const char* error_detail;
} AivmWorkerRuntimeResult;

/*
 * A runtime binds an already validated catalog to the parent's mechanical
 * grants. Artifact lookup is catalog-index based; no path or package lookup
 * occurs here.
 */
AivmWorkerRuntimeStatus aivm_worker_runtime_create(
    const AivmProgram* owner_program,
    const AivmSyscallCapabilityPolicy* parent_policy,
    const AivmSyscallBinding* syscall_bindings,
    size_t syscall_binding_count,
    AivmRuntimeProfile profile,
    size_t outstanding_limit,
    AivmWorkerRuntime** out_runtime);
void aivm_worker_runtime_destroy(AivmWorkerRuntime* runtime);
AivmWorkerRuntimeStatus aivm_worker_runtime_submit(
    AivmWorkerRuntime* runtime,
    size_t worker_catalog_index,
    uint64_t submission_id,
    const uint8_t* payload,
    size_t payload_length);
AivmWorkerRuntimeStatus aivm_worker_runtime_await(
    AivmWorkerRuntime* runtime,
    uint64_t submission_id,
    AivmWorkerRuntimeResult* out_result);
AivmWorkerRuntimeStatus aivm_worker_runtime_cancel(
    AivmWorkerRuntime* runtime,
    uint64_t submission_id,
    int* out_canceled);
AivmWorkerRuntimeStatus aivm_worker_runtime_release(
    AivmWorkerRuntime* runtime,
    uint64_t submission_id);
size_t aivm_worker_runtime_active_limit(const AivmWorkerRuntime* runtime);

#endif
