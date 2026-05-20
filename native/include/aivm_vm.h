#ifndef AIVM_VM_H
#define AIVM_VM_H

#include <stddef.h>

#include "aivm_program.h"
#include "sys/aivm_syscall.h"
#include "aivm_types.h"

typedef enum {
    AIVM_VM_STATUS_READY = 0,
    AIVM_VM_STATUS_RUNNING = 1,
    AIVM_VM_STATUS_HALTED = 2,
    AIVM_VM_STATUS_ERROR = 3
} AivmVmStatus;

typedef enum {
    AIVM_VM_ERR_NONE = 0,
    AIVM_VM_ERR_INVALID_OPCODE = 1,
    AIVM_VM_ERR_STACK_OVERFLOW = 2,
    AIVM_VM_ERR_STACK_UNDERFLOW = 3,
    AIVM_VM_ERR_FRAME_OVERFLOW = 4,
    AIVM_VM_ERR_FRAME_UNDERFLOW = 5,
    AIVM_VM_ERR_LOCAL_OUT_OF_RANGE = 6,
    AIVM_VM_ERR_TYPE_MISMATCH = 7,
    AIVM_VM_ERR_INVALID_PROGRAM = 8,
    AIVM_VM_ERR_STRING_OVERFLOW = 9,
    AIVM_VM_ERR_SYSCALL = 10,
    AIVM_VM_ERR_MEMORY_PRESSURE = 11
} AivmVmError;

typedef enum {
    AIVM_RUNTIME_PROFILE_PRODUCTION = 0,
    AIVM_RUNTIME_PROFILE_DEBUG = 1,
    AIVM_RUNTIME_PROFILE_TOOLING = 2
} AivmRuntimeProfile;

typedef struct {
    size_t stack_capacity;
    size_t call_frame_capacity;
    size_t locals_capacity;
    size_t string_arena_capacity;
    size_t bytes_arena_capacity;
    size_t node_capacity;
    size_t node_attr_capacity;
    size_t node_child_capacity;
    size_t task_capacity;
    size_t par_value_capacity;
    size_t file_read_bytes;
    size_t file_write_bytes;
    size_t network_read_bytes;
    size_t network_write_bytes;
    size_t process_count;
    size_t worker_count;
    size_t ui_window_count;
    size_t debug_artifact_bytes;
    size_t syscall_elapsed_ms;
} AivmRuntimeProfileLimits;

typedef struct {
    size_t return_instruction_pointer;
    size_t frame_base;
    size_t locals_base;
} AivmCallFrame;

typedef struct {
    size_t instruction_pointer;
    size_t target;
    size_t arg_count;
    size_t stack_count;
} AivmCallHistoryEntry;

typedef struct {
    size_t instruction_pointer;
    size_t stack_count;
    size_t pre_restore_stack_count;
    size_t frame_base;
    int has_return_value;
} AivmReturnHistoryEntry;

typedef struct {
    size_t instruction_pointer;
    int opcode;
    size_t stack_count;
} AivmOpcodeHistoryEntry;

#if defined(AIVM_DEBUG_RUNTIME)
enum {
    AIVM_VM_PROFILE_SYSCALL_TARGET_CAPACITY = 64,
    AIVM_VM_PROFILE_SYSCALL_TARGET_LENGTH = 96
};

typedef struct {
    char target[AIVM_VM_PROFILE_SYSCALL_TARGET_LENGTH];
    size_t count;
    double elapsed_seconds;
} AivmProfileSyscallTargetCount;
#endif

typedef enum {
    AIVM_NODE_ATTR_IDENTIFIER = 0,
    AIVM_NODE_ATTR_STRING = 1,
    AIVM_NODE_ATTR_INT = 2,
    AIVM_NODE_ATTR_BOOL = 3
} AivmNodeAttrKind;

typedef struct {
    const char* key;
    AivmNodeAttrKind kind;
    union {
        const char* string_value;
        int64_t int_value;
        int bool_value;
    };
} AivmNodeAttr;

typedef struct {
    const char* kind;
    const char* id;
    size_t attr_start;
    size_t attr_count;
    size_t child_start;
    size_t child_count;
} AivmNodeRecord;

