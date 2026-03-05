#ifndef AIVM_RUNTIME_H
#define AIVM_RUNTIME_H

#include "aivm_program.h"
#include "sys/aivm_syscall.h"
#include "aivm_vm.h"

typedef enum {
    AIVM_RUNTIME_HOST_EVENT_OK = 0,
    AIVM_RUNTIME_HOST_EVENT_INVALID = 1,
    AIVM_RUNTIME_HOST_EVENT_REJECTED = 2
} AivmRuntimeHostEventStatus;

typedef int (*AivmRuntimeHostEnqueueFn)(void* context, const char* event_name, AivmValue payload);
typedef int (*AivmRuntimeHostDrainFn)(void* context, size_t max_events, size_t* out_drained_count);

typedef struct {
    void* context;
    AivmRuntimeHostEnqueueFn enqueue;
    AivmRuntimeHostDrainFn drain;
} AivmRuntimeHostAdapter;

int aivm_execute_program(const AivmProgram* program, AivmVm* vm_out);
int aivm_execute_program_with_syscalls(
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    AivmVm* vm_out);
int aivm_execute_program_with_syscalls_and_argv(
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* const* process_argv,
    size_t process_argv_count,
    AivmVm* vm_out);
AivmRuntimeHostEventStatus aivm_runtime_host_enqueue_event(
    const AivmRuntimeHostAdapter* adapter,
    const char* event_name,
    AivmValue payload);
AivmRuntimeHostEventStatus aivm_runtime_host_drain_events(
    const AivmRuntimeHostAdapter* adapter,
    size_t max_events,
    size_t* out_drained_count);

#endif
