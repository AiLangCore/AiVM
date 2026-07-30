#include "aivm_worker_invocation.h"
#include "aivm_vm_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_worker_function(
    AivmVm* vm,
    size_t function_target,
    AivmValue* out_result)
{
    if (!aivm_frame_push(vm, vm->program->instruction_count, 0U)) {
        return 0;
    }
    vm->instruction_pointer = function_target;
    while (vm->status != AIVM_VM_STATUS_ERROR) {
        if (vm->call_frame_count == 0U &&
            vm->instruction_pointer == vm->program->instruction_count) {
            break;
        }
        if (vm->instruction_pointer >= vm->program->instruction_count) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
                "Worker instruction pointer is outside the bundled artifact.");
            return 0;
        }
        aivm_step(vm);
        if (vm->status == AIVM_VM_STATUS_HALTED &&
            vm->instruction_pointer != vm->program->instruction_count) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
                "Worker halted before its declared function returned.");
            return 0;
        }
    }
    if (vm->status == AIVM_VM_STATUS_ERROR) {
        return 0;
    }
    if (vm->stack_count != 1U) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM,
            "Worker function must return exactly one transport value.");
        return 0;
    }
    *out_result = vm->stack[0];
    return 1;
}

void aivm_worker_invocation_clear(AivmWorkerInvocation* invocation)
{
    if (invocation == NULL) {
        return;
    }
    invocation->payload = NULL;
    invocation->payload_length = 0U;
    invocation->result = NULL;
    invocation->result_length = 0U;
    invocation->status = AIVM_WORKER_INVOCATION_PENDING;
    invocation->vm_error = AIVM_VM_ERR_NONE;
    invocation->error_detail[0] = '\0';
}

void aivm_worker_invocation_release(AivmWorkerInvocation* invocation)
{
    if (invocation == NULL) {
        return;
    }
    free(invocation->payload);
    free(invocation->result);
    aivm_worker_invocation_clear(invocation);
}

int aivm_worker_invocation_set_payload(
    AivmWorkerInvocation* invocation,
    const uint8_t* payload,
    size_t payload_length)
{
    if (invocation == NULL || (payload_length > 0U && payload == NULL)) {
        return 0;
    }
    invocation->payload = (uint8_t*)malloc(payload_length == 0U ? 1U : payload_length);
    if (invocation->payload == NULL) {
        return 0;
    }
    if (payload_length > 0U) {
        memcpy(invocation->payload, payload, payload_length);
    }
    invocation->payload_length = payload_length;
    return 1;
}

void aivm_worker_invocation_run(void* raw_invocation)
{
    AivmWorkerInvocation* invocation = (AivmWorkerInvocation*)raw_invocation;
    AivmVm* vm;
    AivmValue result;
    AivmSyscallCapabilityPolicy worker_policy;
    if (invocation == NULL || invocation->program == NULL ||
        invocation->program->loaded == NULL) {
        return;
    }
    vm = (AivmVm*)calloc(1U, sizeof(*vm));
    if (vm == NULL) {
        invocation->status = AIVM_WORKER_INVOCATION_FAILED;
        invocation->vm_error = AIVM_VM_ERR_MEMORY_PRESSURE;
        (void)snprintf(invocation->error_detail, sizeof(invocation->error_detail),
            "Worker VM allocation failed.");
        return;
    }
    aivm_init_with_syscalls_and_argv_profile(
        vm, invocation->program->loaded,
        invocation->syscall_bindings, invocation->syscall_binding_count,
        NULL, 0U, invocation->profile);
    worker_policy.allowed_capability_mask = invocation->program->required_capabilities;
    aivm_set_syscall_policy(vm, &worker_policy);
    if (!aivm_stack_push(vm,
        aivm_value_bytes(invocation->payload, invocation->payload_length)) ||
        !run_worker_function(vm, invocation->program->function_target, &result)) {
        const char* detail;
        invocation->status = AIVM_WORKER_INVOCATION_FAILED;
        invocation->vm_error = vm->error == AIVM_VM_ERR_NONE
            ? AIVM_VM_ERR_INVALID_PROGRAM : vm->error;
        detail = aivm_vm_error_detail(vm);
        if (detail == NULL || detail[0] == '\0') {
            detail = aivm_vm_error_message(invocation->vm_error);
        }
        (void)snprintf(invocation->error_detail, sizeof(invocation->error_detail),
            "%s", detail == NULL ? "Worker execution failed." : detail);
        aivm_dispose(vm);
        free(vm);
        return;
    }
    if (result.type != AIVM_VAL_BYTES ||
        (result.bytes_value.length > 0U && result.bytes_value.data == NULL)) {
        invocation->status = AIVM_WORKER_INVOCATION_TRANSPORT_ERROR;
        invocation->vm_error = AIVM_VM_ERR_TYPE_MISMATCH;
        (void)snprintf(invocation->error_detail, sizeof(invocation->error_detail),
            "Worker result must be bytes.");
        aivm_dispose(vm);
        free(vm);
        return;
    }
    invocation->result = (uint8_t*)malloc(
        result.bytes_value.length == 0U ? 1U : result.bytes_value.length);
    if (invocation->result == NULL) {
        invocation->status = AIVM_WORKER_INVOCATION_FAILED;
        invocation->vm_error = AIVM_VM_ERR_MEMORY_PRESSURE;
        (void)snprintf(invocation->error_detail, sizeof(invocation->error_detail),
            "Worker result allocation failed.");
        aivm_dispose(vm);
        free(vm);
        return;
    }
    if (result.bytes_value.length > 0U) {
        memcpy(invocation->result, result.bytes_value.data, result.bytes_value.length);
    }
    invocation->result_length = result.bytes_value.length;
    invocation->status = AIVM_WORKER_INVOCATION_COMPLETED;
    aivm_dispose(vm);
    free(vm);
}
