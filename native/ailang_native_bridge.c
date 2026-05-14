#include "ailang_native_bridge.h"

#include <stdio.h>
#include <string.h>

static int bridge_error(char* error, size_t error_len, const char* message)
{
    if (error != NULL && error_len > 0U) {
        (void)snprintf(error, error_len, "%s", message == NULL ? "native bridge error" : message);
    }
    return 0;
}

unsigned ailang_native_bridge_abi_version(void)
{
    return AILANG_NATIVE_BRIDGE_ABI_VERSION;
}

void ailang_native_bridge_init(AilangNativeBridge* bridge)
{
    if (bridge != NULL) {
        memset(bridge, 0, sizeof(*bridge));
    }
}

void ailang_native_value_null(AilangNativeValue* value)
{
    if (value != NULL) {
        memset(value, 0, sizeof(*value));
        value->type = AILANG_NATIVE_NULL;
    }
}

void ailang_native_value_bool(AilangNativeValue* value, int bool_value)
{
    if (value != NULL) {
        memset(value, 0, sizeof(*value));
        value->type = AILANG_NATIVE_BOOL;
        value->as.bool_value = bool_value != 0;
    }
}

void ailang_native_value_int(AilangNativeValue* value, int64_t int_value)
{
    if (value != NULL) {
        memset(value, 0, sizeof(*value));
        value->type = AILANG_NATIVE_INT;
        value->as.int_value = int_value;
    }
}

void ailang_native_value_string(AilangNativeValue* value, const char* string_value)
{
    if (value != NULL) {
        memset(value, 0, sizeof(*value));
        value->type = AILANG_NATIVE_STRING;
        value->as.string_value = string_value == NULL ? "" : string_value;
    }
}

void ailang_native_value_bytes(AilangNativeValue* value, const uint8_t* data, size_t len)
{
    if (value != NULL) {
        memset(value, 0, sizeof(*value));
        value->type = AILANG_NATIVE_BYTES;
        value->as.bytes_value.data = data;
        value->as.bytes_value.len = data == NULL ? 0U : len;
    }
}

int ailang_native_bridge_register(
    AilangNativeBridge* bridge,
    const char* name,
    AilangNativeFn fn,
    void* context,
    char* error,
    size_t error_len)
{
    size_t i;
    size_t name_len;
    if (bridge == NULL || name == NULL || name[0] == '\0' || fn == NULL) {
        return bridge_error(error, error_len, "invalid native function registration");
    }
    name_len = strlen(name);
    if (name_len >= AILANG_NATIVE_BRIDGE_MAX_NAME) {
        return bridge_error(error, error_len, "native function name is too long");
    }
    for (i = 0U; i < bridge->function_count; i += 1U) {
        if (strcmp(bridge->functions[i].name, name) == 0) {
            return bridge_error(error, error_len, "duplicate native function name");
        }
    }
    if (bridge->function_count >= AILANG_NATIVE_BRIDGE_MAX_FUNCTIONS) {
        return bridge_error(error, error_len, "native function table is full");
    }
    memcpy(bridge->functions[bridge->function_count].name, name, name_len + 1U);
    bridge->functions[bridge->function_count].fn = fn;
    bridge->functions[bridge->function_count].context = context;
    bridge->function_count += 1U;
    if (error != NULL && error_len > 0U) {
        error[0] = '\0';
    }
    return 1;
}

int ailang_native_bridge_call(
    const AilangNativeBridge* bridge,
    const char* name,
    const AilangNativeValue* args,
    size_t arg_count,
    AilangNativeValue* result,
    char* error,
    size_t error_len)
{
    size_t i;
    if (bridge == NULL || name == NULL || name[0] == '\0' || result == NULL) {
        return bridge_error(error, error_len, "invalid native function call");
    }
    for (i = 0U; i < bridge->function_count; i += 1U) {
        if (strcmp(bridge->functions[i].name, name) == 0) {
            ailang_native_value_null(result);
            return bridge->functions[i].fn(
                bridge->functions[i].context,
                args,
                arg_count,
                result,
                error,
                error_len);
        }
    }
    return bridge_error(error, error_len, "native function was not registered");
}

size_t ailang_native_bridge_count(const AilangNativeBridge* bridge)
{
    return bridge == NULL ? 0U : bridge->function_count;
}

const char* ailang_native_bridge_name_at(const AilangNativeBridge* bridge, size_t index)
{
    if (bridge == NULL || index >= bridge->function_count) {
        return NULL;
    }
    return bridge->functions[index].name;
}