typedef enum {
    AIVM_TASK_STATE_PENDING = 0,
    AIVM_TASK_STATE_COMPLETED = 1,
    AIVM_TASK_STATE_FAILED = 2,
    AIVM_TASK_STATE_CANCELED = 3
} AivmTaskState;

typedef struct {
    AivmTaskState state;
    int64_t handle;
    AivmValue result;
} AivmCompletedTask;

typedef struct {
    size_t expected_count;
    size_t start_index;
} AivmParContext;

enum {
    AIVM_VM_STACK_CAPACITY = 20000,
    AIVM_VM_STACK_INITIAL_CAPACITY = 1024,
    AIVM_VM_STACK_GROWTH_STEP = 512,
    AIVM_VM_CALLFRAME_CAPACITY = 2048,
    AIVM_VM_CALLFRAME_INITIAL_CAPACITY = 256,
    AIVM_VM_CALLFRAME_GROWTH_STEP = 256,
    AIVM_VM_LOCALS_CAPACITY = 16384,
    AIVM_VM_LOCALS_INITIAL_CAPACITY = 2048,
    AIVM_VM_LOCALS_GROWTH_STEP = 1024,
    AIVM_VM_STRING_ARENA_CAPACITY = 524288,
    AIVM_VM_STRING_ARENA_INITIAL_CAPACITY = 8192,
    AIVM_VM_STRING_ARENA_GROWTH_STEP = 16384,
    AIVM_VM_BYTES_ARENA_CAPACITY = 131072,
    AIVM_VM_BYTES_ARENA_INITIAL_CAPACITY = 32768,
    AIVM_VM_BYTES_ARENA_GROWTH_STEP = 16384,
    AIVM_VM_MAX_SYSCALL_ARGS = 16,
    AIVM_VM_NODE_CAPACITY = 16384,
    AIVM_VM_NODE_ATTR_CAPACITY = 65536,
    AIVM_VM_NODE_CHILD_CAPACITY = 131072,
    AIVM_VM_TASK_CAPACITY = 256,
    AIVM_VM_PAR_CONTEXT_CAPACITY = 64,
    AIVM_VM_PAR_VALUE_CAPACITY = 1024,
    AIVM_VM_FILE_READ_BYTES = 16 * 1024 * 1024,
    AIVM_VM_FILE_WRITE_BYTES = 16 * 1024 * 1024,
    AIVM_VM_NETWORK_READ_BYTES = 1024 * 1024,
    AIVM_VM_NETWORK_WRITE_BYTES = 1024 * 1024,
    AIVM_VM_PROCESS_COUNT = 32,
    AIVM_VM_WORKER_COUNT = 64,
    AIVM_VM_UI_WINDOW_COUNT = 16,
    AIVM_VM_DEBUG_ARTIFACT_BYTES = 64 * 1024 * 1024,
    AIVM_VM_SYSCALL_ELAPSED_MS = 30000,
    AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS = 64,
    AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR = 3,
    AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR = 4,
    AIVM_VM_NODE_GC_PRESSURE_THRESHOLD =
        (AIVM_VM_NODE_CAPACITY * AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR) /
        AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR,
    AIVM_VM_NODE_ATTR_GC_PRESSURE_THRESHOLD =
        (AIVM_VM_NODE_ATTR_CAPACITY * AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR) /
        AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR,
    AIVM_VM_NODE_CHILD_GC_PRESSURE_THRESHOLD =
        (AIVM_VM_NODE_CHILD_CAPACITY * AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_NUMERATOR) /
        AIVM_VM_NODE_GC_PRESSURE_THRESHOLD_DENOMINATOR
};

