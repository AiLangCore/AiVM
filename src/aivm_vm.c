#include "aivm_vm.h"
#include "aivm_vm_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct AivmBytecodeWorkerContext AivmBytecodeWorkerContext;
static void free_bytecode_worker_context(AivmBytecodeWorkerContext* context);

static int size_sub_checked(size_t a, size_t b, size_t* out)
{
    if (out == NULL || a < b) {
        return 0;
    }
    *out = a - b;
    return 1;
}

void aivm_vm_set_local_out_of_range_error(
    AivmVm* vm,
    const char* op_name,
    size_t local_index,
    size_t locals_base)
{
    if (vm == NULL) {
        return;
    }
    (void)snprintf(
        vm->error_detail_storage,
        sizeof(vm->error_detail_storage),
        "Invalid local slot. op=%s index=%llu base=%llu localsCount=%llu frameCount=%llu pc=%llu",
        (op_name == NULL || op_name[0] == '\0') ? "local" : op_name,
        (unsigned long long)local_index,
        (unsigned long long)locals_base,
        (unsigned long long)vm->locals_count,
        (unsigned long long)vm->call_frame_count,
        (unsigned long long)vm->instruction_pointer);
    aivm_set_vm_error(vm, AIVM_VM_ERR_LOCAL_OUT_OF_RANGE, vm->error_detail_storage);
}

int aivm_vm_validate_call_local_state(AivmVm* vm, const char* op_name)
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
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    if (vm->call_frame_count > 0U) {
        const AivmCallFrame* frame = &vm->call_frames[vm->call_frame_count - 1U];
        if (frame->frame_base > vm->stack_count || frame->locals_base > vm->locals_count) {
            const AivmCallHistoryEntry* call0 = vm->recent_call_count > 0U ? &vm->recent_calls[0] : NULL;
            const AivmReturnHistoryEntry* return0 = vm->recent_return_count > 0U ? &vm->recent_returns[0] : NULL;
            const AivmOpcodeHistoryEntry* op0 = vm->recent_opcode_count > 0U ? &vm->recent_opcodes[0] : NULL;
            (void)snprintf(
                vm->error_detail_storage,
                sizeof(vm->error_detail_storage),
                "VM frame invariant failed. op=%s frameBase=%llu localsBase=%llu returnIp=%llu stackCount=%llu localsCount=%llu frameCount=%llu pc=%llu call0Ip=%llu call0Target=%llu call0ArgCount=%llu call0Stack=%llu return0Ip=%llu return0Stack=%llu return0PreRestore=%llu return0FrameBase=%llu op0Ip=%llu op0Opcode=%d op0Stack=%llu",
                (op_name == NULL || op_name[0] == '\0') ? "state" : op_name,
                (unsigned long long)frame->frame_base,
                (unsigned long long)frame->locals_base,
                (unsigned long long)frame->return_instruction_pointer,
                (unsigned long long)vm->stack_count,
                (unsigned long long)vm->locals_count,
                (unsigned long long)vm->call_frame_count,
                (unsigned long long)vm->instruction_pointer,
                (unsigned long long)(call0 != NULL ? call0->instruction_pointer : 0U),
                (unsigned long long)(call0 != NULL ? call0->target : 0U),
                (unsigned long long)(call0 != NULL ? call0->arg_count : 0U),
                (unsigned long long)(call0 != NULL ? call0->stack_count : 0U),
                (unsigned long long)(return0 != NULL ? return0->instruction_pointer : 0U),
                (unsigned long long)(return0 != NULL ? return0->stack_count : 0U),
                (unsigned long long)(return0 != NULL ? return0->pre_restore_stack_count : 0U),
                (unsigned long long)(return0 != NULL ? return0->frame_base : 0U),
                (unsigned long long)(op0 != NULL ? op0->instruction_pointer : 0U),
                op0 != NULL ? op0->opcode : 0,
                (unsigned long long)(op0 != NULL ? op0->stack_count : 0U));
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
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
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    return 1;
}

static int syscall_elapsed_limit_applies(const char* target)
{
    if (target == NULL) {
        return 1;
    }
    if (strcmp(target, "sys.ui.waitFrame") == 0) {
        return 0;
    }
    return 1;
}

int aivm_vm_validate_frame_record(
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
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
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
    if (vm->stack_count > vm->stack_limit ||
        vm->call_frame_count > vm->call_frame_limit ||
        vm->locals_count > vm->locals_limit) {
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "VM state invariant failed. op=return-restore stackCount=%llu stackLimit=%llu frameCount=%llu frameLimit=%llu localsCount=%llu localsLimit=%llu",
            (unsigned long long)vm->stack_count,
            (unsigned long long)vm->stack_limit,
            (unsigned long long)vm->call_frame_count,
            (unsigned long long)vm->call_frame_limit,
            (unsigned long long)vm->locals_count,
            (unsigned long long)vm->locals_limit);
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    if (!aivm_size_add_checked(frame->frame_base, 1U, &max_stack_count)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Return restore size arithmetic overflow.");
        return 0;
    }
    if (pre_restore_stack_count > max_stack_count) {
        if (!size_sub_checked(pre_restore_stack_count, max_stack_count, &extra_stack_values)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Return restore size arithmetic overflow.");
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
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
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
    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
}

#if defined(AIVM_DEBUG_RUNTIME)
static void record_profile_syscall(AivmVm* vm, const char* target, double elapsed_seconds)
{
    size_t index;
    if (vm == NULL || target == NULL) {
        return;
    }
    vm->profile_syscall_count += 1U;
    vm->profile_syscall_elapsed_seconds += elapsed_seconds;
    for (index = 0U; index < vm->profile_syscall_target_count; index += 1U) {
        if (strcmp(vm->profile_syscall_targets[index].target, target) == 0) {
            vm->profile_syscall_targets[index].count += 1U;
            vm->profile_syscall_targets[index].elapsed_seconds += elapsed_seconds;
            return;
        }
    }
    if (vm->profile_syscall_target_count >= AIVM_VM_PROFILE_SYSCALL_TARGET_CAPACITY) {
        return;
    }
    index = vm->profile_syscall_target_count;
    (void)snprintf(
        vm->profile_syscall_targets[index].target,
        sizeof(vm->profile_syscall_targets[index].target),
        "%s",
        target);
    vm->profile_syscall_targets[index].count = 1U;
    vm->profile_syscall_targets[index].elapsed_seconds = elapsed_seconds;
    vm->profile_syscall_target_count += 1U;
}
#endif


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
static int call_debug_task_reclaim_stats(AivmVm* vm, AivmValue* out_result);
static size_t write_u64_decimal(char* output, size_t capacity, uint64_t value);
static int is_syscall_target_string(const char* text);
static const char* find_syscall_suffix_target(const char* text);
int aivm_vm_mark_live_scratch_pair_handles(AivmVm* vm, uint8_t* live_pairs);
int aivm_compact_string_arena(AivmVm* vm);
int aivm_compact_bytes_arena(AivmVm* vm);
static int is_terminal_task_state(AivmTaskState state);

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
        if (!aivm_size_add_checked(offset, candidate_length, &offset)) {
            return NULL;
        }
        if (offset < *new_used) {
            if (!aivm_size_add_checked(offset, 1U, &next_offset)) {
                return NULL;
            }
            offset = next_offset;
        }
    }
    length = strlen(text);
    if (!aivm_size_add_checked(length, 1U, &length) ||
        !aivm_size_add_checked(*new_used, length, &offset) ||
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
    if (!aivm_pointer_in_string_arena(vm, *slot)) {
        return 1;
    }
    relocated = compact_lookup_or_copy_string(*slot, new_arena, new_used);
    if (relocated == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
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
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Negative operand is invalid.");
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
        if (!aivm_size_add_checked(len, 1U, &next_len)) {
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
                    if (!aivm_size_add_checked(j, 1U, &next_j)) {
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

static uint8_t* copy_bytes_to_arena(AivmVm* vm, const uint8_t* input, size_t length)
{
    uint8_t* output;
    size_t i;
    if (vm == NULL) {
        return NULL;
    }
    if (length == 0U) {
        return aivm_bytes_arena_alloc(vm, 0U);
    }
    if (input == NULL) {
        return NULL;
    }
    output = aivm_bytes_arena_alloc(vm, length);
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
    output = aivm_vm_copy_string_to_arena(vm, input);
    if (output == NULL) {
        return 0;
    }
    return aivm_stack_push(vm, aivm_value_string(output));
}

static int push_bytes_copy(AivmVm* vm, const uint8_t* input, size_t length)
{
    uint8_t* output;
    output = copy_bytes_to_arena(vm, input, length);
    if (output == NULL && length > 0U) {
        return 0;
    }
    return aivm_stack_push(vm, aivm_value_bytes(output, length));
}

static int push_string_bytes_copy(AivmVm* vm, const uint8_t* input, size_t length)
{
    char* output;
    size_t size;
    size_t i;
    if (!aivm_size_add_checked(length, 1U, &size)) {
        return 0;
    }
    output = aivm_string_arena_alloc(vm, size);
    if (output == NULL) {
        return 0;
    }
    for (i = 0U; i < length; i += 1U) {
        output[i] = (char)input[i];
    }
    output[length] = '\0';
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
            aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "Syscall string result must be non-null.");
            return 0;
        }
        copied_string = aivm_vm_copy_string_to_arena(vm, io_result->string_value);
        if (copied_string == NULL) {
            return 0;
        }
        *io_result = aivm_value_string(copied_string);
        return 1;
    }
    if (io_result->type == AIVM_VAL_BYTES) {
        if (io_result->bytes_value.length > 0U && io_result->bytes_value.data == NULL) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "Syscall bytes result must provide data.");
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
    char* input_copy = NULL;
    const char* source;

    if (vm == NULL || input == NULL) {
        return 0;
    }

    while (input[length] != '\0') {
        char ch = input[length];
        if (ch == '\\' || ch == '"' || ch == '\n' || ch == '\r' || ch == '\t') {
            if (!aivm_size_add_checked(escaped_length, 2U, &escaped_length)) {
                return 0;
            }
        } else {
            if (!aivm_size_add_checked(escaped_length, 1U, &escaped_length)) {
                return 0;
            }
        }
        if (!aivm_size_add_checked(length, 1U, &next_length)) {
            return 0;
        }
        length = next_length;
    }

    if (!aivm_size_add_checked(escaped_length, 1U, &escaped_length)) {
        return 0;
    }

    source = aivm_vm_snapshot_arena_backed_string(vm, input, length, &input_copy);
    if (source == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String escape operand snapshot failed.");
        return 0;
    }

    output = aivm_string_arena_alloc(vm, escaped_length);
    if (output == NULL) {
        free(input_copy);
        return 0;
    }

    for (i = 0U; i < length; i += 1U) {
        char ch = source[i];
        if (ch == '\\') {
            output[out_index] = '\\';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = '\\';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
        } else if (ch == '"') {
            output[out_index] = '\\';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = '"';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
        } else if (ch == '\n') {
            output[out_index] = '\\';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = 'n';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
        } else if (ch == '\r') {
            output[out_index] = '\\';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = 'r';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
        } else if (ch == '\t') {
            output[out_index] = '\\';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
            output[out_index] = 't';
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
        } else {
            output[out_index] = ch;
            if (!aivm_size_add_checked(out_index, 1U, &next_out_index)) {
                free(input_copy);
                return 0;
            }
            out_index = next_out_index;
        }
    }

    output[out_index] = '\0';
    free(input_copy);
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
        if (!aivm_size_add_checked(count, 1U, &next_count)) {
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
        if (!aivm_size_add_checked(current_rune, 1U, &next_rune)) {
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
    output = aivm_vm_copy_string_range_to_arena(vm, text + start_byte, copy_length);
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
        if (!aivm_size_add_checked(input_length, 1U, &input_length)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "substring input length overflow.");
            return 0;
        }
    }

    rune_count = utf8_rune_count(text);
    if (rune_count == (size_t)-1) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "substring rune count overflow.");
        return 0;
    }
    start_rune = clamp_rune_index(start, rune_count);
    end_rune = clamp_rune_index(start + length, rune_count);
    if (end_rune < start_rune) {
        end_rune = start_rune;
    }

    start_byte = utf8_byte_offset_for_rune(text, start_rune);
    end_byte = utf8_byte_offset_for_rune(text, end_rune);
    output = aivm_vm_copy_string_splice_to_arena(
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

static int push_find_by_runes(AivmVm* vm, const char* text, const char* pattern, int64_t start)
{
    size_t text_runes;
    size_t pattern_runes;
    size_t start_rune;
    size_t candidate_rune;
    size_t candidate_byte;
    size_t pattern_bytes;
    size_t haystack_bytes;

    if (vm == NULL || text == NULL || pattern == NULL) {
        return 0;
    }

    text_runes = utf8_rune_count(text);
    pattern_runes = utf8_rune_count(pattern);
    start_rune = clamp_rune_index(start, text_runes);
    if (pattern_runes == 0U) {
        return aivm_stack_push(vm, aivm_value_int((int64_t)start_rune));
    }
    if (pattern_runes > text_runes || start_rune > text_runes - pattern_runes) {
        return aivm_stack_push(vm, aivm_value_int(-1));
    }

    pattern_bytes = strlen(pattern);
    haystack_bytes = strlen(text);
    candidate_byte = utf8_byte_offset_for_rune(text, start_rune);
    for (candidate_rune = start_rune; candidate_byte + pattern_bytes <= haystack_bytes; candidate_rune += 1U) {
        if (memcmp(text + candidate_byte, pattern, pattern_bytes) == 0) {
            return aivm_stack_push(vm, aivm_value_int((int64_t)candidate_rune));
        }
        if (candidate_rune >= text_runes - pattern_runes) {
            break;
        }
        candidate_byte = utf8_byte_offset_for_rune(text, candidate_rune + 1U);
    }
    return aivm_stack_push(vm, aivm_value_int(-1));
}

static int push_string_from_codepoint(AivmVm* vm, uint32_t cp)
{
    char scratch[5];

    if (vm == NULL || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) {
        return vm == NULL ? 0 : push_string_copy(vm, "");
    }
    if (cp <= 0x7FU) {
        scratch[0] = (char)cp;
        scratch[1] = '\0';
    } else if (cp <= 0x7FFU) {
        scratch[0] = (char)(0xC0U | (cp >> 6U));
        scratch[1] = (char)(0x80U | (cp & 0x3FU));
        scratch[2] = '\0';
    } else if (cp <= 0xFFFFU) {
        scratch[0] = (char)(0xE0U | (cp >> 12U));
        scratch[1] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
        scratch[2] = (char)(0x80U | (cp & 0x3FU));
        scratch[3] = '\0';
    } else {
        scratch[0] = (char)(0xF0U | (cp >> 18U));
        scratch[1] = (char)(0x80U | ((cp >> 12U) & 0x3FU));
        scratch[2] = (char)(0x80U | ((cp >> 6U) & 0x3FU));
        scratch[3] = (char)(0x80U | (cp & 0x3FU));
        scratch[4] = '\0';
    }
    return push_string_copy(vm, scratch);
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
    clock_t syscall_start;
    clock_t syscall_end;
    double syscall_elapsed_seconds;

    if (vm == NULL || out_result == NULL) {
        return 0;
    }
    if (arg_count > AIVM_VM_MAX_SYSCALL_ARGS) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid call argument count.");
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
            aivm_vm_value_type_name(target_value.type),
            (unsigned long long)vm->instruction_pointer);
        aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, vm->error_detail_storage);
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
                        if (!aivm_size_add_checked(raw_len, 1U, &next_raw_len)) {
                            return 0;
                        }
                        raw_len = next_raw_len;
                    }
                    raw_source = aivm_vm_snapshot_arena_backed_string(vm, target_value.string_value, raw_len, &raw_source_copy);
                    if (raw_source == NULL) {
                        return 0;
                    }
                    arg_source = aivm_vm_snapshot_arena_backed_string(vm, args[0].string_value, prefix_len, &arg_source_copy);
                    if (arg_source == NULL) {
                        free(raw_source_copy);
                        return 0;
                    }
                    suffix_source = arg_source + prefix_len;
                    if (!aivm_size_add_checked(raw_len, prefix_len, &out_len) ||
                        !aivm_size_add_checked(out_len, 1U, &bytes_needed)) {
                        free(raw_source_copy);
                        free(arg_source_copy);
                        return 0;
                    }
                    merged = aivm_string_arena_alloc(vm, bytes_needed);
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
                    suffix_target = aivm_vm_copy_string_to_arena(vm, suffix_source);
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
                        aivm_vm_value_type_name(target_value.type),
                        (long long)target_value.int_value,
                        (unsigned long long)effective_arg_count);
                    aivm_set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
                    return 0;
                }
                (void)snprintf(
                    vm->error_detail_storage,
                    sizeof(vm->error_detail_storage),
                    "AIVMS003: Syscall target was not found. rawTargetType=%s rawTarget=%s argCount=%llu",
                    aivm_vm_value_type_name(target_value.type),
                    raw_target_text == NULL ? "<non-string>" : raw_target_text,
                    (unsigned long long)effective_arg_count);
                aivm_set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
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
            aivm_set_vm_error(vm, AIVM_VM_ERR_SYSCALL, syscall_contract_failure_detail(contract_status));
            return 0;
        }
        if (!aivm_syscall_policy_allows(&vm->syscall_policy, AIVM_SYSCALL_CAPABILITY_DEBUG)) {
            (void)snprintf(
                vm->error_detail_storage,
                sizeof(vm->error_detail_storage),
                "AIVMS008: Syscall capability is denied by runtime policy. target=%.72s capability=debug",
                target_value.string_value);
            aivm_set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
            return 0;
        }
        return call_debug_task_reclaim_stats(vm, out_result);
    }

    syscall_start = clock();
    syscall_status = aivm_syscall_dispatch_checked_with_policy(
        vm->syscall_bindings,
        vm->syscall_binding_count,
        &vm->syscall_policy,
        target_value.string_value,
        args,
        effective_arg_count,
        out_result,
        &contract_status);
    syscall_end = clock();
    syscall_elapsed_seconds = (double)(syscall_end - syscall_start) / (double)CLOCKS_PER_SEC;
