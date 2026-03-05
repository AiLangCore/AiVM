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

typedef struct {
    size_t return_instruction_pointer;
    size_t frame_base;
    size_t locals_base;
} AivmCallFrame;

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

typedef struct {
    int64_t handle;
    AivmValue result;
} AivmCompletedTask;

typedef struct {
    size_t expected_count;
    size_t start_index;
} AivmParContext;

enum {
    AIVM_VM_STACK_CAPACITY = 1024,
    AIVM_VM_CALLFRAME_CAPACITY = 256,
    AIVM_VM_LOCALS_CAPACITY = 1024,
    AIVM_VM_STRING_ARENA_CAPACITY = 8192,
    AIVM_VM_BYTES_ARENA_CAPACITY = 32768,
    AIVM_VM_MAX_SYSCALL_ARGS = 16,
    AIVM_VM_NODE_CAPACITY = 256,
    AIVM_VM_NODE_ATTR_CAPACITY = 1024,
    AIVM_VM_NODE_CHILD_CAPACITY = 2048,
    AIVM_VM_TASK_CAPACITY = 256,
    AIVM_VM_PAR_CONTEXT_CAPACITY = 64,
    AIVM_VM_PAR_VALUE_CAPACITY = 1024
};

typedef struct {
    const AivmProgram* program;
    size_t instruction_pointer;
    AivmVmStatus status;
    AivmVmError error;
    const char* error_detail;

    AivmValue stack[AIVM_VM_STACK_CAPACITY];
    size_t stack_count;

    AivmCallFrame call_frames[AIVM_VM_CALLFRAME_CAPACITY];
    size_t call_frame_count;

    AivmValue locals[AIVM_VM_LOCALS_CAPACITY];
    size_t locals_count;
    char string_arena[AIVM_VM_STRING_ARENA_CAPACITY];
    size_t string_arena_used;
    uint8_t bytes_arena[AIVM_VM_BYTES_ARENA_CAPACITY];
    size_t bytes_arena_used;
    const AivmSyscallBinding* syscall_bindings;
    size_t syscall_binding_count;
    const char* const* process_argv;
    size_t process_argv_count;
    int64_t process_argv_node_handle;
    AivmCompletedTask completed_tasks[AIVM_VM_TASK_CAPACITY];
    size_t completed_task_count;
    int64_t next_task_handle;
    AivmParContext par_contexts[AIVM_VM_PAR_CONTEXT_CAPACITY];
    size_t par_context_count;
    AivmValue par_values[AIVM_VM_PAR_VALUE_CAPACITY];
    size_t par_value_count;
    int64_t next_par_node_id;
    AivmNodeRecord nodes[AIVM_VM_NODE_CAPACITY];
    size_t node_count;
    AivmNodeAttr node_attrs[AIVM_VM_NODE_ATTR_CAPACITY];
    size_t node_attr_count;
    int64_t node_children[AIVM_VM_NODE_CHILD_CAPACITY];
    size_t node_child_count;
    size_t string_arena_high_water;
    size_t bytes_arena_high_water;
    size_t node_high_water;
    size_t node_attr_high_water;
    size_t node_child_high_water;
    size_t node_gc_compaction_count;
    size_t node_gc_reclaimed_nodes;
    size_t node_gc_reclaimed_attrs;
    size_t node_gc_reclaimed_children;
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

#endif
