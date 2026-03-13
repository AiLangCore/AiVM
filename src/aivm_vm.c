#include "aivm_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys/aivm_syscall_contracts.h"

static void set_vm_error(AivmVm* vm, AivmVmError error, const char* detail)
{
    if (vm == NULL) {
        return;
    }
    vm->error = error;
    vm->status = AIVM_VM_STATUS_ERROR;
    vm->error_detail = detail;
}

static int size_add_checked(size_t a, size_t b, size_t* out)
{
    if (out == NULL) {
        return 0;
    }
    if (a > ((size_t)-1 - b)) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int size_sub_checked(size_t a, size_t b, size_t* out)
{
    if (out == NULL || a < b) {
        return 0;
    }
    *out = a - b;
    return 1;
}

static void set_vm_local_out_of_range_error(
    AivmVm* vm,
    const char* op_name,
    size_t local_index,
    size_t locals_base)
{
    char detail[256];
    if (vm == NULL) {
        return;
    }
    (void)snprintf(
        detail,
        sizeof(detail),
        "Invalid local slot. op=%s index=%llu base=%llu localsCount=%llu frameCount=%llu pc=%llu",
        (op_name == NULL || op_name[0] == '\0') ? "local" : op_name,
        (unsigned long long)local_index,
        (unsigned long long)locals_base,
        (unsigned long long)vm->locals_count,
        (unsigned long long)vm->call_frame_count,
        (unsigned long long)vm->instruction_pointer);
    set_vm_error(vm, AIVM_VM_ERR_LOCAL_OUT_OF_RANGE, detail);
}

static int validate_vm_call_local_state(AivmVm* vm, const char* op_name)
{
    size_t active_locals_base = 0U;
    if (vm == NULL) {
        return 0;
    }
    if (vm->stack_count > vm->stack_limit ||
        vm->call_frame_count > vm->call_frame_limit ||
        vm->locals_count > vm->locals_limit) {
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "VM state invariant failed. op=%s stackCount=%llu stackLimit=%llu frameCount=%llu frameLimit=%llu localsCount=%llu localsLimit=%llu",
            (op_name == NULL || op_name[0] == '\0') ? "state" : op_name,
            (unsigned long long)vm->stack_count,
            (unsigned long long)vm->stack_limit,
            (unsigned long long)vm->call_frame_count,
            (unsigned long long)vm->call_frame_limit,
            (unsigned long long)vm->locals_count,
            (unsigned long long)vm->locals_limit);
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    if (vm->call_frame_count > 0U) {
        const AivmCallFrame* frame = &vm->call_frames[vm->call_frame_count - 1U];
        if (frame->frame_base > vm->stack_count || frame->locals_base > vm->locals_count) {
            (void)snprintf(
                vm->error_detail_storage,
                sizeof(vm->error_detail_storage),
                "VM frame invariant failed. op=%s frameBase=%llu localsBase=%llu stackCount=%llu localsCount=%llu frameCount=%llu",
                (op_name == NULL || op_name[0] == '\0') ? "state" : op_name,
                (unsigned long long)frame->frame_base,
                (unsigned long long)frame->locals_base,
                (unsigned long long)vm->stack_count,
                (unsigned long long)vm->locals_count,
                (unsigned long long)vm->call_frame_count);
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
            return 0;
        }
        active_locals_base = frame->locals_base;
    }
    if (active_locals_base > vm->locals_count) {
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "VM locals invariant failed. op=%s activeBase=%llu localsCount=%llu frameCount=%llu",
            (op_name == NULL || op_name[0] == '\0') ? "state" : op_name,
            (unsigned long long)active_locals_base,
            (unsigned long long)vm->locals_count,
            (unsigned long long)vm->call_frame_count);
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    return 1;
}

static int validate_vm_frame_record(
    AivmVm* vm,
    const AivmCallFrame* frame,
    const char* op_name)
{
    if (vm == NULL || frame == NULL) {
        return 0;
    }
    if (frame->frame_base > vm->stack_count || frame->locals_base > vm->locals_count) {
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "VM frame record invalid. op=%s frameBase=%llu localsBase=%llu stackCount=%llu localsCount=%llu frameCount=%llu",
            (op_name == NULL || op_name[0] == '\0') ? "frame" : op_name,
            (unsigned long long)frame->frame_base,
            (unsigned long long)frame->locals_base,
            (unsigned long long)vm->stack_count,
            (unsigned long long)vm->locals_count,
            (unsigned long long)vm->call_frame_count);
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    return 1;
}

static int validate_vm_return_restore(
    AivmVm* vm,
    const AivmCallFrame* frame,
    size_t pre_restore_stack_count)
{
    size_t max_stack_count = 0U;
    size_t extra_stack_values = 0U;
    if (vm == NULL || frame == NULL) {
        return 0;
    }
    if (!size_add_checked(frame->frame_base, 1U, &max_stack_count)) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Return restore size arithmetic overflow.");
        return 0;
    }
    if (pre_restore_stack_count > max_stack_count) {
        if (!size_sub_checked(pre_restore_stack_count, max_stack_count, &extra_stack_values)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Return restore size arithmetic overflow.");
            return 0;
        }
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "Return restore invalid. extraStackValues=%llu frameBase=%llu stackCount=%llu localsBase=%llu frameCount=%llu pc=%llu",
            (unsigned long long)extra_stack_values,
            (unsigned long long)frame->frame_base,
            (unsigned long long)pre_restore_stack_count,
            (unsigned long long)frame->locals_base,
            (unsigned long long)vm->call_frame_count,
            (unsigned long long)vm->instruction_pointer);
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    if (!validate_vm_call_local_state(vm, "return-restore")) {
        return 0;
    }
    return 1;
}

static size_t infer_call_arg_count(const AivmProgram* program, size_t target);
static int validate_call_target_layout(
    AivmVm* vm,
    const AivmProgram* program,
    size_t target,
    size_t arg_count);

static const char* vm_value_type_name(AivmValueType type)
{
    switch (type) {
        case AIVM_VAL_VOID: return "void";
        case AIVM_VAL_INT: return "int";
        case AIVM_VAL_BOOL: return "bool";
        case AIVM_VAL_NULL: return "null";
        case AIVM_VAL_STRING: return "string";
        case AIVM_VAL_BYTES: return "bytes";
        case AIVM_VAL_NODE: return "node";
        default: return "unknown";
    }
}

static void set_vm_error_add_int_type_mismatch(AivmVm* vm, AivmValue left, AivmValue right)
{
    const char* left_text = "";
    const char* right_text = "";
    unsigned long long ret0 = 0ULL;
    unsigned long long ret1 = 0ULL;
    if (vm == NULL) {
        return;
    }
    if (vm->call_frame_count > 0U) {
        ret0 = (unsigned long long)vm->call_frames[vm->call_frame_count - 1U].return_instruction_pointer;
        if (vm->call_frame_count > 1U) {
            ret1 = (unsigned long long)vm->call_frames[vm->call_frame_count - 2U].return_instruction_pointer;
        }
    }
    if (left.type == AIVM_VAL_STRING && left.string_value != NULL) {
        left_text = left.string_value;
    }
    if (right.type == AIVM_VAL_STRING && right.string_value != NULL) {
        right_text = right.string_value;
    }
    (void)snprintf(
        vm->error_detail_storage,
        sizeof(vm->error_detail_storage),
        "ADD_INT requires int operands. left=%s(\"%.40s\") right=%s(\"%.40s\") ip=%llu ret0=%llu ret1=%llu",
        vm_value_type_name(left.type),
        left_text,
        vm_value_type_name(right.type),
        right_text,
        (unsigned long long)vm->instruction_pointer,
        ret0,
        ret1);
    set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, vm->error_detail_storage);
}

static void set_vm_error_call_arg_depth(
    AivmVm* vm,
    size_t target,
    size_t arg_count,
    size_t stack_count)
{
    size_t history_index;
    size_t return_index;
    size_t used;
    if (vm == NULL) {
        return;
    }
    (void)snprintf(
        vm->error_detail_storage,
        sizeof(vm->error_detail_storage),
        "Call argument count exceeds stack depth. target=%llu argCount=%llu stackCount=%llu frameCount=%llu pc=%llu",
        (unsigned long long)target,
        (unsigned long long)arg_count,
        (unsigned long long)stack_count,
        (unsigned long long)vm->call_frame_count,
        (unsigned long long)vm->instruction_pointer);
    used = strlen(vm->error_detail_storage);
    for (history_index = 0U; history_index < vm->recent_call_count; history_index += 1U) {
        const AivmCallHistoryEntry* entry = &vm->recent_calls[history_index];
        used += (size_t)snprintf(
            vm->error_detail_storage + used,
            sizeof(vm->error_detail_storage) > used ? sizeof(vm->error_detail_storage) - used : 0U,
            " call%llu={ip=%llu,target=%llu,argCount=%llu,stackBefore=%llu}",
            (unsigned long long)history_index,
            (unsigned long long)entry->instruction_pointer,
            (unsigned long long)entry->target,
            (unsigned long long)entry->arg_count,
            (unsigned long long)entry->stack_count);
        if (used >= sizeof(vm->error_detail_storage)) {
            used = sizeof(vm->error_detail_storage) - 1U;
            break;
        }
    }
    for (return_index = 0U; return_index < vm->recent_return_count; return_index += 1U) {
        const AivmReturnHistoryEntry* entry = &vm->recent_returns[return_index];
        used += (size_t)snprintf(
            vm->error_detail_storage + used,
            sizeof(vm->error_detail_storage) > used ? sizeof(vm->error_detail_storage) - used : 0U,
            " return%llu={ip=%llu,stackAfter=%llu,preRestore=%llu,frameBase=%llu,hasReturn=%d}",
            (unsigned long long)return_index,
            (unsigned long long)entry->instruction_pointer,
            (unsigned long long)entry->stack_count,
            (unsigned long long)entry->pre_restore_stack_count,
            (unsigned long long)entry->frame_base,
            entry->has_return_value);
        if (used >= sizeof(vm->error_detail_storage)) {
            used = sizeof(vm->error_detail_storage) - 1U;
            break;
        }
    }
    for (history_index = 0U; history_index < vm->recent_opcode_count; history_index += 1U) {
        const AivmOpcodeHistoryEntry* entry = &vm->recent_opcodes[history_index];
        used += (size_t)snprintf(
            vm->error_detail_storage + used,
            sizeof(vm->error_detail_storage) > used ? sizeof(vm->error_detail_storage) - used : 0U,
            " op%llu={ip=%llu,opcode=%d,stack=%llu}",
            (unsigned long long)history_index,
            (unsigned long long)entry->instruction_pointer,
            entry->opcode,
            (unsigned long long)entry->stack_count);
        if (used >= sizeof(vm->error_detail_storage)) {
            used = sizeof(vm->error_detail_storage) - 1U;
            break;
        }
    }
    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
}

static void record_recent_call(
    AivmVm* vm,
    size_t instruction_pointer,
    size_t target,
    size_t arg_count,
    size_t stack_count)
{
    size_t i;
    if (vm == NULL) {
        return;
    }
    for (i = sizeof(vm->recent_calls) / sizeof(vm->recent_calls[0]); i > 1U; i -= 1U) {
        vm->recent_calls[i - 1U] = vm->recent_calls[i - 2U];
    }
    vm->recent_calls[0].instruction_pointer = instruction_pointer;
    vm->recent_calls[0].target = target;
    vm->recent_calls[0].arg_count = arg_count;
    vm->recent_calls[0].stack_count = stack_count;
    if (vm->recent_call_count < (sizeof(vm->recent_calls) / sizeof(vm->recent_calls[0]))) {
        vm->recent_call_count += 1U;
    }
}

static void record_recent_return(
    AivmVm* vm,
    size_t instruction_pointer,
    size_t stack_count,
    size_t pre_restore_stack_count,
    size_t frame_base,
    int has_return_value)
{
    size_t i;
    if (vm == NULL) {
        return;
    }
    for (i = sizeof(vm->recent_returns) / sizeof(vm->recent_returns[0]); i > 1U; i -= 1U) {
        vm->recent_returns[i - 1U] = vm->recent_returns[i - 2U];
    }
    vm->recent_returns[0].instruction_pointer = instruction_pointer;
    vm->recent_returns[0].stack_count = stack_count;
    vm->recent_returns[0].pre_restore_stack_count = pre_restore_stack_count;
    vm->recent_returns[0].frame_base = frame_base;
    vm->recent_returns[0].has_return_value = has_return_value;
    if (vm->recent_return_count < (sizeof(vm->recent_returns) / sizeof(vm->recent_returns[0]))) {
        vm->recent_return_count += 1U;
    }
}

static void record_recent_opcode(
    AivmVm* vm,
    size_t instruction_pointer,
    int opcode,
    size_t stack_count)
{
    size_t i;
    if (vm == NULL) {
        return;
    }
    for (i = sizeof(vm->recent_opcodes) / sizeof(vm->recent_opcodes[0]); i > 1U; i -= 1U) {
        vm->recent_opcodes[i - 1U] = vm->recent_opcodes[i - 2U];
    }
    vm->recent_opcodes[0].instruction_pointer = instruction_pointer;
    vm->recent_opcodes[0].opcode = opcode;
    vm->recent_opcodes[0].stack_count = stack_count;
    if (vm->recent_opcode_count < (sizeof(vm->recent_opcodes) / sizeof(vm->recent_opcodes[0]))) {
        vm->recent_opcode_count += 1U;
    }
}


static const char* syscall_failure_detail(AivmSyscallStatus status, AivmContractStatus contract_status);
static const char* syscall_contract_failure_detail_with_args(
    AivmVm* vm,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmContractStatus contract_status);
static const char* syscall_not_found_detail_with_recovery(
    AivmVm* vm,
    AivmValue raw_target_value,
    const char* recovered_target,
    const AivmValue* args,
    size_t arg_count);
static const char* syscall_contract_failure_detail(AivmContractStatus status);
static int lookup_node(const AivmVm* vm, int64_t handle, const AivmNodeRecord** out_node);
static int call_debug_task_reclaim_stats(AivmVm* vm, AivmValue* out_result);
static size_t write_u64_decimal(char* output, size_t capacity, uint64_t value);
static int is_syscall_target_string(const char* text);
static const char* find_syscall_suffix_target(const char* text);
static int mark_live_node_handles(
    AivmVm* vm,
    uint8_t* live,
    const int64_t* extra_handles,
    size_t extra_handle_count);
static int compact_string_arena(AivmVm* vm);
static int create_node_record(
    AivmVm* vm,
    const char* kind,
    const char* id,
    const AivmNodeAttr* attrs,
    size_t attr_count,
    const int64_t* children,
    size_t child_count,
    int64_t* out_handle);

static void increment_counter_saturating(size_t* counter)
{
    size_t next_value;
    if (counter == NULL) {
        return;
    }
    if (size_add_checked(*counter, 1U, &next_value)) {
        *counter = next_value;
    } else {
        *counter = (size_t)-1;
    }
}

static void add_counter_saturating(size_t* counter, size_t delta)
{
    if (counter == NULL) {
        return;
    }
    if (delta > ((size_t)-1 - *counter)) {
        *counter = (size_t)-1;
        return;
    }
    *counter += delta;
}

static size_t grow_limit(size_t current, size_t step, size_t max_value)
{
    size_t next;
    if (current >= max_value) {
        return max_value;
    }
    if (!size_add_checked(current, step, &next) || next > max_value) {
        return max_value;
    }
    return next;
}

static int ensure_stack_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->stack_limit && vm->stack_limit < AIVM_VM_STACK_CAPACITY) {
        vm->stack_limit = grow_limit(vm->stack_limit, AIVM_VM_STACK_GROWTH_STEP, AIVM_VM_STACK_CAPACITY);
    }
    return needed <= vm->stack_limit;
}

static int ensure_call_frame_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->call_frame_limit && vm->call_frame_limit < AIVM_VM_CALLFRAME_CAPACITY) {
        vm->call_frame_limit = grow_limit(vm->call_frame_limit, AIVM_VM_CALLFRAME_GROWTH_STEP, AIVM_VM_CALLFRAME_CAPACITY);
    }
    return needed <= vm->call_frame_limit;
}

static int ensure_locals_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->locals_limit && vm->locals_limit < AIVM_VM_LOCALS_CAPACITY) {
        vm->locals_limit = grow_limit(vm->locals_limit, AIVM_VM_LOCALS_GROWTH_STEP, AIVM_VM_LOCALS_CAPACITY);
    }
    return needed <= vm->locals_limit;
}

static int ensure_string_arena_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->string_arena_limit && vm->string_arena_limit < AIVM_VM_STRING_ARENA_CAPACITY) {
        vm->string_arena_limit = grow_limit(vm->string_arena_limit, AIVM_VM_STRING_ARENA_GROWTH_STEP, AIVM_VM_STRING_ARENA_CAPACITY);
    }
    return needed <= vm->string_arena_limit;
}

static int ensure_bytes_arena_capacity(AivmVm* vm, size_t needed)
{
    if (vm == NULL) {
        return 0;
    }
    while (needed > vm->bytes_arena_limit && vm->bytes_arena_limit < AIVM_VM_BYTES_ARENA_CAPACITY) {
        vm->bytes_arena_limit = grow_limit(vm->bytes_arena_limit, AIVM_VM_BYTES_ARENA_GROWTH_STEP, AIVM_VM_BYTES_ARENA_CAPACITY);
    }
    return needed <= vm->bytes_arena_limit;
}

