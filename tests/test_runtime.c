#include "aivm_program.h"
#include "aivm_runtime.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

typedef struct {
    size_t enqueued_count;
    size_t drained_count;
    int enqueue_fail;
    int drain_fail;
    size_t forced_drain_count;
} HostAdapterState;

static int host_ui_get_window_size(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args == NULL || arg_count != 1U || args[0].type != AIVM_VAL_INT) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_node(320200);
    return AIVM_SYSCALL_OK;
}

static int host_process_argv(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (arg_count != 0U) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_node(1);
    return AIVM_SYSCALL_OK;
}

static int host_adapter_enqueue(void* context, const char* event_name, AivmValue payload)
{
    HostAdapterState* state = (HostAdapterState*)context;
    if (state == NULL || event_name == NULL) {
        return 1;
    }
    if (state->enqueue_fail != 0) {
        return 1;
    }
    if (payload.type == AIVM_VAL_INT && payload.int_value >= 0) {
        state->enqueued_count += 1U;
    }
    return 0;
}

static int host_adapter_drain(void* context, size_t max_events, size_t* out_drained_count)
{
    HostAdapterState* state = (HostAdapterState*)context;
    if (state == NULL || out_drained_count == NULL) {
        return 1;
    }
    if (state->drain_fail != 0) {
        return 1;
    }
    (void)max_events;
    *out_drained_count = state->forced_drain_count;
    state->drained_count += *out_drained_count;
    return 0;
}

int main(void)
{
    AivmVm vm;
    HostAdapterState adapter_state;
    AivmRuntimeHostAdapter adapter;
    size_t drained_count = 0U;
    static const AivmInstruction instructions_ok[] = {
        { .opcode = AIVM_OP_NOP, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmProgram program_ok = {
        .instructions = instructions_ok,
        .instruction_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmInstruction instructions_err[] = {
        { .opcode = (AivmOpcode)99, .operand_int = 0 }
    };
    static const AivmProgram program_err = {
        .instructions = instructions_err,
        .instruction_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmInstruction instructions_sys[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 1 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants_sys[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.ui.getWindowSize" },
        { .type = AIVM_VAL_INT, .int_value = 1 }
    };
    static const AivmSyscallBinding bindings[] = {
        { "sys.ui.getWindowSize", host_ui_get_window_size }
    };
    static const AivmProgram program_sys = {
        .instructions = instructions_sys,
        .instruction_count = 4U,
        .constants = constants_sys,
        .constant_count = 2U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const AivmInstruction instructions_argv[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 0 },
        { .opcode = AIVM_OP_CHILD_COUNT, .operand_int = 0 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants_argv[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.process.args" }
    };
    static const AivmSyscallBinding argv_bindings[] = {
        { "sys.process.args", host_process_argv }
    };
    static const AivmProgram program_argv = {
        .instructions = instructions_argv,
        .instruction_count = 4U,
        .constants = constants_argv,
        .constant_count = 1U,
        .format_version = 0U,
        .format_flags = 0U,
        .section_count = 0U
    };
    static const char* process_argv_values[] = {
        "one",
        "two"
    };

    if (expect(aivm_execute_program(&program_ok, &vm) == 1) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }

    if (expect(aivm_execute_program(&program_err, &vm) == 0) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_ERROR) != 0) {
        return 1;
    }

    if (expect(aivm_execute_program(NULL, &vm) == 0) != 0) {
        return 1;
    }

    if (expect(aivm_execute_program_with_syscalls(&program_sys, bindings, 1U, &vm) == 1) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(aivm_execute_program_with_syscalls_and_argv(
            &program_argv,
            argv_bindings,
            1U,
            process_argv_values,
            2U,
            &vm) == 1) != 0) {
        return 1;
    }
    if (expect(vm.status == AIVM_VM_STATUS_HALTED) != 0) {
        return 1;
    }
    if (expect(vm.stack_count == 1U) != 0) {
        return 1;
    }
    if (expect(vm.stack[0].type == AIVM_VAL_INT && vm.stack[0].int_value == 2) != 0) {
        return 1;
    }

    adapter_state.enqueued_count = 0U;
    adapter_state.drained_count = 0U;
    adapter_state.enqueue_fail = 0;
    adapter_state.drain_fail = 0;
    adapter_state.forced_drain_count = 2U;
    adapter.context = &adapter_state;
    adapter.enqueue = host_adapter_enqueue;
    adapter.drain = host_adapter_drain;

    if (expect(aivm_runtime_host_enqueue_event(NULL, "host.event.tick", aivm_value_int(1)) ==
               AIVM_RUNTIME_HOST_EVENT_INVALID) != 0) {
        return 1;
    }
    if (expect(aivm_runtime_host_enqueue_event(&adapter, "", aivm_value_int(1)) ==
               AIVM_RUNTIME_HOST_EVENT_INVALID) != 0) {
        return 1;
    }
    if (expect(aivm_runtime_host_enqueue_event(&adapter, "host.event.tick", aivm_value_int(1)) ==
               AIVM_RUNTIME_HOST_EVENT_OK) != 0) {
        return 1;
    }
    if (expect(adapter_state.enqueued_count == 1U) != 0) {
        return 1;
    }

    adapter_state.enqueue_fail = 1;
    if (expect(aivm_runtime_host_enqueue_event(&adapter, "host.event.tick", aivm_value_int(2)) ==
               AIVM_RUNTIME_HOST_EVENT_REJECTED) != 0) {
        return 1;
    }

    adapter_state.enqueue_fail = 0;
    if (expect(aivm_runtime_host_drain_events(&adapter, 0U, &drained_count) ==
               AIVM_RUNTIME_HOST_EVENT_INVALID) != 0) {
        return 1;
    }
    if (expect(aivm_runtime_host_drain_events(NULL, 4U, &drained_count) ==
               AIVM_RUNTIME_HOST_EVENT_INVALID) != 0) {
        return 1;
    }
    if (expect(aivm_runtime_host_drain_events(&adapter, 4U, &drained_count) ==
               AIVM_RUNTIME_HOST_EVENT_OK) != 0) {
        return 1;
    }
    if (expect(drained_count == 2U) != 0) {
        return 1;
    }
    if (expect(adapter_state.drained_count == 2U) != 0) {
        return 1;
    }

    adapter_state.drain_fail = 1;
    if (expect(aivm_runtime_host_drain_events(&adapter, 4U, &drained_count) ==
               AIVM_RUNTIME_HOST_EVENT_REJECTED) != 0) {
        return 1;
    }
    if (expect(drained_count == 0U) != 0) {
        return 1;
    }

    adapter_state.drain_fail = 0;
    adapter_state.forced_drain_count = 5U;
    if (expect(aivm_runtime_host_drain_events(&adapter, 4U, &drained_count) ==
               AIVM_RUNTIME_HOST_EVENT_INVALID) != 0) {
        return 1;
    }
    if (expect(drained_count == 0U) != 0) {
        return 1;
    }

    return 0;
}
