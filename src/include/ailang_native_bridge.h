#ifndef AILANG_NATIVE_BRIDGE_H
#define AILANG_NATIVE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AILANG_NATIVE_BRIDGE_ABI_VERSION 1U
#define AILANG_NATIVE_BRIDGE_MAX_FUNCTIONS 128U
#define AILANG_NATIVE_BRIDGE_MAX_NAME 96U
#define AILANG_NATIVE_BRIDGE_MAX_STRING 4096U

typedef enum AilangNativeValueType {
    AILANG_NATIVE_NULL = 0,
    AILANG_NATIVE_BOOL = 1,
    AILANG_NATIVE_INT = 2,
    AILANG_NATIVE_STRING = 3,
    AILANG_NATIVE_BYTES = 4
} AilangNativeValueType;

typedef struct AilangNativeBytes {
    const uint8_t* data;
    size_t len;
} AilangNativeBytes;

typedef struct AilangNativeValue {
    AilangNativeValueType type;
    union {
        int bool_value;
        int64_t int_value;
        const char* string_value;
        AilangNativeBytes bytes_value;
    } as;
} AilangNativeValue;

typedef int (*AilangNativeFn)(
    void* context,
    const AilangNativeValue* args,
    size_t arg_count,
    AilangNativeValue* result,
    char* error,
    size_t error_len);

typedef struct AilangNativeFunction {
    char name[AILANG_NATIVE_BRIDGE_MAX_NAME];
    AilangNativeFn fn;
    void* context;
} AilangNativeFunction;

typedef struct AilangNativeBridge {
    AilangNativeFunction functions[AILANG_NATIVE_BRIDGE_MAX_FUNCTIONS];
    size_t function_count;
} AilangNativeBridge;

unsigned ailang_native_bridge_abi_version(void);
void ailang_native_bridge_init(AilangNativeBridge* bridge);
void ailang_native_value_null(AilangNativeValue* value);
void ailang_native_value_bool(AilangNativeValue* value, int bool_value);
void ailang_native_value_int(AilangNativeValue* value, int64_t int_value);
void ailang_native_value_string(AilangNativeValue* value, const char* string_value);
void ailang_native_value_bytes(AilangNativeValue* value, const uint8_t* data, size_t len);

int ailang_native_bridge_register(
    AilangNativeBridge* bridge,
    const char* name,
    AilangNativeFn fn,
    void* context,
    char* error,
    size_t error_len);

int ailang_native_bridge_call(
    const AilangNativeBridge* bridge,
    const char* name,
    const AilangNativeValue* args,
    size_t arg_count,
    AilangNativeValue* result,
    char* error,
    size_t error_len);

size_t ailang_native_bridge_count(const AilangNativeBridge* bridge);
const char* ailang_native_bridge_name_at(const AilangNativeBridge* bridge, size_t index);

#ifdef __cplusplus
}
#endif

#endif
