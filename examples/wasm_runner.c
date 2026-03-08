#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef AIVM_WASM_WEB
#include <emscripten/emscripten.h>
#endif

#include "aivm_program.h"
#include "aivm_runtime.h"
#include "remote/aivm_remote_channel.h"
#include "remote/aivm_remote_session.h"
#include "sys/aivm_syscall_contracts.h"
#include "aivm_vm.h"

static const char* g_wasm_syscall_error_message = NULL;
static const char* g_wasm_syscall_error_code = NULL;
static char g_wasm_syscall_error_message_buf[256];
static AivmVm* g_wasm_active_vm = NULL;
enum {
    WASM_BYTES_SCRATCH_CAPACITY = 1 << 20,
    WASM_STRING_SCRATCH_CAPACITY = (1 << 21)
};
static uint8_t g_wasm_bytes_scratch[WASM_BYTES_SCRATCH_CAPACITY];
static char g_wasm_string_scratch[WASM_STRING_SCRATCH_CAPACITY];

static int wasm_base64_decode_char(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (int)(ch - 'A');
    }
    if (ch >= 'a' && ch <= 'z') {
        return 26 + (int)(ch - 'a');
    }
    if (ch >= '0' && ch <= '9') {
        return 52 + (int)(ch - '0');
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

static int wasm_bytes_to_base64(const uint8_t* input, size_t input_length, char* output, size_t output_capacity)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t encoded_length;
    size_t i;
    size_t out_index = 0U;

    if (output == NULL) {
        return 0;
    }
    encoded_length = ((input_length + 2U) / 3U) * 4U;
    if (output_capacity < encoded_length + 1U) {
        return 0;
    }
    for (i = 0U; i < input_length; i += 3U) {
        uint32_t chunk = ((uint32_t)input[i]) << 16U;
        size_t remaining = input_length - i;
        chunk |= (remaining > 1U) ? (((uint32_t)input[i + 1U]) << 8U) : 0U;
        chunk |= (remaining > 2U) ? ((uint32_t)input[i + 2U]) : 0U;
        output[out_index++] = table[(chunk >> 18U) & 0x3fU];
        output[out_index++] = table[(chunk >> 12U) & 0x3fU];
        output[out_index++] = (remaining > 1U) ? table[(chunk >> 6U) & 0x3fU] : '=';
        output[out_index++] = (remaining > 2U) ? table[chunk & 0x3fU] : '=';
    }
    output[out_index] = '\0';
    return 1;
}

static int wasm_bytes_from_base64(const char* input, uint8_t* output, size_t output_capacity, size_t* out_length)
{
    size_t input_length = 0U;
    size_t i;
    size_t produced = 0U;

    if (input == NULL || out_length == NULL) {
        return 0;
    }
    while (input[input_length] != '\0') {
        input_length += 1U;
    }
    if ((input_length % 4U) != 0U) {
        return 0;
    }
    for (i = 0U; i < input_length; i += 4U) {
        int c0 = wasm_base64_decode_char(input[i]);
        int c1 = wasm_base64_decode_char(input[i + 1U]);
        int c2 = (input[i + 2U] == '=') ? -2 : wasm_base64_decode_char(input[i + 2U]);
        int c3 = (input[i + 3U] == '=') ? -2 : wasm_base64_decode_char(input[i + 3U]);
        uint32_t chunk;
        if (c0 < 0 || c1 < 0 || c2 == -1 || c3 == -1) {
            return 0;
        }
        chunk = ((uint32_t)c0 << 18U) | ((uint32_t)c1 << 12U);
        if (c2 >= 0) {
            chunk |= ((uint32_t)c2 << 6U);
        }
        if (c3 >= 0) {
            chunk |= (uint32_t)c3;
        }
        if (produced < output_capacity && output != NULL) {
            output[produced] = (uint8_t)((chunk >> 16U) & 0xffU);
        }
        produced += 1U;
        if (c2 >= 0) {
            if (produced <= output_capacity && output != NULL) {
                output[produced - 1U] = (uint8_t)((chunk >> 8U) & 0xffU);
            }
            produced += 1U;
        }
        if (c3 >= 0) {
            if (produced <= output_capacity && output != NULL) {
                output[produced - 1U] = (uint8_t)(chunk & 0xffU);
            }
            produced += 1U;
        }
    }
    if (output == NULL) {
        *out_length = produced;
        return 1;
    }
    if (produced > output_capacity) {
        return 0;
    }
    *out_length = produced;
    return 1;
}