#if defined(AIVM_DEBUG_RUNTIME)
    record_profile_syscall(vm, target_value.string_value, syscall_elapsed_seconds);
#endif
    if (vm->syscall_elapsed_limit_ms > 0U &&
        syscall_elapsed_limit_applies(target_value.string_value) &&
        syscall_elapsed_seconds * 1000.0 > (double)vm->syscall_elapsed_limit_ms) {
        (void)snprintf(
            vm->error_detail_storage,
            sizeof(vm->error_detail_storage),
            "AIVMS007: Syscall resource limit exceeded. target=%.72s elapsed_ms=%.3f limit_ms=%llu",
            target_value.string_value,
            syscall_elapsed_seconds * 1000.0,
            (unsigned long long)vm->syscall_elapsed_limit_ms);
        aivm_set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
        return 0;
    }
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
            aivm_set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
            return 0;
        }
        if (syscall_status == AIVM_SYSCALL_ERR_CONTRACT) {
            aivm_set_vm_error(
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
            aivm_set_vm_error(
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
        if (syscall_status == AIVM_SYSCALL_ERR_UNBOUND) {
            (void)snprintf(
                vm->error_detail_storage,
                sizeof(vm->error_detail_storage),
                "AIVMS006: Syscall target is known but has no host binding. target=%.72s",
                target_value.string_value);
            aivm_set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
            return 0;
        }
        if (syscall_status == AIVM_SYSCALL_ERR_CAPABILITY_DENIED) {
            (void)snprintf(
                vm->error_detail_storage,
                sizeof(vm->error_detail_storage),
                "AIVMS008: Syscall capability is denied by runtime policy. target=%.72s capability=%s",
                target_value.string_value,
                aivm_syscall_capability_name(aivm_syscall_contract_capability(target_value.string_value)));
            aivm_set_vm_error(vm, AIVM_VM_ERR_SYSCALL, vm->error_detail_storage);
            return 0;
        }
        aivm_set_vm_error(vm, AIVM_VM_ERR_SYSCALL, syscall_failure_detail(syscall_status, contract_status));
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
        arg0 = aivm_vm_value_type_name(args[0].type);
    }
    if (arg_count > 1U) {
        arg1 = aivm_vm_value_type_name(args[1].type);
    }
    if (arg_count > 2U) {
        arg2 = aivm_vm_value_type_name(args[2].type);
    }
    (void)snprintf(
        vm->error_detail_storage,
        sizeof(vm->error_detail_storage),
        "AIVMS003: Syscall target was not found. rawTargetType=%s rawTarget=%s recoveredTarget=%s argCount=%llu arg0=%s arg1=%s arg2=%s",
        aivm_vm_value_type_name(raw_target_value.type),
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
        case AIVM_SYSCALL_ERR_UNBOUND:
            return "AIVMS006: Syscall target is known but has no host binding.";
        case AIVM_SYSCALL_ERR_RESOURCE_LIMIT:
            return "AIVMS007: Syscall resource limit exceeded.";
        case AIVM_SYSCALL_ERR_CAPABILITY_DENIED:
            return "AIVMS008: Syscall capability is denied by runtime policy.";
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
    } else if (value.type == AIVM_VAL_NUMBER) {
        wrote = snprintf(
            buffer + used,
            capacity - used,
            "(%.15g)",
            value.number_value);
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
    if (!aivm_size_add_checked(used, 1U, &limit)) {
        return used;
    }
    for (i = 0U; i < max_locals && (base + i) < vm->locals_count && limit < capacity; i += 1U) {
        int wrote = snprintf(
            buffer + used,
            capacity - used,
            " %s%llu=%s",
            prefix,
            (unsigned long long)i,
            aivm_vm_value_type_name(vm->locals[base + i].type));
        if (wrote <= 0) {
            break;
        }
        if ((size_t)wrote >= capacity - used) {
            used = capacity - 1U;
            break;
        }
        if (!aivm_size_add_checked(used, (size_t)wrote, &used)) {
            return capacity - 1U;
        }
        used = append_vm_value_preview(buffer, capacity, used, vm->locals[base + i]);
        if (!aivm_size_add_checked(used, 1U, &limit)) {
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
    if (!aivm_size_add_checked(used, 1U, &limit)) {
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
        if (!aivm_size_add_checked(used, (size_t)wrote, &used)) {
            return capacity - 1U;
        }
        if (!aivm_size_add_checked(used, 1U, &limit)) {
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
        const char* expected = aivm_vm_value_type_name(contract->arg_types[i]);
        const char* actual = (i < arg_count) ? aivm_vm_value_type_name(args[i].type) : "missing";
        size_t limit = 0U;
        if (!aivm_size_add_checked(used, 1U, &limit) ||
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
        if (!aivm_size_add_checked(used, (size_t)wrote, &used)) {
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
    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task state transition was invalid.");
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
        if (!is_terminal_task_state(vm->completed_tasks[index].state)) {
            aivm_counter_increment_saturating(&vm->task_reclaim_skip_pinned_count);
            continue;
        }
        if (!is_task_handle_pinned(vm, vm->completed_tasks[index].handle)) {
            break;
        }
        aivm_counter_increment_saturating(&vm->task_reclaim_skip_pinned_count);
    }
    if (index >= vm->completed_task_count) {
        aivm_counter_increment_saturating(&vm->task_reclaim_exhausted_count);
        return 0;
    }
    if (aivm_size_add_checked(index, 1U, &next_index) &&
        next_index < vm->completed_task_count &&
        size_sub_checked(vm->completed_task_count, next_index, &move_count) &&
        move_count > 0U) {
        if (vm->completed_tasks[index].worker_context != NULL) {
            free_bytecode_worker_context((AivmBytecodeWorkerContext*)vm->completed_tasks[index].worker_context);
            vm->completed_tasks[index].worker_context = NULL;
        }
        memmove(
            &vm->completed_tasks[index],
            &vm->completed_tasks[next_index],
            move_count * sizeof(AivmCompletedTask));
    } else if (index < vm->completed_task_count && vm->completed_tasks[index].worker_context != NULL) {
        free_bytecode_worker_context((AivmBytecodeWorkerContext*)vm->completed_tasks[index].worker_context);
        vm->completed_tasks[index].worker_context = NULL;
    }
    vm->completed_task_count -= 1U;
    aivm_counter_increment_saturating(&vm->task_reclaim_count);
    return 1;
}

static int remove_completed_task_slot(AivmVm* vm, int64_t handle)
{
    size_t index;
    size_t next_index = 0U;
    size_t move_count = 0U;
    if (vm == NULL) {
        return 0;
    }
    for (index = 0U; index < vm->completed_task_count; index += 1U) {
        if (vm->completed_tasks[index].handle == handle) {
            break;
        }
    }
    if (index >= vm->completed_task_count) {
        return 0;
    }
    if (vm->completed_tasks[index].worker_context != NULL) {
        free_bytecode_worker_context((AivmBytecodeWorkerContext*)vm->completed_tasks[index].worker_context);
        vm->completed_tasks[index].worker_context = NULL;
    }
    if (aivm_size_add_checked(index, 1U, &next_index) &&
        next_index < vm->completed_task_count &&
        size_sub_checked(vm->completed_task_count, next_index, &move_count) &&
        move_count > 0U) {
        memmove(
            &vm->completed_tasks[index],
            &vm->completed_tasks[next_index],
            move_count * sizeof(AivmCompletedTask));
    }
    vm->completed_task_count -= 1U;
    aivm_counter_increment_saturating(&vm->task_reclaim_count);
    return 1;
}

static int release_consumed_task_result(AivmVm* vm, int64_t handle)
{
    if (vm == NULL) {
        return 0;
    }
    if (is_task_handle_pinned(vm, handle)) {
        aivm_counter_increment_saturating(&vm->task_reclaim_skip_pinned_count);
        return aivm_collect_safe_point(vm);
    }
    (void)remove_completed_task_slot(vm, handle);
    return aivm_collect_safe_point(vm);
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
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task table capacity exceeded.");
            return 0;
        }
    }
    if (vm->next_task_handle == INT64_MAX) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task handle overflow.");
        return 0;
    }

    handle = vm->next_task_handle;
    vm->next_task_handle += 1;
    if (!aivm_size_add_checked(vm->completed_task_count, 1U, &needed) ||
        needed > AIVM_VM_TASK_CAPACITY) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task table capacity exceeded.");
        return 0;
    }
    task = &vm->completed_tasks[vm->completed_task_count];
    task->state = AIVM_TASK_STATE_PENDING;
    task->handle = handle;
    task->result = result;
    task->worker_context = NULL;
    if (!transition_task_state(vm, task, AIVM_TASK_STATE_COMPLETED)) {
        return 0;
    }
    vm->completed_task_count = needed;
    return aivm_stack_push(vm, aivm_value_int(handle));
}

#if defined(_WIN32)
typedef HANDLE AivmNativeThread;
#else
typedef pthread_t AivmNativeThread;
#endif

struct AivmBytecodeWorkerContext {
    AivmVm worker;
    size_t target;
    AivmValue result;
    int joined;
    int started;
    AivmNativeThread thread;
};

static int run_prepared_worker_vm(AivmVm* worker, size_t target, AivmValue* out_result)
{
    AivmValue result = aivm_value_void();
    if (worker == NULL || out_result == NULL) {
        return 0;
    }
    worker->instruction_pointer = target;
    while (worker->status != AIVM_VM_STATUS_ERROR) {
        if (worker->call_frame_count == 0U &&
            worker->instruction_pointer == worker->program->instruction_count) {
            break;
        }

        if (worker->instruction_pointer >= worker->program->instruction_count) {
            aivm_set_vm_error(worker, AIVM_VM_ERR_INVALID_PROGRAM, "Subroutine terminated without RET.");
            return 0;
        }

        aivm_step(worker);
        if (worker->status == AIVM_VM_STATUS_HALTED) {
            if (worker->call_frame_count == 0U &&
                worker->instruction_pointer == worker->program->instruction_count) {
                break;
            }
            aivm_set_vm_error(worker, AIVM_VM_ERR_INVALID_PROGRAM, "HALT is invalid inside ASYNC_CALL.");
            return 0;
        }
    }

    if (worker->status == AIVM_VM_STATUS_ERROR) {
        return 0;
    }

    if (worker->stack_count > 1U) {
        aivm_set_vm_error(worker, AIVM_VM_ERR_INVALID_PROGRAM, "Return restore invalid.");
        return 0;
    }
    if (worker->stack_count == 1U) {
        result = worker->stack[0];
    }
    *out_result = result;
    return 1;
}

#if defined(_WIN32)
static DWORD WINAPI aivm_bytecode_worker_thread_main(LPVOID raw_context)
#else
static void* aivm_bytecode_worker_thread_main(void* raw_context)
#endif
{
    AivmBytecodeWorkerContext* context = (AivmBytecodeWorkerContext*)raw_context;
    if (context != NULL) {
        (void)run_prepared_worker_vm(&context->worker, context->target, &context->result);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int start_bytecode_worker_thread(AivmBytecodeWorkerContext* context)
{
    if (context == NULL) {
        return 0;
    }
#if defined(_WIN32)
    context->thread = CreateThread(NULL, 16U * 1024U * 1024U, aivm_bytecode_worker_thread_main, context, 0, NULL);
    context->started = context->thread != NULL ? 1 : 0;
    return context->started;
#else
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        context->started = 0;
        return 0;
    }
    if (pthread_attr_setstacksize(&attr, 16U * 1024U * 1024U) != 0) {
        (void)pthread_attr_destroy(&attr);
        context->started = 0;
        return 0;
    }
    if (pthread_create(&context->thread, &attr, aivm_bytecode_worker_thread_main, context) != 0) {
        (void)pthread_attr_destroy(&attr);
        context->started = 0;
        return 0;
    }
    (void)pthread_attr_destroy(&attr);
    context->started = 1;
    return 1;
#endif
}

static int join_bytecode_worker_thread(AivmBytecodeWorkerContext* context)
{
    if (context == NULL || context->joined != 0) {
        return 1;
    }
    if (context->started != 0) {
#if defined(_WIN32)
        (void)WaitForSingleObject(context->thread, INFINITE);
        (void)CloseHandle(context->thread);
        context->thread = NULL;
#else
        (void)pthread_join(context->thread, NULL);
#endif
    }
    context->joined = 1;
    return 1;
}

static void free_bytecode_worker_context(AivmBytecodeWorkerContext* context)
{
    if (context == NULL) {
        return;
    }
    (void)join_bytecode_worker_thread(context);
    aivm_dispose(&context->worker);
    free(context);
}

void aivm_vm_cleanup_bytecode_worker_tasks(AivmVm* vm)
{
    size_t i;
    if (vm == NULL) {
        return;
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (vm->completed_tasks[i].worker_context != NULL) {
            free_bytecode_worker_context((AivmBytecodeWorkerContext*)vm->completed_tasks[i].worker_context);
            vm->completed_tasks[i].worker_context = NULL;
        }
    }
}

typedef struct {
    const AivmVm* src;
    AivmVm* dst;
    int64_t* node_map;
    size_t node_map_count;
    int64_t* pair_map;
    size_t pair_map_count;
} AivmWorkerBoundaryCopy;

static int copy_worker_boundary_value_with_context(
    AivmWorkerBoundaryCopy* context,
    AivmValue source,
    AivmValue* out_value,
    const char* direction);

static int copy_worker_boundary_node(
    AivmWorkerBoundaryCopy* context,
    int64_t source_handle,
    int64_t* out_handle)
{
    const AivmNodeRecord* source_node;
    AivmNodeAttr* attrs = NULL;
    int64_t* children = NULL;
    int64_t copied_child;
    int64_t copied_handle;
    size_t source_index;
    size_t i;
    if (context == NULL || context->src == NULL || context->dst == NULL || out_handle == NULL) {
        return 0;
    }
    if (source_handle <= 0 || (size_t)source_handle >= context->node_map_count) {
        aivm_set_vm_error(context->dst, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker node handle was invalid.");
        return 0;
    }
    if (context->node_map[source_handle] > 0) {
        *out_handle = context->node_map[source_handle];
        return 1;
    }
    if (context->node_map[source_handle] < 0) {
        aivm_set_vm_error(context->dst, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker node graph cycle was invalid.");
        return 0;
    }
    if (!aivm_vm_lookup_node(context->src, source_handle, &source_node)) {
        aivm_set_vm_error(context->dst, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker node handle was invalid.");
        return 0;
    }
    context->node_map[source_handle] = -1;
    attrs = (AivmNodeAttr*)calloc(source_node->attr_count == 0U ? 1U : source_node->attr_count, sizeof(attrs[0]));
    children = (int64_t*)calloc(source_node->child_count == 0U ? 1U : source_node->child_count, sizeof(children[0]));
    if (attrs == NULL || children == NULL) {
        free(attrs);
        free(children);
        aivm_set_vm_error(context->dst, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: worker node copy scratch allocation failed.");
        return 0;
    }
    for (i = 0U; i < source_node->attr_count; i += 1U) {
        if (!aivm_size_add_checked(source_node->attr_start, i, &source_index) ||
            source_index >= context->src->node_attr_count) {
            free(attrs);
            free(children);
            aivm_set_vm_error(context->dst, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker node attr slot was invalid.");
            return 0;
        }
        attrs[i] = context->src->node_attrs[source_index];
    }
    for (i = 0U; i < source_node->child_count; i += 1U) {
        if (!aivm_size_add_checked(source_node->child_start, i, &source_index) ||
            source_index >= context->src->node_child_count) {
            free(attrs);
            free(children);
            aivm_set_vm_error(context->dst, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker node child slot was invalid.");
            return 0;
        }
        if (!copy_worker_boundary_node(context, context->src->node_children[source_index], &copied_child)) {
            free(attrs);
            free(children);
            return 0;
        }
        children[i] = copied_child;
    }
    if (!aivm_vm_create_node_record(
            context->dst,
            source_node->kind,
            source_node->id,
            attrs,
            source_node->attr_count,
            children,
            source_node->child_count,
            &copied_handle)) {
        free(attrs);
        free(children);
        return 0;
    }
    free(attrs);
    free(children);
    context->node_map[source_handle] = copied_handle;
    *out_handle = copied_handle;
    return 1;
}

static int copy_worker_boundary_pair(
    AivmWorkerBoundaryCopy* context,
    int64_t source_handle,
    int64_t* out_handle)
{
    const AivmScratchPair* source_pair;
    AivmValue copied_first;
    AivmValue copied_second;
    int64_t copied_handle;
    if (context == NULL || context->src == NULL || context->dst == NULL || out_handle == NULL) {
        return 0;
    }
    if (source_handle <= 0 || (size_t)source_handle >= context->pair_map_count) {
        aivm_set_vm_error(context->dst, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker pair handle was invalid.");
        return 0;
    }
    if (context->pair_map[source_handle] > 0) {
        *out_handle = context->pair_map[source_handle];
        return 1;
    }
    if (context->pair_map[source_handle] < 0) {
        aivm_set_vm_error(context->dst, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker pair graph cycle was invalid.");
        return 0;
    }
    if (!aivm_vm_lookup_scratch_pair(context->src, source_handle, &source_pair)) {
        aivm_set_vm_error(context->dst, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker pair handle was invalid.");
        return 0;
    }
    context->pair_map[source_handle] = -1;
    if (!copy_worker_boundary_value_with_context(context, source_pair->first, &copied_first, "pair value") ||
        !copy_worker_boundary_value_with_context(context, source_pair->second, &copied_second, "pair value")) {
        return 0;
    }
    if (!aivm_vm_create_scratch_pair(context->dst, copied_first, copied_second, &copied_handle)) {
        return 0;
    }
    context->pair_map[source_handle] = copied_handle;
    *out_handle = copied_handle;
    return 1;
}

static int copy_worker_boundary_value_with_context(
    AivmWorkerBoundaryCopy* context,
    AivmValue source,
    AivmValue* out_value,
    const char* direction)
{
    char* copied_string;
    uint8_t* copied_bytes;
    int64_t copied_handle;
    AivmVm* dst;
    if (context == NULL || context->dst == NULL || out_value == NULL) {
        return 0;
    }
    dst = context->dst;
    switch (source.type) {
        case AIVM_VAL_VOID:
        case AIVM_VAL_INT:
        case AIVM_VAL_NUMBER:
        case AIVM_VAL_BOOL:
        case AIVM_VAL_NULL:
            *out_value = source;
            return 1;

        case AIVM_VAL_STRING:
            if (source.string_value == NULL) {
                aivm_set_vm_error(dst, AIVM_VM_ERR_TYPE_MISMATCH, "Worker boundary string value must be non-null.");
                return 0;
            }
            copied_string = aivm_vm_copy_string_to_arena(dst, source.string_value);
            if (copied_string == NULL) {
                aivm_set_vm_error(dst, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: worker string copy exceeded arena capacity.");
                return 0;
            }
            *out_value = aivm_value_string(copied_string);
            return 1;

        case AIVM_VAL_BYTES:
            if (source.bytes_value.length > 0U && source.bytes_value.data == NULL) {
                aivm_set_vm_error(dst, AIVM_VM_ERR_TYPE_MISMATCH, "Worker boundary bytes value must provide data.");
                return 0;
            }
            copied_bytes = copy_bytes_to_arena(dst, source.bytes_value.data, source.bytes_value.length);
            if (copied_bytes == NULL && source.bytes_value.length > 0U) {
                aivm_set_vm_error(dst, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM002: worker bytes copy exceeded arena capacity.");
                return 0;
            }
            *out_value = aivm_value_bytes(copied_bytes, source.bytes_value.length);
            return 1;

        case AIVM_VAL_NODE:
            if (!copy_worker_boundary_node(context, source.node_handle, &copied_handle)) {
                return 0;
            }
            *out_value = aivm_value_node(copied_handle);
            return 1;

        case AIVM_VAL_PAIR:
            if (!copy_worker_boundary_pair(context, source.pair_handle, &copied_handle)) {
                return 0;
            }
            *out_value = aivm_value_pair(copied_handle);
            return 1;

        case AIVM_VAL_UNKNOWN:
        default:
            (void)snprintf(
                dst->error_detail_storage,
                sizeof(dst->error_detail_storage),
                "ASYNC_CALL worker %s must be immutable scalar, string, or bytes. type=%s",
                direction == NULL ? "value" : direction,
                aivm_vm_value_type_name(source.type));
            aivm_set_vm_error(dst, AIVM_VM_ERR_INVALID_PROGRAM, dst->error_detail_storage);
            return 0;
    }
}

static int copy_worker_boundary_value(
    const AivmVm* src,
    AivmVm* dst,
    AivmValue source,
    AivmValue* out_value,
    const char* direction)
{
    AivmWorkerBoundaryCopy context;
    int result;
    if (src == NULL || dst == NULL || out_value == NULL) {
        return 0;
    }
    memset(&context, 0, sizeof(context));
    context.src = src;
    context.dst = dst;
    context.node_map_count = src->node_count + 1U;
    context.pair_map_count = src->scratch_pair_count + 1U;
    context.node_map = (int64_t*)calloc(context.node_map_count == 0U ? 1U : context.node_map_count, sizeof(context.node_map[0]));
    context.pair_map = (int64_t*)calloc(context.pair_map_count == 0U ? 1U : context.pair_map_count, sizeof(context.pair_map[0]));
    if (context.node_map == NULL || context.pair_map == NULL) {
        free(context.node_map);
        free(context.pair_map);
        aivm_set_vm_error(dst, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: worker boundary copy map allocation failed.");
        return 0;
    }
    result = copy_worker_boundary_value_with_context(&context, source, out_value, direction);
    free(context.node_map);
    free(context.pair_map);
    return result;
}

static int propagate_worker_error(AivmVm* parent, const AivmVm* worker)
{
    const char* detail;
    if (parent == NULL || worker == NULL) {
        return 0;
    }
    detail = aivm_vm_error_detail(worker);
    (void)snprintf(
        parent->error_detail_storage,
        sizeof(parent->error_detail_storage),
        "ASYNC_CALL worker failed: %s",
        detail == NULL ? "" : detail);
    aivm_set_vm_error(parent, worker->error, parent->error_detail_storage);
    return 0;
}

static int start_call_subroutine_worker(AivmVm* vm, size_t target, int64_t* out_handle)
{
    size_t arg_count;
    size_t frame_base;
    size_t i;
    size_t needed = 0U;
    int64_t handle;
    AivmCompletedTask* task;
    AivmBytecodeWorkerContext* context;
    AivmValue copied_value;

    if (vm == NULL || out_handle == NULL) {
        return 0;
    }
    if (target >= vm->program->instruction_count) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid function index.");
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

    frame_base = vm->stack_count - arg_count;

    if (vm->completed_task_count >= AIVM_VM_TASK_CAPACITY) {
        if (!reclaim_oldest_completed_task_slot(vm)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task table capacity exceeded.");
            return 0;
        }
    }
    if (vm->next_task_handle == INT64_MAX) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task handle overflow.");
        return 0;
    }
    if (!aivm_size_add_checked(vm->completed_task_count, 1U, &needed) ||
        needed > AIVM_VM_TASK_CAPACITY) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Task table capacity exceeded.");
        return 0;
    }

    context = (AivmBytecodeWorkerContext*)calloc(1U, sizeof(context[0]));
    if (context == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: bytecode worker context allocation failed.");
        return 0;
    }
    aivm_init(&context->worker, vm->program);
    aivm_set_runtime_profile(&context->worker, vm->runtime_profile);
    aivm_syscall_policy_allow_none(&context->worker.syscall_policy);
    context->target = target;
    context->result = aivm_value_void();

    for (i = 0U; i < arg_count; i += 1U) {
        if (!copy_worker_boundary_value(vm, &context->worker, vm->stack[frame_base + i], &copied_value, "argument") ||
            !aivm_stack_push(&context->worker, copied_value)) {
            (void)propagate_worker_error(vm, &context->worker);
            free_bytecode_worker_context(context);
            return 0;
        }
    }

    if (!aivm_frame_push(&context->worker, context->worker.program->instruction_count, 0U)) {
        (void)propagate_worker_error(vm, &context->worker);
        free_bytecode_worker_context(context);
        return 0;
    }
    if (!start_bytecode_worker_thread(context)) {
        free_bytecode_worker_context(context);
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker thread start failed.");
        return 0;
    }

    handle = vm->next_task_handle;
    vm->next_task_handle += 1;
    task = &vm->completed_tasks[vm->completed_task_count];
    task->state = AIVM_TASK_STATE_PENDING;
    task->handle = handle;
    task->result = aivm_value_void();
    task->worker_context = context;
    vm->completed_task_count = needed;
    vm->stack_count = frame_base;
    *out_handle = handle;
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
        if (!aivm_vm_lookup_node(vm, task->result.node_handle, &node)) {
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
    if (!aivm_size_add_checked(vm->node_count, 1U, &next_node_count)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "debug task stats node id overflow.");
        return 0;
    }
    suffix_length = write_u64_decimal(id_buffer + 25U, sizeof(id_buffer) - 25U, (uint64_t)next_node_count);
    if (suffix_length == 0U) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "debug task stats node id overflow.");
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

    if (!aivm_vm_create_node_record(vm, "DebugTaskReclaimStats", id_buffer, attrs, 3U, NULL, 0U, &handle)) {
        return 0;
    }
    *out_result = aivm_value_node(handle);
    return 1;
}

static int complete_pending_bytecode_task(AivmVm* vm, AivmCompletedTask* task)
{
    AivmBytecodeWorkerContext* context;
    AivmValue copied_result;
    if (vm == NULL || task == NULL) {
        return 0;
    }
    if (task->state != AIVM_TASK_STATE_PENDING || task->worker_context == NULL) {
        return 1;
    }
    context = (AivmBytecodeWorkerContext*)task->worker_context;
    if (!join_bytecode_worker_thread(context)) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "ASYNC_CALL worker join failed.");
        return 0;
    }
    if (context->worker.status == AIVM_VM_STATUS_ERROR) {
        (void)propagate_worker_error(vm, &context->worker);
        return 0;
    }
    if (!copy_worker_boundary_value(&context->worker, vm, context->result, &copied_result, "result")) {
        return 0;
    }
    task->result = copied_result;
    task->state = AIVM_TASK_STATE_COMPLETED;
    task->worker_context = NULL;
    free_bytecode_worker_context(context);
    return 1;
}

static int find_terminal_task_result(AivmVm* vm, int64_t handle, AivmValue* out_result)
{
    size_t i;
    if (vm == NULL || out_result == NULL) {
        return 0;
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (vm->completed_tasks[i].handle == handle &&
            vm->completed_tasks[i].state == AIVM_TASK_STATE_PENDING &&
            !complete_pending_bytecode_task(vm, &vm->completed_tasks[i])) {
            return 0;
        }
        if (is_terminal_task_state(vm->completed_tasks[i].state) &&
            vm->completed_tasks[i].handle == handle) {
            if (!task_terminal_payload_is_valid(vm, &vm->completed_tasks[i])) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Terminal failed/canceled task requires Err node result.");
                return 0;
            }
            *out_result = vm->completed_tasks[i].result;
            return 1;
        }
    }
    return 0;
}

static int mark_live_scratch_pair_value(AivmVm* vm, const AivmValue* value, uint8_t* live_pairs)
{
    size_t index;
    if (vm == NULL || value == NULL || live_pairs == NULL) {
        return 0;
    }
    if (value->type != AIVM_VAL_PAIR) {
        return 1;
    }
    if (value->pair_handle <= 0 || value->pair_handle > (int64_t)vm->scratch_pair_count) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid scratch-pair handle during pair mark.");
        return 0;
    }
    index = (size_t)(value->pair_handle - 1);
    if (live_pairs[index] != 0U) {
        return 1;
    }
    live_pairs[index] = 1U;
    if (!mark_live_scratch_pair_value(vm, &vm->scratch_pairs[index].first, live_pairs) ||
        !mark_live_scratch_pair_value(vm, &vm->scratch_pairs[index].second, live_pairs)) {
        return 0;
    }
    return 1;
}

int aivm_vm_mark_live_scratch_pair_handles(AivmVm* vm, uint8_t* live_pairs)
{
    size_t i;
    if (vm == NULL || live_pairs == NULL) {
        return 0;
    }
    memset(live_pairs, 0, vm->scratch_pair_count * sizeof(live_pairs[0]));
    for (i = 0U; i < vm->stack_count; i += 1U) {
        if (!mark_live_scratch_pair_value(vm, &vm->stack[i], live_pairs)) {
            return 0;
        }
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        if (!mark_live_scratch_pair_value(vm, &vm->locals[i], live_pairs)) {
            return 0;
        }
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (!mark_live_scratch_pair_value(vm, &vm->completed_tasks[i].result, live_pairs)) {
            return 0;
        }
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        if (!mark_live_scratch_pair_value(vm, &vm->par_values[i], live_pairs)) {
            return 0;
        }
    }
    return 1;
}

static int remap_value_scratch_pair_handle(AivmVm* vm, AivmValue* value, const int64_t* pair_map)
{
    int64_t old_handle;
    if (vm == NULL || value == NULL || pair_map == NULL) {
        return 0;
    }
    if (value->type != AIVM_VAL_PAIR) {
        return 1;
    }
    old_handle = value->pair_handle;
    if (old_handle <= 0 || old_handle > (int64_t)vm->scratch_pair_count || pair_map[old_handle] <= 0) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid scratch-pair handle during pair compaction.");
        return 0;
    }
    value->pair_handle = pair_map[old_handle];
    return 1;
}

int aivm_vm_compact_scratch_pairs(AivmVm* vm)
{
    uint8_t* live_pairs = NULL;
    int64_t* pair_map = NULL;
    AivmScratchPair* compacted_pairs = NULL;
    size_t new_pair_count = 0U;
    size_t i;

    if (vm == NULL) {
        return 0;
    }
    if (vm->scratch_pair_count == 0U) {
        return 1;
    }
    live_pairs = (uint8_t*)calloc(vm->scratch_pair_count, sizeof(live_pairs[0]));
    pair_map = (int64_t*)calloc(vm->scratch_pair_count + 1U, sizeof(pair_map[0]));
    compacted_pairs = (AivmScratchPair*)calloc(vm->scratch_pair_count, sizeof(compacted_pairs[0]));
    if (live_pairs == NULL || pair_map == NULL || compacted_pairs == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: scratch-pair compaction allocation failed.");
        goto fail;
    }
    if (!aivm_vm_mark_live_scratch_pair_handles(vm, live_pairs)) {
        goto fail;
    }

    for (i = 0U; i < vm->scratch_pair_count; i += 1U) {
        if (live_pairs[i] != 0U) {
            size_t old_handle_index;
            size_t compacted_handle;
            if (!aivm_size_add_checked(i, 1U, &old_handle_index) ||
                !aivm_size_add_checked(new_pair_count, 1U, &compacted_handle)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM005: scratch-pair compaction handle overflow.");
                goto fail;
            }
            pair_map[old_handle_index] = (int64_t)compacted_handle;
            new_pair_count = compacted_handle;
        }
    }

    for (i = 0U; i < vm->scratch_pair_count; i += 1U) {
        int64_t new_handle;
        if (live_pairs[i] == 0U) {
            continue;
        }
        new_handle = pair_map[i + 1U];
        if (new_handle <= 0) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Dangling live scratch-pair handle during compaction.");
            goto fail;
        }
        compacted_pairs[(size_t)(new_handle - 1)] = vm->scratch_pairs[i];
    }

    for (i = 0U; i < new_pair_count; i += 1U) {
        if (!remap_value_scratch_pair_handle(vm, &compacted_pairs[i].first, pair_map) ||
            !remap_value_scratch_pair_handle(vm, &compacted_pairs[i].second, pair_map)) {
            goto fail;
        }
    }
    for (i = 0U; i < vm->stack_count; i += 1U) {
        if (!remap_value_scratch_pair_handle(vm, &vm->stack[i], pair_map)) {
            goto fail;
        }
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        if (!remap_value_scratch_pair_handle(vm, &vm->locals[i], pair_map)) {
            goto fail;
        }
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (!remap_value_scratch_pair_handle(vm, &vm->completed_tasks[i].result, pair_map)) {
            goto fail;
        }
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        if (!remap_value_scratch_pair_handle(vm, &vm->par_values[i], pair_map)) {
            goto fail;
        }
    }

    memset(vm->scratch_pairs, 0, AIVM_VM_SCRATCH_PAIR_CAPACITY * sizeof(vm->scratch_pairs[0]));
    memcpy(vm->scratch_pairs, compacted_pairs, new_pair_count * sizeof(vm->scratch_pairs[0]));
    vm->scratch_pair_count = new_pair_count;

    free(live_pairs);
    free(pair_map);
    free(compacted_pairs);
    return 1;

fail:
    free(live_pairs);
    free(pair_map);
    free(compacted_pairs);
    return 0;
}

int aivm_compact_string_arena(AivmVm* vm)
{
    uint8_t* live = NULL;
    uint8_t* live_pairs = NULL;
    char* old_arena;
    char* new_arena = NULL;
    size_t new_used = 0U;
    size_t i;

    if (vm == NULL) {
        return 0;
    }
    if (vm->string_arena_used == 0U) {
        return 1;
    }
    live = (uint8_t*)calloc(AIVM_VM_NODE_CAPACITY, sizeof(live[0]));
    live_pairs = (uint8_t*)calloc(AIVM_VM_SCRATCH_PAIR_CAPACITY, sizeof(live_pairs[0]));
    new_arena = (char*)calloc(AIVM_VM_STRING_ARENA_CAPACITY, sizeof(new_arena[0]));
    if (live == NULL || live_pairs == NULL || new_arena == NULL) {
        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena compaction workspace allocation failed.");
        goto fail;
    }
    if (!aivm_vm_mark_live_node_handles(vm, live, NULL, 0U)) {
        goto fail;
    }
    if (!aivm_vm_mark_live_scratch_pair_handles(vm, live_pairs)) {
        goto fail;
    }

    for (i = 0U; i < vm->stack_count; i += 1U) {
        if (!compact_relocate_value_string(vm, &vm->stack[i], new_arena, &new_used)) {
            goto fail;
        }
    }
    for (i = 0U; i < vm->locals_count; i += 1U) {
        if (!compact_relocate_value_string(vm, &vm->locals[i], new_arena, &new_used)) {
            goto fail;
        }
    }
    for (i = 0U; i < vm->completed_task_count; i += 1U) {
        if (!compact_relocate_value_string(vm, &vm->completed_tasks[i].result, new_arena, &new_used)) {
            goto fail;
        }
    }
    for (i = 0U; i < vm->par_value_count; i += 1U) {
        if (!compact_relocate_value_string(vm, &vm->par_values[i], new_arena, &new_used)) {
            goto fail;
        }
    }
    for (i = 0U; i < vm->scratch_pair_count; i += 1U) {
        if (live_pairs[i] == 0U) {
            continue;
        }
        if (!compact_relocate_value_string(vm, &vm->scratch_pairs[i].first, new_arena, &new_used) ||
            !compact_relocate_value_string(vm, &vm->scratch_pairs[i].second, new_arena, &new_used)) {
            goto fail;
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
            goto fail;
        }
        for (attr_i = 0U; attr_i < node->attr_count; attr_i += 1U) {
            size_t attr_slot = 0U;
            AivmNodeAttr* attr;
            if (!aivm_size_add_checked(node->attr_start, attr_i, &attr_slot) ||
                attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM004: node attr slot overflow during string compaction.");
                goto fail;
            }
            attr = &vm->node_attrs[attr_slot];
            if (!compact_relocate_string_ptr(vm, &attr->key, new_arena, &new_used)) {
                goto fail;
            }
            if ((attr->kind == AIVM_NODE_ATTR_IDENTIFIER || attr->kind == AIVM_NODE_ATTR_STRING) &&
                !compact_relocate_string_ptr(vm, &attr->string_value, new_arena, &new_used)) {
                goto fail;
            }
        }
    }

    old_arena = vm->string_arena;
    vm->string_arena = new_arena;
    vm->string_arena_used = new_used;
    free(old_arena);
    free(live);
    free(live_pairs);
    return 1;

fail:
    free(live);
    free(live_pairs);
    free(new_arena);
    return 0;
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
        if (!aivm_size_add_checked(count, 1U, &count)) {
            return 0U;
        }
    } while (value != 0U && count < sizeof(temp));

    if (!aivm_size_add_checked(count, 1U, &i) || i > capacity) {
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
            aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "Runtime string value must be non-null.");
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
    } else if (value.type == AIVM_VAL_NUMBER) {
        char* number_text;
        char number_buffer[64];
        (void)snprintf(number_buffer, sizeof(number_buffer), "%.15g", value.number_value);
        number_text = aivm_vm_copy_string_to_arena(vm, number_buffer);
        if (number_text == NULL) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "Runtime number string allocation failed.");
            return 0;
        }
        node_kind = "Lit";
        node_id = "runtime_number";
        attrs[0].key = "value";
        attrs[0].kind = AIVM_NODE_ATTR_STRING;
        attrs[0].string_value = number_text;
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

    if (!aivm_vm_create_node_record(vm, node_kind, node_id, attrs, attr_count, NULL, 0U, &handle)) {
        return 0;
    }

    *out_handle = handle;
    return 1;
}

int aivm_vm_initialize_process_argv_node(AivmVm* vm)
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
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv exceeds node capacity.");
        return 0;
    }

    for (i = 0U; i < vm->process_argv_count; i += 1U) {
        if (!aivm_size_add_checked(i, 2U, &child_handle_index) ||
            child_handle_index > (size_t)INT64_MAX) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv child handle overflow.");
            return 0;
        }
        child_handles[i] = (int64_t)child_handle_index;
    }
    if (!aivm_vm_create_node_record(
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
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv root handle invariant violated.");
        return 0;
    }

    for (i = 0U; i < vm->process_argv_count; i += 1U) {
        char node_id[40];
        size_t suffix_len;
        const char* arg = vm->process_argv[i];

        memcpy(node_id, "argv_item_", 10U);
        suffix_len = write_u64_decimal(node_id + 10U, sizeof(node_id) - 10U, (uint64_t)i);
        if (suffix_len == 0U) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv node id overflow.");
            return 0;
        }

        value_attr.key = "value";
        value_attr.kind = AIVM_NODE_ATTR_STRING;
        value_attr.string_value = (arg != NULL) ? arg : "";
        if (!aivm_vm_create_node_record(vm, "Lit", node_id, &value_attr, 1U, NULL, 0U, &child_handles[i])) {
            return 0;
        }
        if (!aivm_size_add_checked(i, 2U, &child_handle_index) ||
            child_handle_index > (size_t)INT64_MAX ||
            child_handles[i] != (int64_t)child_handle_index) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv child handle invariant violated.");
            return 0;
        }
    }

    return 1;
}

