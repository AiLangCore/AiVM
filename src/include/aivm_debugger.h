#ifndef AIVM_DEBUGGER_H
#define AIVM_DEBUGGER_H

#include <stddef.h>

#include "aivm_vm.h"

enum {
    AIVM_DEBUGGER_MAX_BREAKPOINTS = 128,
    AIVM_DEBUGGER_MAX_SOURCE_MAPPINGS = 128,
    AIVM_DEBUGGER_SOURCE_NAME_LENGTH = 128
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
    AIVM_DEBUGGER_INSPECT_QUEUE = 4,
    AIVM_DEBUGGER_INSPECT_HEAP = 5,
    AIVM_DEBUGGER_INSPECT_HOST_OPS = 6
} AivmDebuggerInspectKind;

typedef struct {
    char name[AIVM_DEBUGGER_SOURCE_NAME_LENGTH];
    size_t pc;
} AivmDebuggerSourceMapping;

typedef struct {
    size_t pc;
    int opcode;
    size_t stack_count;
    size_t locals_count;
    size_t call_frame_count;
    size_t completed_task_count;
    size_t node_count;
    size_t node_attr_count;
    size_t node_child_count;
    size_t blob_count;
    size_t syscall_binding_count;
    size_t process_argv_count;
    size_t active_host_operation_count;
    AivmVmStatus vm_status;
    AivmVmError vm_error;
    AivmDebuggerState debugger_state;
} AivmDebuggerSnapshot;

typedef struct {
    AivmVm* vm;
    AivmDebuggerState state;
    size_t breakpoints[AIVM_DEBUGGER_MAX_BREAKPOINTS];
    size_t breakpoint_count;
    AivmDebuggerSourceMapping function_mappings[AIVM_DEBUGGER_MAX_SOURCE_MAPPINGS];
    size_t function_mapping_count;
    AivmDebuggerSourceMapping node_mappings[AIVM_DEBUGGER_MAX_SOURCE_MAPPINGS];
    size_t node_mapping_count;
    size_t active_host_operation_count;
    size_t executed_step_count;
} AivmDebugger;

void aivm_debugger_init(AivmDebugger* debugger, AivmVm* vm);
AivmDebuggerStatus aivm_debugger_break_pc(AivmDebugger* debugger, size_t pc);
AivmDebuggerStatus aivm_debugger_register_function(AivmDebugger* debugger, const char* name, size_t pc);
AivmDebuggerStatus aivm_debugger_register_node(AivmDebugger* debugger, const char* node_id, size_t pc);
AivmDebuggerStatus aivm_debugger_break_function(AivmDebugger* debugger, const char* name);
AivmDebuggerStatus aivm_debugger_break_node(AivmDebugger* debugger, const char* node_id);
AivmDebuggerStatus aivm_debugger_clear_breakpoints(AivmDebugger* debugger);
void aivm_debugger_set_active_host_operation_count(AivmDebugger* debugger, size_t count);
AivmDebuggerStatus aivm_debugger_pause(AivmDebugger* debugger, AivmDebuggerSnapshot* out_snapshot);
AivmDebuggerStatus aivm_debugger_step(AivmDebugger* debugger, AivmDebuggerSnapshot* out_snapshot);
AivmDebuggerStatus aivm_debugger_step_over(
    AivmDebugger* debugger,
    size_t max_steps,
    AivmDebuggerSnapshot* out_snapshot);
AivmDebuggerStatus aivm_debugger_step_out(
    AivmDebugger* debugger,
    size_t max_steps,
    AivmDebuggerSnapshot* out_snapshot);
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
