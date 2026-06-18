#include "aivm_debugger.h"

#include <string.h>

static int has_breakpoint(const AivmDebugger* debugger, size_t pc)
{
    size_t index;
    if (debugger == NULL) {
        return 0;
    }
    for (index = 0U; index < debugger->breakpoint_count; index += 1U) {
        if (debugger->breakpoints[index] == pc) {
            return 1;
        }
    }
    return 0;
}

static void sync_state(AivmDebugger* debugger)
{
    if (debugger == NULL || debugger->vm == NULL) {
        return;
    }
    if (debugger->vm->status == AIVM_VM_STATUS_HALTED) {
        debugger->state = AIVM_DEBUGGER_STATE_HALTED;
    } else if (debugger->vm->status == AIVM_VM_STATUS_ERROR) {
        debugger->state = AIVM_DEBUGGER_STATE_ERROR;
    }
}

static void capture_snapshot(AivmDebugger* debugger, AivmDebuggerSnapshot* out_snapshot)
{
    AivmVm* vm;
    if (debugger == NULL || out_snapshot == NULL) {
        return;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    out_snapshot->debugger_state = debugger->state;
    vm = debugger->vm;
    if (vm == NULL) {
        out_snapshot->vm_status = AIVM_VM_STATUS_ERROR;
        out_snapshot->vm_error = AIVM_VM_ERR_INVALID_PROGRAM;
        return;
    }
    out_snapshot->pc = vm->instruction_pointer;
    out_snapshot->opcode = -1;
    if (vm->program != NULL &&
        vm->program->instructions != NULL &&
        vm->instruction_pointer < vm->program->instruction_count) {
        out_snapshot->opcode = (int)vm->program->instructions[vm->instruction_pointer].opcode;
    }
    out_snapshot->stack_count = vm->stack_count;
    out_snapshot->locals_count = vm->locals_count;
    out_snapshot->call_frame_count = vm->call_frame_count;
    out_snapshot->completed_task_count = vm->completed_task_count;
    out_snapshot->vm_status = vm->status;
    out_snapshot->vm_error = vm->error;
}

void aivm_debugger_init(AivmDebugger* debugger, AivmVm* vm)
{
    if (debugger == NULL) {
        return;
    }
    memset(debugger, 0, sizeof(*debugger));
    debugger->vm = vm;
    debugger->state = AIVM_DEBUGGER_STATE_READY;
    sync_state(debugger);
}

AivmDebuggerStatus aivm_debugger_break_pc(AivmDebugger* debugger, size_t pc)
{
    if (debugger == NULL || debugger->vm == NULL || debugger->vm->program == NULL) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    if (pc >= debugger->vm->program->instruction_count) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    if (has_breakpoint(debugger, pc)) {
        return AIVM_DEBUGGER_OK;
    }
    if (debugger->breakpoint_count >= AIVM_DEBUGGER_MAX_BREAKPOINTS) {
        return AIVM_DEBUGGER_ERR_BREAKPOINT_LIMIT;
    }
    debugger->breakpoints[debugger->breakpoint_count] = pc;
    debugger->breakpoint_count += 1U;
    return AIVM_DEBUGGER_OK;
}

AivmDebuggerStatus aivm_debugger_clear_breakpoints(AivmDebugger* debugger)
{
    if (debugger == NULL) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    debugger->breakpoint_count = 0U;
    return AIVM_DEBUGGER_OK;
}

AivmDebuggerStatus aivm_debugger_pause(AivmDebugger* debugger, AivmDebuggerSnapshot* out_snapshot)
{
    if (debugger == NULL || debugger->vm == NULL) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    sync_state(debugger);
    if (debugger->state != AIVM_DEBUGGER_STATE_HALTED &&
        debugger->state != AIVM_DEBUGGER_STATE_ERROR) {
        debugger->state = AIVM_DEBUGGER_STATE_PAUSED;
    }
    capture_snapshot(debugger, out_snapshot);
    if (debugger->state == AIVM_DEBUGGER_STATE_HALTED) {
        return AIVM_DEBUGGER_ERR_HALTED;
    }
    if (debugger->state == AIVM_DEBUGGER_STATE_ERROR) {
        return AIVM_DEBUGGER_ERR_VM;
    }
    return AIVM_DEBUGGER_OK;
}

AivmDebuggerStatus aivm_debugger_step(AivmDebugger* debugger, AivmDebuggerSnapshot* out_snapshot)
{
    if (debugger == NULL || debugger->vm == NULL) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    sync_state(debugger);
    if (debugger->state == AIVM_DEBUGGER_STATE_HALTED) {
        capture_snapshot(debugger, out_snapshot);
        return AIVM_DEBUGGER_ERR_HALTED;
    }
    if (debugger->state == AIVM_DEBUGGER_STATE_ERROR) {
        capture_snapshot(debugger, out_snapshot);
        return AIVM_DEBUGGER_ERR_VM;
    }
    debugger->state = AIVM_DEBUGGER_STATE_RUNNING;
    aivm_step(debugger->vm);
    debugger->executed_step_count += 1U;
    debugger->state = AIVM_DEBUGGER_STATE_PAUSED;
    sync_state(debugger);
    capture_snapshot(debugger, out_snapshot);
    if (debugger->state == AIVM_DEBUGGER_STATE_HALTED) {
        return AIVM_DEBUGGER_ERR_HALTED;
    }
    if (debugger->state == AIVM_DEBUGGER_STATE_ERROR) {
        return AIVM_DEBUGGER_ERR_VM;
    }
    return AIVM_DEBUGGER_OK;
}

AivmDebuggerStatus aivm_debugger_continue(
    AivmDebugger* debugger,
    size_t max_steps,
    AivmDebuggerSnapshot* out_snapshot)
{
    size_t steps = 0U;
    if (debugger == NULL || debugger->vm == NULL || max_steps == 0U) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    sync_state(debugger);
    if (debugger->state == AIVM_DEBUGGER_STATE_HALTED) {
        capture_snapshot(debugger, out_snapshot);
        return AIVM_DEBUGGER_ERR_HALTED;
    }
    if (debugger->state == AIVM_DEBUGGER_STATE_ERROR) {
        capture_snapshot(debugger, out_snapshot);
        return AIVM_DEBUGGER_ERR_VM;
    }
    if (has_breakpoint(debugger, debugger->vm->instruction_pointer)) {
        debugger->state = AIVM_DEBUGGER_STATE_PAUSED;
        capture_snapshot(debugger, out_snapshot);
        return AIVM_DEBUGGER_OK;
    }
    debugger->state = AIVM_DEBUGGER_STATE_RUNNING;
    while (steps < max_steps) {
        aivm_step(debugger->vm);
        debugger->executed_step_count += 1U;
        steps += 1U;
        sync_state(debugger);
        if (debugger->state == AIVM_DEBUGGER_STATE_HALTED) {
            capture_snapshot(debugger, out_snapshot);
            return AIVM_DEBUGGER_ERR_HALTED;
        }
        if (debugger->state == AIVM_DEBUGGER_STATE_ERROR) {
            capture_snapshot(debugger, out_snapshot);
            return AIVM_DEBUGGER_ERR_VM;
        }
        if (has_breakpoint(debugger, debugger->vm->instruction_pointer)) {
            debugger->state = AIVM_DEBUGGER_STATE_PAUSED;
            capture_snapshot(debugger, out_snapshot);
            return AIVM_DEBUGGER_OK;
        }
    }
    debugger->state = AIVM_DEBUGGER_STATE_PAUSED;
    capture_snapshot(debugger, out_snapshot);
    return AIVM_DEBUGGER_ERR_STEP_LIMIT;
}

AivmDebuggerStatus aivm_debugger_inspect(
    AivmDebugger* debugger,
    AivmDebuggerInspectKind kind,
    AivmDebuggerSnapshot* out_snapshot)
{
    if (debugger == NULL || debugger->vm == NULL) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    if (kind != AIVM_DEBUGGER_INSPECT_STACK &&
        kind != AIVM_DEBUGGER_INSPECT_LOCALS &&
        kind != AIVM_DEBUGGER_INSPECT_FRAME &&
        kind != AIVM_DEBUGGER_INSPECT_TASKS &&
        kind != AIVM_DEBUGGER_INSPECT_QUEUE) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    sync_state(debugger);
    capture_snapshot(debugger, out_snapshot);
    return debugger->state == AIVM_DEBUGGER_STATE_ERROR ? AIVM_DEBUGGER_ERR_VM : AIVM_DEBUGGER_OK;
}

const char* aivm_debugger_status_name(AivmDebuggerStatus status)
{
    switch (status) {
        case AIVM_DEBUGGER_OK:
            return "ok";
        case AIVM_DEBUGGER_ERR_INVALID:
            return "invalid";
        case AIVM_DEBUGGER_ERR_BREAKPOINT_LIMIT:
            return "breakpoint-limit";
        case AIVM_DEBUGGER_ERR_STEP_LIMIT:
            return "step-limit";
        case AIVM_DEBUGGER_ERR_HALTED:
            return "halted";
        case AIVM_DEBUGGER_ERR_VM:
            return "vm-error";
        default:
            return "unknown";
    }
}

const char* aivm_debugger_state_name(AivmDebuggerState state)
{
    switch (state) {
        case AIVM_DEBUGGER_STATE_READY:
            return "ready";
        case AIVM_DEBUGGER_STATE_PAUSED:
            return "paused";
        case AIVM_DEBUGGER_STATE_RUNNING:
            return "running";
        case AIVM_DEBUGGER_STATE_HALTED:
            return "halted";
        case AIVM_DEBUGGER_STATE_ERROR:
            return "error";
        default:
            return "unknown";
    }
}