typedef struct {
    unsigned int storage_magic;
    const AivmProgram* program;
    AivmRuntimeProfile runtime_profile;
    size_t syscall_elapsed_limit_ms;
    size_t instruction_pointer;
    AivmVmStatus status;
    AivmVmError error;
    const char* error_detail;
    char error_detail_storage[4096];

    AivmValue* stack;
    size_t stack_count;
    size_t stack_limit;

    AivmCallFrame call_frames[AIVM_VM_CALLFRAME_CAPACITY];
    size_t call_frame_count;
    size_t call_frame_limit;
    AivmCallHistoryEntry recent_calls[4];
    size_t recent_call_count;
    AivmReturnHistoryEntry recent_returns[4];
    size_t recent_return_count;
    AivmOpcodeHistoryEntry recent_opcodes[24];
    size_t recent_opcode_count;
#if defined(AIVM_DEBUG_RUNTIME)
    size_t profile_instruction_count;
    size_t profile_opcode_counts[64];
    size_t profile_syscall_count;
    double profile_syscall_elapsed_seconds;
    AivmProfileSyscallTargetCount profile_syscall_targets[AIVM_VM_PROFILE_SYSCALL_TARGET_CAPACITY];
    size_t profile_syscall_target_count;
#endif

    AivmValue* locals;
    size_t locals_count;
    size_t locals_limit;
    char* string_arena;
    size_t string_arena_used;
    size_t string_arena_limit;
    uint8_t* bytes_arena;
    size_t bytes_arena_used;
    size_t bytes_arena_limit;
    const AivmSyscallBinding* syscall_bindings;
    size_t syscall_binding_count;
    const char* const* process_argv;
    size_t process_argv_count;
    int64_t process_argv_node_handle;
    AivmCompletedTask completed_tasks[AIVM_VM_TASK_CAPACITY];
    size_t completed_task_count;
    int64_t next_task_handle;
    size_t task_reclaim_count;
    size_t task_reclaim_skip_pinned_count;
    size_t task_reclaim_exhausted_count;
    AivmParContext par_contexts[AIVM_VM_PAR_CONTEXT_CAPACITY];
    size_t par_context_count;
    AivmValue par_values[AIVM_VM_PAR_VALUE_CAPACITY];
    size_t par_value_count;
    int64_t next_par_node_id;
    AivmNodeRecord* nodes;
    size_t node_count;
    AivmNodeAttr* node_attrs;
    size_t node_attr_count;
    int64_t* node_children;
    size_t node_child_count;
    int64_t ui_default_window_size_node_handle;
    int64_t ui_empty_event_node_handle;
    size_t string_arena_high_water;
    size_t bytes_arena_high_water;
    size_t node_high_water;
    size_t node_attr_high_water;
    size_t node_child_high_water;
    size_t node_gc_compaction_count;
    size_t node_gc_attempt_count;
    size_t node_gc_reclaimed_nodes;
    size_t node_gc_reclaimed_attrs;
    size_t node_gc_reclaimed_children;
    size_t node_allocations_since_gc;
    size_t string_arena_pressure_count;
    size_t bytes_arena_pressure_count;
    size_t node_arena_pressure_count;
} AivmVm;

void aivm_init(AivmVm* vm, const AivmProgram* program);
void aivm_init_with_syscalls(
    AivmVm* vm,
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count);
void aivm_init_with_syscalls_and_argv(
    AivmVm* vm,
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* const* process_argv,
    size_t process_argv_count);
void aivm_reset_state(AivmVm* vm);
void aivm_dispose(AivmVm* vm);
void aivm_halt(AivmVm* vm);
int aivm_stack_push(AivmVm* vm, AivmValue value);
int aivm_stack_pop(AivmVm* vm, AivmValue* out_value);
int aivm_frame_push(AivmVm* vm, size_t return_instruction_pointer, size_t frame_base);
int aivm_frame_pop(AivmVm* vm, AivmCallFrame* out_frame);
int aivm_local_set(AivmVm* vm, size_t index, AivmValue value);
int aivm_local_get(const AivmVm* vm, size_t index, AivmValue* out_value);
void aivm_step(AivmVm* vm);
void aivm_run(AivmVm* vm);
const char* aivm_vm_error_code(AivmVmError error);
const char* aivm_vm_error_message(AivmVmError error);
const char* aivm_vm_error_detail(const AivmVm* vm);
const char* aivm_runtime_profile_name(AivmRuntimeProfile profile);
int aivm_runtime_profile_from_name(const char* name, AivmRuntimeProfile* out_profile);
AivmRuntimeProfile aivm_runtime_default_profile(void);
AivmRuntimeProfileLimits aivm_runtime_profile_limits(AivmRuntimeProfile profile);
void aivm_set_runtime_profile(AivmVm* vm, AivmRuntimeProfile profile);

#endif