static int pointer_in_string_arena(const AivmVm* vm, const char* text)
{
    if (vm == NULL || text == NULL || vm->string_arena_used == 0U) {
        return 0;
    }
    return text >= vm->string_arena &&
           text < (vm->string_arena + vm->string_arena_used);
}

static char* compact_lookup_or_copy_string(
    const char* text,
    char* new_arena,
    size_t* new_used)
{
    size_t offset = 0U;
    size_t length;
    char* output;
    size_t next_offset;
    if (text == NULL || new_arena == NULL || new_used == NULL) {
        return NULL;
    }
    while (offset < *new_used) {
        char* candidate = &new_arena[offset];
        size_t candidate_length = strlen(candidate);
        if (strcmp(candidate, text) == 0) {
            return candidate;
        }
        if (!size_add_checked(offset, candidate_length, &offset)) {
            return NULL;
        }
        if (offset < *new_used) {
            if (!size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
    }
    length = strlen(text);
    if (!size_add_checked(length, 1U, &length) ||
        !size_add_checked(*new_used, length, &offset) ||
        offset > AIVM_VM_STRING_ARENA_CAPACITY) {
        return NULL;
    }
    output = &new_arena[*new_used];
    memcpy(output, text, length);
    *new_used = offset;
    return output;
}

static int compact_relocate_string_ptr(
    AivmVm* vm,
    const char** slot,
    char* new_arena,
    size_t* new_used)
{
    char* relocated;
    if (vm == NULL || slot == NULL || *slot == NULL) {
        return 1;
    }
    if (!pointer_in_string_arena(vm, *slot)) {
        return 1;
    }
    relocated = compact_lookup_or_copy_string(*slot, new_arena, new_used);
    if (relocated == NULL) {
        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
        return 0;
    }
    *slot = relocated;
    return 1;
}

static int compact_relocate_value_string(
    AivmVm* vm,
    AivmValue* value,
    char* new_arena,
    size_t* new_used)
{
    if (vm == NULL || value == NULL) {
        return 0;
    }
    if (value->type != AIVM_VAL_STRING || value->string_value == NULL) {
        return 1;
    }
    return compact_relocate_string_ptr(vm, &value->string_value, new_arena, new_used);
}

static int operand_to_index(AivmVm* vm, int64_t operand, size_t* out_index)
{
    if (vm == NULL || out_index == NULL) {
        return 0;
    }

    if (operand < 0) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Negative operand is invalid.");
        return 0;
    }

    *out_index = (size_t)operand;
    return 1;
}

static int is_syscall_target_string(const char* text)
{
    return text != NULL &&
        text[0] == 's' &&
        text[1] == 'y' &&
        text[2] == 's' &&
        text[3] == '.';
}

static const char* find_syscall_suffix_target(const char* text)
{
    size_t i;
    size_t len;
    size_t next_len;
    if (text == NULL) {
        return NULL;
    }
    len = 0U;
    while (text[len] != '\0') {
        if (!size_add_checked(len, 1U, &next_len)) {
            return NULL;
        }
        len = next_len;
    }
    if (len < 4U) {
        return NULL;
    }
    for (i = len - 4U; ; ) {
        if (text[i] == 's' && text[i + 1U] == 'y' && text[i + 2U] == 's' && text[i + 3U] == '.') {
            size_t j = i + 4U;
            size_t next_j;
            if (j < len) {
                while (j < len) {
                    char ch = text[j];
                    if (!((ch >= 'a' && ch <= 'z') ||
                          (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') ||
                          ch == '.' || ch == '_')) {
                        break;
                    }
                    if (!size_add_checked(j, 1U, &next_j)) {
                        return NULL;
                    }
                    j = next_j;
                }
                if (j == len) {
                    return &text[i];
                }
            }
        }
        if (i == 0U) {
            break;
        }
        i -= 1U;
    }
    return NULL;
}

static char* arena_alloc(AivmVm* vm, size_t size)
{
    char* start;
    size_t needed = 0U;
    if (vm == NULL) {
        return NULL;
    }
    if (!size_add_checked(vm->string_arena_used, size, &needed)) {
        increment_counter_saturating(&vm->string_arena_pressure_count);
        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
        return NULL;
    }
    if (needed > vm->string_arena_limit) {
        if (!compact_string_arena(vm)) {
            increment_counter_saturating(&vm->string_arena_pressure_count);
            if (vm->status != AIVM_VM_STATUS_ERROR) {
                set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
            }
            return NULL;
        }
        if (!size_add_checked(vm->string_arena_used, size, &needed)) {
            increment_counter_saturating(&vm->string_arena_pressure_count);
            set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
            return NULL;
        }
    }
    if (needed > vm->string_arena_limit &&
        !ensure_string_arena_capacity(vm, needed)) {
        increment_counter_saturating(&vm->string_arena_pressure_count);
        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
        return NULL;
    }

    start = &vm->string_arena[vm->string_arena_used];
    vm->string_arena_used = needed;
    if (vm->string_arena_used > vm->string_arena_high_water) {
        vm->string_arena_high_water = vm->string_arena_used;
    }
    return start;
}

static uint8_t* bytes_arena_alloc(AivmVm* vm, size_t size)
{
    uint8_t* start;
    size_t needed = 0U;
    if (vm == NULL) {
        return NULL;
    }
    if (!size_add_checked(vm->bytes_arena_used, size, &needed)) {
        increment_counter_saturating(&vm->bytes_arena_pressure_count);
        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM002: bytes arena capacity exceeded.");
        return NULL;
    }
    if (needed > vm->bytes_arena_limit &&
        !ensure_bytes_arena_capacity(vm, needed)) {
        increment_counter_saturating(&vm->bytes_arena_pressure_count);
        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM002: bytes arena capacity exceeded.");
        return NULL;
    }
    start = &vm->bytes_arena[vm->bytes_arena_used];
    vm->bytes_arena_used = needed;
    if (vm->bytes_arena_used > vm->bytes_arena_high_water) {
        vm->bytes_arena_high_water = vm->bytes_arena_used;
    }
    return start;
}

static char* lookup_string_in_arena(AivmVm* vm, const char* input)
{
    size_t offset = 0U;
    size_t next_offset;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    if (vm->string_arena_used > 0U &&
        input >= vm->string_arena &&
        input < (vm->string_arena + vm->string_arena_used)) {
        return (char*)input;
    }
    while (offset < vm->string_arena_used) {
        const char* candidate = &vm->string_arena[offset];
        if (strcmp(candidate, input) == 0) {
            return (char*)candidate;
        }
        while (offset < vm->string_arena_used && vm->string_arena[offset] != '\0') {
            if (!size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
        if (offset < vm->string_arena_used) {
            if (!size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
    }
    return NULL;
}

static char* lookup_string_range_in_arena(AivmVm* vm, const char* input, size_t length)
{
    size_t offset = 0U;
    size_t next_offset;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    while (offset < vm->string_arena_used) {
        char* candidate = &vm->string_arena[offset];
        size_t candidate_length = strlen(candidate);
        if (candidate_length == length && memcmp(candidate, input, length) == 0) {
            return candidate;
        }
        if (!size_add_checked(offset, candidate_length, &offset)) {
            return NULL;
        }
        if (offset < vm->string_arena_used) {
            if (!size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
    }
    return NULL;
}

static char* alloc_temp_string_copy(const char* input, size_t length);

static const char* snapshot_arena_backed_string(
    AivmVm* vm,
    const char* input,
    size_t length,
    char** out_temp_copy);

static char* copy_string_to_arena(AivmVm* vm, const char* input)
{
    size_t length = 0U;
    size_t bytes_needed = 0U;
    size_t i;
    size_t next_length;
    char* output;
    char* source_copy = NULL;
    const char* source = input;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    output = lookup_string_in_arena(vm, input);
    if (output != NULL) {
        return output;
    }
    while (input[length] != '\0') {
        if (!size_add_checked(length, 1U, &next_length)) {
            return NULL;
        }
        length = next_length;
    }
    if (!size_add_checked(length, 1U, &bytes_needed)) {
        return NULL;
    }
    source = snapshot_arena_backed_string(vm, input, length, &source_copy);
    if (source == NULL) {
        return NULL;
    }
    output = arena_alloc(vm, bytes_needed);
    if (output == NULL) {
        free(source_copy);
        return NULL;
    }
    for (i = 0U; i < length; i += 1U) {
        output[i] = source[i];
    }
    output[length] = '\0';
    free(source_copy);
    return output;
}

static char* copy_string_range_to_arena(AivmVm* vm, const char* input, size_t length)
{
    char* output;
    size_t i;
    size_t bytes_needed = 0U;
    char* source_copy = NULL;
    const char* source = input;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    output = lookup_string_range_in_arena(vm, input, length);
    if (output != NULL) {
        return output;
    }
    if (!size_add_checked(length, 1U, &bytes_needed)) {
        return NULL;
    }
    source = snapshot_arena_backed_string(vm, input, length, &source_copy);
    if (source == NULL) {
        return NULL;
    }
    output = arena_alloc(vm, bytes_needed);
    if (output == NULL) {
        free(source_copy);
        return NULL;
    }
    for (i = 0U; i < length; i += 1U) {
        output[i] = source[i];
    }
    output[length] = '\0';
    free(source_copy);
    return output;
}

static char* alloc_temp_string_copy(const char* input, size_t length)
{
    char* copy = NULL;
    size_t bytes_needed = 0U;
    if (input == NULL) {
        return NULL;
    }
    if (!size_add_checked(length, 1U, &bytes_needed)) {
        return NULL;
    }
    copy = (char*)malloc(bytes_needed);
    if (copy == NULL) {
        return NULL;
    }
    if (length > 0U) {
        memcpy(copy, input, length);
    }
    copy[length] = '\0';
    return copy;
}

static const char* snapshot_arena_backed_string(
    AivmVm* vm,
    const char* input,
    size_t length,
    char** out_temp_copy)
{
    if (out_temp_copy != NULL) {
        *out_temp_copy = NULL;
    }
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    if (!pointer_in_string_arena(vm, input)) {
        return input;
    }
    if (out_temp_copy == NULL) {
        return NULL;
    }
    *out_temp_copy = alloc_temp_string_copy(input, length);
    if (*out_temp_copy == NULL) {
        return NULL;
    }
    return *out_temp_copy;
}

static char* copy_string_splice_to_arena(
    AivmVm* vm,
    const char* prefix,
    size_t prefix_length,
    const char* suffix,
    size_t suffix_length)
{
    size_t offset = 0U;
    size_t next_offset;
    size_t total_length;
    size_t bytes_needed = 0U;
    char* output;
    size_t i;
    char* prefix_copy = NULL;
    char* suffix_copy = NULL;
    const char* prefix_source = prefix;
    const char* suffix_source = suffix;
    if (vm == NULL || prefix == NULL || suffix == NULL) {
        return NULL;
    }
    if (!size_add_checked(prefix_length, suffix_length, &total_length) ||
        !size_add_checked(total_length, 1U, &bytes_needed)) {
        return NULL;
    }
    while (offset < vm->string_arena_used) {
        char* candidate = &vm->string_arena[offset];
        size_t candidate_length = strlen(candidate);
        if (candidate_length == total_length &&
            memcmp(candidate, prefix, prefix_length) == 0 &&
            memcmp(candidate + prefix_length, suffix, suffix_length) == 0) {
            return candidate;
        }
        if (!size_add_checked(offset, candidate_length, &offset)) {
            return NULL;
        }
        if (offset < vm->string_arena_used) {
            if (!size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
    }
    prefix_source = snapshot_arena_backed_string(vm, prefix, prefix_length, &prefix_copy);
    if (prefix_source == NULL) {
        return NULL;
    }
    suffix_source = snapshot_arena_backed_string(vm, suffix, suffix_length, &suffix_copy);
    if (suffix_source == NULL) {
        free(prefix_copy);
        return NULL;
    }
    output = arena_alloc(vm, bytes_needed);
    if (output == NULL) {
        free(prefix_copy);
        free(suffix_copy);
        return NULL;
    }
    for (i = 0U; i < prefix_length; i += 1U) {
        output[i] = prefix_source[i];
    }
    for (i = 0U; i < suffix_length; i += 1U) {
        output[prefix_length + i] = suffix_source[i];
    }
    output[total_length] = '\0';
    free(prefix_copy);
    free(suffix_copy);
    return output;
}

static uint8_t* copy_bytes_to_arena(AivmVm* vm, const uint8_t* input, size_t length)
{
    uint8_t* output;
    size_t i;
    if (vm == NULL) {
        return NULL;
    }
    if (length == 0U) {
        return bytes_arena_alloc(vm, 0U);
    }
    if (input == NULL) {
        return NULL;
    }
    output = bytes_arena_alloc(vm, length);
    if (output == NULL) {
        return NULL;
    }
    for (i = 0U; i < length; i += 1U) {
        output[i] = input[i];
    }
    return output;
}

static int push_string_copy(AivmVm* vm, const char* input)
{
    char* output;
    output = copy_string_to_arena(vm, input);
    if (output == NULL) {
        return 0;
    }
    return aivm_stack_push(vm, aivm_value_string(output));
}

static int materialize_syscall_result(AivmVm* vm, AivmValue* io_result)
{
    char* copied_string;
    uint8_t* copied_bytes;
    if (vm == NULL || io_result == NULL) {
        return 0;
    }
    if (io_result->type == AIVM_VAL_STRING) {
        if (io_result->string_value == NULL) {
            set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "Syscall string result must be non-null.");
            return 0;
        }
        copied_string = copy_string_to_arena(vm, io_result->string_value);
        if (copied_string == NULL) {
            return 0;
        }
        *io_result = aivm_value_string(copied_string);
        return 1;
    }
    if (io_result->type == AIVM_VAL_BYTES) {
        if (io_result->bytes_value.length > 0U && io_result->bytes_value.data == NULL) {
            set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "Syscall bytes result must provide data.");
            return 0;
        }
        copied_bytes = copy_bytes_to_arena(vm, io_result->bytes_value.data, io_result->bytes_value.length);
        if (copied_bytes == NULL && io_result->bytes_value.length > 0U) {
            return 0;
        }
        *io_result = aivm_value_bytes(copied_bytes, io_result->bytes_value.length);
        return 1;
    }
    return 1;
}

static int push_escaped_string(AivmVm* vm, const char* input)
{
    size_t length = 0U;
    size_t escaped_length = 0U;
    size_t i;
    size_t out_index = 0U;
    size_t next_length;
    size_t next_out_index;
    char* output;

    if (vm == NULL || input == NULL) {
        return 0;
    }

    while (input[length] != '\0') {
        char ch = input[length];
        if (ch == '\\' || ch == '"' || ch == '\n' || ch == '\r' || ch == '\t') {
            if (!size_add_checked(escaped_length, 2U, &escaped_length)) {
                return 0;
            }
        } else {
            if (!size_add_checked(escaped_length, 1U, &escaped_length)) {
                return 0;
            }
        }
        if (!size_add_checked(length, 1U, &next_length)) {
            return 0;
        }
        length = next_length;
    }

    if (!size_add_checked(escaped_length, 1U, &escaped_length)) {
        return 0;
    }

    output = arena_alloc(vm, escaped_length);
    if (output == NULL) {
        return 0;
    }

    for (i = 0U; i < length; i += 1U) {
        char ch = input[i];
        if (ch == '\\') {
            output[out_index] = '\\';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = '\\';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
        } else if (ch == '"') {
            output[out_index] = '\\';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = '"';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
        } else if (ch == '\n') {
            output[out_index] = '\\';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = 'n';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
        } else if (ch == '\r') {
            output[out_index] = '\\';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = 'r';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
        } else if (ch == '\t') {
            output[out_index] = '\\';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = 't';
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
        } else {
            output[out_index] = ch;
            if (!size_add_checked(out_index, 1U, &next_out_index)) {
                return 0;
            }
            out_index = next_out_index;
        }
    }

    output[out_index] = '\0';
    return aivm_stack_push(vm, aivm_value_string(output));
}

static size_t utf8_next_index(const char* text, size_t index)
{
    unsigned char ch;
    if (text == NULL || text[index] == '\0') {
        return index;
    }

    ch = (unsigned char)text[index];
    if ((ch & 0x80U) == 0U) {
        return index + 1U;
    }
    if ((ch & 0xE0U) == 0xC0U &&
        (text[index + 1U] & 0xC0) == 0x80) {
        return index + 2U;
    }
    if ((ch & 0xF0U) == 0xE0U &&
        (text[index + 1U] & 0xC0) == 0x80 &&
        (text[index + 2U] & 0xC0) == 0x80) {
        return index + 3U;
    }
    if ((ch & 0xF8U) == 0xF0U &&
        (text[index + 1U] & 0xC0) == 0x80 &&
        (text[index + 2U] & 0xC0) == 0x80 &&
        (text[index + 3U] & 0xC0) == 0x80) {
        return index + 4U;
    }
    return index + 1U;
}

static size_t utf8_rune_count(const char* text)
{
    size_t byte_index = 0U;
    size_t count = 0U;
    size_t next_count;
    if (text == NULL) {
        return 0U;
    }
    while (text[byte_index] != '\0') {
        byte_index = utf8_next_index(text, byte_index);
        if (!size_add_checked(count, 1U, &next_count)) {
            return (size_t)-1;
        }
        count = next_count;
    }
    return count;
}

static size_t utf8_byte_offset_for_rune(const char* text, size_t rune_index)
{
    size_t byte_index = 0U;
    size_t current_rune = 0U;
    size_t next_rune;
    if (text == NULL) {
        return 0U;
    }
    while (text[byte_index] != '\0' && current_rune < rune_index) {
        byte_index = utf8_next_index(text, byte_index);
        if (!size_add_checked(current_rune, 1U, &next_rune)) {
            return byte_index;
        }
        current_rune = next_rune;
    }
    return byte_index;
}

static size_t clamp_rune_index(int64_t value, size_t max_value)
{
    if (value <= 0) {
        return 0U;
    }
    if ((uint64_t)value >= (uint64_t)max_value) {
        return max_value;
    }
    return (size_t)value;
}

static int push_substring_by_runes(AivmVm* vm, const char* text, int64_t start, int64_t length)
{
    size_t rune_count;
    size_t start_rune;
    size_t end_rune;
    size_t start_byte;
    size_t end_byte;
    size_t copy_length;
    char* output;

    if (vm == NULL) {
        return 0;
    }
    if (text == NULL || length <= 0) {
        return push_string_copy(vm, "");
    }

    rune_count = utf8_rune_count(text);
    start_rune = clamp_rune_index(start, rune_count);
    end_rune = clamp_rune_index(start + length, rune_count);
    if (end_rune < start_rune) {
        end_rune = start_rune;
    }

    start_byte = utf8_byte_offset_for_rune(text, start_rune);
    end_byte = utf8_byte_offset_for_rune(text, end_rune);
    copy_length = end_byte - start_byte;
    output = copy_string_range_to_arena(vm, text + start_byte, copy_length);
    if (output == NULL) {
        return 0;
    }
    return aivm_stack_push(vm, aivm_value_string(output));
}

static int push_remove_by_runes(AivmVm* vm, const char* text, int64_t start, int64_t length)
{
    size_t rune_count;
    size_t start_rune;
    size_t end_rune;
    size_t start_byte;
    size_t end_byte;
    size_t input_length = 0U;
    char* output;

    if (vm == NULL) {
        return 0;
    }
    if (text == NULL) {
        return push_string_copy(vm, "");
    }
    if (length <= 0) {
        return push_string_copy(vm, text);
    }

    while (text[input_length] != '\0') {
        if (!size_add_checked(input_length, 1U, &input_length)) {
            set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "substring input length overflow.");
            return 0;
        }
    }

    rune_count = utf8_rune_count(text);
    if (rune_count == (size_t)-1) {
        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "substring rune count overflow.");
        return 0;
    }
    start_rune = clamp_rune_index(start, rune_count);
    end_rune = clamp_rune_index(start + length, rune_count);
    if (end_rune < start_rune) {
        end_rune = start_rune;
    }

    start_byte = utf8_byte_offset_for_rune(text, start_rune);
    end_byte = utf8_byte_offset_for_rune(text, end_rune);
    output = copy_string_splice_to_arena(
        vm,
        text,
        start_byte,
        text + end_byte,
        input_length - end_byte);
    if (output == NULL) {
        return 0;
    }
    return aivm_stack_push(vm, aivm_value_string(output));
}

static int call_sys_with_arity(AivmVm* vm, size_t arg_count, AivmValue* out_result)
{
    AivmValue args[AIVM_VM_MAX_SYSCALL_ARGS];
    AivmValue target_value;
    AivmValue raw_target_value;
    size_t effective_arg_count = arg_count;
    AivmSyscallStatus syscall_status;
    AivmContractStatus contract_status = AIVM_CONTRACT_OK;
    int allow_positional_recovery = 0;
    size_t i;

    if (vm == NULL || out_result == NULL) {
        return 0;
    }
    if (arg_count > AIVM_VM_MAX_SYSCALL_ARGS) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid call argument count.");
        return 0;
    }

    for (i = 0U; i < arg_count; i += 1U) {
        if (!aivm_stack_pop(vm, &args[arg_count - i - 1U])) {
            return 0;
        }
    }
    if (!aivm_stack_pop(vm, &target_value)) {
        return 0;
    }
    raw_target_value = target_value;
    if (target_value.type != AIVM_VAL_STRING || target_value.string_value == NULL) {
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "CALL_SYS target must be string. got=%s ip=%llu",
            vm_value_type_name(target_value.type),
            (unsigned long long)vm->instruction_pointer);
        set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, vm->error_detail_storage);
        return 0;
    }
    if (!is_syscall_target_string(target_value.string_value)) {
        int recovered = 0;
        allow_positional_recovery =
            raw_target_value.type != AIVM_VAL_STRING ||
            raw_target_value.string_value == NULL;
        if (allow_positional_recovery) {
            for (i = 0U; i < effective_arg_count; i += 1U) {
                if (args[i].type == AIVM_VAL_STRING && is_syscall_target_string(args[i].string_value)) {
                    size_t j;
                    target_value = args[i];
                    for (j = i; j + 1U < effective_arg_count; j += 1U) {
                        args[j] = args[j + 1U];
                    }
                    effective_arg_count -= 1U;
                    recovered = 1;
                    break;
                }
            }
            if (!recovered &&
                vm->stack_count > 0U &&
                vm->stack[vm->stack_count - 1U].type == AIVM_VAL_STRING &&
                is_syscall_target_string(vm->stack[vm->stack_count - 1U].string_value)) {
                target_value = vm->stack[vm->stack_count - 1U];
                vm->stack_count -= 1U;
                recovered = 1;
            }
        }
        if (!recovered) {
            if (effective_arg_count == 1U &&
                raw_target_value.type == AIVM_VAL_STRING &&
                raw_target_value.string_value != NULL &&
                strncmp(raw_target_value.string_value, "sys", 3U) == 0 &&
                args[0].type == AIVM_VAL_STRING &&
                args[0].string_value != NULL) {
                const char* suffix_target = find_syscall_suffix_target(args[0].string_value);
                if (suffix_target != NULL && is_syscall_target_string(suffix_target)) {
                    const char* raw_source = target_value.string_value;
                    const char* arg_source = args[0].string_value;
                    const char* suffix_source = suffix_target;
                    char* raw_source_copy = NULL;
                    char* arg_source_copy = NULL;
                    size_t raw_len = 0U;
                    size_t next_raw_len;
                    size_t prefix_len = (size_t)(suffix_target - args[0].string_value);
                    size_t out_len;
                    size_t bytes_needed;
                    char* merged;
                    while (target_value.string_value[raw_len] != '\0') {
                        if (!size_add_checked(raw_len, 1U, &next_raw_len)) {
                            return 0;
                        }
                        raw_len = next_raw_len;
                    }
                    raw_source = snapshot_arena_backed_string(vm, target_value.string_value, raw_len, &raw_source_copy);
                    if (raw_source == NULL) {
                        return 0;
                    }
                    arg_source = snapshot_arena_backed_string(vm, args[0].string_value, prefix_len, &arg_source_copy);
                    if (arg_source == NULL) {
                        free(raw_source_copy);
                        return 0;
                    }
                    suffix_source = arg_source + prefix_len;
                    if (!size_add_checked(raw_len, prefix_len, &out_len) ||
                        !size_add_checked(out_len, 1U, &bytes_needed)) {
                        free(raw_source_copy);
                        free(arg_source_copy);
                        return 0;
                    }
                    merged = arena_alloc(vm, bytes_needed);
                    if (merged == NULL) {
                        free(raw_source_copy);
                        free(arg_source_copy);
                        return 0;
                    }
                    if (raw_len > 0U) {
                        memcpy(merged, raw_source, raw_len);
                    }
                    if (prefix_len > 0U) {
                        memcpy(merged + raw_len, arg_source, prefix_len);
                    }
                    merged[out_len] = '\0';
                    args[0] = aivm_value_string(merged);
                    suffix_target = copy_string_to_arena(vm, suffix_source);
                    free(raw_source_copy);
                    free(arg_source_copy);
                    if (suffix_target == NULL) {
                        return 0;
                    }
                    target_value = aivm_value_string(suffix_target);
                    recovered = 1;
                }
            }
            if (!recovered) {
                const char* raw_target_text = NULL;
                if (target_value.type == AIVM_VAL_STRING && target_value.string_value != NULL) {
                    raw_target_text = target_value.string_value;
                } else if (target_value.type == AIVM_VAL_INT) {
                    (void)snprintf(
                        vm->error_detail_storage,
                        sizeof(vm->error_detail_storage),
                        "AIVMS003: Syscall target was not found. rawTargetType=%s rawTargetInt=%lld argCount=%llu",
                        vm_value_type_name(target_value.type),
                        (long long)target_value.int_value,
                        (unsigned long long)effective_arg_count);
                    set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
                    return 0;
                }
                (void)snprintf(
                    vm->error_detail_storage,
                    sizeof(vm->error_detail_storage),
                    "AIVMS003: Syscall target was not found. rawTargetType=%s rawTarget=%s argCount=%llu",
                    vm_value_type_name(target_value.type),
                    raw_target_text == NULL ? "<non-string>" : raw_target_text,
                    (unsigned long long)effective_arg_count);
                set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
                return 0;
            }
        }
    }
    if (strcmp(target_value.string_value, "sys.debug.taskReclaimStats") == 0) {
        AivmValueType expected_return_type = AIVM_VAL_VOID;
        contract_status = aivm_syscall_contract_validate(
            target_value.string_value,
            args,
            effective_arg_count,
            &expected_return_type);
        if (contract_status != AIVM_CONTRACT_OK) {
            set_vm_error(vm, AIVM_VM_ERR_SYSCALL, syscall_contract_failure_detail(contract_status));
            return 0;
        }
        return call_debug_task_reclaim_stats(vm, out_result);
    }

    syscall_status = aivm_syscall_dispatch_checked_with_contract(
        vm->syscall_bindings,
        vm->syscall_binding_count,
        target_value.string_value,
        args,
        effective_arg_count,
        out_result,
        &contract_status);
    if (syscall_status != AIVM_SYSCALL_OK) {
        if (syscall_status == AIVM_SYSCALL_ERR_INVALID) {
            (void)snprintf(
                vm->error_detail_storage,
                sizeof(vm->error_detail_storage),
                "AIVMS001: Syscall dispatch input was invalid. target=\"%.72s\" bindingCount=%llu argCount=%llu hasBindings=%s",
                target_value.string_value,
                (unsigned long long)vm->syscall_binding_count,
                (unsigned long long)effective_arg_count,
                vm->syscall_bindings == NULL ? "false" : "true");
            set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
            return 0;
        }
        if (syscall_status == AIVM_SYSCALL_ERR_CONTRACT) {
            set_vm_error(
                vm,
                AIVM_VM_ERR_SYSCALL,
                syscall_contract_failure_detail_with_args(
                    vm,
                    target_value.string_value,
                    args,
                    effective_arg_count,
                    contract_status));
            return 0;
        }
        if (syscall_status == AIVM_SYSCALL_ERR_NOT_FOUND) {
            set_vm_error(
                vm,
                AIVM_VM_ERR_SYSCALL,
                syscall_not_found_detail_with_recovery(
                    vm,
                    raw_target_value,
                    target_value.string_value,
                    args,
                    effective_arg_count));
            return 0;
        }
        set_vm_error(vm, AIVM_VM_ERR_SYSCALL, syscall_failure_detail(syscall_status, contract_status));
        return 0;
    }

    if (!materialize_syscall_result(vm, out_result)) {
        return 0;
    }
    return 1;
}

static const char* syscall_not_found_detail_with_recovery(
    AivmVm* vm,
    AivmValue raw_target_value,
    const char* recovered_target,
    const AivmValue* args,
    size_t arg_count)
{
    const char* raw_target_text = "<non-string>";
    const char* recovered_text = recovered_target == NULL ? "<null>" : recovered_target;
    const char* arg0 = "void";
    const char* arg1 = "void";
    const char* arg2 = "void";
    if (vm == NULL) {
        return "AIVMS003: Syscall target was not found.";
    }
    if (raw_target_value.type == AIVM_VAL_STRING && raw_target_value.string_value != NULL) {
        raw_target_text = raw_target_value.string_value;
    }
    if (arg_count > 0U) {
        arg0 = vm_value_type_name(args[0].type);
    }
    if (arg_count > 1U) {
        arg1 = vm_value_type_name(args[1].type);
    }
    if (arg_count > 2U) {
        arg2 = vm_value_type_name(args[2].type);
    }
    (void)snprintf(
        vm->error_detail_storage,
        sizeof(vm->error_detail_storage),
        "AIVMS003: Syscall target was not found. rawTargetType=%s rawTarget=%s recoveredTarget=%s argCount=%llu arg0=%s arg1=%s arg2=%s",
        vm_value_type_name(raw_target_value.type),
        raw_target_text,
        recovered_text,
        (unsigned long long)arg_count,
        arg0,
        arg1,
        arg2);
    return vm->error_detail_storage;
}

static const char* syscall_failure_detail(AivmSyscallStatus status, AivmContractStatus contract_status)
{
    switch (status) {
        case AIVM_SYSCALL_ERR_INVALID:
            return "AIVMS001: Syscall dispatch input was invalid.";
        case AIVM_SYSCALL_ERR_NULL_RESULT:
            return "AIVMS002: Syscall dispatch result pointer was null.";
        case AIVM_SYSCALL_ERR_NOT_FOUND:
            return "AIVMS003: Syscall target was not found.";
        case AIVM_SYSCALL_ERR_CONTRACT:
            return syscall_contract_failure_detail(contract_status);
        case AIVM_SYSCALL_ERR_RETURN_TYPE:
            return "AIVMS005: Syscall return type violated contract.";
        case AIVM_SYSCALL_OK:
            return "AIVMS000: Syscall dispatch succeeded.";
        default:
            return "AIVMS999: Unknown syscall dispatch status.";
    }
}

static size_t append_vm_value_preview(char* buffer, size_t capacity, size_t used, AivmValue value)
{
    int wrote = 0;
    if (buffer == NULL || capacity == 0U || used >= capacity) {
        return used;
    }
    if (value.type == AIVM_VAL_INT) {
        wrote = snprintf(
            buffer + used,
            capacity - used,
            "(%lld)",
            (long long)value.int_value);
    } else if (value.type == AIVM_VAL_BOOL) {
        wrote = snprintf(
            buffer + used,
            capacity - used,
            "(%s)",
            value.bool_value ? "true" : "false");
    } else if (value.type == AIVM_VAL_STRING && value.string_value != NULL) {
        wrote = snprintf(
            buffer + used,
            capacity - used,
            "(\"%.24s\")",
            value.string_value);
    }
    if (wrote <= 0) {
        return used;
    }
    if ((size_t)wrote >= capacity - used) {
        return capacity - 1U;
    }
    return used + (size_t)wrote;
}

static size_t append_frame_local_previews(
    AivmVm* vm,
    size_t frame_index,
    const char* prefix,
    char* buffer,
    size_t capacity,
    size_t used)
{
    size_t base = 0U;
    size_t i;
    size_t max_locals = 3U;
    size_t limit = 0U;
    if (vm == NULL || buffer == NULL || capacity == 0U || used >= capacity || prefix == NULL) {
        return used;
    }
    if (vm->call_frame_count == 0U || frame_index >= vm->call_frame_count) {
        return used;
    }
    base = vm->call_frames[frame_index].locals_base;
    if (!size_add_checked(used, 1U, &limit)) {
        return used;
    }
    for (i = 0U; i < max_locals && (base + i) < vm->locals_count && limit < capacity; i += 1U) {
        int wrote = snprintf(
            buffer + used,
            capacity - used,
            " %s%llu=%s",
            prefix,
            (unsigned long long)i,
            vm_value_type_name(vm->locals[base + i].type));
        if (wrote <= 0) {
            break;
        }
        if ((size_t)wrote >= capacity - used) {
            used = capacity - 1U;
            break;
        }
        if (!size_add_checked(used, (size_t)wrote, &used)) {
            return capacity - 1U;
        }
        used = append_vm_value_preview(buffer, capacity, used, vm->locals[base + i]);
        if (!size_add_checked(used, 1U, &limit)) {
            break;
        }
    }
    return used;
}

static size_t append_frame_return_previews(AivmVm* vm, char* buffer, size_t capacity, size_t used)
{
    size_t i;
    size_t limit = 0U;
    if (vm == NULL || buffer == NULL || capacity == 0U || used >= capacity) {
        return used;
    }
    if (!size_add_checked(used, 1U, &limit)) {
        return used;
    }
    for (i = 0U; i < vm->call_frame_count && i < 3U && limit < capacity; i += 1U) {
        size_t frame_index = vm->call_frame_count - 1U - i;
        int wrote = snprintf(
            buffer + used,
            capacity - used,
            " ret%llu=%llu",
            (unsigned long long)i,
            (unsigned long long)vm->call_frames[frame_index].return_instruction_pointer);
        if (wrote <= 0) {
            break;
        }
        if ((size_t)wrote >= capacity - used) {
            return capacity - 1U;
        }
        if (!size_add_checked(used, (size_t)wrote, &used)) {
            return capacity - 1U;
        }
        if (!size_add_checked(used, 1U, &limit)) {
            break;
        }
    }
    return used;
}

static const char* syscall_contract_failure_detail_with_args(
    AivmVm* vm,
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmContractStatus contract_status)
{
    const AivmSyscallContract* contract;
    size_t i;
    size_t used = 0U;

    if (vm == NULL) {
        return syscall_contract_failure_detail(contract_status);
    }
    if (target == NULL) {
        return syscall_contract_failure_detail(contract_status);
    }
    if (contract_status == AIVM_CONTRACT_ERR_UNKNOWN_TARGET) {
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "AIVMS004/AIVMC001: Syscall target was not found. target=%s",
            target);
        return vm->error_detail_storage;
    }
    if (contract_status != AIVM_CONTRACT_ERR_ARG_TYPE) {
        return syscall_contract_failure_detail(contract_status);
    }
    contract = aivm_syscall_contract_find_by_target(target);
    if (contract == NULL) {
        return syscall_contract_failure_detail(contract_status);
    }

    used = (size_t)snprintf(
        vm->error_detail_storage,
        sizeof(vm->error_detail_storage),
        "AIVMS004/AIVMC003: Syscall argument type was invalid. target=%s",
        target);
    if (used >= sizeof(vm->error_detail_storage)) {
        used = sizeof(vm->error_detail_storage) - 1U;
    }

    for (i = 0U; i < contract->arg_count; i += 1U) {
        const char* expected = vm_value_type_name(contract->arg_types[i]);
        const char* actual = (i < arg_count) ? vm_value_type_name(args[i].type) : "missing";
        size_t limit = 0U;
        if (!size_add_checked(used, 1U, &limit) ||
            limit >= sizeof(vm->error_detail_storage)) {
            break;
        }
        int wrote = snprintf(
            vm->error_detail_storage + used,
            sizeof(vm->error_detail_storage) - used,
            " arg%llu=%s->%s",
            (unsigned long long)i,
            actual,
            expected);
        if (wrote <= 0) {
            break;
        }
        if ((size_t)wrote >= sizeof(vm->error_detail_storage) - used) {
            used = sizeof(vm->error_detail_storage) - 1U;
            break;
        }
        if (!size_add_checked(used, (size_t)wrote, &used)) {
            used = sizeof(vm->error_detail_storage) - 1U;
            break;
        }
        if (i < arg_count) {
            used = append_vm_value_preview(
                vm->error_detail_storage,
                sizeof(vm->error_detail_storage),
                used,
                args[i]);
        }
    }
    used = append_frame_return_previews(
        vm,
        vm->error_detail_storage,
        sizeof(vm->error_detail_storage),
        used);
    if (vm->call_frame_count > 0U) {
        used = append_frame_local_previews(
            vm,
            vm->call_frame_count - 1U,
            "local",
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            used);
    }
    if (vm->call_frame_count > 1U) {
        used = append_frame_local_previews(
            vm,
            vm->call_frame_count - 2U,
            "callerLocal",
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            used);
    }
    if (vm->call_frame_count > 2U) {
        used = append_frame_local_previews(
            vm,
            vm->call_frame_count - 3U,
            "caller2Local",
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            used);
    }
    return vm->error_detail_storage;
}

static const char* syscall_contract_failure_detail(AivmContractStatus status)
{
    switch (status) {
        case AIVM_CONTRACT_ERR_UNKNOWN_TARGET:
            return "AIVMS004/AIVMC001: Syscall target was not found.";
        case AIVM_CONTRACT_ERR_ARG_COUNT:
            return "AIVMS004/AIVMC002: Syscall argument count was invalid.";
        case AIVM_CONTRACT_ERR_ARG_TYPE:
            return "AIVMS004/AIVMC003: Syscall argument type was invalid.";
        case AIVM_CONTRACT_ERR_UNKNOWN_ID:
            return "AIVMS004/AIVMC004: Syscall contract ID was not found.";
        case AIVM_CONTRACT_OK:
            return "AIVMS004/AIVMC000: Syscall contract validation passed.";
        default:
            return "AIVMS004/AIVMC999: Unknown syscall contract validation status.";
    }
}

static int transition_task_state(AivmVm* vm, AivmCompletedTask* task, AivmTaskState next_state)
{
    if (vm == NULL || task == NULL) {
        return 0;
    }
    if (task->state == AIVM_TASK_STATE_PENDING &&
        (next_state == AIVM_TASK_STATE_COMPLETED ||
         next_state == AIVM_TASK_STATE_FAILED ||
         next_state == AIVM_TASK_STATE_CANCELED)) {
        task->state = next_state;
        return 1;
    }
    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task state transition was invalid.");
    return 0;
}

static int value_matches_task_handle(AivmValue value, int64_t handle)
{
    return value.type == AIVM_VAL_INT && value.int_value == handle;
}

static int is_task_handle_pinned(const AivmVm* vm, int64_t handle)
{
    size_t i;
    if (vm == NULL) {
        return 0;
    }
    for (i = 0U; i < vm->stack_count; i += 1U) {
        if (value_matches_task_handle(vm->stack[i], handle)) {
            return 1;
        }
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        if (value_matches_task_handle(vm->locals[i], handle)) {
            return 1;
        }
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        if (value_matches_task_handle(vm->par_values[i], handle)) {
            return 1;
        }
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (vm->completed_tasks[i].handle != handle &&
            value_matches_task_handle(vm->completed_tasks[i].result, handle)) {
            return 1;
        }
    }
    return 0;
}

static int reclaim_oldest_completed_task_slot(AivmVm* vm)
{
    size_t index;
    size_t next_index = 0U;
    size_t move_count = 0U;
    if (vm == NULL) {
        return 0;
    }
    if (vm->completed_task_count == 0U) {
        return 1;
    }
    for (index = 0U; index < vm->completed_task_count; index += 1U) {
        if (!is_task_handle_pinned(vm, vm->completed_tasks[index].handle)) {
            break;
        }
        increment_counter_saturating(&vm->task_reclaim_skip_pinned_count);
    }
    if (index >= vm->completed_task_count) {
        increment_counter_saturating(&vm->task_reclaim_exhausted_count);
        return 0;
    }
    if (size_add_checked(index, 1U, &next_index) &&
        next_index < vm->completed_task_count &&
        size_sub_checked(vm->completed_task_count, next_index, &move_count) &&
        move_count > 0U) {
        memmove(
            &vm->completed_tasks[index],
            &vm->completed_tasks[next_index],
            move_count * sizeof(AivmCompletedTask));
    }
    vm->completed_task_count -= 1U;
    increment_counter_saturating(&vm->task_reclaim_count);
    return 1;
}

static int push_completed_task(AivmVm* vm, AivmValue result)
{
    AivmCompletedTask* task;
    int64_t handle;
    size_t needed = 0U;
    if (vm == NULL) {
        return 0;
    }
    if (vm->completed_task_count >= AIVM_VM_TASK_CAPACITY) {
        if (!reclaim_oldest_completed_task_slot(vm)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task table capacity exceeded.");
            return 0;
        }
    }
    if (vm->next_task_handle == INT64_MAX) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task handle overflow.");
        return 0;
    }

    handle = vm->next_task_handle;
    vm->next_task_handle += 1;
    if (!size_add_checked(vm->completed_task_count, 1U, &needed) ||
        needed > AIVM_VM_TASK_CAPACITY) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task table capacity exceeded.");
        return 0;
    }
    task = &vm->completed_tasks[vm->completed_task_count];
    task->state = AIVM_TASK_STATE_PENDING;
    task->handle = handle;
    task->result = result;
    if (!transition_task_state(vm, task, AIVM_TASK_STATE_COMPLETED)) {
        return 0;
    }
    vm->completed_task_count = needed;
    return aivm_stack_push(vm, aivm_value_int(handle));
}