static int collect_safe_point_internal(AivmVm* vm, int collect_bytes)
{
    if (vm == NULL) {
        return 0;
    }
    if (vm->node_count > 0U) {
        if (!aivm_vm_compact_node_arenas_with_map(vm, NULL, 0U, NULL)) {
            return 0;
        }
        vm->node_allocations_since_gc = 0U;
    }
    if (vm->scratch_pair_count > 0U && !aivm_vm_compact_scratch_pairs(vm)) {
        return 0;
    }
    if (vm->string_arena_used > 0U && !aivm_compact_string_arena(vm)) {
        return 0;
    }
    if (collect_bytes != 0 && vm->bytes_arena_used > 0U && !aivm_compact_bytes_arena(vm)) {
        return 0;
    }
    return 1;
}

int aivm_collect_safe_point(AivmVm* vm)
{
    return collect_safe_point_internal(vm, 1);
}

void aivm_halt(AivmVm* vm)
{
    if (vm == NULL || vm->program == NULL) {
        return;
    }

    vm->instruction_pointer = vm->program->instruction_count;
    vm->status = AIVM_VM_STATUS_HALTED;
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
        if (!aivm_size_add_checked(count, 1U, &next_count) ||
            !aivm_size_add_checked(index, 1U, &next_index)) {
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
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
        return 0;
    }
    for (i = 0U; i < arg_count; i += 1U) {
        size_t local_index = 0U;
        size_t instruction_index = 0U;
        size_t j;
        const AivmInstruction* instruction;
        if (!aivm_size_add_checked(target, i, &instruction_index) ||
            instruction_index >= program->instruction_count) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call target layout exceeded instruction range.");
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
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
                return 0;
            }
        }
        seen[seen_count] = local_index;
        if (!aivm_size_add_checked(seen_count, 1U, &next_seen_count)) {
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call target local layout count overflow.");
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
        aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Program instruction buffer is null.");
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
    aivm_vm_record_recent_opcode(vm, vm->instruction_pointer, (int)instruction->opcode, vm->stack_count);
#if defined(AIVM_DEBUG_RUNTIME)
    vm->profile_instruction_count += 1U;
    if ((size_t)instruction->opcode < (sizeof(vm->profile_opcode_counts) / sizeof(vm->profile_opcode_counts[0]))) {
        vm->profile_opcode_counts[(size_t)instruction->opcode] += 1U;
    }
#endif

    switch (instruction->opcode) {
        case AIVM_OP_NOP:
            vm->instruction_pointer += 1U;
            break;

        case AIVM_OP_HALT:
            aivm_halt(vm);
            break;

        case AIVM_OP_STUB:
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_OPCODE, "STUB opcode is invalid at runtime.");
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
        aivm_vm_set_local_out_of_range_error(vm, "load", local_index, locals_base);
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
            if (!aivm_vm_value_is_numeric(left) || !aivm_vm_value_is_numeric(right)) {
                aivm_vm_set_add_numeric_type_error(vm, left, right);
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (left.type == AIVM_VAL_INT && right.type == AIVM_VAL_INT) {
                if (!aivm_stack_push(vm, aivm_value_int(left.int_value + right.int_value))) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else if (!aivm_stack_push(vm, aivm_vm_numeric_result(aivm_vm_value_as_number(left) + aivm_vm_value_as_number(right)))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_SUB_NUM:
        case AIVM_OP_MUL_NUM:
        case AIVM_OP_DIV_NUM:
        case AIVM_OP_MOD_NUM:
        case AIVM_OP_POW_NUM: {
            AivmValue right;
            AivmValue left;
            double left_number;
            double right_number;
            double result_number;
            if (!aivm_stack_pop(vm, &right) || !aivm_stack_pop(vm, &left)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_vm_value_is_numeric(left) || !aivm_vm_value_is_numeric(right)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "Numeric operation requires number operands.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            left_number = aivm_vm_value_as_number(left);
            right_number = aivm_vm_value_as_number(right);
            if (instruction->opcode == AIVM_OP_SUB_NUM) {
                if (left.type == AIVM_VAL_INT && right.type == AIVM_VAL_INT) {
                    if (!aivm_stack_push(vm, aivm_value_int(left.int_value - right.int_value))) {
                        vm->instruction_pointer = vm->program->instruction_count;
                        break;
                    }
                    vm->instruction_pointer += 1U;
                    break;
                }
                result_number = left_number - right_number;
            } else if (instruction->opcode == AIVM_OP_MUL_NUM) {
                if (left.type == AIVM_VAL_INT && right.type == AIVM_VAL_INT) {
                    if (!aivm_stack_push(vm, aivm_value_int(left.int_value * right.int_value))) {
                        vm->instruction_pointer = vm->program->instruction_count;
                        break;
                    }
                    vm->instruction_pointer += 1U;
                    break;
                }
                result_number = left_number * right_number;
            } else if (instruction->opcode == AIVM_OP_DIV_NUM) {
                if (right_number == 0.0) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "DIV requires non-zero divisor.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                result_number = left_number / right_number;
            } else if (instruction->opcode == AIVM_OP_MOD_NUM) {
                double quotient;
                if (right_number == 0.0) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MOD requires non-zero divisor.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                if (left.type == AIVM_VAL_INT && right.type == AIVM_VAL_INT) {
                    if (!aivm_stack_push(vm, aivm_value_int(left.int_value % right.int_value))) {
                        vm->instruction_pointer = vm->program->instruction_count;
                        break;
                    }
                    vm->instruction_pointer += 1U;
                    break;
                }
                quotient = aivm_vm_double_trunc_toward_zero(left_number / right_number);
                result_number = left_number - (quotient * right_number);
            } else {
                result_number = aivm_vm_double_pow_whole(left_number, right_number);
            }
            if (!aivm_stack_push(vm, aivm_vm_numeric_result(result_number))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_LT_NUM: {
            AivmValue right;
            AivmValue left;
            if (!aivm_stack_pop(vm, &right) || !aivm_stack_pop(vm, &left)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_vm_value_is_numeric(left) || !aivm_vm_value_is_numeric(right)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "LT requires number operands.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_bool(aivm_vm_value_as_number(left) < aivm_vm_value_as_number(right) ? 1 : 0))) {
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Jump target out of range.");
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "JUMP_IF_FALSE requires bool.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (condition.bool_value == 0) {
                if (target > vm->program->instruction_count) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Jump target out of range.");
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
            int is_tail_call = 0;
            if (!operand_to_index(vm, instruction->operand_int, &target)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (target >= vm->program->instruction_count) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call target out of range.");
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
            aivm_vm_record_recent_call(vm, vm->instruction_pointer, target, arg_count, vm->stack_count);
            frame_base = vm->stack_count - arg_count;
            if (vm->call_frame_count > 0U) {
                const AivmCallFrame* current_frame = &vm->call_frames[vm->call_frame_count - 1U];
                if (frame_base < current_frame->frame_base) {
                    (void)snprintf(
                        vm->error_detail_storage,
                        sizeof(vm->error_detail_storage),
                        "Call argument frame crosses caller frame base. target=%llu argCount=%llu argBase=%llu callerFrameBase=%llu stackCount=%llu pc=%llu",
                        (unsigned long long)target,
                        (unsigned long long)arg_count,
                        (unsigned long long)frame_base,
                        (unsigned long long)current_frame->frame_base,
                        (unsigned long long)vm->stack_count,
                        (unsigned long long)vm->instruction_pointer);
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, vm->error_detail_storage);
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            }
            if (vm->call_frame_count > 0U &&
                vm->instruction_pointer + 1U < vm->program->instruction_count &&
                (vm->program->instructions[vm->instruction_pointer + 1U].opcode == AIVM_OP_RETURN ||
                 vm->program->instructions[vm->instruction_pointer + 1U].opcode == AIVM_OP_RET)) {
                is_tail_call = 1;
            }
            if (is_tail_call != 0) {
                AivmCallFrame* current_frame = &vm->call_frames[vm->call_frame_count - 1U];
                size_t tail_frame_base = current_frame->frame_base;
                if (tail_frame_base > frame_base) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Tail-call frame base exceeds argument base.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                if (arg_count > 0U) {
                    memmove(
                        &vm->stack[tail_frame_base],
                        &vm->stack[frame_base],
                        arg_count * sizeof(vm->stack[0]));
                }
                vm->stack_count = tail_frame_base + arg_count;
                vm->locals_count = current_frame->locals_base;
                vm->instruction_pointer = target;
                break;
            }
            if (!aivm_size_add_checked(vm->instruction_pointer, 1U, &return_ip) ||
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Return instruction pointer out of range.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (vm->stack_count < frame.frame_base) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call frame base exceeds stack depth.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            pre_restore_stack_count = vm->stack_count;
            if (vm->stack_count > frame.frame_base) {
                return_value = vm->stack[vm->stack_count - 1U];
                has_return_value = 1;
            }
            if (!validate_vm_return_restore(vm, &frame, pre_restore_stack_count)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->stack_count = frame.frame_base;
            vm->locals_count = frame.locals_base;
            if (has_return_value != 0) {
                if (!aivm_stack_push(vm, return_value)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            }
            aivm_vm_record_recent_return(
                vm,
                frame.return_instruction_pointer,
                vm->stack_count,
                pre_restore_stack_count,
                frame.frame_base,
                has_return_value);
            {
                int collect_nodes = aivm_vm_should_attempt_return_safe_point(vm);
                int collect_bytes = aivm_vm_should_attempt_bytes_return_safe_point(vm);
                if ((collect_nodes || collect_bytes) && !collect_safe_point_internal(vm, collect_bytes)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            }
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "EQ_INT requires int operands.");
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid CONST index.");
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
            char* left_copy = NULL;
            char* right_copy = NULL;
            const char* left_source;
            const char* right_source;

            if (!aivm_stack_pop(vm, &right) || !aivm_stack_pop(vm, &left)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (left.type != AIVM_VAL_STRING ||
                right.type != AIVM_VAL_STRING ||
                left.string_value == NULL ||
                right.string_value == NULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_CONCAT requires string operands.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            while (left.string_value[left_length] != '\0') {
                if (!aivm_size_add_checked(left_length, 1U, &next_length)) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String concat left length overflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                left_length = next_length;
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                break;
            }
            while (right.string_value[right_length] != '\0') {
                if (!aivm_size_add_checked(right_length, 1U, &next_length)) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String concat right length overflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                right_length = next_length;
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                break;
            }

            if (!aivm_size_add_checked(left_length, right_length, &total_length) ||
                !aivm_size_add_checked(total_length, 1U, &bytes_needed)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String concat size arithmetic overflow.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            left_source = aivm_vm_snapshot_arena_backed_string(vm, left.string_value, left_length, &left_copy);
            right_source = aivm_vm_snapshot_arena_backed_string(vm, right.string_value, right_length, &right_copy);
            if (left_source == NULL || right_source == NULL) {
                free(left_copy);
                free(right_copy);
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String concat operand snapshot failed.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            output = aivm_string_arena_alloc(vm, bytes_needed);
            if (output == NULL) {
                free(left_copy);
                free(right_copy);
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            for (i = 0U; i < left_length; i += 1U) {
                output[i] = left_source[i];
            }
            for (i = 0U; i < right_length; i += 1U) {
                size_t output_slot = 0U;
                if (!aivm_size_add_checked(left_length, i, &output_slot) ||
                    output_slot >= total_length) {
                    free(left_copy);
                    free(right_copy);
                    aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "String concat output slot overflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                output[output_slot] = right_source[i];
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                free(left_copy);
                free(right_copy);
                break;
            }
            output[left_length + right_length] = '\0';
            free(left_copy);
            free(right_copy);

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
            char number_buffer[64];
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
                    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "TO_STRING input string must be non-null.");
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
            if (value.type == AIVM_VAL_NUMBER) {
                (void)snprintf(number_buffer, sizeof(number_buffer), "%.15g", value.number_value);
                if (!push_string_copy(vm, number_buffer)) {
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
                    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "TO_STRING bytes data must be non-null.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                if (!aivm_size_add_checked(value.bytes_value.length, value.bytes_value.length, &body_len) ||
                    !aivm_size_add_checked(2U, body_len, &out_len) ||
                    !aivm_size_add_checked(out_len, 1U, &bytes_needed)) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "TO_STRING bytes size arithmetic overflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                bytes_output = aivm_string_arena_alloc(vm, bytes_needed);
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
                    if (!aivm_size_add_checked(i, i, &body_offset) ||
                        !aivm_size_add_checked(2U, body_offset, &high_slot) ||
                        !aivm_size_add_checked(high_slot, 1U, &low_slot) ||
                        low_slot >= out_len) {
                        aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "TO_STRING bytes output slot overflow.");
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

            aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "TO_STRING unsupported value kind.");
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_ESCAPE requires string operand.");
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_SUBSTRING requires (string,int,int).");
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_REMOVE requires (string,int,int).");
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

        case AIVM_OP_STR_FIND: {
            AivmValue start_value;
            AivmValue pattern_value;
            AivmValue text_value;
            if (!aivm_stack_pop(vm, &start_value) ||
                !aivm_stack_pop(vm, &pattern_value) ||
                !aivm_stack_pop(vm, &text_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (text_value.type != AIVM_VAL_STRING ||
                pattern_value.type != AIVM_VAL_STRING ||
                start_value.type != AIVM_VAL_INT ||
                text_value.string_value == NULL ||
                pattern_value.string_value == NULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_FIND requires (string,string,int).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!push_find_by_runes(vm, text_value.string_value, pattern_value.string_value, start_value.int_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_STR_FROM_CODEPOINT: {
            AivmValue codepoint_value;
            if (!aivm_stack_pop(vm, &codepoint_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (codepoint_value.type != AIVM_VAL_INT) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_FROM_CODEPOINT requires int operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (codepoint_value.int_value < 0) {
                if (!push_string_copy(vm, "")) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else if (!push_string_from_codepoint(vm, (uint32_t)codepoint_value.int_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_STR_DECODE_UNICODE_HEX4: {
            AivmValue text_value;
            uint32_t cp;
            if (!aivm_stack_pop(vm, &text_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (text_value.type != AIVM_VAL_STRING || text_value.string_value == NULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_DECODE_UNICODE_HEX4 requires string operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_hex4_to_u32(text_value.string_value, &cp)) {
                if (!push_string_copy(vm, "")) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else if (!push_string_from_codepoint(vm, cp)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_STR_DECODE_UNICODE_SURROGATE_PAIR_HEX4: {
            AivmValue low_value;
            AivmValue high_value;
            uint32_t high_surrogate;
            uint32_t low_surrogate;
            uint32_t cp;
            if (!aivm_stack_pop(vm, &low_value) ||
                !aivm_stack_pop(vm, &high_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (high_value.type != AIVM_VAL_STRING ||
                low_value.type != AIVM_VAL_STRING ||
                high_value.string_value == NULL ||
                low_value.string_value == NULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_DECODE_UNICODE_SURROGATE_PAIR_HEX4 requires (string,string).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_hex4_to_u32(high_value.string_value, &high_surrogate) ||
                !aivm_hex4_to_u32(low_value.string_value, &low_surrogate) ||
                high_surrogate < 0xD800U || high_surrogate > 0xDBFFU ||
                low_surrogate < 0xDC00U || low_surrogate > 0xDFFFU) {
                if (!push_string_copy(vm, "")) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else {
                cp = 0x10000U + ((high_surrogate - 0xD800U) << 10U) + (low_surrogate - 0xDC00U);
                if (!push_string_from_codepoint(vm, cp)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_LENGTH: {
            AivmValue bytes_value;
            if (!aivm_stack_pop(vm, &bytes_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (bytes_value.type != AIVM_VAL_BYTES) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_LENGTH requires bytes operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_int((int64_t)bytes_value.bytes_value.length))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_AT: {
            AivmValue index_value;
            AivmValue bytes_value;
            size_t index;
            if (!aivm_stack_pop(vm, &index_value) ||
                !aivm_stack_pop(vm, &bytes_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (bytes_value.type != AIVM_VAL_BYTES || index_value.type != AIVM_VAL_INT) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_AT requires (bytes,int).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (index_value.int_value < 0) {
                if (!aivm_stack_push(vm, aivm_value_int(-1))) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else {
                index = (size_t)index_value.int_value;
                if (index >= bytes_value.bytes_value.length || bytes_value.bytes_value.data == NULL) {
                    if (!aivm_stack_push(vm, aivm_value_int(-1))) {
                        vm->instruction_pointer = vm->program->instruction_count;
                        break;
                    }
                } else if (!aivm_stack_push(vm, aivm_value_int((int64_t)bytes_value.bytes_value.data[index]))) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_SLICE: {
            AivmValue length_value;
            AivmValue start_value;
            AivmValue bytes_value;
            size_t start;
            size_t length;
            size_t end;
            if (!aivm_stack_pop(vm, &length_value) ||
                !aivm_stack_pop(vm, &start_value) ||
                !aivm_stack_pop(vm, &bytes_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (bytes_value.type != AIVM_VAL_BYTES ||
                start_value.type != AIVM_VAL_INT ||
                length_value.type != AIVM_VAL_INT) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_SLICE requires (bytes,int,int).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (start_value.int_value <= 0) {
                start = 0U;
            } else if ((uint64_t)start_value.int_value >= (uint64_t)bytes_value.bytes_value.length) {
                start = bytes_value.bytes_value.length;
            } else {
                start = (size_t)start_value.int_value;
            }
            length = (length_value.int_value <= 0) ? 0U : (size_t)length_value.int_value;
            end = start + length;
            if (end < start || end > bytes_value.bytes_value.length) {
                end = bytes_value.bytes_value.length;
            }
            if (start >= end || bytes_value.bytes_value.data == NULL) {
                if (!push_bytes_copy(vm, NULL, 0U)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else if (!push_bytes_copy(vm, &bytes_value.bytes_value.data[start], end - start)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_CONCAT: {
            AivmValue right_value;
            AivmValue left_value;
            size_t total_length;
            uint8_t* output;
            if (!aivm_stack_pop(vm, &right_value) ||
                !aivm_stack_pop(vm, &left_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (left_value.type != AIVM_VAL_BYTES || right_value.type != AIVM_VAL_BYTES) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_CONCAT requires (bytes,bytes).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_size_add_checked(left_value.bytes_value.length, right_value.bytes_value.length, &total_length)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM002: bytes arena capacity exceeded.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            output = aivm_bytes_arena_alloc(vm, total_length);
            if (output == NULL) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (left_value.bytes_value.length > 0U) {
                if (left_value.bytes_value.data == NULL) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_CONCAT requires non-null left bytes data.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                memcpy(output, left_value.bytes_value.data, left_value.bytes_value.length);
            }
            if (right_value.bytes_value.length > 0U) {
                if (right_value.bytes_value.data == NULL) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_CONCAT requires non-null right bytes data.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                memcpy(output + left_value.bytes_value.length, right_value.bytes_value.data, right_value.bytes_value.length);
            }
            if (!aivm_stack_push(vm, aivm_value_bytes(output, total_length))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_FROM_UTF8_STRING: {
            AivmValue text_value;
            size_t length;
            if (!aivm_stack_pop(vm, &text_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (text_value.type != AIVM_VAL_STRING || text_value.string_value == NULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_FROM_UTF8_STRING requires string operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            length = strlen(text_value.string_value);
            if (!push_bytes_copy(vm, (const uint8_t*)text_value.string_value, length)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_TO_UTF8_STRING: {
            AivmValue bytes_value;
            if (!aivm_stack_pop(vm, &bytes_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (bytes_value.type != AIVM_VAL_BYTES) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_TO_UTF8_STRING requires bytes operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (bytes_value.bytes_value.length == 0U) {
                if (!push_string_copy(vm, "")) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else if (bytes_value.bytes_value.data == NULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_TO_UTF8_STRING requires non-null bytes data.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            } else if (!aivm_bytes_is_valid_utf8_without_nul(bytes_value.bytes_value.data, bytes_value.bytes_value.length)) {
                if (!push_string_copy(vm, "")) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            } else if (!push_string_bytes_copy(vm, bytes_value.bytes_value.data, bytes_value.bytes_value.length)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_FROM_BASE64: {
            AivmValue text_value;
            size_t output_length = 0U;
            uint8_t* output;
            if (!aivm_stack_pop(vm, &text_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (text_value.type != AIVM_VAL_STRING || text_value.string_value == NULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_FROM_BASE64 requires string operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_bytes_from_base64(text_value.string_value, NULL, 0U, &output_length)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "BYTES_FROM_BASE64 invalid base64 input.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            output = aivm_bytes_arena_alloc(vm, output_length);
            if (output == NULL) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_bytes_from_base64(text_value.string_value, output, output_length, &output_length)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "BYTES_FROM_BASE64 invalid base64 input.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_bytes(output, output_length))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_TO_BASE64: {
            AivmValue bytes_value;
            size_t output_length;
            size_t output_capacity;
            char* output;
            if (!aivm_stack_pop(vm, &bytes_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (bytes_value.type != AIVM_VAL_BYTES) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_TO_BASE64 requires bytes operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (bytes_value.bytes_value.length > 0U && bytes_value.bytes_value.data == NULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_TO_BASE64 requires non-null bytes data.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (bytes_value.bytes_value.length > ((SIZE_MAX - 2U) / 3U)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            output_length = ((bytes_value.bytes_value.length + 2U) / 3U) * 4U;
            if (!aivm_size_add_checked(output_length, 1U, &output_capacity)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "AIVMM001: string arena capacity exceeded.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            output = aivm_string_arena_alloc(vm, output_capacity);
            if (output == NULL) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_bytes_to_base64(bytes_value.bytes_value.data, bytes_value.bytes_value.length, output, output_capacity)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "BYTES_TO_BASE64 failed.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_string(output))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_FROM_BYTE: {
            AivmValue byte_value;
            uint8_t output[1];
            if (!aivm_stack_pop(vm, &byte_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (byte_value.type != AIVM_VAL_INT || byte_value.int_value < 0 || byte_value.int_value > 255) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_FROM_BYTE requires int in range 0..255.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            output[0] = (uint8_t)byte_value.int_value;
            if (!push_bytes_copy(vm, output, sizeof(output))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_U32_LE: {
            AivmValue int_value;
            uint64_t encoded;
            uint8_t output[4];
            if (!aivm_stack_pop(vm, &int_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (int_value.type != AIVM_VAL_INT || int_value.int_value < 0 || (uint64_t)int_value.int_value > 0xFFFFFFFFULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_U32_LE requires int in range 0..4294967295.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            encoded = (uint64_t)int_value.int_value;
            output[0] = (uint8_t)(encoded & 0xFFU);
            output[1] = (uint8_t)((encoded >> 8U) & 0xFFU);
            output[2] = (uint8_t)((encoded >> 16U) & 0xFFU);
            output[3] = (uint8_t)((encoded >> 24U) & 0xFFU);
            if (!push_bytes_copy(vm, output, sizeof(output))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_BYTES_I64_LE: {
            AivmValue int_value;
            uint64_t encoded;
            uint8_t output[8];
            if (!aivm_stack_pop(vm, &int_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (int_value.type != AIVM_VAL_INT) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "BYTES_I64_LE requires int operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            encoded = (uint64_t)int_value.int_value;
            output[0] = (uint8_t)(encoded & 0xFFU);
            output[1] = (uint8_t)((encoded >> 8U) & 0xFFU);
            output[2] = (uint8_t)((encoded >> 16U) & 0xFFU);
            output[3] = (uint8_t)((encoded >> 24U) & 0xFFU);
            output[4] = (uint8_t)((encoded >> 32U) & 0xFFU);
            output[5] = (uint8_t)((encoded >> 40U) & 0xFFU);
            output[6] = (uint8_t)((encoded >> 48U) & 0xFFU);
            output[7] = (uint8_t)((encoded >> 56U) & 0xFFU);
            if (!push_bytes_copy(vm, output, sizeof(output))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_MAKE_PAIR: {
            AivmValue second;
            AivmValue first;
            int64_t handle;
            if (!aivm_stack_pop(vm, &second) ||
                !aivm_stack_pop(vm, &first)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_vm_create_scratch_pair(vm, first, second, &handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_pair(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_PAIR_FIRST:
        case AIVM_OP_PAIR_SECOND: {
            AivmValue pair_value;
            const AivmScratchPair* pair;
            if (!aivm_stack_pop(vm, &pair_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (pair_value.type != AIVM_VAL_PAIR ||
                !aivm_vm_lookup_scratch_pair(vm, pair_value.pair_handle, &pair)) {
                aivm_set_vm_error(
                    vm,
                    AIVM_VM_ERR_TYPE_MISMATCH,
                    instruction->opcode == AIVM_OP_PAIR_FIRST
                        ? "PAIR_FIRST requires pair operand."
                        : "PAIR_SECOND requires pair operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(
                    vm,
                    instruction->opcode == AIVM_OP_PAIR_FIRST ? pair->first : pair->second)) {
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
            int64_t handle;
            if (!operand_to_index(vm, instruction->operand_int, &target)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!start_call_subroutine_worker(vm, target, &handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, aivm_value_int(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "AWAIT requires valid task handle.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!find_terminal_task_result(vm, handle_value.int_value, &completed)) {
                if (vm->status != AIVM_VM_STATUS_ERROR) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "AWAIT requires valid task handle.");
                }
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_push(vm, completed)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!release_consumed_task_result(vm, handle_value.int_value)) {
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
            if (!aivm_size_add_checked(vm->par_context_count, 1U, &needed_context_count) ||
                needed_context_count > AIVM_VM_PAR_CONTEXT_CAPACITY) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_BEGIN exceeded context capacity.");
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_FORK requires active Par context.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_size_add_checked(vm->par_value_count, 1U, &needed_value_count) ||
                needed_value_count > AIVM_VM_PAR_VALUE_CAPACITY) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_FORK exceeded value capacity.");
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
            int64_t consumed_task_handles[AIVM_VM_NODE_CHILD_CAPACITY];
            size_t consumed_task_count = 0U;
            char id_buffer[32];
            size_t id_length;
            size_t i;
            int64_t block_handle;
            if (!operand_to_index(vm, instruction->operand_int, &join_count)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (vm->par_context_count == 0U) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN requires active Par context.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            context = vm->par_contexts[vm->par_context_count - 1U];
            if (context.expected_count != join_count ||
                vm->par_value_count < context.start_index ||
                (vm->par_value_count - context.start_index) != join_count) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN branch count mismatch.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (join_count > AIVM_VM_NODE_CHILD_CAPACITY) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN exceeded child capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            for (i = 0U; i < join_count; i += 1U) {
                size_t par_index = 0U;
                AivmValue value;
                AivmValue task_result;
                int64_t child_handle;
                if (!aivm_size_add_checked(context.start_index, i, &par_index) ||
                    par_index >= vm->par_value_count) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN value index was invalid.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                value = vm->par_values[par_index];
                if (value.type == AIVM_VAL_INT &&
                    find_terminal_task_result(vm, value.int_value, &task_result)) {
                    consumed_task_handles[consumed_task_count] = value.int_value;
                    consumed_task_count += 1U;
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_JOIN failed to build block id.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->next_par_node_id += 1;
            if (!aivm_vm_create_node_record(vm, "Block", id_buffer, NULL, 0U, child_handles, join_count, &block_handle)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->par_context_count -= 1U;
            vm->par_value_count = context.start_index;
            if (!aivm_stack_push(vm, aivm_value_node(block_handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            for (i = 0U; i < consumed_task_count; i += 1U) {
                if (!release_consumed_task_result(vm, consumed_task_handles[i])) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
            }
            if (vm->status == AIVM_VM_STATUS_ERROR) {
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "STR_UTF8_BYTE_COUNT requires string operand.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            while (value.string_value[count] != '\0') {
                if (count == INT64_MAX) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "STR_UTF8_BYTE_COUNT overflow.");
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
            if (node_value.type != AIVM_VAL_NODE || !aivm_vm_lookup_node(vm, node_value.node_handle, &node)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "NODE_KIND requires node operand.");
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
            if (node_value.type != AIVM_VAL_NODE || !aivm_vm_lookup_node(vm, node_value.node_handle, &node)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "NODE_ID requires node operand.");
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
            if (node_value.type != AIVM_VAL_NODE || !aivm_vm_lookup_node(vm, node_value.node_handle, &node)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "ATTR_COUNT requires node operand.");
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
            if (node_value.type != AIVM_VAL_NODE || index_value.type != AIVM_VAL_INT || !aivm_vm_lookup_node(vm, node_value.node_handle, &node)) {
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, attr_error);
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
            if (node_value.type != AIVM_VAL_NODE || !aivm_vm_lookup_node(vm, node_value.node_handle, &node)) {
                (void)snprintf(
                    vm->error_detail_storage,
                    sizeof(vm->error_detail_storage),
                    "CHILD_COUNT requires node operand; actual=%s handle=%lld nodeCount=%llu.",
                    aivm_vm_value_type_name(node_value.type),
                    (long long)node_value.node_handle,
                    (unsigned long long)vm->node_count);
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, vm->error_detail_storage);
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
            if (node_value.type != AIVM_VAL_NODE || index_value.type != AIVM_VAL_INT || !aivm_vm_lookup_node(vm, node_value.node_handle, &node)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "CHILD_AT requires (node,int).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (index_value.int_value < 0 || (size_t)index_value.int_value >= node->child_count) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "CHILD_AT index out of range.");
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_BLOCK requires string id.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_vm_create_node_record(vm, "Block", id_value.string_value, NULL, 0U, NULL, 0U, &handle)) {
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
            int64_t* new_children = NULL;
            AivmNodeAttr* attrs = NULL;
            int64_t handle;
            size_t needed_child_count = 0U;
            size_t i;
            if (!aivm_stack_pop(vm, &child_value) || !aivm_stack_pop(vm, &node_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_value.type != AIVM_VAL_NODE || child_value.type != AIVM_VAL_NODE ||
                !aivm_vm_lookup_node(vm, node_value.node_handle, &base_node)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "APPEND_CHILD requires (node,node).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            child_handle = child_value.node_handle;
            if (!aivm_vm_lookup_node(vm, child_handle, &child_node)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "APPEND_CHILD child node handle was invalid.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            (void)child_node;
            if (base_node->attr_count > AIVM_VM_NODE_ATTR_CAPACITY ||
                !aivm_size_add_checked(base_node->child_count, 1U, &needed_child_count) ||
                needed_child_count > AIVM_VM_NODE_CHILD_CAPACITY) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_CHILD exceeded VM node capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            new_children = (int64_t*)calloc(needed_child_count, sizeof(int64_t));
            attrs = (AivmNodeAttr*)calloc(base_node->attr_count == 0U ? 1U : base_node->attr_count, sizeof(AivmNodeAttr));
            if (new_children == NULL || attrs == NULL) {
                free(new_children);
                free(attrs);
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "APPEND_CHILD scratch allocation failed.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            for (i = 0U; i < base_node->attr_count; i += 1U) {
                size_t attr_slot = 0U;
                if (!aivm_size_add_checked(base_node->attr_start, i, &attr_slot) ||
                    attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY) {
                    free(new_children);
                    free(attrs);
                    new_children = NULL;
                    attrs = NULL;
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_CHILD attr slot was invalid.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attrs[i] = vm->node_attrs[attr_slot];
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                if (new_children != NULL) {
                    free(new_children);
                }
                if (attrs != NULL) {
                    free(attrs);
                }
                break;
            }
            for (i = 0U; i < base_node->child_count; i += 1U) {
                size_t child_slot = 0U;
                if (!aivm_size_add_checked(base_node->child_start, i, &child_slot) ||
                    child_slot >= AIVM_VM_NODE_CHILD_CAPACITY) {
                    free(new_children);
                    free(attrs);
                    new_children = NULL;
                    attrs = NULL;
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_CHILD child slot was invalid.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                new_children[i] = vm->node_children[child_slot];
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                if (new_children != NULL) {
                    free(new_children);
                }
                if (attrs != NULL) {
                    free(attrs);
                }
                break;
            }
            new_children[base_node->child_count] = child_handle;
            if (!aivm_vm_create_node_record(
                vm,
                base_node->kind,
                base_node->id,
                attrs,
                base_node->attr_count,
                new_children,
                needed_child_count,
                &handle)) {
                free(new_children);
                free(attrs);
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            free(new_children);
            free(attrs);
            if (!aivm_stack_push(vm, aivm_value_node(handle))) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_APPEND_ATTR: {
            AivmValue attr_value;
            AivmValue node_value;
            const AivmNodeRecord* base_node;
            const AivmNodeRecord* attr_node;
            AivmNodeAttr* attrs = NULL;
            int64_t* children = NULL;
            int64_t handle;
            size_t needed_attr_count = 0U;
            size_t i;
            if (!aivm_stack_pop(vm, &attr_value) || !aivm_stack_pop(vm, &node_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (node_value.type != AIVM_VAL_NODE || attr_value.type != AIVM_VAL_NODE ||
                !aivm_vm_lookup_node(vm, node_value.node_handle, &base_node) ||
                !aivm_vm_lookup_node(vm, attr_value.node_handle, &attr_node)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "APPEND_ATTR requires (node,node).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (attr_node->attr_count != 1U ||
                !aivm_size_add_checked(base_node->attr_count, 1U, &needed_attr_count) ||
                needed_attr_count > AIVM_VM_NODE_ATTR_CAPACITY ||
                base_node->child_count > AIVM_VM_NODE_CHILD_CAPACITY) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_ATTR requires single-attr node and capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            attrs = (AivmNodeAttr*)calloc(needed_attr_count, sizeof(AivmNodeAttr));
            children = (int64_t*)calloc(base_node->child_count == 0U ? 1U : base_node->child_count, sizeof(int64_t));
            if (attrs == NULL || children == NULL) {
                free(attrs);
                free(children);
                aivm_set_vm_error(vm, AIVM_VM_ERR_MEMORY_PRESSURE, "APPEND_ATTR scratch allocation failed.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            for (i = 0U; i < base_node->attr_count; i += 1U) {
                size_t attr_slot = 0U;
                if (!aivm_size_add_checked(base_node->attr_start, i, &attr_slot) ||
                    attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY) {
                    free(attrs);
                    free(children);
                    attrs = NULL;
                    children = NULL;
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_ATTR attr slot was invalid.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attrs[i] = vm->node_attrs[attr_slot];
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                if (attrs != NULL) {
                    free(attrs);
                }
                if (children != NULL) {
                    free(children);
                }
                break;
            }
            attrs[base_node->attr_count] = vm->node_attrs[attr_node->attr_start];
            attrs[base_node->attr_count].key = attr_node->id;
            for (i = 0U; i < base_node->child_count; i += 1U) {
                size_t child_slot = 0U;
                if (!aivm_size_add_checked(base_node->child_start, i, &child_slot) ||
                    child_slot >= AIVM_VM_NODE_CHILD_CAPACITY) {
                    free(attrs);
                    free(children);
                    attrs = NULL;
                    children = NULL;
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_ATTR child slot was invalid.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                children[i] = vm->node_children[child_slot];
            }
            if (vm->instruction_pointer == vm->program->instruction_count) {
                if (attrs != NULL) {
                    free(attrs);
                }
                if (children != NULL) {
                    free(children);
                }
                break;
            }
            if (!aivm_vm_create_node_record(
                vm,
                base_node->kind,
                base_node->id,
                attrs,
                needed_attr_count,
                children,
                base_node->child_count,
                &handle)) {
                free(attrs);
                free(children);
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            free(attrs);
            free(children);
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_ERR requires (string,string,string,string).");
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
            if (!aivm_vm_create_node_record(vm, "Err", id_value.string_value, attrs, 3U, NULL, 0U, &handle)) {
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
                    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_LIT_STRING requires (string,string).");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attr.kind = AIVM_NODE_ATTR_STRING;
                attr.string_value = value.string_value;
            } else if (instruction->opcode == AIVM_OP_MAKE_LIT_INT) {
                if (id_value.type != AIVM_VAL_STRING || id_value.string_value == NULL ||
                    value.type != AIVM_VAL_INT) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_LIT_INT requires (string,int).");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attr.kind = AIVM_NODE_ATTR_INT;
                attr.int_value = value.int_value;
            } else {
                if (id_value.type != AIVM_VAL_STRING || id_value.string_value == NULL ||
                    value.type != AIVM_VAL_BOOL) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_LIT_BOOL requires (string,bool).");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attr.kind = AIVM_NODE_ATTR_BOOL;
                attr.bool_value = value.bool_value != 0 ? 1 : 0;
            }
            if (!aivm_vm_create_node_record(vm, "Lit", id_value.string_value, &attr, 1U, NULL, 0U, &handle)) {
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
                !aivm_vm_lookup_node(vm, template_value.node_handle, &template_node)) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_NODE requires (node,int>=0).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            argc = (size_t)argc_value.int_value;
            if (argc > AIVM_VM_NODE_CHILD_CAPACITY ||
                template_node->attr_count > AIVM_VM_NODE_ATTR_CAPACITY ||
                vm->stack_count < argc) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_NODE arguments exceeded VM limits.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            for (i = 0U; i < template_node->attr_count; i += 1U) {
                size_t attr_slot = 0U;
                if (!aivm_size_add_checked(template_node->attr_start, i, &attr_slot) ||
                    attr_slot >= AIVM_VM_NODE_ATTR_CAPACITY) {
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_NODE attr slot was invalid.");
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
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_NODE child index underflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                children[child_index] = child_handle;
            }
            if (vm->status == AIVM_VM_STATUS_ERROR) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            if (!aivm_vm_create_node_record(
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

        case AIVM_OP_MAKE_NODE_EMPTY: {
            AivmValue id_value;
            AivmValue kind_value;
            int64_t handle;
            if (!aivm_stack_pop(vm, &id_value) ||
                !aivm_stack_pop(vm, &kind_value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (kind_value.type != AIVM_VAL_STRING || kind_value.string_value == NULL ||
                id_value.type != AIVM_VAL_STRING || id_value.string_value == NULL) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_NODE_EMPTY requires (string,string).");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_vm_create_node_record(vm, kind_value.string_value, id_value.string_value, NULL, 0U, NULL, 0U, &handle)) {
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_FIELD_STRING requires (string,any).");
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
            if (!aivm_vm_create_node_record(vm, "Field", "field", attrs, 1U, children, 1U, &handle)) {
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
                aivm_set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_MAP requires int child count.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            count = (size_t)count_value.int_value;
            if (count > AIVM_VM_NODE_CHILD_CAPACITY || vm->stack_count < count) {
                aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_MAP count exceeded VM limits.");
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
                    aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "MAKE_MAP child index underflow.");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                children[child_index] = child_handle;
            }
            if (vm->status == AIVM_VM_STATUS_ERROR) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_vm_create_node_record(vm, "Map", "map", NULL, 0U, children, count, &handle)) {
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
            aivm_set_vm_error(vm, AIVM_VM_ERR_INVALID_OPCODE, "Unsupported opcode.");
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
