#include "aivm_debugger.h"

#include <stdio.h>
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

static AivmDebuggerStatus register_mapping(
    AivmDebuggerSourceMapping* mappings,
    size_t* mapping_count,
    const AivmDebugger* debugger,
    const char* name,
    size_t pc)
{
    size_t index;
    if (mappings == NULL ||
        mapping_count == NULL ||
        debugger == NULL ||
        debugger->vm == NULL ||
        debugger->vm->program == NULL ||
        name == NULL ||
        name[0] == '\0' ||
        pc >= debugger->vm->program->instruction_count) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    for (index = 0U; index < *mapping_count; index += 1U) {
        if (strcmp(mappings[index].name, name) == 0) {
            mappings[index].pc = pc;
            return AIVM_DEBUGGER_OK;
        }
    }
    if (*mapping_count >= AIVM_DEBUGGER_MAX_SOURCE_MAPPINGS) {
        return AIVM_DEBUGGER_ERR_BREAKPOINT_LIMIT;
    }
    (void)snprintf(mappings[*mapping_count].name, sizeof(mappings[*mapping_count].name), "%s", name);
    mappings[*mapping_count].pc = pc;
    *mapping_count += 1U;
    return AIVM_DEBUGGER_OK;
}

static int find_mapping(
    const AivmDebuggerSourceMapping* mappings,
    size_t mapping_count,
    const char* name,
    size_t* out_pc)
{
    size_t index;
    if (mappings == NULL || name == NULL || out_pc == NULL) {
        return 0;
    }
    for (index = 0U; index < mapping_count; index += 1U) {
        if (strcmp(mappings[index].name, name) == 0) {
            *out_pc = mappings[index].pc;
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
    out_snapshot->node_count = vm->node_count;
    out_snapshot->node_attr_count = vm->node_attr_count;
    out_snapshot->node_child_count = vm->node_child_count;
    out_snapshot->blob_count = vm->blob_count;
    out_snapshot->syscall_binding_count = vm->syscall_binding_count;
    out_snapshot->process_argv_count = vm->process_argv_count;
    out_snapshot->active_host_operation_count = debugger->active_host_operation_count;
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

AivmDebuggerStatus aivm_debugger_register_function(AivmDebugger* debugger, const char* name, size_t pc)
{
    if (debugger == NULL) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    return register_mapping(
        debugger->function_mappings,
        &debugger->function_mapping_count,
        debugger,
        name,
        pc);
}

AivmDebuggerStatus aivm_debugger_register_node(AivmDebugger* debugger, const char* node_id, size_t pc)
{
    if (debugger == NULL) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    return register_mapping(debugger->node_mappings, &debugger->node_mapping_count, debugger, node_id, pc);
}

AivmDebuggerStatus aivm_debugger_break_function(AivmDebugger* debugger, const char* name)
{
    size_t pc;
    if (debugger == NULL || !find_mapping(debugger->function_mappings, debugger->function_mapping_count, name, &pc)) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    return aivm_debugger_break_pc(debugger, pc);
}

AivmDebuggerStatus aivm_debugger_break_node(AivmDebugger* debugger, const char* node_id)
{
    size_t pc;
    if (debugger == NULL || !find_mapping(debugger->node_mappings, debugger->node_mapping_count, node_id, &pc)) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    return aivm_debugger_break_pc(debugger, pc);
}

AivmDebuggerStatus aivm_debugger_clear_breakpoints(AivmDebugger* debugger)
{
    if (debugger == NULL) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    debugger->breakpoint_count = 0U;
    return AIVM_DEBUGGER_OK;
}

void aivm_debugger_set_active_host_operation_count(AivmDebugger* debugger, size_t count)
{
    if (debugger != NULL) {
        debugger->active_host_operation_count = count;
    }
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

AivmDebuggerStatus aivm_debugger_step_over(
    AivmDebugger* debugger,
    size_t max_steps,
    AivmDebuggerSnapshot* out_snapshot)
{
    size_t initial_frame_count;
    size_t steps = 0U;
    AivmDebuggerStatus status;
    if (debugger == NULL || debugger->vm == NULL || max_steps == 0U) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    initial_frame_count = debugger->vm->call_frame_count;
    status = aivm_debugger_step(debugger, out_snapshot);
    if (status != AIVM_DEBUGGER_OK || debugger->vm->call_frame_count <= initial_frame_count) {
        return status;
    }
    while (steps < max_steps && debugger->vm->call_frame_count > initial_frame_count) {
        status = aivm_debugger_step(debugger, out_snapshot);
        if (status != AIVM_DEBUGGER_OK) {
            return status;
        }
        steps += 1U;
        if (has_breakpoint(debugger, debugger->vm->instruction_pointer)) {
            debugger->state = AIVM_DEBUGGER_STATE_PAUSED;
            capture_snapshot(debugger, out_snapshot);
            return AIVM_DEBUGGER_OK;
        }
    }
    if (debugger->vm->call_frame_count > initial_frame_count) {
        capture_snapshot(debugger, out_snapshot);
        return AIVM_DEBUGGER_ERR_STEP_LIMIT;
    }
    capture_snapshot(debugger, out_snapshot);
    return AIVM_DEBUGGER_OK;
}

AivmDebuggerStatus aivm_debugger_step_out(
    AivmDebugger* debugger,
    size_t max_steps,
    AivmDebuggerSnapshot* out_snapshot)
{
    size_t initial_frame_count;
    size_t steps = 0U;
    AivmDebuggerStatus status;
    if (debugger == NULL || debugger->vm == NULL || max_steps == 0U) {
        return AIVM_DEBUGGER_ERR_INVALID;
    }
    initial_frame_count = debugger->vm->call_frame_count;
    if (initial_frame_count == 0U) {
        return aivm_debugger_step_over(debugger, max_steps, out_snapshot);
    }
    while (steps < max_steps && debugger->vm->call_frame_count >= initial_frame_count) {
        status = aivm_debugger_step(debugger, out_snapshot);
        if (status != AIVM_DEBUGGER_OK) {
            return status;
        }
        steps += 1U;
        if (has_breakpoint(debugger, debugger->vm->instruction_pointer)) {
            debugger->state = AIVM_DEBUGGER_STATE_PAUSED;
            capture_snapshot(debugger, out_snapshot);
            return AIVM_DEBUGGER_OK;
        }
    }
    if (debugger->vm->call_frame_count >= initial_frame_count) {
        capture_snapshot(debugger, out_snapshot);
        return AIVM_DEBUGGER_ERR_STEP_LIMIT;
    }
    capture_snapshot(debugger, out_snapshot);
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
        kind != AIVM_DEBUGGER_INSPECT_QUEUE &&
        kind != AIVM_DEBUGGER_INSPECT_HEAP &&
        kind != AIVM_DEBUGGER_INSPECT_HOST_OPS) {
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