static int execute_call_subroutine_sync(AivmVm* vm, size_t target, AivmValue* out_result)
{
    size_t baseline_frame_count;
    size_t arg_count;
    size_t frame_base;
    size_t return_ip;
    size_t pre_restore_stack_count = 0U;
    size_t max_stack_count = 0U;
    size_t extra_stack_values = 0U;
    AivmValue result = aivm_value_void();

    if (vm == NULL || out_result == NULL) {
        return 0;
    }
    if (target >= vm->program->instruction_count) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid function index.");
        return 0;
    }
    arg_count = infer_call_arg_count(vm->program, target);
    if (arg_count > vm->stack_count) {
        set_vm_error_call_arg_depth(vm, target, arg_count, vm->stack_count);
        return 0;
    }
    if (!validate_call_target_layout(vm, vm->program, target, arg_count)) {
        return 0;
    }

    baseline_frame_count = vm->call_frame_count;
    frame_base = vm->stack_count - arg_count;
    if (!size_add_checked(vm->instruction_pointer, 1U, &return_ip)) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Return instruction pointer overflowed.");
        return 0;
    }

    if (!aivm_frame_push(vm, return_ip, frame_base)) {
        return 0;
    }
    vm->instruction_pointer = target;

    while (vm->status != AIVM_VM_STATUS_ERROR) {
        if (vm->call_frame_count == baseline_frame_count &&
            vm->instruction_pointer == return_ip) {
            break;
        }

        if (vm->instruction_pointer >= vm->program->instruction_count) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Subroutine terminated without RET.");
            return 0;
        }

        aivm_step(vm);
        if (vm->status == AIVM_VM_STATUS_HALTED) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "HALT is invalid inside ASYNC_CALL.");
            return 0;
        }
    }

    if (vm->status == AIVM_VM_STATUS_ERROR) {
        return 0;
    }

    pre_restore_stack_count = vm->stack_count;
    if (vm->stack_count > frame_base) {
        result = vm->stack[vm->stack_count - 1U];
    }
    if (!size_add_checked(frame_base, 1U, &max_stack_count)) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Async return restore size arithmetic overflow.");
        return 0;
    }
    if (pre_restore_stack_count > max_stack_count) {
        if (!size_sub_checked(pre_restore_stack_count, max_stack_count, &extra_stack_values)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Async return restore size arithmetic overflow.");
            return 0;
        }
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "Async return restore invalid. extraStackValues=%llu frameBase=%llu stackCount=%llu frameCount=%llu pc=%llu",
            (unsigned long long)extra_stack_values,
            (unsigned long long)frame_base,
            (unsigned long long)pre_restore_stack_count,
            (unsigned long long)vm->call_frame_count,
            (unsigned long long)vm->instruction_pointer);
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    vm->stack_count = frame_base;
    *out_result = result;
    return 1;
}

