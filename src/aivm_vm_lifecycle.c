#include "aivm_vm_internal.h"

#include <stdlib.h>
#include <string.h>

static void prepare_vm_for_init(AivmVm* vm)
{
    if (vm == NULL) {
        return;
    }
    if (vm->storage_magic != AIVM_VM_STORAGE_MAGIC) {
        memset(vm, 0, sizeof(*vm));
        vm->storage_magic = AIVM_VM_STORAGE_MAGIC;
    }
}

void aivm_reset_state(AivmVm* vm)
{
    if (vm == NULL) {
        return;
    }
    if (!aivm_vm_ensure_storage(vm)) {
        return;
    }
    aivm_vm_cleanup_bytecode_worker_tasks(vm);
    aivm_vm_cleanup_worker_runtime(vm);
    aivm_vm_cleanup_worker_task_groups(vm);
    aivm_release_all_blobs(vm);
    aivm_vm_reset_node_builders(vm);
    aivm_vm_reset_maps(vm);

    vm->instruction_pointer = 0U;
    vm->status = AIVM_VM_STATUS_READY;
    vm->error = AIVM_VM_ERR_NONE;
    vm->error_detail = NULL;
    vm->stack_count = 0U;
    vm->stack_limit = AIVM_VM_STACK_INITIAL_CAPACITY;
    vm->call_frame_count = 0U;
    vm->call_frame_limit = AIVM_VM_CALLFRAME_INITIAL_CAPACITY;
    vm->recent_call_count = 0U;
    vm->recent_return_count = 0U;
    vm->recent_opcode_count = 0U;
#if defined(AIVM_DEBUG_RUNTIME)
    vm->profile_instruction_count = 0U;
    memset(vm->profile_opcode_counts, 0, sizeof(vm->profile_opcode_counts));
    vm->profile_syscall_count = 0U;
    vm->profile_syscall_elapsed_seconds = 0.0;
    memset(vm->profile_syscall_targets, 0, sizeof(vm->profile_syscall_targets));
    vm->profile_syscall_target_count = 0U;
#endif
    vm->locals_count = 0U;
    vm->locals_limit = AIVM_VM_LOCALS_INITIAL_CAPACITY;
    vm->string_arena_used = 0U;
    vm->string_arena_limit = vm->string_arena_capacity < AIVM_VM_STRING_ARENA_INITIAL_CAPACITY
        ? vm->string_arena_capacity
        : AIVM_VM_STRING_ARENA_INITIAL_CAPACITY;
    vm->string_arena[0] = '\0';
    aivm_vm_reset_string_intern_index(vm);
    vm->utf8_offset_cache_text = NULL;
    vm->utf8_offset_cache_rune = 0U;
    vm->utf8_offset_cache_byte = 0U;
    vm->bytes_arena_used = 0U;
    vm->bytes_arena_limit = vm->bytes_arena_storage_capacity < AIVM_VM_BYTES_ARENA_INITIAL_CAPACITY
        ? vm->bytes_arena_storage_capacity
        : AIVM_VM_BYTES_ARENA_INITIAL_CAPACITY;
    vm->bytes_arena_gc_threshold = vm->bytes_arena_limit;
    vm->bytes_arena[0] = 0U;
    vm->completed_task_count = 0U;
    vm->next_task_handle = 1;
    vm->worker_task_group_count = 0U;
    vm->next_worker_task_group_handle = 1;
    vm->task_reclaim_count = 0U;
    vm->task_reclaim_skip_pinned_count = 0U;
    vm->task_reclaim_exhausted_count = 0U;
    vm->par_context_count = 0U;
    vm->par_value_count = 0U;
    vm->scratch_pair_count = 0U;
    vm->blob_count = 0U;
    vm->blob_bytes_used = 0U;
    vm->blob_bytes_high_water = 0U;
    vm->blob_pressure_count = 0U;
    vm->next_blob_handle = 1;
    vm->next_par_node_id = 1;
    vm->node_count = 0U;
    vm->node_attr_count = 0U;
    vm->node_child_count = 0U;
    vm->string_arena_high_water = 0U;
    vm->bytes_arena_high_water = 0U;
    vm->node_high_water = 0U;
    vm->node_attr_high_water = 0U;
    vm->node_child_high_water = 0U;
    vm->node_gc_compaction_count = 0U;
    vm->node_gc_attempt_count = 0U;
    vm->node_gc_reclaimed_nodes = 0U;
    vm->node_gc_reclaimed_attrs = 0U;
    vm->node_gc_reclaimed_children = 0U;
    vm->node_allocations_since_gc = 0U;
    vm->string_arena_pressure_count = 0U;
    vm->bytes_arena_pressure_count = 0U;
    vm->node_arena_pressure_count = 0U;
    vm->host_memory_growth_suspended = 0;
    vm->network_read_bytes_used = 0U;
    vm->network_write_bytes_used = 0U;
    vm->process_argv_node_handle = 0;
    vm->ui_default_window_size_node_handle = 0;
    vm->ui_empty_event_node_handle = 0;
    (void)aivm_vm_initialize_process_argv_node(vm);
    vm->node_allocations_since_gc = 0U;
}

