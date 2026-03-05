#include "aivm_vm.h"
#include <string.h>

static void set_vm_error(AivmVm* vm, AivmVmError error, const char* detail)
{
    if (vm == NULL) {
        return;
    }
    vm->error = error;
    vm->status = AIVM_VM_STATUS_ERROR;
    vm->error_detail = detail;
}

static const char* syscall_failure_detail(AivmSyscallStatus status, AivmContractStatus contract_status);
static const char* syscall_contract_failure_detail(AivmContractStatus status);
static int lookup_node(const AivmVm* vm, int64_t handle, const AivmNodeRecord** out_node);

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

static char* arena_alloc(AivmVm* vm, size_t size)
{
    char* start;
    if (vm == NULL) {
        return NULL;
    }
    if (vm->string_arena_used + size > AIVM_VM_STRING_ARENA_CAPACITY) {
        set_vm_error(vm, AIVM_VM_ERR_STRING_OVERFLOW, "VM string arena overflow.");
        return NULL;
    }

    start = &vm->string_arena[vm->string_arena_used];
    vm->string_arena_used += size;
    return start;
}

static char* copy_string_to_arena(AivmVm* vm, const char* input)
{
    size_t length = 0U;
    size_t i;
    char* output;
    if (vm == NULL || input == NULL) {
        return NULL;
    }
    while (input[length] != '\0') {
        length += 1U;
    }
    output = arena_alloc(vm, length + 1U);
    if (output == NULL) {
        return NULL;
    }
    for (i = 0U; i < length; i += 1U) {
        output[i] = input[i];
    }
    output[length] = '\0';
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

static int push_escaped_string(AivmVm* vm, const char* input)
{
    size_t length = 0U;
    size_t escaped_length = 0U;
    size_t i;
    size_t out_index = 0U;
    char* output;

    if (vm == NULL || input == NULL) {
        return 0;
    }

    while (input[length] != '\0') {
        char ch = input[length];
        if (ch == '\\' || ch == '"' || ch == '\n' || ch == '\r' || ch == '\t') {
            escaped_length += 2U;
        } else {
            escaped_length += 1U;
        }
        length += 1U;
    }

    output = arena_alloc(vm, escaped_length + 1U);
    if (output == NULL) {
        return 0;
    }

    for (i = 0U; i < length; i += 1U) {
        char ch = input[i];
        if (ch == '\\') {
            output[out_index++] = '\\';
            output[out_index++] = '\\';
        } else if (ch == '"') {
            output[out_index++] = '\\';
            output[out_index++] = '"';
        } else if (ch == '\n') {
            output[out_index++] = '\\';
            output[out_index++] = 'n';
        } else if (ch == '\r') {
            output[out_index++] = '\\';
            output[out_index++] = 'r';
        } else if (ch == '\t') {
            output[out_index++] = '\\';
            output[out_index++] = 't';
        } else {
            output[out_index++] = ch;
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
    if (text == NULL) {
        return 0U;
    }
    while (text[byte_index] != '\0') {
        byte_index = utf8_next_index(text, byte_index);
        count += 1U;
    }
    return count;
}

static size_t utf8_byte_offset_for_rune(const char* text, size_t rune_index)
{
    size_t byte_index = 0U;
    size_t current_rune = 0U;
    if (text == NULL) {
        return 0U;
    }
    while (text[byte_index] != '\0' && current_rune < rune_index) {
        byte_index = utf8_next_index(text, byte_index);
        current_rune += 1U;
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
    size_t i;
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
    output = arena_alloc(vm, copy_length + 1U);
    if (output == NULL) {
        return 0;
    }
    for (i = 0U; i < copy_length; i += 1U) {
        output[i] = text[start_byte + i];
    }
    output[copy_length] = '\0';
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
    size_t output_length;
    size_t i;
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
        input_length += 1U;
    }

    rune_count = utf8_rune_count(text);
    start_rune = clamp_rune_index(start, rune_count);
    end_rune = clamp_rune_index(start + length, rune_count);
    if (end_rune < start_rune) {
        end_rune = start_rune;
    }

    start_byte = utf8_byte_offset_for_rune(text, start_rune);
    end_byte = utf8_byte_offset_for_rune(text, end_rune);
    output_length = input_length - (end_byte - start_byte);
    output = arena_alloc(vm, output_length + 1U);
    if (output == NULL) {
        return 0;
    }

    for (i = 0U; i < start_byte; i += 1U) {
        output[i] = text[i];
    }
    for (; i < output_length; i += 1U) {
        output[i] = text[end_byte + (i - start_byte)];
    }
    output[output_length] = '\0';
    return aivm_stack_push(vm, aivm_value_string(output));
}

static int call_sys_with_arity(AivmVm* vm, size_t arg_count, AivmValue* out_result)
{
    AivmValue args[AIVM_VM_MAX_SYSCALL_ARGS];
    AivmValue target_value;
    AivmSyscallStatus syscall_status;
    AivmContractStatus contract_status = AIVM_CONTRACT_OK;
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
    if (target_value.type != AIVM_VAL_STRING || target_value.string_value == NULL) {
        set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "CALL_SYS target must be string.");
        return 0;
    }

    syscall_status = aivm_syscall_dispatch_checked_with_contract(
        vm->syscall_bindings,
        vm->syscall_binding_count,
        target_value.string_value,
        args,
        arg_count,
        out_result,
        &contract_status);
    if (syscall_status != AIVM_SYSCALL_OK) {
        set_vm_error(vm, AIVM_VM_ERR_SYSCALL, syscall_failure_detail(syscall_status, contract_status));
        return 0;
    }

    return 1;
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
    return 0;
}

static int reclaim_oldest_completed_task_slot(AivmVm* vm)
{
    size_t index;
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
        vm->task_reclaim_skip_pinned_count += 1U;
    }
    if (index >= vm->completed_task_count) {
        vm->task_reclaim_exhausted_count += 1U;
        return 0;
    }
    if (index + 1U < vm->completed_task_count) {
        memmove(
            &vm->completed_tasks[index],
            &vm->completed_tasks[index + 1U],
            (vm->completed_task_count - index - 1U) * sizeof(AivmCompletedTask));
    }
    vm->completed_task_count -= 1U;
    vm->task_reclaim_count += 1U;
    return 1;
}

static int push_completed_task(AivmVm* vm, AivmValue result)
{
    AivmCompletedTask* task;
    int64_t handle;
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
    task = &vm->completed_tasks[vm->completed_task_count];
    task->state = AIVM_TASK_STATE_PENDING;
    task->handle = handle;
    task->result = result;
    if (!transition_task_state(vm, task, AIVM_TASK_STATE_COMPLETED)) {
        return 0;
    }
    vm->completed_task_count += 1U;
    return aivm_stack_push(vm, aivm_value_int(handle));
}

static int execute_call_subroutine_sync(AivmVm* vm, size_t target, AivmValue* out_result)
{
    size_t baseline_frame_count;
    size_t frame_base;
    size_t return_ip;
    AivmValue result = aivm_value_void();

    if (vm == NULL || out_result == NULL) {
        return 0;
    }
    if (target >= vm->program->instruction_count) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Invalid function index.");
        return 0;
    }

    baseline_frame_count = vm->call_frame_count;
    frame_base = vm->stack_count;
    return_ip = vm->instruction_pointer + 1U;

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

    if (vm->stack_count > frame_base) {
        result = vm->stack[vm->stack_count - 1U];
        vm->stack_count = frame_base;
    }
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
    size_t i;
    if (vm == NULL || kind == NULL || id == NULL || out_handle == NULL) {
        return 0;
    }
    if (vm->node_count >= AIVM_VM_NODE_CAPACITY ||
        vm->node_attr_count + attr_count > AIVM_VM_NODE_ATTR_CAPACITY ||
        vm->node_child_count + child_count > AIVM_VM_NODE_CHILD_CAPACITY) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Node arena capacity exceeded.");
        return 0;
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
        AivmNodeAttr* out_attr = &vm->node_attrs[vm->node_attr_count + i];
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
        vm->node_children[vm->node_child_count + i] = children[i];
    }

    vm->node_attr_count += attr_count;
    vm->node_child_count += child_count;
    vm->node_count += 1U;
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
        count += 1U;
    } while (value != 0U && count < sizeof(temp));

    if (count + 1U > capacity) {
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

    if (vm == NULL) {
        return 0;
    }

    vm->process_argv_node_handle = 0;
    if (vm->process_argv_count > AIVM_VM_NODE_CAPACITY) {
        set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "process argv exceeds node capacity.");
        return 0;
    }

    for (i = 0U; i < vm->process_argv_count; i += 1U) {
        child_handles[i] = (int64_t)(i + 2U);
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
        if (child_handles[i] != (int64_t)(i + 2U)) {
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
    vm->call_frame_count = 0U;
    vm->locals_count = 0U;
    vm->string_arena_used = 0U;
    vm->string_arena[0] = '\0';
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
    vm->process_argv_node_handle = 0;
    (void)initialize_process_argv_node(vm);
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
    if (vm == NULL) {
        return 0;
    }

    if (vm->stack_count >= AIVM_VM_STACK_CAPACITY) {
        set_vm_error(vm, AIVM_VM_ERR_STACK_OVERFLOW, "Stack overflow.");
        return 0;
    }

    vm->stack[vm->stack_count] = value;
    vm->stack_count += 1U;
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
    if (vm == NULL) {
        return 0;
    }

    if (vm->call_frame_count >= AIVM_VM_CALLFRAME_CAPACITY) {
        set_vm_error(vm, AIVM_VM_ERR_FRAME_OVERFLOW, "Call-frame overflow.");
        return 0;
    }

    vm->call_frames[vm->call_frame_count].return_instruction_pointer = return_instruction_pointer;
    vm->call_frames[vm->call_frame_count].frame_base = frame_base;
    vm->call_frames[vm->call_frame_count].locals_base = vm->locals_count;
    vm->call_frame_count += 1U;
    return 1;
}

int aivm_frame_pop(AivmVm* vm, AivmCallFrame* out_frame)
{
    if (vm == NULL || out_frame == NULL) {
        return 0;
    }

    if (vm->call_frame_count == 0U) {
        set_vm_error(vm, AIVM_VM_ERR_FRAME_UNDERFLOW, "Call-frame underflow.");
        return 0;
    }

    vm->call_frame_count -= 1U;
    *out_frame = vm->call_frames[vm->call_frame_count];
    return 1;
}

int aivm_local_set(AivmVm* vm, size_t index, AivmValue value)
{
    if (vm == NULL) {
        return 0;
    }

    if (index >= AIVM_VM_LOCALS_CAPACITY) {
        set_vm_error(vm, AIVM_VM_ERR_LOCAL_OUT_OF_RANGE, "Invalid local slot.");
        return 0;
    }

    vm->locals[index] = value;
    if (index >= vm->locals_count) {
        vm->locals_count = index + 1U;
    }
    return 1;
}

int aivm_local_get(const AivmVm* vm, size_t index, AivmValue* out_value)
{
    if (vm == NULL || out_value == NULL) {
        return 0;
    }

    if (index >= vm->locals_count) {
        return 0;
    }

    *out_value = vm->locals[index];
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

    switch (instruction->opcode) {
        case AIVM_OP_NOP:
            vm->instruction_pointer += 1U;
            break;

        case AIVM_OP_HALT:
            aivm_halt(vm);
            break;

        case AIVM_OP_STUB:
            vm->instruction_pointer += 1U;
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
                set_vm_error(vm, AIVM_VM_ERR_LOCAL_OUT_OF_RANGE, "Invalid local slot.");
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
                set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "ADD_INT requires int operands.");
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
            if (!operand_to_index(vm, instruction->operand_int, &target)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (target >= vm->program->instruction_count) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "Call target out of range.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_frame_push(vm, vm->instruction_pointer + 1U, vm->stack_count)) {
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
            if (vm->stack_count > frame.frame_base) {
                return_value = vm->stack[vm->stack_count - 1U];
                has_return_value = 1;
            }
            vm->stack_count = frame.frame_base;
            vm->locals_count = frame.locals_base;
            if (has_return_value != 0) {
                if (!aivm_stack_push(vm, return_value)) {
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
                left_length += 1U;
            }
            while (right.string_value[right_length] != '\0') {
                right_length += 1U;
            }

            output = arena_alloc(vm, left_length + right_length + 1U);
            if (output == NULL) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }

            for (i = 0U; i < left_length; i += 1U) {
                output[i] = left.string_value[i];
            }
            for (i = 0U; i < right_length; i += 1U) {
                output[left_length + i] = right.string_value[i];
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
                vm->instruction_pointer = vm->program->instruction_count;
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
                vm->instruction_pointer = vm->program->instruction_count;
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
            if (!operand_to_index(vm, instruction->operand_int, &expected_count)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (vm->par_context_count >= AIVM_VM_PAR_CONTEXT_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_BEGIN exceeded context capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->par_contexts[vm->par_context_count].expected_count = expected_count;
            vm->par_contexts[vm->par_context_count].start_index = vm->par_value_count;
            vm->par_context_count += 1U;
            vm->instruction_pointer += 1U;
            break;
        }

        case AIVM_OP_PAR_FORK: {
            AivmValue value;
            if (vm->par_context_count == 0U) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_FORK requires active Par context.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (vm->par_value_count >= AIVM_VM_PAR_VALUE_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "PAR_FORK exceeded value capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            if (!aivm_stack_pop(vm, &value)) {
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            vm->par_values[vm->par_value_count] = value;
            vm->par_value_count += 1U;
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
                AivmValue value = vm->par_values[context.start_index + i];
                AivmValue task_result;
                int64_t child_handle;
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
                count += 1;
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
                base_node->child_count + 1U > AIVM_VM_NODE_CHILD_CAPACITY) {
                set_vm_error(vm, AIVM_VM_ERR_INVALID_PROGRAM, "APPEND_CHILD exceeded VM node capacity.");
                vm->instruction_pointer = vm->program->instruction_count;
                break;
            }
            for (i = 0U; i < base_node->attr_count; i += 1U) {
                attrs[i] = vm->node_attrs[base_node->attr_start + i];
            }
            for (i = 0U; i < base_node->child_count; i += 1U) {
                new_children[i] = vm->node_children[base_node->child_start + i];
            }
            new_children[base_node->child_count] = child_handle;
            if (!create_node_record(
                vm,
                base_node->kind,
                base_node->id,
                attrs,
                base_node->attr_count,
                new_children,
                base_node->child_count + 1U,
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
        case AIVM_OP_MAKE_LIT_INT: {
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
            } else {
                if (id_value.type != AIVM_VAL_STRING || id_value.string_value == NULL ||
                    value.type != AIVM_VAL_INT) {
                    set_vm_error(vm, AIVM_VM_ERR_TYPE_MISMATCH, "MAKE_LIT_INT requires (string,int).");
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                attr.kind = AIVM_NODE_ATTR_INT;
                attr.int_value = value.int_value;
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
                attrs[i] = vm->node_attrs[template_node->attr_start + i];
            }
            for (i = 0U; i < argc; i += 1U) {
                AivmValue child_value;
                int64_t child_handle;
                if (!aivm_stack_pop(vm, &child_value)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                if (!create_runtime_node_from_value(vm, child_value, &child_handle)) {
                    vm->instruction_pointer = vm->program->instruction_count;
                    break;
                }
                children[argc - i - 1U] = child_handle;
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