static int is_terminal_task_state(AivmTaskState state)
{
    return state == AIVM_TASK_STATE_COMPLETED ||
        state == AIVM_TASK_STATE_FAILED ||
        state == AIVM_TASK_STATE_CANCELED;
}

static int task_terminal_payload_is_valid(const AivmVm* vm, const AivmCompletedTask* task)
{
    const AivmNodeRecord* node;
    if (vm == NULL || task == NULL) {
        return 0;
    }
    if (task->state == AIVM_TASK_STATE_FAILED || task->state == AIVM_TASK_STATE_CANCELED) {
        if (task->result.type != AIVM_VAL_NODE) {
            return 0;
        }
        if (!lookup_node(vm, task->result.node_handle, &node)) {
            return 0;
        }
        return strcmp(node->kind, "Err") == 0;
    }
    return 1;
}

static int call_debug_task_reclaim_stats(AivmVm* vm, AivmValue* out_result)
{
    AivmNodeAttr attrs[3];
    int64_t handle;
    char id_buffer[40];
    size_t next_node_count = 0U;
    size_t suffix_length;
    if (vm == NULL || out_result == NULL) {
        return 0;
    }
    memcpy(id_buffer, "debug_task_reclaim_stats_", 25U);
    if (!size_add_checked(vm->node_count, 1U, &next_node_count)) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "debug task stats node id overflow.");
        return 0;
    }
    suffix_length = write_u64_decimal(id_buffer + 25U, sizeof(id_buffer) - 25U, (uint64_t)next_node_count);
    if (suffix_length == 0U) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "debug task stats node id overflow.");
        return 0;
    }

    attrs[0].key = "reclaimed";
    attrs[0].kind = AIVM_NODE_ATTR_INT;
    attrs[0].int_value = (int64_t)vm->task_reclaim_count;
    attrs[1].key = "skipPinned";
    attrs[1].kind = AIVM_NODE_ATTR_INT;
    attrs[1].int_value = (int64_t)vm->task_reclaim_skip_pinned_count;
    attrs[2].key = "exhausted";
    attrs[2].kind = AIVM_NODE_ATTR_INT;
    attrs[2].int_value = (int64_t)vm->task_reclaim_exhausted_count;

    if (!create_node_record(vm, "DebugTaskReclaimStats", id_buffer, attrs, 3U, NULL, 0U, &handle)) {
        return 0;
    }
    *out_result = aivm_value_node(handle);
    return 1;
}

static int find_terminal_task_result(AivmVm* vm, int64_t handle, AivmValue* out_result)
{
    size_t i;
    if (vm == NULL || out_result == NULL) {
        return 0;
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (is_terminal_task_state(vm->completed_tasks[i].state) &&
            vm->completed_tasks[i].handle == handle) {
            if (!task_terminal_payload_is_valid(vm, &vm->completed_tasks[i])) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Terminal failed/canceled task requires Err node result.");
                return 0;
            }
            *out_result = vm->completed_tasks[i].result;
            return 1;
        }
    }
    return 0;
}

static int lookup_node(const AivmVm* vm, int64_t handle, const AivmNodeRecord** out_node)
{
    size_t index;
    if (vm == NULL || out_node == NULL) {
        return 0;
    }
    if (handle <= 0) {
        return 0;
    }
    index = (size_t)(handle - 1);
    if (index >= vm->node_count) {
        return 0;
    }
    *out_node = &vm->nodes[index];
    return 1;
}

static int remap_value_node_handle(AivmValue* value, const int64_t* handle_map)
{
    int64_t old_handle;
    if (value == NULL || handle_map == NULL) {
        return 0;
    }
    if (value->type != AIVM_VAL_NODE) {
        return 1;
    }
    old_handle = value->node_handle;
    if (old_handle <= 0 || old_handle > (int64_t)AIVM_VM_NODE_CAPACITY) {
        return 0;
    }
    if (handle_map[old_handle] <= 0) {
        return 0;
    }
    value->node_handle = handle_map[old_handle];
    return 1;
}

static int mark_live_node_handles(
    AivmVm* vm,
    uint8_t* live,
    const int64_t* extra_handles,
    size_t extra_handle_count)
{
    int64_t queue[AIVM_VM_NODE_CAPACITY];
    size_t queue_read = 0U;
    size_t queue_write = 0U;
    size_t i;

    if (vm == NULL || live == NULL) {
        return 0;
    }

    #define ENQUEUE_HANDLE(handle_value) \
        do { \
            int64_t __h = (handle_value); \
            if (__h > 0 && __h <= (int64_t)vm->node_count) { \
                size_t __idx = (size_t)(__h - 1); \
                if (live[__idx] == 0U) { \
                    size_t __next_queue_write; \
                    if (queue_write >= AIVM_VM_NODE_CAPACITY) { \
                        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM003: node mark queue capacity exceeded."); \
                        return 0; \
                    } \
                    if (!size_add_checked(queue_write, 1U, &__next_queue_write)) { \
                        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM003: node mark queue overflow."); \
                        return 0; \
                    } \
                    live[__idx] = 1U; \
                    queue[queue_write] = __h; \
                    queue_write = __next_queue_write; \
                } \
            } \
        } while (0)

    ENQUEUE_HANDLE(vm->process_argv_node_handle);
    ENQUEUE_HANDLE(vm->ui_default_window_size_node_handle);
    ENQUEUE_HANDLE(vm->ui_empty_event_node_handle);
    for (i = 0U; i < vm->stack_count; i += 1U) {
        if (vm->stack[i].type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->stack[i].node_handle);
        }
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        if (vm->locals[i].type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->locals[i].node_handle);
        }
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (vm->completed_tasks[i].result.type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->completed_tasks[i].result.node_handle);
        }
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        if (vm->par_values[i].type == AIVM_VAL_NODE) {
            ENQUEUE_HANDLE(vm->par_values[i].node_handle);
        }
    }
    if (extra_handles != NULL) {
        for (i = 0U; i < extra_handle_count; i += 1U) {
            int64_t handle = extra_handles[i];
            if (handle <= 0) {
                continue;
            }
            if (handle > (int64_t)vm->node_count) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid extra node handle during GC mark.");
                return 0;
            }
            ENQUEUE_HANDLE(handle);
        }
    }

    while (queue_read < queue_write) {
        const AivmNodeRecord* node;
        int64_t handle = queue[queue_read];
        size_t child_index;
        if (!size_add_checked(queue_read, 1U, &queue_read)) {
            set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM003: node mark queue overflow.");
            return 0;
        }
        if (!lookup_node(vm, handle, &node)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid node handle during GC mark.");
            return 0;
        }
        for (child_index = 0U; child_index < node->child_count; child_index += 1U) {
            size_t child_slot;
            if (!size_add_checked(node->child_start, child_index, &child_slot) ||
                child_slot >= AIVM_VM_NODE_CHILD_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid child slot during GC mark.");
                return 0;
            }
            ENQUEUE_HANDLE(vm->node_children[child_slot]);
        }
    }

    #undef ENQUEUE_HANDLE
    return 1;
}

