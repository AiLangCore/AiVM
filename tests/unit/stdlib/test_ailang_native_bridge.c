#include "ailang_native_bridge.h"

#include <stdio.h>
#include <string.h>

static int add_native(
    void* context,
    const AilangNativeValue* args,
    size_t arg_count,
    AilangNativeValue* result,
    char* error,
    size_t error_len)
{
    (void)context;
    if (arg_count != 2U ||
        args == NULL ||
        args[0].type != AILANG_NATIVE_INT ||
        args[1].type != AILANG_NATIVE_INT) {
        (void)snprintf(error, error_len, "expected two int args");
        return 0;
    }
    ailang_native_value_int(result, args[0].as.int_value + args[1].as.int_value);
    return 1;
}

int main(void)
{
    AilangNativeBridge bridge;
    AilangNativeValue args[2];
    AilangNativeValue result;
    char error[256];

    ailang_native_bridge_init(&bridge);
    if (ailang_native_bridge_abi_version() != AILANG_NATIVE_BRIDGE_ABI_VERSION) {
        return 1;
    }
    if (!ailang_native_bridge_register(&bridge, "test.add", add_native, NULL, error, sizeof(error))) {
        return 2;
    }
    if (ailang_native_bridge_register(&bridge, "test.add", add_native, NULL, error, sizeof(error))) {
        return 3;
    }
    if (ailang_native_bridge_count(&bridge) != 1U ||
        strcmp(ailang_native_bridge_name_at(&bridge, 0U), "test.add") != 0) {
        return 4;
    }
    ailang_native_value_int(&args[0], 20);
    ailang_native_value_int(&args[1], 22);
    if (!ailang_native_bridge_call(&bridge, "test.add", args, 2U, &result, error, sizeof(error))) {
        return 5;
    }
    if (result.type != AILANG_NATIVE_INT || result.as.int_value != 42) {
        return 6;
    }
    if (ailang_native_bridge_call(&bridge, "missing", args, 2U, &result, error, sizeof(error))) {
        return 7;
    }
    return 0;
}