static int native_syscall_bytes_from_utf8_string(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t length = 0U;
    (void)target;
    if (args == NULL || result == NULL || arg_count != 1U ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    while (args[0].string_value[length] != '\0') {
        length += 1U;
    }
    *result = aivm_value_bytes((const uint8_t*)args[0].string_value, length);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_to_utf8_string(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t i;
    (void)target;
    if (args == NULL || result == NULL || arg_count != 1U ||
        args[0].type != AIVM_VAL_BYTES ||
        (args[0].bytes_value.length > 0U && args[0].bytes_value.data == NULL) ||
        args[0].bytes_value.length + 1U > WASM_STRING_SCRATCH_CAPACITY) {
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    for (i = 0U; i < args[0].bytes_value.length; i += 1U) {
        g_wasm_string_scratch[i] = (char)args[0].bytes_value.data[i];
    }
    g_wasm_string_scratch[args[0].bytes_value.length] = '\0';
    *result = aivm_value_string(g_wasm_string_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_to_base64(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args == NULL || result == NULL || arg_count != 1U ||
        args[0].type != AIVM_VAL_BYTES ||
        (args[0].bytes_value.length > 0U && args[0].bytes_value.data == NULL)) {
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!wasm_bytes_to_base64(
            args[0].bytes_value.data,
            args[0].bytes_value.length,
            g_wasm_string_scratch,
            sizeof(g_wasm_string_scratch))) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_string(g_wasm_string_scratch);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_bytes_from_base64(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    size_t output_length = 0U;
    (void)target;
    if (args == NULL || result == NULL || arg_count != 1U ||
        args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (!wasm_bytes_from_base64(args[0].string_value, NULL, 0U, &output_length) ||
        output_length > sizeof(g_wasm_bytes_scratch) ||
        !wasm_bytes_from_base64(args[0].string_value, g_wasm_bytes_scratch, sizeof(g_wasm_bytes_scratch), &output_length)) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_bytes(g_wasm_bytes_scratch, output_length);
    return AIVM_SYSCALL_OK;
}

static const char* opcode_name(AivmOpcode opcode)
{
    switch (opcode) {
        case AIVM_OP_NOP: return "NOP";
        case AIVM_OP_HALT: return "HALT";
        case AIVM_OP_STUB: return "STUB";
        case AIVM_OP_PUSH_INT: return "PUSH_INT";
        case AIVM_OP_POP: return "POP";
        case AIVM_OP_STORE_LOCAL: return "STORE_LOCAL";
        case AIVM_OP_LOAD_LOCAL: return "LOAD_LOCAL";
        case AIVM_OP_ADD_INT: return "ADD_INT";
        case AIVM_OP_JUMP: return "JUMP";
        case AIVM_OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
        case AIVM_OP_PUSH_BOOL: return "PUSH_BOOL";
        case AIVM_OP_CALL: return "CALL";
        case AIVM_OP_RET: return "RET";
        case AIVM_OP_EQ_INT: return "EQ_INT";
        case AIVM_OP_EQ: return "EQ";
        case AIVM_OP_CONST: return "CONST";
        case AIVM_OP_STR_CONCAT: return "STR_CONCAT";
        case AIVM_OP_TO_STRING: return "TO_STRING";
        case AIVM_OP_STR_ESCAPE: return "STR_ESCAPE";
        case AIVM_OP_RETURN: return "RETURN";
        case AIVM_OP_STR_SUBSTRING: return "STR_SUBSTRING";
        case AIVM_OP_STR_REMOVE: return "STR_REMOVE";
        case AIVM_OP_CALL_SYS: return "CALL_SYS";
        case AIVM_OP_ASYNC_CALL: return "ASYNC_CALL";
        case AIVM_OP_ASYNC_CALL_SYS: return "ASYNC_CALL_SYS";
        case AIVM_OP_AWAIT: return "AWAIT";
        case AIVM_OP_PAR_BEGIN: return "PAR_BEGIN";
        case AIVM_OP_PAR_FORK: return "PAR_FORK";
        case AIVM_OP_PAR_JOIN: return "PAR_JOIN";
        case AIVM_OP_PAR_CANCEL: return "PAR_CANCEL";
        case AIVM_OP_STR_UTF8_BYTE_COUNT: return "STR_UTF8_BYTE_COUNT";
        case AIVM_OP_NODE_KIND: return "NODE_KIND";
        case AIVM_OP_NODE_ID: return "NODE_ID";
        case AIVM_OP_ATTR_COUNT: return "ATTR_COUNT";
        case AIVM_OP_ATTR_KEY: return "ATTR_KEY";
        case AIVM_OP_ATTR_VALUE_KIND: return "ATTR_VALUE_KIND";
        case AIVM_OP_ATTR_VALUE_STRING: return "ATTR_VALUE_STRING";
        case AIVM_OP_ATTR_VALUE_INT: return "ATTR_VALUE_INT";
        case AIVM_OP_ATTR_VALUE_BOOL: return "ATTR_VALUE_BOOL";
        case AIVM_OP_CHILD_COUNT: return "CHILD_COUNT";
        case AIVM_OP_CHILD_AT: return "CHILD_AT";
        case AIVM_OP_MAKE_BLOCK: return "MAKE_BLOCK";
        case AIVM_OP_APPEND_CHILD: return "APPEND_CHILD";
        case AIVM_OP_MAKE_ERR: return "MAKE_ERR";
        case AIVM_OP_MAKE_LIT_STRING: return "MAKE_LIT_STRING";
        case AIVM_OP_MAKE_LIT_INT: return "MAKE_LIT_INT";
        case AIVM_OP_MAKE_NODE: return "MAKE_NODE";
        case AIVM_OP_MAKE_FIELD_STRING: return "MAKE_FIELD_STRING";
        case AIVM_OP_MAKE_MAP: return "MAKE_MAP";
        default: return "UNKNOWN";
    }
}

static void print_vm_failure(const AivmProgram* program, const AivmVm* vm, const char* message)
{
    size_t pc = 0U;
    size_t display_pc = 0U;
    const char* op = "UNKNOWN";
    const char* call_target = "unknown";
    const char* phase = "exec";
    const char* detail = "none";
    char typed_code[32];
    const char* detail_end = NULL;
    size_t typed_len = 0U;
    if (vm != NULL) {
        pc = vm->instruction_pointer;
        display_pc = pc;
        if (program != NULL && program->instruction_count > 0U && display_pc >= program->instruction_count) {
            display_pc = program->instruction_count - 1U;
        }
        detail = aivm_vm_error_detail(vm);
        if (detail == NULL || detail[0] == '\0') {
            detail = "none";
        }
        if (vm->error == AIVM_VM_ERR_SYSCALL) {
            phase = "syscall";
        }
        if (program != NULL && program->instructions != NULL && display_pc < program->instruction_count) {
            const AivmInstruction* inst = &program->instructions[display_pc];
            op = opcode_name(inst->opcode);
            if ((inst->opcode == AIVM_OP_CALL_SYS || inst->opcode == AIVM_OP_ASYNC_CALL_SYS) &&
                inst->operand_int >= 0) {
                size_t arg_count = (size_t)inst->operand_int;
                if (display_pc > arg_count) {
                    size_t target_index = display_pc - arg_count - 1U;
                    if (target_index < program->instruction_count) {
                        const AivmInstruction* target_inst = &program->instructions[target_index];
                        if (target_inst->opcode == AIVM_OP_CONST &&
                            target_inst->operand_int >= 0 &&
                            (size_t)target_inst->operand_int < program->constant_count) {
                            AivmValue target_val = program->constants[target_inst->operand_int];
                            if (target_val.type == AIVM_VAL_STRING && target_val.string_value != NULL) {
                                call_target = target_val.string_value;
                            }
                        }
                    }
                }
            }
        }
        if (vm->error == AIVM_VM_ERR_SYSCALL &&
            program != NULL &&
            program->instructions != NULL &&
            program->instruction_count > 0U) {
            size_t cursor = (pc == 0U) ? 0U : (pc - 1U);
            if (cursor >= program->instruction_count) {
                cursor = program->instruction_count - 1U;
            }
            while (1) {
                const AivmInstruction* probe = &program->instructions[cursor];
                if (probe->opcode == AIVM_OP_CALL_SYS || probe->opcode == AIVM_OP_ASYNC_CALL_SYS) {
                    op = opcode_name(probe->opcode);
                    display_pc = cursor;
                    call_target = "unknown";
                    if (probe->operand_int >= 0) {
                        size_t arg_count = (size_t)probe->operand_int;
                        if (cursor > arg_count) {
                            size_t target_index = cursor - arg_count - 1U;
                            if (target_index < program->instruction_count) {
                                const AivmInstruction* target_inst = &program->instructions[target_index];
                                if (target_inst->opcode == AIVM_OP_CONST &&
                                    target_inst->operand_int >= 0 &&
                                    (size_t)target_inst->operand_int < program->constant_count) {
                                    AivmValue target_val = program->constants[target_inst->operand_int];
                                    if (target_val.type == AIVM_VAL_STRING && target_val.string_value != NULL) {
                                        call_target = target_val.string_value;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
                if (cursor == 0U) {
                    break;
                }
                cursor -= 1U;
            }
        }
    }
    memset(typed_code, 0, sizeof(typed_code));
    detail_end = strchr(detail, ':');
    if (detail_end == NULL) {
        detail_end = strchr(detail, '/');
    } else {
        const char* slash = strchr(detail, '/');
        if (slash != NULL && slash < detail_end) {
            detail_end = slash;
        }
    }
    if (detail_end == NULL) {
        detail_end = detail + strlen(detail);
    }
    typed_len = (size_t)(detail_end - detail);
    if (typed_len > 0U && typed_len < sizeof(typed_code)) {
        memcpy(typed_code, detail, typed_len);
        typed_code[typed_len] = '\0';
    }
    if (vm != NULL && vm->error == AIVM_VM_ERR_SYSCALL && typed_code[0] != '\0') {
        fprintf(
            stderr,
            "Err#err1(code=%s message=\"phase=%s function=main pc=%llu nodeId=unknown opcode=%s callTarget=%s vmCode=%s vmMessage=%s detail=%s\" nodeId=vm)\n",
            typed_code,
            phase,
            (unsigned long long)display_pc,
            op,
            call_target,
            aivm_vm_error_code(vm->error),
            aivm_vm_error_message(vm->error),
            detail);
        return;
    }
    fprintf(
        stderr,
        "Err#err1(code=RUN001 message=\"%s phase=%s function=main pc=%llu nodeId=unknown opcode=%s callTarget=%s vmCode=%s vmMessage=%s detail=%s\" nodeId=vm)\n",
        (message == NULL) ? "VM execution failed." : message,
        phase,
        (unsigned long long)display_pc,
        op,
        call_target,
        (vm == NULL) ? "AIVM999" : aivm_vm_error_code(vm->error),
        (vm == NULL) ? "Unknown vm error." : aivm_vm_error_message(vm->error),
        detail);
}

static int native_syscall_unavailable(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)args;
    (void)arg_count;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (target == NULL) {
        target = "sys.unknown";
    }
    if (snprintf(
            g_wasm_syscall_error_message_buf,
            sizeof(g_wasm_syscall_error_message_buf),
            "%s is not available on this target.",
            target) <= 0) {
        g_wasm_syscall_error_message = "syscall is not available on this target.";
    } else {
        g_wasm_syscall_error_message = g_wasm_syscall_error_message_buf;
    }
    g_wasm_syscall_error_code = "RUN101";
    result->type = AIVM_VAL_VOID;
    return AIVM_SYSCALL_ERR_NOT_FOUND;
}

static int set_binding_handler(
    AivmSyscallBinding* bindings,
    size_t binding_count,
    const char* target,
    AivmSyscallHandler handler)
{
    size_t index;
    if (bindings == NULL || target == NULL || handler == NULL) {
        return 0;
    }
    for (index = 0U; index < binding_count; index += 1U) {
        if (bindings[index].target != NULL && strcmp(bindings[index].target, target) == 0) {
            bindings[index].handler = handler;
            return 1;
        }
    }
    return 0;
}

static int ensure_binding_handler(
    AivmSyscallBinding* bindings,
    size_t* binding_count,
    size_t binding_capacity,
    const char* target,
    AivmSyscallHandler handler)
{
    if (bindings == NULL || binding_count == NULL || target == NULL || handler == NULL) {
        return 0;
    }
    if (set_binding_handler(bindings, *binding_count, target, handler)) {
        return 1;
    }
    if (*binding_count >= binding_capacity) {
        return 0;
    }
    bindings[*binding_count].target = target;
    bindings[*binding_count].handler = handler;
    *binding_count += 1U;
    return 1;
}

static int read_binary_file(const char* path, unsigned char** out_bytes, size_t* out_size)
{
    FILE* f;
    unsigned char* bytes;
    size_t capacity = 0U;
    size_t used = 0U;
    size_t nread;
    unsigned char chunk[4096];

    if (path == NULL || out_bytes == NULL || out_size == NULL) {
        return 0;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    bytes = NULL;
    for (;;) {
        nread = fread(chunk, 1U, sizeof(chunk), f);
        if (nread > 0U) {
            unsigned char* grown;
            if (used + nread > capacity) {
                size_t new_capacity = (capacity == 0U) ? 4096U : capacity;
                while (new_capacity < used + nread) {
                    if (new_capacity > ((size_t)-1) / 2U) {
                        free(bytes);
                        fclose(f);
                        return 0;
                    }
                    new_capacity *= 2U;
                }
                grown = (unsigned char*)realloc(bytes, new_capacity);
                if (grown == NULL) {
                    free(bytes);
                    fclose(f);
                    return 0;
                }
                bytes = grown;
                capacity = new_capacity;
            }
            memcpy(bytes + used, chunk, nread);
            used += nread;
        }
        if (nread < sizeof(chunk)) {
            if (ferror(f) != 0) {
                free(bytes);
                fclose(f);
                return 0;
            }
            break;
        }
    }
    fclose(f);

    *out_bytes = bytes;
    *out_size = used;
    return 1;
}

static int read_binary_stdin(unsigned char** out_bytes, size_t* out_size)
{
    unsigned char* bytes;
    size_t capacity = 0U;
    size_t used = 0U;
    size_t nread;
    unsigned char chunk[4096];

    if (out_bytes == NULL || out_size == NULL) {
        return 0;
    }

    bytes = NULL;
    for (;;) {
        nread = fread(chunk, 1U, sizeof(chunk), stdin);
        if (nread > 0U) {
            unsigned char* grown;
            if (used + nread > capacity) {
                size_t new_capacity = (capacity == 0U) ? 4096U : capacity;
                while (new_capacity < used + nread) {
                    if (new_capacity > ((size_t)-1) / 2U) {
                        free(bytes);
                        return 0;
                    }
                    new_capacity *= 2U;
                }
                grown = (unsigned char*)realloc(bytes, new_capacity);
                if (grown == NULL) {
                    free(bytes);
                    return 0;
                }
                bytes = grown;
                capacity = new_capacity;
            }
            memcpy(bytes + used, chunk, nread);
            used += nread;
        }
        if (nread < sizeof(chunk)) {
            if (ferror(stdin) != 0) {
                free(bytes);
                return 0;
            }
            break;
        }
    }

    *out_bytes = bytes;
    *out_size = used;
    return 1;
}

static int native_syscall_stdout_write_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_STRING || args[0].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    printf("%s\n", args[0].string_value);
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_process_argv(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    *result = aivm_value_node(1);
    return AIVM_SYSCALL_OK;
}

#ifdef AIVM_WASM_WEB
EM_JS(int, aivm_web_stdin_fill, (char* out_ptr, int out_len), {
    if (typeof globalThis.__aivmStdinRead !== 'function') {
        return -2;
    }
    const value = globalThis.__aivmStdinRead();
    if (value === null) {
        if (out_len > 0) {
            HEAP8[out_ptr] = 0;
        }
        return -1;
    }
    const text = String(value ?? "");
    const maxChars = out_len > 0 ? out_len - 1 : 0;
    if (maxChars <= 0) {
        return 0;
    }
    const clipped = text.length > maxChars ? text.slice(0, maxChars) : text;
    stringToUTF8(clipped, out_ptr, out_len);
    return lengthBytesUTF8(clipped);
});

EM_JS(int, aivm_web_ui_create_window, (int window_id, const char* title_ptr, int width, int height), {
    if (typeof globalThis.__aivmUiCreateWindow !== 'function') {
        return -1;
    }
    const title = UTF8ToString(title_ptr);
    return globalThis.__aivmUiCreateWindow(window_id, title, width, height) | 0;
});

EM_JS(int, aivm_web_ui_begin_frame, (int window_id), {
    if (typeof globalThis.__aivmUiBeginFrame !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiBeginFrame(window_id) | 0;
});

EM_JS(int, aivm_web_ui_draw_rect, (int window_id, int x, int y, int w, int h, const char* color_ptr), {
    if (typeof globalThis.__aivmUiDrawRect !== 'function') {
        return -1;
    }
    const color = UTF8ToString(color_ptr);
    return globalThis.__aivmUiDrawRect(window_id, x, y, w, h, color) | 0;
});

EM_JS(int, aivm_web_ui_draw_text, (int window_id, int x, int y, const char* text_ptr, const char* color_ptr, int size), {
    if (typeof globalThis.__aivmUiDrawText !== 'function') {
        return -1;
    }
    const text = UTF8ToString(text_ptr);
    const color = UTF8ToString(color_ptr);
    return globalThis.__aivmUiDrawText(window_id, x, y, text, color, size) | 0;
});

EM_JS(int, aivm_web_ui_draw_line, (int window_id, int x1, int y1, int x2, int y2, const char* color_ptr, int width), {
    if (typeof globalThis.__aivmUiDrawLine !== 'function') {
        return -1;
    }
    const color = UTF8ToString(color_ptr);
    return globalThis.__aivmUiDrawLine(window_id, x1, y1, x2, y2, color, width) | 0;
});

EM_JS(int, aivm_web_ui_draw_ellipse, (int window_id, int x, int y, int w, int h, const char* color_ptr), {
    if (typeof globalThis.__aivmUiDrawEllipse !== 'function') {
        return -1;
    }
    const color = UTF8ToString(color_ptr);
    return globalThis.__aivmUiDrawEllipse(window_id, x, y, w, h, color) | 0;
});

EM_JS(int, aivm_web_ui_draw_path, (int window_id, const char* path_ptr, const char* color_ptr, int stroke_width), {
    if (typeof globalThis.__aivmUiDrawPath !== 'function') {
        return -1;
    }
    const path = UTF8ToString(path_ptr);
    const color = UTF8ToString(color_ptr);
    return globalThis.__aivmUiDrawPath(window_id, path, color, stroke_width) | 0;
});

EM_JS(int, aivm_web_ui_draw_image, (int window_id, int x, int y, int w, int h, const char* src_ptr), {
    if (typeof globalThis.__aivmUiDrawImage !== 'function') {
        return -1;
    }
    const src = UTF8ToString(src_ptr);
    return globalThis.__aivmUiDrawImage(window_id, x, y, w, h, src) | 0;
});

EM_JS(int, aivm_web_ui_end_frame, (int window_id), {
    if (typeof globalThis.__aivmUiEndFrame !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiEndFrame(window_id) | 0;
});

EM_JS(int, aivm_web_ui_present, (int window_id), {
    if (typeof globalThis.__aivmUiPresent !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiPresent(window_id) | 0;
});

EM_JS(int, aivm_web_ui_close_window, (int window_id), {
    if (typeof globalThis.__aivmUiCloseWindow !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiCloseWindow(window_id) | 0;
});

EM_JS(int, aivm_web_ui_wait_frame, (int window_id), {
    if (typeof globalThis.__aivmUiWaitFrame !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiWaitFrame(window_id) | 0;
});

EM_JS(int, aivm_web_ui_get_window_width, (int window_id), {
    if (typeof globalThis.__aivmUiGetWindowWidth !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiGetWindowWidth(window_id) | 0;
});

EM_JS(int, aivm_web_ui_get_window_height, (int window_id), {
    if (typeof globalThis.__aivmUiGetWindowHeight !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiGetWindowHeight(window_id) | 0;
});

EM_JS(int, aivm_web_ui_poll_event_type, (int window_id), {
    if (typeof globalThis.__aivmUiPollEventType !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiPollEventType(window_id) | 0;
});

EM_JS(int, aivm_web_ui_poll_event_x, (int window_id), {
    if (typeof globalThis.__aivmUiPollEventX !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiPollEventX(window_id) | 0;
});

EM_JS(int, aivm_web_ui_poll_event_y, (int window_id), {
    if (typeof globalThis.__aivmUiPollEventY !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiPollEventY(window_id) | 0;
});

EM_JS(int, aivm_web_ui_poll_event_target_id, (int window_id, char* out_ptr, int out_len), {
    if (typeof globalThis.__aivmUiPollEventTargetId !== 'function') {
        return -1;
    }
    if (!out_ptr || out_len <= 0) {
        return -1;
    }
    stringToUTF8(globalThis.__aivmUiPollEventTargetId(window_id) || "", out_ptr, out_len);
    return 0;
});

EM_JS(int, aivm_web_ui_poll_event_key, (int window_id, char* out_ptr, int out_len), {
    if (typeof globalThis.__aivmUiPollEventKey !== 'function') {
        return -1;
    }
    if (!out_ptr || out_len <= 0) {
        return -1;
    }
    stringToUTF8(globalThis.__aivmUiPollEventKey(window_id) || "", out_ptr, out_len);
    return 0;
});

EM_JS(int, aivm_web_ui_poll_event_text, (int window_id, char* out_ptr, int out_len), {
    if (typeof globalThis.__aivmUiPollEventText !== 'function') {
        return -1;
    }
    if (!out_ptr || out_len <= 0) {
        return -1;
    }
    stringToUTF8(globalThis.__aivmUiPollEventText(window_id) || "", out_ptr, out_len);
    return 0;
});

EM_JS(int, aivm_web_ui_poll_event_modifiers, (int window_id, char* out_ptr, int out_len), {
    if (typeof globalThis.__aivmUiPollEventModifiers !== 'function') {
        return -1;
    }
    if (!out_ptr || out_len <= 0) {
        return -1;
    }
    stringToUTF8(globalThis.__aivmUiPollEventModifiers(window_id) || "", out_ptr, out_len);
    return 0;
});

EM_JS(int, aivm_web_ui_poll_event_repeat, (int window_id), {
    if (typeof globalThis.__aivmUiPollEventRepeat !== 'function') {
        return -1;
    }
    return globalThis.__aivmUiPollEventRepeat(window_id) | 0;
});
#endif

static int g_ui_next_window_id = 1;

static int ui_fail_not_available(AivmValue* result)
{
    g_wasm_syscall_error_message = "ui bridge is not available on this target.";
    g_wasm_syscall_error_code = "RUN101";
    if (result != NULL) {
        result->type = AIVM_VAL_VOID;
    }
    return AIVM_SYSCALL_ERR_NOT_FOUND;
}

#ifdef AIVM_WASM_WEB
static char g_ui_event_target_id[128];
static char g_ui_event_key[64];
static char g_ui_event_text[64];
static char g_ui_event_modifiers[64];
static char g_ui_event_modifiers_normalized[32];

static int modifiers_has_token(const char* text, const char* token)
{
    const char* p = text;
    size_t token_len;
    if (text == NULL || token == NULL) {
        return 0;
    }
    token_len = strlen(token);
    if (token_len == 0U) {
        return 0;
    }
    while (p != NULL && *p != '\0') {
        const char* end = strchr(p, ',');
        size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);
        if (len == token_len && strncmp(p, token, token_len) == 0) {
            return 1;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return 0;
}

static const char* normalize_modifiers(const char* text)
{
    size_t used = 0U;
    int has_any = 0;
    struct TokenEntry {
        const char* token;
    };
    static const struct TokenEntry tokens[] = {
        { "alt" },
        { "ctrl" },
        { "meta" },
        { "shift" }
    };
    size_t i;
    g_ui_event_modifiers_normalized[0] = '\0';
    for (i = 0U; i < sizeof(tokens) / sizeof(tokens[0]); i += 1U) {
        const char* token = tokens[i].token;
        size_t token_len;
        if (!modifiers_has_token(text, token)) {
            continue;
        }
        token_len = strlen(token);
        if (has_any) {
            if (used + 1U >= sizeof(g_ui_event_modifiers_normalized)) {
                break;
            }
            g_ui_event_modifiers_normalized[used] = ',';
            used += 1U;
        }
        if (used + token_len >= sizeof(g_ui_event_modifiers_normalized)) {
            break;
        }
        memcpy(g_ui_event_modifiers_normalized + used, token, token_len);
        used += token_len;
        has_any = 1;
    }
    g_ui_event_modifiers_normalized[used] = '\0';
    return g_ui_event_modifiers_normalized;
}

static int ui_update_window_size_node(AivmVm* vm, int width, int height)
{
    size_t node_index;
    size_t i;
    AivmNodeRecord* node;
    if (vm == NULL || vm->ui_default_window_size_node_handle <= 0) {
        return 0;
    }
    node_index = (size_t)(vm->ui_default_window_size_node_handle - 1);
    if (node_index >= vm->node_count) {
        return 0;
    }
    node = &vm->nodes[node_index];
    for (i = 0U; i < node->attr_count; i += 1U) {
        AivmNodeAttr* attr = &vm->node_attrs[node->attr_start + i];
        if (attr->kind != AIVM_NODE_ATTR_INT || attr->key == NULL) {
            continue;
        }
        if (strcmp(attr->key, "width") == 0) {
            attr->int_value = width;
        } else if (strcmp(attr->key, "height") == 0) {
            attr->int_value = height;
        }
    }
    return 1;
}

static int ui_update_event_node(
    AivmVm* vm,
    int event_type,
    const char* target_id,
    int x,
    int y,
    const char* key,
    const char* text,
    const char* modifiers,
    int repeat_flag)
{
    size_t node_index;
    size_t i;
    AivmNodeRecord* node;
    const char* type_text = "none";
    const char* target_text = (target_id != NULL) ? target_id : "";
    const char* key_text = (key != NULL) ? key : "";
    const char* input_text = (text != NULL) ? text : "";
    const char* modifiers_text = normalize_modifiers((modifiers != NULL) ? modifiers : "");
    int normalized_x = x;
    int normalized_y = y;
    int normalized_repeat = (repeat_flag != 0) ? 1 : 0;
    if (vm == NULL || vm->ui_empty_event_node_handle <= 0) {
        return 0;
    }
    if (event_type == 1) {
        type_text = "closed";
        normalized_x = -1;
        normalized_y = -1;
        key_text = "";
        input_text = "";
        modifiers_text = "";
        normalized_repeat = 0;
    } else if (event_type == 2) {
        type_text = "click";
        key_text = "";
        input_text = "";
        modifiers_text = "";
        normalized_repeat = 0;
    } else if (event_type == 3) {
        type_text = "key";
        normalized_x = -1;
        normalized_y = -1;
    } else {
        normalized_x = -1;
        normalized_y = -1;
        target_text = "";
        key_text = "";
        input_text = "";
        modifiers_text = "";
        normalized_repeat = 0;
    }
    node_index = (size_t)(vm->ui_empty_event_node_handle - 1);
    if (node_index >= vm->node_count) {
        return 0;
    }
    node = &vm->nodes[node_index];
    for (i = 0U; i < node->attr_count; i += 1U) {
        AivmNodeAttr* attr = &vm->node_attrs[node->attr_start + i];
        if (attr->key == NULL) {
            continue;
        }
        if (strcmp(attr->key, "type") == 0 && attr->kind == AIVM_NODE_ATTR_STRING) {
            attr->string_value = type_text;
        } else if (strcmp(attr->key, "targetId") == 0 && attr->kind == AIVM_NODE_ATTR_STRING) {
            attr->string_value = target_text;
        } else if (strcmp(attr->key, "x") == 0 && attr->kind == AIVM_NODE_ATTR_INT) {
            attr->int_value = normalized_x;
        } else if (strcmp(attr->key, "y") == 0 && attr->kind == AIVM_NODE_ATTR_INT) {
            attr->int_value = normalized_y;
        } else if (strcmp(attr->key, "key") == 0 && attr->kind == AIVM_NODE_ATTR_STRING) {
            attr->string_value = key_text;
        } else if (strcmp(attr->key, "text") == 0 && attr->kind == AIVM_NODE_ATTR_STRING) {
            attr->string_value = input_text;
        } else if (strcmp(attr->key, "modifiers") == 0 && attr->kind == AIVM_NODE_ATTR_STRING) {
            attr->string_value = modifiers_text;
        } else if (strcmp(attr->key, "repeat") == 0 && attr->kind == AIVM_NODE_ATTR_BOOL) {
            attr->bool_value = normalized_repeat;
        }
    }
    return 1;
}
#endif

static int native_syscall_ui_create_window(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    int window_id;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 3U || args == NULL ||
        args[0].type != AIVM_VAL_STRING ||
        args[0].string_value == NULL ||
        args[1].type != AIVM_VAL_INT ||
        args[2].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    if (args[1].int_value <= 0 || args[2].int_value <= 0) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    window_id = g_ui_next_window_id;
    if (window_id <= 0 || window_id >= 2147483647) {
        g_ui_next_window_id = 1;
        window_id = g_ui_next_window_id;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_create_window(window_id, args[0].string_value, (int)args[1].int_value, (int)args[2].int_value) != 0) {
        return ui_fail_not_available(result);
    }
    (void)ui_update_window_size_node(g_wasm_active_vm, (int)args[1].int_value, (int)args[2].int_value);
#else
    return ui_fail_not_available(result);
#endif
    g_ui_next_window_id += 1;
    *result = aivm_value_int(window_id);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_begin_frame(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_begin_frame((int)args[0].int_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_rect(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 6U || args == NULL ||
        args[0].type != AIVM_VAL_INT ||
        args[1].type != AIVM_VAL_INT ||
        args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_INT ||
        args[4].type != AIVM_VAL_INT ||
        args[5].type != AIVM_VAL_STRING ||
        args[5].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_draw_rect(
            (int)args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            (int)args[3].int_value,
            (int)args[4].int_value,
            args[5].string_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_text(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 6U || args == NULL ||
        args[0].type != AIVM_VAL_INT ||
        args[1].type != AIVM_VAL_INT ||
        args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_STRING ||
        args[3].string_value == NULL ||
        args[4].type != AIVM_VAL_STRING ||
        args[4].string_value == NULL ||
        args[5].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_draw_text(
            (int)args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            args[3].string_value,
            args[4].string_value,
            (int)args[5].int_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 7U || args == NULL ||
        args[0].type != AIVM_VAL_INT ||
        args[1].type != AIVM_VAL_INT ||
        args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_INT ||
        args[4].type != AIVM_VAL_INT ||
        args[5].type != AIVM_VAL_STRING ||
        args[5].string_value == NULL ||
        args[6].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_draw_line(
            (int)args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            (int)args[3].int_value,
            (int)args[4].int_value,
            args[5].string_value,
            (int)args[6].int_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_ellipse(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 6U || args == NULL ||
        args[0].type != AIVM_VAL_INT ||
        args[1].type != AIVM_VAL_INT ||
        args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_INT ||
        args[4].type != AIVM_VAL_INT ||
        args[5].type != AIVM_VAL_STRING ||
        args[5].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_draw_ellipse(
            (int)args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            (int)args[3].int_value,
            (int)args[4].int_value,
            args[5].string_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_path(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 4U || args == NULL ||
        args[0].type != AIVM_VAL_INT ||
        args[1].type != AIVM_VAL_STRING ||
        args[1].string_value == NULL ||
        args[2].type != AIVM_VAL_STRING ||
        args[2].string_value == NULL ||
        args[3].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_draw_path(
            (int)args[0].int_value,
            args[1].string_value,
            args[2].string_value,
            (int)args[3].int_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_draw_image(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 6U || args == NULL ||
        args[0].type != AIVM_VAL_INT ||
        args[1].type != AIVM_VAL_INT ||
        args[2].type != AIVM_VAL_INT ||
        args[3].type != AIVM_VAL_INT ||
        args[4].type != AIVM_VAL_INT ||
        args[5].type != AIVM_VAL_STRING ||
        args[5].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_draw_image(
            (int)args[0].int_value,
            (int)args[1].int_value,
            (int)args[2].int_value,
            (int)args[3].int_value,
            (int)args[4].int_value,
            args[5].string_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_end_frame(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_end_frame((int)args[0].int_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_present(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_present((int)args[0].int_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_wait_frame(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_wait_frame((int)args[0].int_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_ui_poll_event(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    #ifdef AIVM_WASM_WEB
    {
        int event_type = aivm_web_ui_poll_event_type((int)args[0].int_value);
        int event_x = aivm_web_ui_poll_event_x((int)args[0].int_value);
        int event_y = aivm_web_ui_poll_event_y((int)args[0].int_value);
        int event_repeat;
        if (event_type < 0 || event_x < -1 || event_y < -1 ||
            aivm_web_ui_poll_event_target_id((int)args[0].int_value, g_ui_event_target_id, (int)sizeof(g_ui_event_target_id)) != 0 ||
            aivm_web_ui_poll_event_key((int)args[0].int_value, g_ui_event_key, (int)sizeof(g_ui_event_key)) != 0 ||
            aivm_web_ui_poll_event_text((int)args[0].int_value, g_ui_event_text, (int)sizeof(g_ui_event_text)) != 0 ||
            aivm_web_ui_poll_event_modifiers((int)args[0].int_value, g_ui_event_modifiers, (int)sizeof(g_ui_event_modifiers)) != 0) {
            return ui_fail_not_available(result);
        }
        event_repeat = aivm_web_ui_poll_event_repeat((int)args[0].int_value);
        if (event_repeat < 0) {
            return ui_fail_not_available(result);
        }
        (void)ui_update_event_node(
            g_wasm_active_vm,
            event_type,
            g_ui_event_target_id,
            event_x,
            event_y,
            g_ui_event_key,
            g_ui_event_text,
            g_ui_event_modifiers,
            event_repeat);
    }
    if (g_wasm_active_vm == NULL || g_wasm_active_vm->ui_empty_event_node_handle <= 0) {
        return ui_fail_not_available(result);
    }
    *result = aivm_value_node(g_wasm_active_vm->ui_empty_event_node_handle);
    return AIVM_SYSCALL_OK;
    #else
    (void)g_wasm_active_vm;
    return ui_fail_not_available(result);
    #endif
}

static int native_syscall_ui_get_window_size(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    #ifdef AIVM_WASM_WEB
    {
        int width = aivm_web_ui_get_window_width((int)args[0].int_value);
        int height = aivm_web_ui_get_window_height((int)args[0].int_value);
        if (width <= 0 || height <= 0) {
            return ui_fail_not_available(result);
        }
        (void)ui_update_window_size_node(g_wasm_active_vm, width, height);
    }
    if (g_wasm_active_vm == NULL || g_wasm_active_vm->ui_default_window_size_node_handle <= 0) {
        return ui_fail_not_available(result);
    }
    *result = aivm_value_node(g_wasm_active_vm->ui_default_window_size_node_handle);
    return AIVM_SYSCALL_OK;
    #else
    (void)g_wasm_active_vm;
    return ui_fail_not_available(result);
    #endif
}

static int native_syscall_ui_close_window(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 1U || args == NULL || args[0].type != AIVM_VAL_INT) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    if (aivm_web_ui_close_window((int)args[0].int_value) != 0) {
        return ui_fail_not_available(result);
    }
#else
    return ui_fail_not_available(result);
#endif
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int native_syscall_console_read_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static char line[4096];
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    {
        int n = aivm_web_stdin_fill(line, (int)sizeof(line));
        if (n == -2) {
            g_wasm_syscall_error_message = "stdin bridge is not available on this target.";
            g_wasm_syscall_error_code = "RUN101";
            result->type = AIVM_VAL_VOID;
            return AIVM_SYSCALL_ERR_NOT_FOUND;
        }
        if (n < 0) {
            line[0] = '\0';
        }
    }
#else
    if (fgets(line, sizeof(line), stdin) == NULL) {
        line[0] = '\0';
    } else {
        size_t n = strlen(line);
        while (n > 0U && (line[n - 1U] == '\n' || line[n - 1U] == '\r')) {
            line[--n] = '\0';
        }
    }
#endif
    *result = aivm_value_string(line);
    return AIVM_SYSCALL_OK;
}

static int native_syscall_console_read_all_stdin(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    static char text[16384];
    size_t used = 0U;
    (void)target;
    (void)args;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 0U) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
#ifdef AIVM_WASM_WEB
    {
        char line[4096];
        int first = 1;
        for (;;) {
            int n = aivm_web_stdin_fill(line, (int)sizeof(line));
            size_t line_len;
            if (n == -2) {
                g_wasm_syscall_error_message = "stdin bridge is not available on this target.";
                g_wasm_syscall_error_code = "RUN101";
                result->type = AIVM_VAL_VOID;
                return AIVM_SYSCALL_ERR_NOT_FOUND;
            }
            if (n < 0 || n == 0) {
                break;
            }
            line_len = strlen(line);
            if (!first) {
                if (used + 1U >= sizeof(text)) {
                    break;
                }
                text[used++] = '\n';
            }
            if (used + line_len >= sizeof(text)) {
                line_len = sizeof(text) - used - 1U;
            }
            if (line_len == 0U) {
                break;
            }
            memcpy(text + used, line, line_len);
            used += line_len;
            first = 0;
        }
    }
#else
    {
        int ch;
        while ((ch = fgetc(stdin)) != EOF) {
            if (used + 1U >= sizeof(text)) {
                break;
            }
            text[used++] = (char)ch;
        }
    }
#endif
    text[used] = '\0';
    *result = aivm_value_string(text);
    return AIVM_SYSCALL_OK;
}

#ifndef AIVM_WASM_WEB
static AivmRemoteServerConfig g_remote_server_config;
static AivmRemoteServerSession g_remote_server_session;
static int g_remote_session_ready = 0;
static uint32_t g_remote_next_request_id = 1U;
#endif

#ifndef AIVM_WASM_WEB
static int remote_parse_caps_csv(
    const char* csv,
    char out_caps[AIVM_REMOTE_MAX_CAPS][AIVM_REMOTE_MAX_TEXT + 1],
    uint32_t* out_count)
{
    const char* cursor;
    uint32_t count = 0U;
    if (out_caps == NULL || out_count == NULL) {
        return 0;
    }
    if (csv == NULL || *csv == '\0') {
        *out_count = 0U;
        return 1;
    }
    cursor = csv;
    while (*cursor != '\0') {
        const char* end = cursor;
        size_t len;
        if (count >= AIVM_REMOTE_MAX_CAPS) {
            return 0;
        }
        while (*end != '\0' && *end != ',') {
            end += 1;
        }
        len = (size_t)(end - cursor);
        if (len > AIVM_REMOTE_MAX_TEXT) {
            return 0;
        }
        if (len > 0U) {
            memcpy(out_caps[count], cursor, len);
            out_caps[count][len] = '\0';
            count += 1U;
        }
        cursor = (*end == ',') ? (end + 1) : end;
    }
    *out_count = count;
    return 1;
}
#endif

static int remote_token_authorized(void)
{
    const char* expected = getenv("AIVM_REMOTE_EXPECTED_TOKEN");
    const char* provided = getenv("AIVM_REMOTE_SESSION_TOKEN");
    size_t expected_len;
    size_t provided_len;
    if (expected == NULL && provided == NULL) {
        /*
         * Deterministic fallback for wasm host modes where environment
         * propagation is unavailable. Partial token state remains rejected.
         */
        return 1;
    }
    if (expected == NULL || provided == NULL) {
        return 0;
    }
    expected_len = strlen(expected);
    provided_len = strlen(provided);
    if (expected_len == 0U || provided_len == 0U) {
        return 0;
    }
    if (expected_len > 256U || provided_len > 256U) {
        return 0;
    }
    return strcmp(expected, provided) == 0;
}

#ifdef AIVM_WASM_WEB
EM_JS(int, aivm_web_remote_call_sync, (const char* cap_ptr, const char* op_ptr, int value), {
    const cap = UTF8ToString(cap_ptr);
    const op = UTF8ToString(op_ptr);
    if (typeof globalThis.__aivmRemoteCall !== 'function') {
        return -2147483647;
    }
    return Asyncify.handleSleep((wakeUp) => {
        Promise.resolve()
            .then(() => globalThis.__aivmRemoteCall(cap, op, value))
            .then((v) => wakeUp((v | 0)))
            .catch(() => wakeUp(-2147483647));
    });
});
#endif

#ifndef AIVM_WASM_WEB
static int remote_session_ensure_ready(void)
{
    uint8_t request_bytes[1024];
    uint8_t response_bytes[1024];
    size_t request_len = 0U;
    size_t response_len = 0U;
    AivmRemoteHello hello;
    AivmRemoteWelcome welcome;
    uint32_t response_id = 0U;
    AivmRemoteCodecStatus codec_status;
    AivmRemoteSessionStatus session_status;
    uint32_t i;
    const char* caps_env;

    if (g_remote_session_ready) {
        return 1;
    }

    memset(&g_remote_server_config, 0, sizeof(g_remote_server_config));
    g_remote_server_config.proto_version = 1U;
    caps_env = getenv("AIVM_REMOTE_CAPS");
    if (caps_env == NULL || caps_env[0] == '\0') {
        (void)snprintf(
            g_remote_server_config.allowed_caps[0],
            sizeof(g_remote_server_config.allowed_caps[0]),
            "%s",
            "cap.remote");
        g_remote_server_config.allowed_caps_count = 1U;
    } else if (!remote_parse_caps_csv(
                   caps_env,
                   g_remote_server_config.allowed_caps,
                   &g_remote_server_config.allowed_caps_count)) {
        return 0;
    }

    aivm_remote_server_session_init(&g_remote_server_session);
    memset(&hello, 0, sizeof(hello));
    hello.proto_version = 1U;
    (void)snprintf(hello.client_name, sizeof(hello.client_name), "%s", "wasm-runner");
    hello.requested_caps_count = g_remote_server_config.allowed_caps_count;
    for (i = 0U; i < hello.requested_caps_count; i += 1U) {
        (void)snprintf(
            hello.requested_caps[i],
            sizeof(hello.requested_caps[i]),
            "%s",
            g_remote_server_config.allowed_caps[i]);
    }
    codec_status = aivm_remote_encode_hello(
        1U,
        &hello,
        request_bytes,
        sizeof(request_bytes),
        &request_len);
    if (codec_status != AIVM_REMOTE_CODEC_OK) {
        return 0;
    }
    session_status = aivm_remote_server_process_frame(
        &g_remote_server_config,
        &g_remote_server_session,
        request_bytes,
        request_len,
        response_bytes,
        sizeof(response_bytes),
        &response_len);
    if (session_status != AIVM_REMOTE_SESSION_OK) {
        return 0;
    }
    memset(&welcome, 0, sizeof(welcome));
    codec_status = aivm_remote_decode_welcome(
        response_bytes,
        response_len,
        &response_id,
        &welcome);
    if (codec_status != AIVM_REMOTE_CODEC_OK || response_id != 1U) {
        return 0;
    }
    g_remote_session_ready = 1;
    return 1;
}
#endif

static int remote_session_invoke_call(
    const char* cap,
    const char* op,
    int64_t value,
    AivmValue* result)
{
#ifdef AIVM_WASM_WEB
    int web_result;
    if (cap == NULL || op == NULL || result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    web_result = aivm_web_remote_call_sync(cap, op, (int)value);
    if (web_result == -2147483647) {
        g_wasm_syscall_error_message = "web remote client is not available.";
        g_wasm_syscall_error_code = "RUN101";
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    *result = aivm_value_int((int64_t)web_result);
    return AIVM_SYSCALL_OK;
#else
    uint8_t request_bytes[1024];
    uint8_t response_bytes[1024];
    size_t request_len = 0U;
    size_t response_len = 0U;
    uint32_t request_id;
    uint32_t response_id = 0U;
    AivmRemoteCall call;
    AivmRemoteResult call_result;
    AivmRemoteError call_error;
    AivmRemoteCodecStatus codec_status;
    AivmRemoteSessionStatus session_status;

    if (cap == NULL || op == NULL || result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (!remote_session_ensure_ready()) {
        g_wasm_syscall_error_message = "remote session bootstrap failed.";
        g_wasm_syscall_error_code = "RUN101";
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }

    memset(&call, 0, sizeof(call));
    (void)snprintf(call.cap, sizeof(call.cap), "%s", cap);
    (void)snprintf(call.op, sizeof(call.op), "%s", op);
    call.value = value;
    request_id = g_remote_next_request_id++;
    if (g_remote_next_request_id == 0U) {
        g_remote_next_request_id = 1U;
    }
    codec_status = aivm_remote_encode_call(
        request_id,
        &call,
        request_bytes,
        sizeof(request_bytes),
        &request_len);
    if (codec_status != AIVM_REMOTE_CODEC_OK) {
        g_wasm_syscall_error_message = "remote call encoding failed.";
        g_wasm_syscall_error_code = "RUN101";
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    session_status = aivm_remote_server_process_frame(
        &g_remote_server_config,
        &g_remote_server_session,
        request_bytes,
        request_len,
        response_bytes,
        sizeof(response_bytes),
        &response_len);
    if (session_status != AIVM_REMOTE_SESSION_OK) {
        g_wasm_syscall_error_message = "remote call processing failed.";
        g_wasm_syscall_error_code = "RUN101";
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }

    codec_status = aivm_remote_decode_result(
        response_bytes,
        response_len,
        &response_id,
        &call_result);
    if (codec_status == AIVM_REMOTE_CODEC_OK && response_id == request_id) {
        *result = aivm_value_int(call_result.value);
        return AIVM_SYSCALL_OK;
    }
    codec_status = aivm_remote_decode_error(
        response_bytes,
        response_len,
        &response_id,
        &call_error);
    if (codec_status == AIVM_REMOTE_CODEC_OK && response_id == request_id) {
        if (snprintf(
                g_wasm_syscall_error_message_buf,
                sizeof(g_wasm_syscall_error_message_buf),
                "%s",
                call_error.message) <= 0) {
            g_wasm_syscall_error_message = "remote call failed.";
        } else {
            g_wasm_syscall_error_message = g_wasm_syscall_error_message_buf;
        }
        g_wasm_syscall_error_code = "RUN101";
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }

    g_wasm_syscall_error_message = "remote response decoding failed.";
    g_wasm_syscall_error_code = "RUN101";
    result->type = AIVM_VAL_VOID;
    return AIVM_SYSCALL_ERR_INVALID;
#endif
}

static int native_syscall_remote_call(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    const char* cap;
    const char* op;
    (void)target;
    if (result == NULL) {
        return AIVM_SYSCALL_ERR_NULL_RESULT;
    }
    if (arg_count != 3U ||
        args == NULL ||
        args[0].type != AIVM_VAL_STRING ||
        args[1].type != AIVM_VAL_STRING ||
        args[2].type != AIVM_VAL_INT ||
        args[0].string_value == NULL ||
        args[1].string_value == NULL) {
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_CONTRACT;
    }
    cap = args[0].string_value;
    op = args[1].string_value;
    if (strlen(cap) > 64U || strlen(op) > 64U) {
        g_wasm_syscall_error_message = "remote call exceeds deterministic frame limits.";
        g_wasm_syscall_error_code = "RUN101";
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_INVALID;
    }
    if (!remote_token_authorized()) {
        g_wasm_syscall_error_message = "remote session token is missing or invalid for this target.";
        g_wasm_syscall_error_code = "RUN101";
        result->type = AIVM_VAL_VOID;
        return AIVM_SYSCALL_ERR_NOT_FOUND;
    }
    return remote_session_invoke_call(cap, op, args[2].int_value, result);
}

int main(int argc, char** argv)
{
    unsigned char* bytes = NULL;
    size_t byte_count = 0U;
    AivmProgram program;
    AivmProgramLoadResult load_result;
    AivmSyscallBinding bindings[128];
    size_t binding_count = 0U;
    uint32_t syscall_id;
    static AivmVm vm;
    const char* const* app_argv = NULL;
    size_t app_argc = 0U;

    if (argc < 2 || argv == NULL) {
        fprintf(stderr, "Usage: aivm-runtime-wasm32 <app.aibc1|- for stdin> [args...]\n");
        return 2;
    }

    if (argc > 2) {
        app_argv = (const char* const*)&argv[2];
        app_argc = (size_t)(argc - 2);
    }

    if ((strcmp(argv[1], "-") == 0 && !read_binary_stdin(&bytes, &byte_count)) ||
        (strcmp(argv[1], "-") != 0 && !read_binary_file(argv[1], &bytes, &byte_count))) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Failed to read AiBC1 file.\" nodeId=program)\n");
        return 2;
    }

    load_result = aivm_program_load_aibc1(bytes, byte_count, &program);
    free(bytes);
    if (load_result.status != AIVM_PROGRAM_OK) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Failed to load AiBC1 program.\" nodeId=program)\n");
        return 2;
    }

    for (syscall_id = 0U; syscall_id <= 255U; syscall_id += 1U) {
        const AivmSyscallContract* contract = aivm_syscall_contract_find_by_id(syscall_id);
        if (contract == NULL) {
            continue;
        }
        if (binding_count >= (sizeof(bindings) / sizeof(bindings[0]))) {
            fprintf(stderr, "Err#err1(code=RUN001 message=\"Wasm syscall binding overflow.\" nodeId=syscall)\n");
            return 2;
        }
        bindings[binding_count].target = contract->target;
        bindings[binding_count].handler = native_syscall_unavailable;
        if (strcmp(contract->target, "sys.stdout.writeLine") == 0 ||
            strcmp(contract->target, "sys.stdout_writeLine") == 0 ||
            strcmp(contract->target, "io.print") == 0 ||
            strcmp(contract->target, "io.write") == 0) {
            bindings[binding_count].handler = native_syscall_stdout_write_line;
        } else if (strcmp(contract->target, "sys.bytes.fromUtf8String") == 0) {
            bindings[binding_count].handler = native_syscall_bytes_from_utf8_string;
        } else if (strcmp(contract->target, "sys.bytes.toUtf8String") == 0) {
            bindings[binding_count].handler = native_syscall_bytes_to_utf8_string;
        } else if (strcmp(contract->target, "sys.bytes.toBase64") == 0) {
            bindings[binding_count].handler = native_syscall_bytes_to_base64;
        } else if (strcmp(contract->target, "sys.bytes.fromBase64") == 0) {
            bindings[binding_count].handler = native_syscall_bytes_from_base64;
        } else if (strcmp(contract->target, "sys.process.args") == 0 ||
                   strcmp(contract->target, "sys.process_argv") == 0) {
            bindings[binding_count].handler = native_syscall_process_argv;
        } else if (strcmp(contract->target, "sys.console.readLine") == 0) {
            bindings[binding_count].handler = native_syscall_console_read_line;
        } else if (strcmp(contract->target, "sys.console.readAllStdin") == 0) {
            bindings[binding_count].handler = native_syscall_console_read_all_stdin;
        } else if (strcmp(contract->target, "sys.remote.call") == 0) {
            bindings[binding_count].handler = native_syscall_remote_call;
        } else if (strcmp(contract->target, "sys.ui.createWindow") == 0) {
            bindings[binding_count].handler = native_syscall_ui_create_window;
        } else if (strcmp(contract->target, "sys.ui.beginFrame") == 0) {
            bindings[binding_count].handler = native_syscall_ui_begin_frame;
        } else if (strcmp(contract->target, "sys.ui.drawRect") == 0) {
            bindings[binding_count].handler = native_syscall_ui_draw_rect;
        } else if (strcmp(contract->target, "sys.ui.drawText") == 0) {
            bindings[binding_count].handler = native_syscall_ui_draw_text;
        } else if (strcmp(contract->target, "sys.ui.drawLine") == 0) {
            bindings[binding_count].handler = native_syscall_ui_draw_line;
        } else if (strcmp(contract->target, "sys.ui.drawEllipse") == 0) {
            bindings[binding_count].handler = native_syscall_ui_draw_ellipse;
        } else if (strcmp(contract->target, "sys.ui.drawPath") == 0) {
            bindings[binding_count].handler = native_syscall_ui_draw_path;
        } else if (strcmp(contract->target, "sys.ui.drawImage") == 0) {
            bindings[binding_count].handler = native_syscall_ui_draw_image;
        } else if (strcmp(contract->target, "sys.ui.endFrame") == 0) {
            bindings[binding_count].handler = native_syscall_ui_end_frame;
        } else if (strcmp(contract->target, "sys.ui.present") == 0) {
            bindings[binding_count].handler = native_syscall_ui_present;
        } else if (strcmp(contract->target, "sys.ui.waitFrame") == 0) {
            bindings[binding_count].handler = native_syscall_ui_wait_frame;
        } else if (strcmp(contract->target, "sys.ui.pollEvent") == 0) {
            bindings[binding_count].handler = native_syscall_ui_poll_event;
        } else if (strcmp(contract->target, "sys.ui.getWindowSize") == 0) {
            bindings[binding_count].handler = native_syscall_ui_get_window_size;
        } else if (strcmp(contract->target, "sys.ui.closeWindow") == 0) {
            bindings[binding_count].handler = native_syscall_ui_close_window;
        }
        binding_count += 1U;
    }
    if (!ensure_binding_handler(bindings, &binding_count, sizeof(bindings) / sizeof(bindings[0]), "sys.bytes.fromUtf8String", native_syscall_bytes_from_utf8_string) ||
        !ensure_binding_handler(bindings, &binding_count, sizeof(bindings) / sizeof(bindings[0]), "sys.bytes.toUtf8String", native_syscall_bytes_to_utf8_string) ||
        !ensure_binding_handler(bindings, &binding_count, sizeof(bindings) / sizeof(bindings[0]), "sys.bytes.toBase64", native_syscall_bytes_to_base64) ||
        !ensure_binding_handler(bindings, &binding_count, sizeof(bindings) / sizeof(bindings[0]), "sys.bytes.fromBase64", native_syscall_bytes_from_base64)) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Wasm bytes syscall binding missing.\" nodeId=syscall)\n");
        return 2;
    }
    if (!ensure_binding_handler(bindings, &binding_count, sizeof(bindings) / sizeof(bindings[0]), "sys.image.decodeToRgbaBase64", native_syscall_unavailable)) {
        fprintf(stderr, "Err#err1(code=RUN001 message=\"Wasm image decode binding missing.\" nodeId=syscall)\n");
        return 2;
    }

    g_wasm_active_vm = &vm;

    if (!aivm_execute_program_with_syscalls_and_argv(
            &program,
            bindings,
            binding_count,
            app_argv,
            app_argc,
            &vm) ||
        vm.status == AIVM_VM_STATUS_ERROR) {
        if (g_wasm_syscall_error_message != NULL) {
            fprintf(
                stderr,
                "Err#err1(code=%s message=\"%s\" nodeId=vm)\n",
                (g_wasm_syscall_error_code != NULL) ? g_wasm_syscall_error_code : "RUN001",
                g_wasm_syscall_error_message);
            g_wasm_active_vm = NULL;
            return 3;
        }
        print_vm_failure(&program, &vm, "AiBC1 execution failed.");
        g_wasm_active_vm = NULL;
        return 3;
    }
    if (vm.status == AIVM_VM_STATUS_HALTED && vm.stack_count > 0U) {
        const AivmValue* top = &vm.stack[vm.stack_count - 1U];
        if (top->type == AIVM_VAL_INT) {
            printf("Ok#ok1(type=int value=%d)\n", (int)top->int_value);
            g_wasm_active_vm = NULL;
            return (int)top->int_value;
        }
    }

    g_wasm_active_vm = NULL;

    return 0;
}