static int compact_string_arena(AivmVm* vm)
{
    uint8_t live[AIVM_VM_NODE_CAPACITY];
    char new_arena[AIVM_VM_STRING_ARENA_CAPACITY];
    size_t new_used = 0U;
    size_t i;

    if (vm == NULL) {
        return 0;
    }
    if (vm->string_arena_used == 0U) {
        return 1;
    }

    memset(live, 0, sizeof(live));
    memset(new_arena, 0, sizeof(new_arena));
    if (!mark_live_node_handles(vm, live, NULL, 0U)) {
        return 0;
    }

    for (i = 0U; i < vm->stack_count; i += 1U) {
        if (!compact_relocate_value_string(vm, &vm->stack[i], new_arena, &new_used)) {
            return 0;
        }
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        if (!compact_relocate_value_string(vm, &vm->locals[i], new_arena, &new_used)) {
            return 0;
        }
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (!compact_relocate_value_string(vm, &vm->completed_tasks[i].result, new_arena, &new_used)) {
            return 0;
        }
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        if (!compact_relocate_value_string(vm, &vm->par_values[i], new_arena, &new_used)) {
            return 0;
        }
    }
    for (i = 0U; i < vm->node_count; i += 1U) {
        size_t attr_i;
        AivmNodeRecord* node;
        if (live[i] == 0U) {
            continue;
        }
        node = &vm->nodes[i];
        if (!compact_relocate_string_ptr(vm, &node->kind, new_arena, &new_used) ||
            !compact_relocate_string_ptr(vm, &node->id, new_arena, &new_used)) {
            return 0;
        }
        for (attr_i = 0U; attr_i < node->attr_count; attr_i += 1U) {
            size_t attr_slot = 0U;
            AivmNodeAttr* attr;
            if (!size_add_checked(node->attr_start, attr_i, &attr_slot) ||
                attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node attr slot overflow during string compaction.");
                return 0;
            }
            attr = &vm->node_attrs[attr_slot];
            if (!compact_relocate_string_ptr(vm, &attr->key, new_arena, &new_used)) {
                return 0;
            }
            if ((attr->kind == AIVM_NODE_ATTR_IDENTIFIER || attr->kind == AIVM_NODE_ATTR_STRING) &&
                !compact_relocate_string_ptr(vm, &attr->string_value, new_arena, &new_used)) {
                return 0;
            }
        }
    }

    memcpy(vm->string_arena, new_arena, new_used);
    vm->string_arena_used = new_used;
    if (new_used < AIVM_VM_STRING_ARENA_CAPACITY) {
        vm->string_arena[new_used] = '\0';
    }
    return 1;
}

static int compact_node_arenas_with_map(
    AivmVm* vm,
    const int64_t* extra_handles,
    size_t extra_handle_count,
    int64_t* out_handle_map)
{
    uint8_t live[AIVM_VM_NODE_CAPACITY];
    int64_t handle_map[AIVM_VM_NODE_CAPACITY + 1U];
    AivmNodeRecord new_nodes[AIVM_VM_NODE_CAPACITY];
    AivmNodeAttr new_attrs[AIVM_VM_NODE_ATTR_CAPACITY];
    int64_t new_children[AIVM_VM_NODE_CHILD_CAPACITY];
    size_t new_node_count = 0U;
    size_t new_attr_count = 0U;
    size_t new_child_count = 0U;
    size_t old_node_count;
    size_t old_attr_count;
    size_t old_child_count;
    size_t i;

    if (vm == NULL) {
        return 0;
    }
    increment_counter_saturating(&vm->node_gc_attempt_count);
    if (vm->node_count == 0U) {
        return 1;
    }
    old_node_count = vm->node_count;
    old_attr_count = vm->node_attr_count;
    old_child_count = vm->node_child_count;

    memset(live, 0, sizeof(live));
    memset(handle_map, 0, sizeof(handle_map));
    if (!mark_live_node_handles(vm, live, extra_handles, extra_handle_count)) {
        return 0;
    }

    for (i = 0U; i < vm->node_count; i += 1U) {
        if (live[i] != 0U) {
            size_t old_handle_index;
            size_t compacted_handle;
            if (!size_add_checked(i, 1U, &old_handle_index) ||
                !size_add_checked(new_node_count, 1U, &compacted_handle)) {
                set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node compaction handle overflow.");
                return 0;
            }
            handle_map[old_handle_index] = (int64_t)compacted_handle;
            new_node_count = compacted_handle;
        }
    }

    for (i = 0U; i < vm->node_count; i += 1U) {
        const AivmNodeRecord* old_node;
        AivmNodeRecord* out_node;
        size_t attr_i;
        size_t child_i;

        if (live[i] == 0U) {
            continue;
        }
        old_node = &vm->nodes[i];
        {
            size_t old_handle_index;
            int64_t compacted_handle;
            if (!size_add_checked(i, 1U, &old_handle_index)) {
                set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node compaction handle overflow.");
                return 0;
            }
            compacted_handle = handle_map[old_handle_index];
            if (compacted_handle <= 0) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Dangling live node handle during node GC.");
                return 0;
            }
            out_node = &new_nodes[(size_t)(compacted_handle - 1)];
        }
        *out_node = *old_node;
        out_node->attr_start = new_attr_count;
        out_node->child_start = new_child_count;

        {
            size_t needed_attr_count;
            size_t needed_child_count;
            if (!size_add_checked(new_attr_count, old_node->attr_count, &needed_attr_count) ||
                !size_add_checked(new_child_count, old_node->child_count, &needed_child_count) ||
                needed_attr_count > AIVM_VM_NODE_ATTR_CAPACITY ||
                needed_child_count > AIVM_VM_NODE_CHILD_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node compaction capacity exceeded.");
                return 0;
            }
        }

        for (attr_i = 0U; attr_i < old_node->attr_count; attr_i += 1U) {
            size_t new_attr_slot = 0U;
            size_t old_attr_slot = 0U;
            if (!size_add_checked(new_attr_count, attr_i, &new_attr_slot) ||
                !size_add_checked(old_node->attr_start, attr_i, &old_attr_slot) ||
                new_attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY ||
                old_attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node attr slot overflow during node GC.");
                return 0;
            }
            new_attrs[new_attr_slot] = vm->node_attrs[old_attr_slot];
        }
        for (child_i = 0U; child_i < old_node->child_count; child_i += 1U) {
            size_t old_child_slot = 0U;
            size_t new_child_slot = 0U;
            int64_t old_child;
            if (!size_add_checked(old_node->child_start, child_i, &old_child_slot) ||
                !size_add_checked(new_child_count, child_i, &new_child_slot) ||
                old_child_slot >= AIVM_VM_NODE_CHILD_CAPACITY ||
                new_child_slot >= AIVM_VM_NODE_CHILD_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node child slot overflow during node GC.");
                return 0;
            }
            old_child = vm->node_children[old_child_slot];
            if (old_child <= 0 || old_child > (int64_t)AIVM_VM_NODE_CAPACITY || handle_map[old_child] <= 0) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Dangling child handle during node GC.");
                return 0;
            }
            new_children[new_child_slot] = handle_map[old_child];
        }
        if (!size_add_checked(new_attr_count, old_node->attr_count, &new_attr_count) ||
            !size_add_checked(new_child_count, old_node->child_count, &new_child_count)) {
            set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node compaction capacity exceeded.");
            return 0;
        }
    }

    memcpy(vm->nodes, new_nodes, sizeof(new_nodes));
    memcpy(vm->node_attrs, new_attrs, sizeof(new_attrs));
    memcpy(vm->node_children, new_children, sizeof(new_children));
    vm->node_count = new_node_count;
    vm->node_attr_count = new_attr_count;
    vm->node_child_count = new_child_count;
    increment_counter_saturating(&vm->node_gc_compaction_count);
    add_counter_saturating(&vm->node_gc_reclaimed_nodes, old_node_count - new_node_count);
    add_counter_saturating(&vm->node_gc_reclaimed_attrs, old_attr_count - new_attr_count);
    add_counter_saturating(&vm->node_gc_reclaimed_children, old_child_count - new_child_count);

    for (i = 0U; i < vm->stack_count; i += 1U) {
        if (!remap_value_node_handle(&vm->stack[i], handle_map)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid stack node handle during node GC.");
            return 0;
        }
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        if (!remap_value_node_handle(&vm->locals[i], handle_map)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid local node handle during node GC.");
            return 0;
        }
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (!remap_value_node_handle(&vm->completed_tasks[i].result, handle_map)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid completed-task node handle during node GC.");
            return 0;
        }
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        if (!remap_value_node_handle(&vm->par_values[i], handle_map)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid parallel-value node handle during node GC.");
            return 0;
        }
    }
    if (vm->process_argv_node_handle > 0) {
        if (vm->process_argv_node_handle > (int64_t)AIVM_VM_NODE_CAPACITY ||
            handle_map[vm->process_argv_node_handle] <= 0) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid process argv node handle during node GC.");
            return 0;
        }
        vm->process_argv_node_handle = handle_map[vm->process_argv_node_handle];
    }
    if (vm->ui_default_window_size_node_handle > 0) {
        if (vm->ui_default_window_size_node_handle > (int64_t)AIVM_VM_NODE_CAPACITY ||
            handle_map[vm->ui_default_window_size_node_handle] <= 0) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid ui window size node handle during node GC.");
            return 0;
        }
        vm->ui_default_window_size_node_handle = handle_map[vm->ui_default_window_size_node_handle];
    }
    if (vm->ui_empty_event_node_handle > 0) {
        if (vm->ui_empty_event_node_handle > (int64_t)AIVM_VM_NODE_CAPACITY ||
            handle_map[vm->ui_empty_event_node_handle] <= 0) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid ui event node handle during node GC.");
            return 0;
        }
        vm->ui_empty_event_node_handle = handle_map[vm->ui_empty_event_node_handle];
    }
    if (out_handle_map != NULL) {
        memcpy(out_handle_map, handle_map, sizeof(handle_map));
    }
    return 1;
}

static int remap_child_handles_for_compaction(
    AivmVm* vm,
    int64_t* remapped_children,
    const int64_t* children,
    size_t child_count,
    const int64_t* handle_map)
{
    size_t i;
    (void)vm;
    if (child_count == 0U) {
        return 1;
    }
    if (remapped_children == NULL || children == NULL || handle_map == NULL) {
        return 0;
    }
    for (i = 0U; i < child_count; i += 1U) {
        int64_t handle = children[i];
        if (handle <= 0 || handle > (int64_t)AIVM_VM_NODE_CAPACITY || handle_map[handle] <= 0) {
            return 0;
        }
        remapped_children[i] = handle_map[handle];
    }
    return 1;
}

static int should_attempt_proactive_node_gc(const AivmVm* vm)
{
    if (vm == NULL) {
        return 0;
    }
    if (vm->node_count < AIVM_VM_NODE_GC_PRESSURE_THRESHOLD) {
        return 0;
    }
    if (vm->node_allocations_since_gc < AIVM_VM_NODE_GC_INTERVAL_ALLOCATIONS) {
        return 0;
    }
    return 1;
}

static int create_node_record(
    AivmVm* vm,
    const char* kind,
    const char* id,
    const AivmNodeAttr* attrs,
    size_t attr_count,
    const int64_t* children,
    size_t child_count,
    int64_t* out_handle)
{
    AivmNodeRecord* node;
    int64_t remapped_children[AIVM_VM_NODE_CHILD_CAPACITY];
    const int64_t* effective_children = children;
    int64_t handle_map[AIVM_VM_NODE_CAPACITY + 1U];
    size_t needed_attr_count = 0U;
    size_t needed_child_count = 0U;
    size_t needed_node_count = 0U;
    size_t i;
    if (vm == NULL || kind == NULL || id == NULL || out_handle == NULL) {
        return 0;
    }
    if (should_attempt_proactive_node_gc(vm)) {
        if (!compact_node_arenas_with_map(vm, children, child_count, handle_map)) {
            return 0;
        }
        if (!remap_child_handles_for_compaction(vm, remapped_children, children, child_count, handle_map)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid child handle remap during proactive node GC.");
            return 0;
        }
        if (child_count > 0U) {
            effective_children = remapped_children;
        }
        vm->node_allocations_since_gc = 0U;
    }
    if (!size_add_checked(vm->node_attr_count, attr_count, &needed_attr_count) ||
        !size_add_checked(vm->node_child_count, child_count, &needed_child_count) ||
        !size_add_checked(vm->node_count, 1U, &needed_node_count)) {
        increment_counter_saturating(&vm->node_arena_pressure_count);
        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node arena capacity exceeded.");
        return 0;
    }
    if (needed_node_count > AIVM_VM_NODE_CAPACITY ||
        needed_attr_count > AIVM_VM_NODE_ATTR_CAPACITY ||
        needed_child_count > AIVM_VM_NODE_CHILD_CAPACITY) {
        if (!compact_node_arenas_with_map(vm, effective_children, child_count, handle_map)) {
            return 0;
        }
        if (!remap_child_handles_for_compaction(vm, remapped_children, effective_children, child_count, handle_map)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid child handle remap during node GC.");
            return 0;
        }
        if (child_count > 0U) {
            effective_children = remapped_children;
        }
        vm->node_allocations_since_gc = 0U;
        if (!size_add_checked(vm->node_attr_count, attr_count, &needed_attr_count) ||
            !size_add_checked(vm->node_child_count, child_count, &needed_child_count) ||
            !size_add_checked(vm->node_count, 1U, &needed_node_count) ||
            needed_node_count > AIVM_VM_NODE_CAPACITY ||
            needed_attr_count > AIVM_VM_NODE_ATTR_CAPACITY ||
            needed_child_count > AIVM_VM_NODE_CHILD_CAPACITY) {
            increment_counter_saturating(&vm->node_arena_pressure_count);
            set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node arena capacity exceeded.");
            return 0;
        }
    }

    node = &vm->nodes[vm->node_count];
    node->kind = copy_string_to_arena(vm, kind);
    node->id = copy_string_to_arena(vm, id);
    if (node->kind == NULL || node->id == NULL) {
        return 0;
    }
    node->attr_start = vm->node_attr_count;
    node->attr_count = attr_count;
    node->child_start = vm->node_child_count;
    node->child_count = child_count;

    for (i = 0U; i < attr_count; i += 1U) {
        AivmNodeAttr attr = attrs[i];
        size_t attr_slot = 0U;
        AivmNodeAttr* out_attr;
        if (!size_add_checked(vm->node_attr_count, i, &attr_slot) ||
            attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY) {
            increment_counter_saturating(&vm->node_arena_pressure_count);
            set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node arena capacity exceeded.");
            return 0;
        }
        out_attr = &vm->node_attrs[attr_slot];
        out_attr->key = copy_string_to_arena(vm, attr.key);
        out_attr->kind = attr.kind;
        if (out_attr->key == NULL) {
            return 0;
        }
        if (attr.kind == AIVM_NODE_ATTR_IDENTIFIER || attr.kind == AIVM_NODE_ATTR_STRING) {
            out_attr->string_value = copy_string_to_arena(vm, attr.string_value == NULL ? "" : attr.string_value);
            if (out_attr->string_value == NULL) {
                return 0;
            }
        } else if (attr.kind == AIVM_NODE_ATTR_INT) {
            out_attr->int_value = attr.int_value;
        } else {
            out_attr->bool_value = attr.bool_value != 0 ? 1 : 0;
        }
    }

    for (i = 0U; i < child_count; i += 1U) {
        size_t child_slot = 0U;
        if (!size_add_checked(vm->node_child_count, i, &child_slot) ||
            child_slot >= AIVM_VM_NODE_CHILD_CAPACITY) {
            increment_counter_saturating(&vm->node_arena_pressure_count);
            set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: node arena capacity exceeded.");
            return 0;
        }
        vm->node_children[child_slot] = effective_children[i];
    }

    vm->node_attr_count = needed_attr_count;
    vm->node_child_count = needed_child_count;
    vm->node_count = needed_node_count;
    if (vm->node_count > vm->node_high_water) {
        vm->node_high_water = vm->node_count;
    }
    if (vm->node_attr_count > vm->node_attr_high_water) {
        vm->node_attr_high_water = vm->node_attr_count;
    }
    if (vm->node_child_count > vm->node_child_high_water) {
        vm->node_child_high_water = vm->node_child_count;
    }
    {
        size_t updated_allocations_since_gc;
        if (size_add_checked(vm->node_allocations_since_gc, 1U, &updated_allocations_since_gc)) {
            vm->node_allocations_since_gc = updated_allocations_since_gc;
        }
    }
    *out_handle = (int64_t)vm->node_count;
    return 1;
}

static size_t write_u64_decimal(char* output, size_t capacity, uint64_t value)
{
    char temp[32];
    size_t count = 0U;
    size_t i;

    if (output == NULL || capacity == 0U) {
        return 0U;
    }

    do {
        uint64_t digit = value % 10U;
        value /= 10U;
        temp[count] = (char)('0' + (char)digit);
        if (!size_add_checked(count, 1U, &count)) {
            return 0U;
        }
    } while (value != 0U && count < sizeof(temp));

    if (!size_add_checked(count, 1U, &i) || i > capacity) {
        return 0U;
    }

    for (i = 0U; i < count; i += 1U) {
        output[i] = temp[count - i - 1U];
    }
    output[count] = '\0';
    return count;
}

