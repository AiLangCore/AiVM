#include <string.h>

#include "sys/aivm_syscall.h"

static const uint8_t k_test_bytes[] = { 'd', 'a', 't', 'a' };

static int expect(int condition)
{
    return condition ? 0 : 1;
}

static int handler_echo(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (arg_count == 1U && args != NULL) {
        *result = args[0];
        return AIVM_SYSCALL_OK;
    }

    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_draw_rect(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (arg_count == 6U) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_window_size(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args != NULL && arg_count == 1U && args[0].type == AIVM_VAL_INT) {
        *result = aivm_value_node(1);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_window_size_wrong_type(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    (void)arg_count;
    *result = aivm_value_string("wrong");
    return AIVM_SYSCALL_OK;
}

static int handler_poll_event(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args != NULL && arg_count == 1U && args[0].type == AIVM_VAL_INT) {
        *result = aivm_value_node(2);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_poll_event_wrong_type(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    (void)arg_count;
    *result = aivm_value_void();
    return AIVM_SYSCALL_OK;
}

static int handler_console_write(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args != NULL && arg_count == 1U && args[0].type == AIVM_VAL_STRING) {
        *result = aivm_value_void();
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_console_read_line(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (arg_count == 0U) {
        *result = aivm_value_string("line");
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_process_cwd(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (arg_count == 0U) {
        *result = aivm_value_string("/tmp");
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_process_argv(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (arg_count == 0U) {
        *result = aivm_value_node(7);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_time_now(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    (void)args;
    if (arg_count == 0U) {
        *result = aivm_value_int(1234);
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_fs_read_file(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args != NULL && arg_count == 1U && args[0].type == AIVM_VAL_STRING) {
        *result = aivm_value_bytes(k_test_bytes, sizeof(k_test_bytes));
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

static int handler_crypto_base64_encode(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args != NULL && arg_count == 1U && args[0].type == AIVM_VAL_STRING) {
        *result = aivm_value_string("ZW5jb2RlZA==");
        return AIVM_SYSCALL_OK;
    }
    *result = aivm_value_void();
    return AIVM_SYSCALL_ERR_INVALID;
}

int main(void)
{
    AivmValue result;
    AivmValue arg;
    AivmValue window_arg;
    AivmValue rect_args[6];
    AivmContractStatus contract_status = AIVM_CONTRACT_OK;
    AivmSyscallStatus status;
    static const AivmSyscallBinding bindings[] = {
        { "sys.echo", handler_echo }
    };
    static const AivmSyscallBinding ui_bindings[] = {
        { "sys.ui.drawRect", handler_draw_rect },
        { "sys.ui.getWindowSize", handler_window_size },
        { "sys.ui.pollEvent", handler_poll_event }
    };
    static const AivmSyscallBinding ui_bad_return_bindings[] = {
        { "sys.ui.getWindowSize", handler_window_size_wrong_type },
        { "sys.ui.pollEvent", handler_poll_event_wrong_type }
    };
    static const AivmSyscallBinding console_bindings[] = {
        { "sys.console.write", handler_console_write },
        { "sys.console.readLine", handler_console_read_line }
    };
    static const AivmSyscallBinding process_bindings[] = {
        { "sys.process.cwd", handler_process_cwd },
        { "sys.process.args", handler_process_argv }
    };
    static const AivmSyscallBinding system_bindings[] = {
        { "sys.time.nowUnixMs", handler_time_now },
        { "sys.fs.file.read", handler_fs_read_file }
    };
    static const AivmSyscallBinding crypto_bindings[] = {
        { "sys.crypto.base64Encode", handler_crypto_base64_encode }
    };

    status = aivm_syscall_invoke(NULL, "sys.echo", NULL, 0U, &result);
    if (expect(status == AIVM_SYSCALL_ERR_INVALID) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_syscall_status_code(status), "AIVMS001") == 0) != 0) {
        return 1;
    }

    status = aivm_syscall_dispatch(NULL, 0U, "sys.echo", NULL, 0U, &result);
    if (expect(status == AIVM_SYSCALL_ERR_INVALID) != 0) {
        return 1;
    }

    status = aivm_syscall_dispatch(bindings, 1U, "sys.missing", NULL, 0U, &result);
    if (expect(status == AIVM_SYSCALL_ERR_NOT_FOUND) != 0) {
        return 1;
    }

    arg = aivm_value_int(123);
    status = aivm_syscall_dispatch(bindings, 1U, "sys.echo", &arg, 1U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(result.int_value == 123) != 0) {
        return 1;
    }

    rect_args[0] = aivm_value_int(0);
    rect_args[1] = aivm_value_int(0);
    rect_args[2] = aivm_value_int(10);
    rect_args[3] = aivm_value_int(20);
    rect_args[4] = aivm_value_int(1);
    rect_args[5] = aivm_value_string("#fff");
    status = aivm_syscall_dispatch_checked(ui_bindings, 3U, "sys.ui.drawRect", rect_args, 6U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_VOID) != 0) {
        return 1;
    }

    status = aivm_syscall_dispatch_checked(ui_bindings, 3U, "sys.ui.drawRect", rect_args, 5U, &result);
    if (expect(status == AIVM_SYSCALL_ERR_CONTRACT) != 0) {
        return 1;
    }

    status = aivm_syscall_dispatch_checked_with_contract(ui_bindings, 3U, "sys.ui.drawRect", rect_args, 5U, &result, &contract_status);
    if (expect(status == AIVM_SYSCALL_ERR_CONTRACT) != 0) {
        return 1;
    }
    if (expect(contract_status == AIVM_CONTRACT_ERR_ARG_COUNT) != 0) {
        return 1;
    }

    window_arg = aivm_value_int(1);
    status = aivm_syscall_dispatch_checked(ui_bindings, 3U, "sys.ui.getWindowSize", &window_arg, 1U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }

    status = aivm_syscall_dispatch_checked(ui_bad_return_bindings, 2U, "sys.ui.getWindowSize", &window_arg, 1U, &result);
    if (expect(status == AIVM_SYSCALL_ERR_RETURN_TYPE) != 0) {
        return 1;
    }

    status = aivm_syscall_dispatch_checked(ui_bindings, 3U, "sys.ui.pollEvent", &window_arg, 1U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }

    status = aivm_syscall_dispatch_checked(ui_bad_return_bindings, 2U, "sys.ui.pollEvent", &window_arg, 1U, &result);
    if (expect(status == AIVM_SYSCALL_ERR_RETURN_TYPE) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_syscall_status_message(status), "Syscall return type violated contract.") == 0) != 0) {
        return 1;
    }
    status = aivm_syscall_dispatch_checked_with_contract(ui_bindings, 3U, "sys.unknown", NULL, 0U, &result, &contract_status);
    if (expect(status == AIVM_SYSCALL_ERR_CONTRACT) != 0) {
        return 1;
    }
    if (expect(contract_status == AIVM_CONTRACT_ERR_UNKNOWN_TARGET) != 0) {
        return 1;
    }
    arg = aivm_value_string("hello");
    status = aivm_syscall_dispatch_checked(console_bindings, 2U, "sys.console.write", &arg, 1U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_VOID) != 0) {
        return 1;
    }
    status = aivm_syscall_dispatch_checked(console_bindings, 2U, "sys.console.readLine", NULL, 0U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    status = aivm_syscall_dispatch_checked(process_bindings, 2U, "sys.process.cwd", NULL, 0U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    status = aivm_syscall_dispatch_checked(process_bindings, 2U, "sys.process.args", NULL, 0U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    status = aivm_syscall_dispatch_checked(system_bindings, 2U, "sys.time.nowUnixMs", NULL, 0U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    arg = aivm_value_string("x");
    status = aivm_syscall_dispatch_checked(system_bindings, 2U, "sys.fs.file.read", &arg, 1U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    status = aivm_syscall_dispatch_checked(crypto_bindings, 1U, "sys.crypto.base64Encode", &arg, 1U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(strcmp(aivm_syscall_status_code((AivmSyscallStatus)-999), "AIVMS999") == 0) != 0) {
        return 1;
    }

    return 0;
}
