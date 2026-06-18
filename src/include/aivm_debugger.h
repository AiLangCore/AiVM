#ifndef AIVM_DEBUGGER_H
#define AIVM_DEBUGGER_H

#include <stddef.h>

#include "aivm_vm.h"

enum {
    AIVM_DEBUGGER_MAX_BREAKPOINTS = 128
};

typedef enum {
    AIVM_DEBUGGER_OK = 0,
    AIVM_DEBUGGER_ERR_INVALID = 1,
    AIVM_DEBUGGER_ERR_BREAKPOINT_LIMIT = 2,
    AIVM_DEBUGGER_ERR_STEP_LIMIT = 3,
    AIVM_DEBUGGER_ERR_HALTED = 4,
    AIVM_DEBUGGER_ERR_VM = 5
} AivmDebuggerStatus;

typedef enum {
    AIVM_DEBUGGER_STATE_READY = 0,
    AIVM_DEBUGGER_STATE_PAUSED = 1,
    AIVM_DEBUGGER_STATE_RUNNING = 2,
    AIVM_DEBUGGER_STATE_HALTED = 3,
    AIVM_DEBUGGER_STATE_ERROR = 4
} AivmDebuggerState;

typedef enum {
    AIVM_DEBUGGER_INSPECT_STACK = 0,
    AIVM_DEBUGGER_INSPECT_LOCALS = 1,
    AIVM_DEBUGGER_INSPECT_FRAME = 2,
    AIVM_DEBUGGER_INSPECT_TASKS = 3,
    AIVM_DEBUGGER_INSPECT_QUEUE = 4
} AivmDebuggerInspectKind;

typedef struct {
    size_t pc;
    int opcode;
    size_t stack_count;
    size_t locals_count;
    size_t call_frame_count;
    size_t completed_task_count;
    AivmVmStatus vm_status;
    AivmVmError vm_error;
    AivmDebuggerState debugger_state;
} AivmDebuggerSnapshot;

typedef struct {
    AivmVm* vm;
    AivmDebuggerState state;
    size_t breakpoints[AIVM_DEBUGGER_MAX_BREAKPOINTS];
    size_t breakpoint_count;
    size_t executed_step_count;
} AivmDebugger;

void aivm_debugger_init(AivmDebugger* debugger, AivmVm* vm);
AivmDebuggerStatus aivm_debugger_break_pc(AivmDebugger* debugger, size_t pc);
AivmDebuggerStatus aivm_debugger_clear_breakpoints(AivmDebugger* debugger);
AivmDebuggerStatus aivm_debugger_pause(AivmDebugger* debugger, AivmDebuggerSnapshot* out_snapshot);
AivmDebuggerStatus aivm_debugger_step(AivmDebugger* debugger, AivmDebuggerSnapshot* out_snapshot);
AivmDebuggerStatus aivm_debugger_continue(
    AivmDebugger* debugger,
    size_t max_steps,
    AivmDebuggerSnapshot* out_snapshot);
AivmDebuggerStatus aivm_debugger_inspect(
    AivmDebugger* debugger,
    AivmDebuggerInspectKind kind,
    AivmDebuggerSnapshot* out_snapshot);
const char* aivm_debugger_status_name(AivmDebuggerStatus status);
const char* aivm_debugger_state_name(AivmDebuggerState state);

#endif