static int create_runtime_node_from_value(AivmVm* vm, AivmValue value, int64_t* out_handle)
{
    AivmNodeAttr attrs[3];
    const char* node_kind;
    const char* node_id;
    size_t attr_count;
    int64_t handle;

    if (vm == NULL || out_handle == NULL) {
        return 0;
    }

    if (value.type == AIVM_VAL_NODE) {
        *out_handle = value.node_handle;
        return 1;
    }

    if (value.type == AIVM_VAL_STRING) {
        if (value.string_value == NULL) {
            set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "Runtime string value must be non-null.");
            return 0;
        }
        node_kind = "Lit";
        node_id = "runtime_string";
        attrs[0].key = "value";
        attrs[0].kind = AIVM_NODE_ATTR_STRING;
        attrs[0].string_value = value.string_value;
        attr_count = 1U;
    } else if (value.type == AIVM_VAL_INT) {
        node_kind = "Lit";
        node_id = "runtime_int";
        attrs[0].key = "value";
        attrs[0].kind = AIVM_NODE_ATTR_INT;
        attrs[0].int_value = value.int_value;
        attr_count = 1U;
    } else if (value.type == AIVM_VAL_BOOL) {
        node_kind = "Lit";
        node_id = "runtime_bool";
        attrs[0].key = "value";
        attrs[0].kind = AIVM_NODE_ATTR_BOOL;
        attrs[0].bool_value = value.bool_value != 0 ? 1 : 0;
        attr_count = 1U;
    } else if (value.type == AIVM_VAL_NULL) {
        node_kind = "Null";
        node_id = "runtime_null";
        attr_count = 0U;
    } else if (value.type == AIVM_VAL_BYTES) {
        node_kind = "Lit";
        node_id = "runtime_bytes";
        attr_count = 1U;
        attrs[0].key = "byteLength";
        attrs[0].kind = AIVM_NODE_ATTR_INT;
        attrs[0].int_value = (int64_t)value.bytes_value.length;
    } else if (value.type == AIVM_VAL_VOID) {
        node_kind = "Block";
        node_id = "void";
        attr_count = 0U;
    } else {
        node_kind = "Err";
        node_id = "runtime_err";
        attrs[0].key = "code";
        attrs[0].kind = AIVM_NODE_ATTR_IDENTIFIER;
        attrs[0].string_value = "RUN030";
        attrs[1].key = "message";
        attrs[1].kind = AIVM_NODE_ATTR_STRING;
        attrs[1].string_value = "Unsupported runtime value.";
        attrs[2].key = "nodeId";
        attrs[2].kind = AIVM_NODE_ATTR_IDENTIFIER;
        attrs[2].string_value = "runtime";
        attr_count = 3U;
    }

    if (!create_node_record(vm, node_kind, node_id, attrs, attr_count, NULL, 0U, &handle)) {
        return 0;
    }

    *out_handle = handle;
    return 1;
}

static int initialize_process_argv_node(AivmVm* vm)
{
    int64_t child_handles[AIVM_VM_NODE_CAPACITY];
    AivmNodeAttr value_attr;
    size_t i;
    size_t child_handle_index = 0U;

    if (vm == NULL) {
        return 0;
    }

    vm->process_argv_node_handle = 0;
    if (vm->process_argv_count > AIVM_VM_NODE_CAPACITY) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv exceeds node capacity.");
        return 0;
    }

    for (i = 0U; i < vm->process_argv_count; i += 1U) {
        if (!size_add_checked(i, 2U, &child_handle_index) ||
            child_handle_index > (size_t)INT64_MAX) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv child handle overflow.");
            return 0;
        }
        child_handles[i] = (int64_t)child_handle_index;
    }
    if (!create_node_record(
            vm,
            "Block",
            "argv0",
            NULL,
            0U,
            child_handles,
            vm->process_argv_count,
            &vm->process_argv_node_handle)) {
        return 0;
    }
    if (vm->process_argv_node_handle != 1) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv root handle invariant violated.");
        return 0;
    }

    for (i = 0U; i < vm->process_argv_count; i += 1U) {
        char node_id[40];
        size_t suffix_len;
        const char* arg = vm->process_argv[i];

        memcpy(node_id, "argv_item_", 10U);
        suffix_len = write_u64_decimal(node_id + 10U, sizeof(node_id) - 10U, (uint64_t)i);
        if (suffix_len == 0U) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv node id overflow.");
            return 0;
        }

        value_attr.key = "value";
        value_attr.kind = AIVM_NODE_ATTR_STRING;
        value_attr.string_value = (arg != NULL) ? arg : "";
        if (!create_node_record(vm, "Lit", node_id, &value_attr, 1U, NULL, 0U, &child_handles[i])) {
            return 0;
        }
        if (!size_add_checked(i, 2U, &child_handle_index) ||
            child_handle_index > (size_t)INT64_MAX ||
            child_handles[i] != (int64_t)child_handle_index) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv child handle invariant violated.");
            return 0;
        }
    }

    return 1;
}

void aivm_reset_state(AivmVm* vm)
{
    if (vm == NULL) {
        return;
    }

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
    vm->locals_count = 0U;
    vm->locals_limit = AIVM_VM_LOCALS_INITIAL_CAPACITY;
    vm->string_arena_used = 0U;
    vm->string_arena_limit = AIVM_VM_STRING_ARENA_INITIAL_CAPACITY;
    vm->string_arena[0] = '\0';
    vm->bytes_arena_used = 0U;
    vm->bytes_arena_limit = AIVM_VM_BYTES_ARENA_INITIAL_CAPACITY;
    vm->bytes_arena[0] = 0U;
    vm->completed_task_count = 0U;
    vm->next_task_handle = 1;
    vm->task_reclaim_count = 0U;
    vm->task_reclaim_skip_pinned_count = 0U;
    vm->task_reclaim_exhausted_count = 0U;
    vm->par_context_count = 0U;
    vm->par_value_count = 0U;
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
    vm->process_argv_node_handle = 0;
    vm->ui_default_window_size_node_handle = 0;
    vm->ui_empty_event_node_handle = 0;
    (void)initialize_process_argv_node(vm);
    vm->node_allocations_since_gc = 0U;
}

void aivm_init(AivmVm* vm, const AivmProgram* program)
{
    if (vm == NULL) {
        return;
    }

    vm->program = program;
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
    if (vm == NULL) {
        return;
    }

    vm->program = program;
    vm->syscall_bindings = bindings;
    vm->syscall_binding_count = binding_count;
    vm->process_argv = process_argv;
    vm->process_argv_count = process_argv_count;
    aivm_reset_state(vm);
}

void aivm_halt(AivmVm* vm)
{
    if (vm == NULL || vm->program == NULL) {
        return;
    }

    vm->instruction_pointer = vm->program->instruction_count;
    vm->status = AIVM_VM_STATUS_HALTED;
}

int aivm_stack_push(AivmVm* vm, AivmValue value)
{
    size_t needed = 0U;
    if (vm == NULL) {
        return 0;
    }

    if (!size_add_checked(vm->stack_count, 1U, &needed) ||
        !ensure_stack_capacity(vm, needed)) {
        set_vm_error(vm, AIVM_VM_ERR_STACK_OVERFLOW, "Stack overflow.");
        return 0;
    }

    vm->stack[vm->stack_count] = value;
    vm->stack_count = needed;
    return 1;
}

int aivm_stack_pop(AivmVm* vm, AivmValue* out_value)
{
    if (vm == NULL || out_value == NULL) {
        return 0;
    }

    if (vm->stack_count == 0U) {
        set_vm_error(vm, AIVM_VM_ERR_STACK_UNDERFLOW, "Stack underflow.");
        return 0;
    }

    vm->stack_count -= 1U;
    *out_value = vm->stack[vm->stack_count];
    return 1;
}

int aivm_frame_push(AivmVm* vm, size_t return_instruction_pointer, size_t frame_base)
{
    size_t needed = 0U;
    if (vm == NULL) {
        return 0;
    }
    if (!validate_vm_call_local_state(vm, "frame-push")) {
        return 0;
    }
    if (frame_base > vm->stack_count) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call frame base exceeds stack depth.");
        return 0;
    }

    if (!size_add_checked(vm->call_frame_count, 1U, &needed) ||
        !ensure_call_frame_capacity(vm, needed)) {
        set_vm_error(vm, AIVM_VM_ERR_FRAME_OVERFLOW, "Call-frame overflow.");
        return 0;
    }

    vm->call_frames[vm->call_frame_count].return_instruction_pointer = return_instruction_pointer;
    vm->call_frames[vm->call_frame_count].frame_base = frame_base;
    vm->call_frames[vm->call_frame_count].locals_base = vm->locals_count;
    vm->call_frame_count = needed;
    return 1;
}

int aivm_frame_pop(AivmVm* vm, AivmCallFrame* out_frame)
{
    if (vm == NULL || out_frame == NULL) {
        return 0;
    }
    if (!validate_vm_call_local_state(vm, "frame-pop")) {
        return 0;
    }

    if (vm->call_frame_count == 0U) {
        set_vm_error(vm, AIVM_VM_ERR_FRAME_UNDERFLOW, "Call-frame underflow.");
        return 0;
    }

    vm->call_frame_count -= 1U;
    *out_frame = vm->call_frames[vm->call_frame_count];
    if (!validate_vm_frame_record(vm, out_frame, "frame-pop")) {
        return 0;
    }
    return 1;
}

int aivm_local_set(AivmVm* vm, size_t index, AivmValue value)
{
    size_t base = 0U;
    size_t absolute_index;
    size_t needed = 0U;
    if (vm == NULL) {
        return 0;
    }
    if (!validate_vm_call_local_state(vm, "local-set")) {
        return 0;
    }

    if (vm->call_frame_count > 0U) {
        base = vm->call_frames[vm->call_frame_count - 1U].locals_base;
    }
    if (base >= AIVM_VM_LOCALS_CAPACITY || index >= (AIVM_VM_LOCALS_CAPACITY - base)) {
        set_vm_local_out_of_range_error(vm, "store", index, base);
        return 0;
    }
    if (!size_add_checked(base, index, &absolute_index) ||
        !size_add_checked(absolute_index, 1U, &needed) ||
        !ensure_locals_capacity(vm, needed)) {
        set_vm_local_out_of_range_error(vm, "store", index, base);
        return 0;
    }
    vm->locals[absolute_index] = value;
    if (absolute_index >= vm->locals_count) {
        vm->locals_count = needed;
    }
    return 1;
}

int aivm_local_get(const AivmVm* vm, size_t index, AivmValue* out_value)
{
    size_t base = 0U;
    size_t absolute_index;
    if (vm == NULL || out_value == NULL) {
        return 0;
    }
    if (((AivmVm*)vm)->stack_count > ((AivmVm*)vm)->stack_limit ||
        ((AivmVm*)vm)->call_frame_count > ((AivmVm*)vm)->call_frame_limit ||
        ((AivmVm*)vm)->locals_count > ((AivmVm*)vm)->locals_limit) {
        return 0;
    }

    if (vm->call_frame_count > 0U) {
        base = vm->call_frames[vm->call_frame_count - 1U].locals_base;
        if (vm->call_frames[vm->call_frame_count - 1U].frame_base > vm->stack_count ||
            base > vm->locals_count) {
            return 0;
        }
    }
    if (base >= AIVM_VM_LOCALS_CAPACITY || index >= (AIVM_VM_LOCALS_CAPACITY - base)) {
        return 0;
    }
    if (!size_add_checked(base, index, &absolute_index)) {
        return 0;
    }
    if (absolute_index >= vm->locals_count) {
        return 0;
    }

    *out_value = vm->locals[absolute_index];
    return 1;
}

static size_t infer_call_arg_count(const AivmProgram* program, size_t target)
{
    size_t index = target;
    size_t count = 0U;
    size_t next_index;
    size_t next_count;
    if (program == NULL || program->instructions == NULL || target >= program->instruction_count) {
        return 0U;
    }
    while (index < program->instruction_count && program->instructions[index].opcode == AIVM_OP_STORE_LOCAL) {
        if (!size_add_checked(count, 1U, &next_count) ||
            !size_add_checked(index, 1U, &next_index)) {
            return 0U;
        }
        count = next_count;
        index = next_index;
    }
    return count;
}

static int validate_call_target_layout(
    AivmVm* vm,
    const AivmProgram* program,
    size_t target,
    size_t arg_count)
{
    size_t i;
    size_t seen[64];
    size_t seen_count = 0U;
    size_t next_seen_count;
    if (vm == NULL || program == NULL || program->instructions == NULL) {
        return 0;
    }
    if (arg_count > (sizeof(seen) / sizeof(seen[0]))) {
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "Call target layout invalid. target=%llu argCount=%llu exceeds checked layout window",
            (unsigned long long)target,
            (unsigned long long)arg_count);
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    for (i = 0U; i < arg_count; i += 1U) {
        size_t local_index = 0U;
        size_t instruction_index = 0U;
        size_t j;
        const AivmInstruction* instruction;
        if (!size_add_checked(target, i, &instruction_index) ||
            instruction_index >= program->instruction_count) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call target layout exceeded instruction range.");
            return 0;
        }
        instruction = &program->instructions[instruction_index];
        if (instruction->opcode != AIVM_OP_STORE_LOCAL) {
            (void)snprintf(
                vm->error_detail_storage,
                sizeof(vm->error_detail_storage),
                "Call target layout invalid. target=%llu arg=%llu op=%d expected=STORE_LOCAL",
                (unsigned long long)target,
                (unsigned long long)i,
                (int)instruction->opcode);
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
            return 0;
        }
        if (!operand_to_index(vm, instruction->operand_int, &local_index)) {
            return 0;
        }
        for (j = 0U; j < seen_count; j += 1U) {
            if (seen[j] == local_index) {
                (void)snprintf(
                    vm->error_detail_storage,
                    sizeof(vm->error_detail_storage),
                    "Call target local layout invalid. target=%llu arg=%llu duplicateLocal=%llu argCount=%llu",
                    (unsigned long long)target,
                    (unsigned long long)i,
                    (unsigned long long)local_index,
                    (unsigned long long)arg_count);
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
                return 0;
            }
        }
        seen[seen_count] = local_index;
        if (!size_add_checked(seen_count, 1U, &next_seen_count)) {
            set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call target local layout count overflow.");
            return 0;
        }
        seen_count = next_seen_count;
    }
    return 1;
}

