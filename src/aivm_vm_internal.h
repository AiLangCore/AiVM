#ifndef AIVM_VM_INTERNAL_H
#define AIVM_VM_INTERNAL_H

#include "aivm_vm.h"

#define AIVM_VM_STORAGE_MAGIC 0xA117A11DU

void aivm_set_vm_error(AivmVm* vm, AivmVmError error, const char* detail);
int aivm_size_add_checked(size_t a, size_t b, size_t* out);
int aivm_vm_ensure_storage(AivmVm* vm);
void aivm_counter_increment_saturating(size_t* counter);
void aivm_release_all_blobs(AivmVm* vm);
int aivm_compact_string_arena(AivmVm* vm);
int aivm_compact_bytes_arena(AivmVm* vm);
char* aivm_string_arena_alloc(AivmVm* vm, size_t size);
uint8_t* aivm_bytes_arena_alloc(AivmVm* vm, size_t size);
const char* aivm_vm_snapshot_arena_backed_string(
    AivmVm* vm,
    const char* input,
    size_t length,
    char** out_temp_copy);
char* aivm_vm_copy_string_to_arena(AivmVm* vm, const char* input);
char* aivm_vm_copy_string_range_to_arena(AivmVm* vm, const char* input, size_t length);
char* aivm_vm_copy_string_splice_to_arena(
    AivmVm* vm,
    const char* prefix,
    size_t prefix_length,
    const char* suffix,
    size_t suffix_length);
int aivm_pointer_in_string_arena(const AivmVm* vm, const char* text);
int aivm_vm_mark_live_scratch_pair_handles(AivmVm* vm, uint8_t* live_pairs);
int aivm_vm_compact_scratch_pairs(AivmVm* vm);
int aivm_vm_lookup_node(const AivmVm* vm, int64_t handle, const AivmNodeRecord** out_node);
int aivm_vm_lookup_scratch_pair(const AivmVm* vm, int64_t handle, const AivmScratchPair** out_pair);
int aivm_vm_create_scratch_pair(AivmVm* vm, AivmValue first, AivmValue second, int64_t* out_handle);
int aivm_vm_create_node_record(
    AivmVm* vm,
    const char* kind,
    const char* id,
    const AivmNodeAttr* attrs,
    size_t attr_count,
    const int64_t* children,
    size_t child_count,
    int64_t* out_handle);
int aivm_vm_mark_live_node_handles(
    AivmVm* vm,
    uint8_t* live,
    const int64_t* extra_handles,
    size_t extra_handle_count);
int aivm_vm_compact_node_arenas_with_map(
    AivmVm* vm,
    const int64_t* extra_handles,
    size_t extra_handle_count,
    int64_t* out_handle_map);
int aivm_vm_remap_child_handles_for_compaction(
    AivmVm* vm,
    int64_t* remapped_children,
    const int64_t* children,
    size_t child_count,
    const int64_t* handle_map);
int aivm_vm_should_attempt_proactive_node_gc(
    const AivmVm* vm,
    size_t incoming_attr_count,
    size_t incoming_child_count);
int aivm_vm_should_attempt_return_safe_point(const AivmVm* vm);
int aivm_vm_should_attempt_bytes_return_safe_point(const AivmVm* vm);

const char* aivm_vm_value_type_name(AivmValueType type);
int aivm_vm_value_is_numeric(AivmValue value);
double aivm_vm_value_as_number(AivmValue value);
AivmValue aivm_vm_numeric_result(double value);
double aivm_vm_double_trunc_toward_zero(double value);
double aivm_vm_double_pow_whole(double base, double exponent);
void aivm_vm_set_add_numeric_type_error(AivmVm* vm, AivmValue left, AivmValue right);

int aivm_bytes_from_base64(const char* input, uint8_t* out_bytes, size_t out_capacity, size_t* out_length);
int aivm_bytes_to_base64(const uint8_t* input, size_t input_len, char* out_text, size_t out_capacity);
int aivm_bytes_is_valid_utf8_without_nul(const uint8_t* data, size_t len);
int aivm_hex4_to_u32(const char* text, uint32_t* out);

void aivm_vm_set_local_out_of_range_error(
    AivmVm* vm,
    const char* op_name,
    size_t local_index,
    size_t locals_base);
int aivm_vm_validate_call_local_state(AivmVm* vm, const char* op_name);
int aivm_vm_validate_frame_record(AivmVm* vm, const AivmCallFrame* frame, const char* op_name);
void aivm_vm_record_recent_call(
    AivmVm* vm,
    size_t instruction_pointer,
    size_t target,
    size_t arg_count,
    size_t stack_count);
void aivm_vm_record_recent_return(
    AivmVm* vm,
    size_t instruction_pointer,
    size_t stack_count,
    size_t pre_restore_stack_count,
    size_t frame_base,
    int has_return_value);
void aivm_vm_record_recent_opcode(
    AivmVm* vm,
    size_t instruction_pointer,
    int opcode,
    size_t stack_count);
void aivm_vm_cleanup_bytecode_worker_tasks(AivmVm* vm);
int aivm_vm_initialize_process_argv_node(AivmVm* vm);

#endif