void aivm_dispose(AivmVm* vm)
{
    if (vm == NULL) {
        return;
    }
    aivm_vm_cleanup_bytecode_worker_tasks(vm);
    aivm_vm_cleanup_worker_runtime(vm);
    aivm_vm_cleanup_worker_task_groups(vm);
    aivm_release_all_blobs(vm);
    aivm_vm_reset_node_builders(vm);
    aivm_vm_reset_maps(vm);
    free(vm->stack);
    free(vm->locals);
    free(vm->string_arena);
    free(vm->string_intern_entries);
    free(vm->bytes_arena);
    free(vm->nodes);
    free(vm->node_attrs);
    free(vm->node_children);
    free(vm->scratch_pairs);
    vm->stack = NULL;
    vm->locals = NULL;
    vm->string_arena = NULL;
    vm->string_arena_capacity = 0U;
    vm->string_arena_storage_capacity = 0U;
    vm->string_intern_entries = NULL;
    vm->string_intern_capacity = 0U;
    vm->string_intern_count = 0U;
    vm->string_intern_complete = 0;
    vm->bytes_arena = NULL;
    vm->bytes_arena_capacity = 0U;
    vm->bytes_arena_storage_capacity = 0U;
    vm->nodes = NULL;
    vm->node_attrs = NULL;
    vm->node_children = NULL;
    vm->scratch_pairs = NULL;
    vm->stack_count = 0U;
    vm->locals_count = 0U;
    vm->node_count = 0U;
    vm->node_attr_count = 0U;
    vm->node_child_count = 0U;
    vm->scratch_pair_count = 0U;
    vm->blob_count = 0U;
    vm->blob_bytes_used = 0U;
}

void aivm_init(AivmVm* vm, const AivmProgram* program)
{
    aivm_init_with_profile(vm, program, aivm_runtime_default_profile());
}

void aivm_init_with_profile(AivmVm* vm, const AivmProgram* program, AivmRuntimeProfile profile)
{
    if (vm == NULL) {
        return;
    }
    prepare_vm_for_init(vm);

    vm->program = program;
    aivm_set_runtime_profile(vm, profile);
    vm->syscall_bindings = NULL;
    vm->syscall_binding_count = 0U;
    vm->process_argv = NULL;
    vm->process_argv_count = 0U;
    aivm_reset_state(vm);
}

void aivm_init_with_syscalls(
    AivmVm* vm,
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count)
{
    aivm_init_with_syscalls_and_argv(vm, program, bindings, binding_count, NULL, 0U);
}

void aivm_init_with_syscalls_and_argv(
    AivmVm* vm,
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* const* process_argv,
    size_t process_argv_count)
{
    aivm_init_with_syscalls_and_argv_profile(
        vm,
        program,
        bindings,
        binding_count,
        process_argv,
        process_argv_count,
        aivm_runtime_default_profile());
}

void aivm_init_with_syscalls_and_argv_profile(
    AivmVm* vm,
    const AivmProgram* program,
    const AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* const* process_argv,
    size_t process_argv_count,
    AivmRuntimeProfile profile)
{
    if (vm == NULL) {
        return;
    }
    prepare_vm_for_init(vm);

    vm->program = program;
    aivm_set_runtime_profile(vm, profile);
    vm->syscall_bindings = bindings;
    vm->syscall_binding_count = binding_count;
    vm->process_argv = process_argv;
    vm->process_argv_count = process_argv_count;
    aivm_reset_state(vm);
}