void aivm_step(AivmVm* vm)
{
    const AivmInstruction* instruction;

    if (vm == NULL || vm->program == NULL) {
        return;
    }

    if (vm->program->instructions == NULL) {
        if (vm->program->instruction_count == 0U) {
            vm->status = AIVM_VM_STATUS_HALTED;
            return;
        }
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Program instruction buffer is null.");
        vm->instruction_pointer = vm->program->instruction_count;
        return;
    }

    if (vm->status == AIVM_VM_STATUS_HALTED || vm->status == AIVM_VM_STATUS_ERROR) {
        return;
    }

    if (vm->instruction_pointer >= vm->program->instruction_count) {
        vm->status = AIVM_VM_STATUS_HALTED;
        return;
    }

    vm->status = AIVM_VM_STATUS_RUNNING;
    vm->error_detail = NULL;
    instruction = &vm->program->instructions[vm->instruction_pointer];
    record_recent_opcode(vm, vm->instruction_pointer, (int)instruction->opcode, vm->stack_count);

    switch (instruction->opcode) {
        case AIVM_OP_NOP:
            vm->instruction_pointer += 1U;
            break;

        case AIVM_OP_HALT:
            aivm_halt(vm);
            break;

        case AIVM_OP_STUB:
            set_vm_error(vm, AIVM_VM_ERR_INVALID_OPCODE, "STUB opcode is invalid at runtime.");
            break;

        case AIVM_OP_PUSH_INT:
            if (!aivm_stack_push(vm, aivm_value_int(instruction->operand_int))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;

        case AIVM_OP_POP: {
            AivmValue popped;
            if (!aivm_stack_pop(vm, &popped)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_STORE_LOCAL: {
            AivmValue popped;
            size_t local_index;
            if (!operand_to_index(vm, instruction->operand_int, &local_index)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_pop(vm, &popped)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_local_set(vm, local_index, popped)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_LOAD_LOCAL: {
            AivmValue local_value;
            size_t local_index;
            if (!operand_to_index(vm, instruction->operand_int, &local_index)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_local_get(vm, local_index, &local_value)) {
                size_t locals_base = 0U;
                if (vm->call_frame_count > 0U) {
                    locals_base = vm->call_frames[vm->call_frame_count - 1U].locals_base;
                }
                set_vm_local_out_of_range_error(vm, "load", local_index, locals_base);
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, local_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_ADD_INT: {
            AivmValue right;
            AivmValue left;
            if (!aivm_stack_pop(vm, &right) || !aivm_stack_pop(vm, &left)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (left.type != AIVM_VAL_INT || right.type != AIVM_VAL_INT) {
                set_vm_error_add_int_type_mismatch(vm, left, right);
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_int(left.int_value + right.int_value))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_JUMP: {
            size_t target;
            if (!operand_to_index(vm, instruction->operand_int, &target)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (target > vm->program->instruction_count) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Jump target out of range.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer = target;
            break;
        }

        case AIVM_OP_JUMP_IF_FALSE: {
            AivmValue condition;
            size_t target;
            if (!operand_to_index(vm, instruction->operand_int, &target)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_pop(vm, &condition)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (condition.type != AIVM_VAL_BOOL) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "JUMP_IF_FALSE requires bool.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (condition.bool_value == 0) {
                if (target > vm->program->instruction_count) {
                    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Jump target out of range.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                vm->instruction_pointer = target;
            } else {
                vm->instruction_pointer += 1U;
            }
            break;
        }

        case AIVM_OP_PUSH_BOOL:
            if (!aivm_stack_push(vm, aivm_value_bool((instruction->operand_int != 0) ? 1 : 0))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;

        case AIVM_OP_CALL: {
            size_t target;
            size_t arg_count = 0U;
            size_t frame_base = 0U;
            size_t return_ip = 0U;
            if (!operand_to_index(vm, instruction->operand_int, &target)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (target >= vm->program->instruction_count) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call target out of range.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            arg_count = infer_call_arg_count(vm->program, target);
            if (arg_count > vm->stack_count) {
                set_vm_error_call_arg_depth(vm, target, arg_count, vm->stack_count);
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!validate_call_target_layout(vm, vm->program, target, arg_count)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            record_recent_call(vm, vm->instruction_pointer, target, arg_count, vm->stack_count);
            frame_base = vm->stack_count - arg_count;
            if (!size_add_checked(vm->instruction_pointer, 1U, &return_ip) ||
                !aivm_frame_push(vm, return_ip, frame_base)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer = target;
            break;
        }

        case AIVM_OP_RET:
        case AIVM_OP_RETURN: {
            AivmCallFrame frame;
            AivmValue return_value = aivm_value_void();
            int has_return_value = 0;
            size_t pre_restore_stack_count = 0U;
            if (vm->call_frame_count == 0U) {
                aivm_halt(vm);
                break;
            }
            if (!aivm_frame_pop(vm, &frame)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (frame.return_instruction_pointer > vm->program->instruction_count) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Return instruction pointer out of range.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (vm->stack_count < frame.frame_base) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call frame base exceeds stack depth.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            pre_restore_stack_count = vm->stack_count;
            if (vm->stack_count > frame.frame_base) {
                return_value = vm->stack[vm->stack_count - 1U];
                has_return_value = 1;
            }
            vm->stack_count = frame.frame_base;
            vm->locals_count = frame.locals_base;
            if (!validate_vm_return_restore(vm, &frame, pre_restore_stack_count)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (has_return_value != 0) {
                if (!aivm_stack_push(vm, return_value)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            }
            record_recent_return(
                vm,
                frame.return_instruction_pointer,
                vm->stack_count,
                pre_restore_stack_count,
                frame.frame_base,
                has_return_value);
            vm->instruction_pointer = frame.return_instruction_pointer;
            break;
        }

        case AIVM_OP_EQ_INT: {
            AivmValue right;
            AivmValue left;
            if (!aivm_stack_pop(vm, &right) || !aivm_stack_pop(vm, &left)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (left.type != AIVM_VAL_INT || right.type != AIVM_VAL_INT) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "EQ_INT requires int operands.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_bool(left.int_value == right.int_value ? 1 : 0))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_EQ: {
            AivmValue right;
            AivmValue left;
            if (!aivm_stack_pop(vm, &right) || !aivm_stack_pop(vm, &left)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_bool(aivm_value_equals(left, right)))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_CONST: {
            size_t constant_index;
            if (!operand_to_index(vm, instruction->operand_int, &constant_index)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (vm->program->constants == NULL || constant_index >= vm->program->constant_count) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid CONST index.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, vm->program->constants[constant_index])) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_STR_CONCAT: {
            AivmValue right;
            AivmValue left;
            size_t left_length = 0U;
            size_t right_length = 0U;
            size_t next_length;
            size_t total_length = 0U;
            size_t bytes_needed = 0U;
            size_t i;
            char* output;

            if (!aivm_stack_pop(vm, &right) || !aivm_stack_pop(vm, &left)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (left.type != AIVM_VAL_STRING ||
                right.type != AIVM_VAL_STRING ||
                left.string_value == NULL ||
                right.string_value == NULL) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_CONCAT requires string operands.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            while (left.string_value[left_length] != '\0') {
                if (!size_add_checked(left_length, 1U, &next_length)) {
                    set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String concat left length overflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                left_length = next_length;
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                break;
            }
            while (right.string_value[right_length] != '\0') {
                if (!size_add_checked(right_length, 1U, &next_length)) {
                    set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String concat right length overflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                right_length = next_length;
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                break;
            }

            if (!size_add_checked(left_length, right_length, &total_length) ||
                !size_add_checked(total_length, 1U, &bytes_needed)) {
                set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String concat size arithmetic overflow.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            output = arena_alloc(vm, bytes_needed);
            if (output == NULL) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            for (i = 0U; i < left_length; i += 1U) {
                output[i] = left.string_value[i];
            }
            for (i = 0U; i < right_length; i += 1U) {
                size_t output_slot = 0U;
                if (!size_add_checked(left_length, i, &output_slot) ||
                    output_slot >= total_length) {
                    set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String concat output slot overflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                output[output_slot] = right.string_value[i];
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                break;
            }
            output[left_length + right_length] = '\0';

            if (!aivm_stack_push(vm, aivm_value_string(output))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_TO_STRING: {
            AivmValue value;
            char bool_buffer[6];
            char int_buffer[32];
            char* bytes_output;
            size_t int_index;
            uint64_t magnitude;
            int negative = 0;

            if (!aivm_stack_pop(vm, &value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            if (value.type == AIVM_VAL_STRING) {
                if (value.string_value == NULL || !push_string_copy(vm, value.string_value)) {
                    set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "TO_STRING input string must be non-null.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                vm->instruction_pointer += 1U;
                break;
            }
            if (value.type == AIVM_VAL_BOOL) {
                if (value.bool_value != 0) {
                    bool_buffer[0] = 't';
                    bool_buffer[1] = 'r';
                    bool_buffer[2] = 'u';
                    bool_buffer[3] = 'e';
                    bool_buffer[4] = '\0';
                } else {
                    bool_buffer[0] = 'f';
                    bool_buffer[1] = 'a';
                    bool_buffer[2] = 'l';
                    bool_buffer[3] = 's';
                    bool_buffer[4] = 'e';
                    bool_buffer[5] = '\0';
                }
                if (!push_string_copy(vm, bool_buffer)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                vm->instruction_pointer += 1U;
                break;
            }
            if (value.type == AIVM_VAL_VOID) {
                if (!push_string_copy(vm, "null")) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                vm->instruction_pointer += 1U;
                break;
            }
            if (value.type == AIVM_VAL_NULL) {
                if (!push_string_copy(vm, "null")) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                vm->instruction_pointer += 1U;
                break;
            }
            if (value.type == AIVM_VAL_INT) {
                int_index = sizeof(int_buffer) - 1U;
                int_buffer[int_index] = '\0';
                if (value.int_value < 0) {
                    negative = 1;
                    magnitude = (uint64_t)(-(value.int_value + 1)) + 1U;
                } else {
                    magnitude = (uint64_t)value.int_value;
                }
                do {
                    uint64_t digit = magnitude % 10U;
                    magnitude /= 10U;
                    int_index -= 1U;
                    int_buffer[int_index] = (char)('0' + (char)digit);
                } while (magnitude != 0U);
                if (negative != 0) {
                    int_index -= 1U;
                    int_buffer[int_index] = '-';
                }
                if (!push_string_copy(vm, &int_buffer[int_index])) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                vm->instruction_pointer += 1U;
                break;
            }
            if (value.type == AIVM_VAL_BYTES) {
                static const char hex[] = "0123456789abcdef";
                size_t i;
                size_t body_len = 0U;
                size_t out_len = 0U;
                size_t bytes_needed = 0U;
                if (value.bytes_value.length > 0U && value.bytes_value.data == NULL) {
                    set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "TO_STRING bytes data must be non-null.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                if (!size_add_checked(value.bytes_value.length, value.bytes_value.length, &body_len) ||
                    !size_add_checked(2U, body_len, &out_len) ||
                    !size_add_checked(out_len, 1U, &bytes_needed)) {
                    set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "TO_STRING bytes size arithmetic overflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                bytes_output = arena_alloc(vm, bytes_needed);
                if (bytes_output == NULL) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                bytes_output[0] = '0';
                bytes_output[1] = 'x';
                for (i = 0U; i < value.bytes_value.length; i += 1U) {
                    uint8_t b = value.bytes_value.data[i];
                    size_t body_offset = 0U;
                    size_t high_slot = 0U;
                    size_t low_slot = 0U;
                    if (!size_add_checked(i, i, &body_offset) ||
                        !size_add_checked(2U, body_offset, &high_slot) ||
                        !size_add_checked(high_slot, 1U, &low_slot) ||
                        low_slot >= out_len) {
                        set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "TO_STRING bytes output slot overflow.");
                        vm->instruction_pointer = vm->program->instruction_count;
                        break;
                    }
                    bytes_output[high_slot] = hex[(b >> 4U) & 0x0fU];
                    bytes_output[low_slot] = hex[b & 0x0fU];
                }
                if (vm->instruction_pointer == vm->program->instruction_count) {
                    break;
                }
                bytes_output[out_len] = '\0';
                if (!aivm_stack_push(vm, aivm_value_string(bytes_output))) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                vm->instruction_pointer += 1U;
                break;
            }

            set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "TO_STRING unsupported value kind.");
            vm->instruction_pointer = vm->program->instruction_count;
            break;
        }

        case AIVM_OP_STR_ESCAPE: {
            AivmValue value;
            if (!aivm_stack_pop(vm, &value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (value.type != AIVM_VAL_STRING || value.string_value == NULL) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_ESCAPE requires string operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!push_escaped_string(vm, value.string_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_STR_SUBSTRING: {
            AivmValue length_value;
            AivmValue start_value;
            AivmValue text_value;
            if (!aivm_stack_pop(vm, &length_value) ||
                !aivm_stack_pop(vm, &start_value) ||
                !aivm_stack_pop(vm, &text_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (text_value.type != AIVM_VAL_STRING ||
                start_value.type != AIVM_VAL_INT ||
                length_value.type != AIVM_VAL_INT) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_SUBSTRING requires (string,int,int).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!push_substring_by_runes(vm, text_value.string_value, start_value.int_value, length_value.int_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_STR_REMOVE: {
            AivmValue length_value;
            AivmValue start_value;
            AivmValue text_value;
            if (!aivm_stack_pop(vm, &length_value) ||
                !aivm_stack_pop(vm, &start_value) ||
                !aivm_stack_pop(vm, &text_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (text_value.type != AIVM_VAL_STRING ||
                start_value.type != AIVM_VAL_INT ||
                length_value.type != AIVM_VAL_INT) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_REMOVE requires (string,int,int).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!push_remove_by_runes(vm, text_value.string_value, start_value.int_value, length_value.int_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_CALL_SYS: {
            size_t arg_count;
            AivmValue result;

            if (!operand_to_index(vm, instruction->operand_int, &arg_count)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!call_sys_with_arity(vm, arg_count, &result)) {
                break;
            }
            if (!aivm_stack_push(vm, result)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_ASYNC_CALL: {
            size_t target;
            AivmValue result;
            if (!operand_to_index(vm, instruction->operand_int, &target)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!execute_call_subroutine_sync(vm, target, &result)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!push_completed_task(vm, result)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            break;
        }

        case AIVM_OP_ASYNC_CALL_SYS: {
            size_t arg_count;
            AivmValue result;
            if (!operand_to_index(vm, instruction->operand_int, &arg_count)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!call_sys_with_arity(vm, arg_count, &result)) {
                break;
            }
            if (!push_completed_task(vm, result)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_AWAIT: {
            AivmValue handle_value;
            AivmValue completed;
            if (!aivm_stack_pop(vm, &handle_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (handle_value.type != AIVM_VAL_INT) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "AWAIT requires valid task handle.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!find_terminal_task_result(vm, handle_value.int_value, &completed)) {
                if (vm->status != AIVM_VM_STATUS_ERROR) {
                    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "AWAIT requires valid task handle.");
                }
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, completed)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_PAR_BEGIN: {
            size_t expected_count;
            size_t needed_context_count = 0U;
            if (!operand_to_index(vm, instruction->operand_int, &expected_count)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!size_add_checked(vm->par_context_count, 1U, &needed_context_count) ||
                needed_context_count > AIVM_VM_PAR_CONTEXT_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_BEGIN exceeded context capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->par_contexts[vm->par_context_count].expected_count = expected_count;
            vm->par_contexts[vm->par_context_count].start_index = vm->par_value_count;
            vm->par_context_count = needed_context_count;
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_PAR_FORK: {
            AivmValue value;
            size_t needed_value_count = 0U;
            if (vm->par_context_count == 0U) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_FORK requires active Par context.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!size_add_checked(vm->par_value_count, 1U, &needed_value_count) ||
                needed_value_count > AIVM_VM_PAR_VALUE_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_FORK exceeded value capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_pop(vm, &value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->par_values[vm->par_value_count] = value;
            vm->par_value_count = needed_value_count;
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_PAR_JOIN: {
            AivmParContext context;
            size_t join_count;
            int64_t child_handles[AIVM_VM_NODE_CHILD_CAPACITY];
            char id_buffer[32];
            size_t id_length;
            size_t i;
            int64_t block_handle;
            if (!operand_to_index(vm, instruction->operand_int, &join_count)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (vm->par_context_count == 0U) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN requires active Par context.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            context = vm->par_contexts[vm->par_context_count - 1U];
            if (context.expected_count != join_count ||
                vm->par_value_count < context.start_index ||
                (vm->par_value_count - context.start_index) != join_count) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN branch count mismatch.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (join_count > AIVM_VM_NODE_CHILD_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN exceeded child capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            for (i = 0U; i < join_count; i += 1U) {
                size_t par_index = 0U;
                AivmValue value;
                AivmValue task_result;
                int64_t child_handle;
                if (!size_add_checked(context.start_index, i, &par_index) ||
                    par_index >= vm->par_value_count) {
                    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN value index was invalid.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                value = vm->par_values[par_index];
                if (value.type == AIVM_VAL_INT &&
                    find_terminal_task_result(vm, value.int_value, &task_result)) {
                    value = task_result;
                }
                if (!create_runtime_node_from_value(vm, value, &child_handle)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                child_handles[i] = child_handle;
            }
            if (vm->status == AIVM_VM_STATUS_ERROR) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            id_buffer[0] = 'p';
            id_buffer[1] = 'a';
            id_buffer[2] = 'r';
            id_buffer[3] = '_';
            id_length = write_u64_decimal(&id_buffer[4], sizeof(id_buffer) - 4U, (uint64_t)vm->next_par_node_id);
            if (id_length == 0U) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN failed to build block id.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->next_par_node_id += 1;
            if (!create_node_record(vm, "Block", id_buffer, NULL, 0U, child_handles, join_count, &block_handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->par_context_count -= 1U;
            vm->par_value_count = context.start_index;
            if (!aivm_stack_push(vm, aivm_value_node(block_handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_PAR_CANCEL:
            vm->instruction_pointer += 1U;
            break;

        case AIVM_OP_STR_UTF8_BYTE_COUNT: {
            AivmValue value;
            int64_t count = 0;
            int64_t next_count = 0;
            if (!aivm_stack_pop(vm, &value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (value.type != AIVM_VAL_STRING || value.string_value == NULL) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_UTF8_BYTE_COUNT requires string operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            while (value.string_value[count] != '\0') {
                if (count == INT64_MAX) {
                    set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "STR_UTF8_BYTE_COUNT overflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                next_count = count + 1;
                count = next_count;
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_int(count))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_NODE_KIND: {
            AivmValue node_value;
            const AivmNodeRecord* node;
            if (!aivm_stack_pop(vm, &node_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_value.type != AIVM_VAL_NODE || !lookup_node(vm, node_value.node_handle, &node)) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "NODE_KIND requires node operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!push_string_copy(vm, node->kind)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_NODE_ID: {
            AivmValue node_value;
            const AivmNodeRecord* node;
            if (!aivm_stack_pop(vm, &node_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_value.type != AIVM_VAL_NODE || !lookup_node(vm, node_value.node_handle, &node)) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "NODE_ID requires node operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!push_string_copy(vm, node->id)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_ATTR_COUNT: {
            AivmValue node_value;
            const AivmNodeRecord* node;
            if (!aivm_stack_pop(vm, &node_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_value.type != AIVM_VAL_NODE || !lookup_node(vm, node_value.node_handle, &node)) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "ATTR_COUNT requires node operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_int((int64_t)node->attr_count))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_ATTR_KEY:
        case AIVM_OP_ATTR_VALUE_KIND:
        case AIVM_OP_ATTR_VALUE_STRING:
        case AIVM_OP_ATTR_VALUE_INT:
        case AIVM_OP_ATTR_VALUE_BOOL: {
            AivmValue index_value;
            AivmValue node_value;
            const AivmNodeRecord* node;
            const AivmNodeAttr* attr = NULL;
            int has_attr = 0;
            if (!aivm_stack_pop(vm, &index_value) || !aivm_stack_pop(vm, &node_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_value.type != AIVM_VAL_NODE || index_value.type != AIVM_VAL_INT || !lookup_node(vm, node_value.node_handle, &node)) {
                const char* attr_error = "ATTR_KEY requires (node,int).";
                if (instruction->opcode == AIVM_OP_ATTR_VALUE_KIND) {
                    attr_error = "ATTR_VALUE_KIND requires (node,int).";
                } else if (instruction->opcode == AIVM_OP_ATTR_VALUE_STRING) {
                    attr_error = "ATTR_VALUE_STRING requires (node,int).";
                } else if (instruction->opcode == AIVM_OP_ATTR_VALUE_INT) {
                    attr_error = "ATTR_VALUE_INT requires (node,int).";
                } else if (instruction->opcode == AIVM_OP_ATTR_VALUE_BOOL) {
                    attr_error = "ATTR_VALUE_BOOL requires (node,int).";
                }
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, attr_error);
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (index_value.int_value >= 0 && (size_t)index_value.int_value < node->attr_count) {
                attr = &vm->node_attrs[node->attr_start + (size_t)index_value.int_value];
                has_attr = 1;
            }

            if (instruction->opcode == AIVM_OP_ATTR_KEY) {
                if (!push_string_copy(vm, has_attr != 0 ? attr->key : "")) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else if (instruction->opcode == AIVM_OP_ATTR_VALUE_KIND) {
                const char* kind_text = "";
                if (has_attr != 0) {
                    if (attr->kind == AIVM_NODE_ATTR_IDENTIFIER) {
                        kind_text = "identifier";
                    } else if (attr->kind == AIVM_NODE_ATTR_STRING) {
                        kind_text = "string";
                    } else if (attr->kind == AIVM_NODE_ATTR_INT) {
                        kind_text = "int";
                    } else if (attr->kind == AIVM_NODE_ATTR_BOOL) {
                        kind_text = "bool";
                    }
                }
                if (!push_string_copy(vm, kind_text)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else if (instruction->opcode == AIVM_OP_ATTR_VALUE_STRING) {
                const char* value_text = "";
                if (has_attr != 0 &&
                    (attr->kind == AIVM_NODE_ATTR_IDENTIFIER || attr->kind == AIVM_NODE_ATTR_STRING) &&
                    attr->string_value != NULL) {
                    value_text = attr->string_value;
                }
                if (!push_string_copy(vm, value_text)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else if (instruction->opcode == AIVM_OP_ATTR_VALUE_INT) {
                int64_t value_int = 0;
                if (has_attr != 0 && attr->kind == AIVM_NODE_ATTR_INT) {
                    value_int = attr->int_value;
                }
                if (!aivm_stack_push(vm, aivm_value_int(value_int))) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else {
                int value_bool = 0;
                if (has_attr != 0 && attr->kind == AIVM_NODE_ATTR_BOOL) {
                    value_bool = attr->bool_value != 0 ? 1 : 0;
                }
                if (!aivm_stack_push(vm, aivm_value_bool(value_bool))) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_CHILD_COUNT: {
            AivmValue node_value;
            const AivmNodeRecord* node;
            if (!aivm_stack_pop(vm, &node_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_value.type != AIVM_VAL_NODE || !lookup_node(vm, node_value.node_handle, &node)) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "CHILD_COUNT requires node operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_int((int64_t)node->child_count))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_CHILD_AT: {
            AivmValue index_value;
            AivmValue node_value;
            const AivmNodeRecord* node;
            int64_t child_handle;
            if (!aivm_stack_pop(vm, &index_value) || !aivm_stack_pop(vm, &node_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_value.type != AIVM_VAL_NODE || index_value.type != AIVM_VAL_INT || !lookup_node(vm, node_value.node_handle, &node)) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "CHILD_AT requires (node,int).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (index_value.int_value < 0 || (size_t)index_value.int_value >= node->child_count) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "CHILD_AT index out of range.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            child_handle = vm->node_children[node->child_start + (size_t)index_value.int_value];
            if (!aivm_stack_push(vm, aivm_value_node(child_handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_MAKE_BLOCK: {
            AivmValue id_value;
            int64_t handle;
            if (!aivm_stack_pop(vm, &id_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (id_value.type != AIVM_VAL_STRING || id_value.string_value == NULL) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_BLOCK requires string id.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!create_node_record(vm, "Block", id_value.string_value, NULL, 0U, NULL, 0U, &handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_node(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_APPEND_CHILD: {
            AivmValue child_value;
            AivmValue node_value;
            const AivmNodeRecord* base_node;
            const AivmNodeRecord* child_node;
            int64_t child_handle;
            int64_t new_children[AIVM_VM_NODE_CHILD_CAPACITY];
            AivmNodeAttr attrs[AIVM_VM_NODE_ATTR_CAPACITY];
            int64_t handle;
            size_t needed_child_count = 0U;
            size_t i;
            if (!aivm_stack_pop(vm, &child_value) || !aivm_stack_pop(vm, &node_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_value.type != AIVM_VAL_NODE || child_value.type != AIVM_VAL_NODE ||
                !lookup_node(vm, node_value.node_handle, &base_node)) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "APPEND_CHILD requires (node,node).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            child_handle = child_value.node_handle;
            if (!lookup_node(vm, child_handle, &child_node)) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "APPEND_CHILD child node handle was invalid.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            (void)child_node;
            if (base_node->attr_count > AIVM_VM_NODE_ATTR_CAPACITY ||
                !size_add_checked(base_node->child_count, 1U, &needed_child_count) ||
                needed_child_count > AIVM_VM_NODE_CHILD_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_CHILD exceeded VM node capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            for (i = 0U; i < base_node->attr_count; i += 1U) {
                size_t attr_slot = 0U;
                if (!size_add_checked(base_node->attr_start, i, &attr_slot) ||
                    attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY) {
                    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_CHILD attr slot was invalid.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attrs[i] = vm->node_attrs[attr_slot];
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                break;
            }
            for (i = 0U; i < base_node->child_count; i += 1U) {
                size_t child_slot = 0U;
                if (!size_add_checked(base_node->child_start, i, &child_slot) ||
                    child_slot >= AIVM_VM_NODE_CHILD_CAPACITY) {
                    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_CHILD child slot was invalid.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                new_children[i] = vm->node_children[child_slot];
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                break;
            }
            new_children[base_node->child_count] = child_handle;
            if (!create_node_record(
                vm,
                base_node->kind,
                base_node->id,
                attrs,
                base_node->attr_count,
                new_children,
                needed_child_count,
                &handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_node(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_MAKE_ERR: {
            AivmValue node_id_value;
            AivmValue message_value;
            AivmValue code_value;
            AivmValue id_value;
            AivmNodeAttr attrs[3];
            int64_t handle;
            if (!aivm_stack_pop(vm, &node_id_value) ||
                !aivm_stack_pop(vm, &message_value) ||
                !aivm_stack_pop(vm, &code_value) ||
                !aivm_stack_pop(vm, &id_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_id_value.type != AIVM_VAL_STRING || node_id_value.string_value == NULL ||
                message_value.type != AIVM_VAL_STRING || message_value.string_value == NULL ||
                code_value.type != AIVM_VAL_STRING || code_value.string_value == NULL ||
                id_value.type != AIVM_VAL_STRING || id_value.string_value == NULL) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_ERR requires (string,string,string,string).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            attrs[0].key = "code";
            attrs[0].kind = AIVM_NODE_ATTR_IDENTIFIER;
            attrs[0].string_value = code_value.string_value;
            attrs[1].key = "message";
            attrs[1].kind = AIVM_NODE_ATTR_STRING;
            attrs[1].string_value = message_value.string_value;
            attrs[2].key = "nodeId";
            attrs[2].kind = AIVM_NODE_ATTR_IDENTIFIER;
            attrs[2].string_value = node_id_value.string_value;
            if (!create_node_record(vm, "Err", id_value.string_value, attrs, 3U, NULL, 0U, &handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_node(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_MAKE_LIT_STRING:
        case AIVM_OP_MAKE_LIT_INT:
        case AIVM_OP_MAKE_LIT_BOOL: {
            AivmValue value;
            AivmValue id_value;
            AivmNodeAttr attr;
            int64_t handle;
            if (!aivm_stack_pop(vm, &value) || !aivm_stack_pop(vm, &id_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            attr.key = "value";
            if (instruction->opcode == AIVM_OP_MAKE_LIT_STRING) {
                if (id_value.type != AIVM_VAL_STRING || id_value.string_value == NULL ||
                    value.type != AIVM_VAL_STRING || value.string_value == NULL) {
                    set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_LIT_STRING requires (string,string).");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attr.kind = AIVM_NODE_ATTR_STRING;
                attr.string_value = value.string_value;
            } else if (instruction->opcode == AIVM_OP_MAKE_LIT_INT) {
                if (id_value.type != AIVM_VAL_STRING || id_value.string_value == NULL ||
                    value.type != AIVM_VAL_INT) {
                    set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_LIT_INT requires (string,int).");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attr.kind = AIVM_NODE_ATTR_INT;
                attr.int_value = value.int_value;
            } else {
                if (id_value.type != AIVM_VAL_STRING || id_value.string_value == NULL ||
                    value.type != AIVM_VAL_BOOL) {
                    set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_LIT_BOOL requires (string,bool).");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attr.kind = AIVM_NODE_ATTR_BOOL;
                attr.bool_value = value.bool_value != 0 ? 1 : 0;
            }
            if (!create_node_record(vm, "Lit", id_value.string_value, &attr, 1U, NULL, 0U, &handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_node(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_MAKE_NODE:
        {
            AivmValue argc_value;
            AivmValue template_value;
            const AivmNodeRecord* template_node;
            AivmNodeAttr attrs[AIVM_VM_NODE_ATTR_CAPACITY];
            int64_t children[AIVM_VM_NODE_CHILD_CAPACITY];
            int64_t handle;
            size_t argc;
            size_t i;

            if (!aivm_stack_pop(vm, &argc_value) ||
                !aivm_stack_pop(vm, &template_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (argc_value.type != AIVM_VAL_INT ||
                argc_value.int_value < 0 ||
                template_value.type != AIVM_VAL_NODE ||
                !lookup_node(vm, template_value.node_handle, &template_node)) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_NODE requires (node,int>=0).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            argc = (size_t)argc_value.int_value;
            if (argc > AIVM_VM_NODE_CHILD_CAPACITY ||
                template_node->attr_count > AIVM_VM_NODE_ATTR_CAPACITY ||
                vm->stack_count < argc) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_NODE arguments exceeded VM limits.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            for (i = 0U; i < template_node->attr_count; i += 1U) {
                size_t attr_slot = 0U;
                if (!size_add_checked(template_node->attr_start, i, &attr_slot) ||
                    attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY) {
                    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_NODE attr slot was invalid.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attrs[i] = vm->node_attrs[attr_slot];
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                break;
            }
            for (i = 0U; i < argc; i += 1U) {
                AivmValue child_value;
                int64_t child_handle;
                size_t child_index = 0U;
                if (!aivm_stack_pop(vm, &child_value)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                if (!create_runtime_node_from_value(vm, child_value, &child_handle)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                if (!size_sub_checked(argc, i + 1U, &child_index)) {
                    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_NODE child index underflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                children[child_index] = child_handle;
            }
            if (vm->status == AIVM_VM_STATUS_ERROR) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            if (!create_node_record(
                vm,
                template_node->kind,
                template_node->id,
                attrs,
                template_node->attr_count,
                children,
                argc,
                &handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_node(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_MAKE_FIELD_STRING: {
            AivmValue value;
            AivmValue key_value;
            AivmNodeAttr attrs[1];
            int64_t child_handle = -1;
            int64_t handle = -1;
            int64_t children[1];
            if (!aivm_stack_pop(vm, &value) || !aivm_stack_pop(vm, &key_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (key_value.type != AIVM_VAL_STRING || key_value.string_value == NULL) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_FIELD_STRING requires (string,any).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!create_runtime_node_from_value(vm, value, &child_handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            attrs[0].key = "key";
            attrs[0].kind = AIVM_NODE_ATTR_STRING;
            attrs[0].string_value = key_value.string_value;
            children[0] = child_handle;
            if (!create_node_record(vm, "Field", "field", attrs, 1U, children, 1U, &handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_node(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_MAKE_MAP: {
            AivmValue count_value;
            int64_t handle = -1;
            int64_t children[AIVM_VM_NODE_CHILD_CAPACITY];
            size_t count = 0U;
            size_t i = 0U;
            if (!aivm_stack_pop(vm, &count_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (count_value.type != AIVM_VAL_INT || count_value.int_value < 0) {
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_MAP requires int child count.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            count = (size_t)count_value.int_value;
            if (count > AIVM_VM_NODE_CHILD_CAPACITY || vm->stack_count < count) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_MAP count exceeded VM limits.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            for (i = 0U; i < count; i += 1U) {
                AivmValue child_value;
                int64_t child_handle = -1;
                size_t child_index = 0U;
                if (!aivm_stack_pop(vm, &child_value)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                if (!create_runtime_node_from_value(vm, child_value, &child_handle)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                if (!size_sub_checked(count, i + 1U, &child_index)) {
                    set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_MAP child index underflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                children[child_index] = child_handle;
            }
            if (vm->status == AIVM_VM_STATUS_ERROR) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!create_node_record(vm, "Map", "map", NULL, 0U, children, count, &handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_node(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        default:
            set_vm_error(vm, AIVM_VM_ERR_INVALID_OPCODE, "Unsupported opcode.");
            vm->instruction_pointer = vm->program->instruction_count;
            break;
    }

    if (vm->status == AIVM_VM_STATUS_RUNNING &&
        vm->instruction_pointer >= vm->program->instruction_count) {
        vm->status = AIVM_VM_STATUS_HALTED;
    }
}

void aivm_run(AivmVm* vm)
{
    if (vm == NULL || vm->program == NULL) {
        return;
    }

    if (vm->program->instruction_count == 0U) {
        vm->status = AIVM_VM_STATUS_HALTED;
        return;
    }

    while (vm->instruction_pointer < vm->program->instruction_count &&
           vm->status != AIVM_VM_STATUS_ERROR &&
           vm->status != AIVM_VM_STATUS_HALTED) {
        aivm_step(vm);
    }
}

const char* aivm_vm_error_code(AivmVmError error)
{
    switch (error) {
        case AIVM_VM_ERR_NONE:
            return "AIVM000";
        case AIVM_VM_ERR_INVALID_OPCODE:
            return "AIVM001";
        case AIVM_VM_ERR_STACK_OVERFLOW:
            return "AIVM002";
        case AIVM_VM_ERR_STACK_UNDERFLOW:
            return "AIVM003";
        case AIVM_VM_ERR_FRAME_OVERFLOW:
            return "AIVM004";
        case AIVM_VM_ERR_FRAME_UNDERFLOW:
            return "AIVM005";
        case AIVM_VM_ERR_LOCAL_OUT_OF_RANGE:
            return "AIVM006";
        case AIVM_VM_ERR_TYPE_MISMATCH:
            return "AIVM007";
        case AIVM_VM_ERR_INVALID_PROGRAM:
            return "AIVM008";
        case AIVM_VM_ERR_STRING_OVERFLOW:
            return "AIVM009";
        case AIVM_VM_ERR_SYSCALL:
            return "AIVM010";
        case AIVM_VM_ERR_MEMORY_PRESSURE:
            return "AIVM011";
        default:
            return "AIVM999";
    }
}

const char* aivm_vm_error_message(AivmVmError error)
{
    switch (error) {
        case AIVM_VM_ERR_NONE:
            return "No error.";
        case AIVM_VM_ERR_INVALID_OPCODE:
            return "Unsupported opcode.";
        case AIVM_VM_ERR_STACK_OVERFLOW:
            return "Stack overflow.";
        case AIVM_VM_ERR_STACK_UNDERFLOW:
            return "Stack underflow.";
        case AIVM_VM_ERR_FRAME_OVERFLOW:
            return "Call frame overflow.";
        case AIVM_VM_ERR_FRAME_UNDERFLOW:
            return "Call frame underflow.";
        case AIVM_VM_ERR_LOCAL_OUT_OF_RANGE:
            return "Local index out of range.";
        case AIVM_VM_ERR_TYPE_MISMATCH:
            return "Type mismatch.";
        case AIVM_VM_ERR_INVALID_PROGRAM:
            return "Invalid program state.";
        case AIVM_VM_ERR_STRING_OVERFLOW:
            return "VM string arena overflow.";
        case AIVM_VM_ERR_SYSCALL:
            return "Syscall dispatch failed.";
        case AIVM_VM_ERR_MEMORY_PRESSURE:
            return "VM memory pressure limit exceeded.";
        default:
            return "Unknown VM error.";
    }
}

const char* aivm_vm_error_detail(const AivmVm* vm)
{
    if (vm == NULL || vm->error_detail == NULL) {
        return "";
    }
    return vm->error_detail;
}
