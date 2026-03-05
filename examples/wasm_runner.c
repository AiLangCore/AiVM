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
    if (!remote_parse_caps_csv(
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
        } else if (strcmp(contract->target, "sys.ui.endFrame") == 0) {
            bindings[binding_count].handler = native_syscall_ui_end_frame;
        } else if (strcmp(contract->target, "sys.ui.present") == 0) {
            bindings[binding_count].handler = native_syscall_ui_present;
        } else if (strcmp(contract->target, "sys.ui.closeWindow") == 0) {
            bindings[binding_count].handler = native_syscall_ui_close_window;
        }
        binding_count += 1U;
    }

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
            return 3;
        }
        fprintf(stderr, "Err#err1(code=RUN001 message=\"AiBC1 execution failed.\" nodeId=vm)\n");
        return 3;
    }
    if (vm.status == AIVM_VM_STATUS_HALTED && vm.stack_count > 0U) {
        const AivmValue* top = &vm.stack[vm.stack_count - 1U];
        if (top->type == AIVM_VAL_INT) {
            printf("Ok#ok1(type=int value=%d)\n", (int)top->int_value);
            return (int)top->int_value;
        }
    }

    return 0;
}
