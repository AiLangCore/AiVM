#include <stdio.h>

#include "aivm_c_api.h"
#include "aivm_host_abi.h"

typedef struct {
    size_t enqueued_count;
    size_t drained_count;
} TestHostContext;

static int expect_line(int condition, int line)
{
    if (condition) {
        return 0;
    }
    (void)fprintf(stderr, "expect failed at line %d\n", line);
    return 1;
}

#define expect(condition) expect_line((condition), __LINE__)

static int host_echo_int(
    const char* target,
    const AivmValue* args,
    size_t arg_count,
    AivmValue* result)
{
    (void)target;
    if (args == NULL ||
        result == NULL ||
        arg_count != 3U ||
        args[0].type != AIVM_VAL_STRING ||
        args[1].type != AIVM_VAL_STRING ||
        args[2].type != AIVM_VAL_INT) {
        return AIVM_SYSCALL_ERR_INVALID;
    }
    *result = aivm_value_int(args[2].int_value);
    return AIVM_SYSCALL_OK;
}

static int host_enqueue(void* context, const char* event_name, AivmValue payload)
{
    TestHostContext* host;
    if (context == NULL || event_name == NULL || payload.type != AIVM_VAL_INT) {
        return 0;
    }
    host = (TestHostContext*)context;
    host->enqueued_count++;
    return 0;
}

static int host_drain(void* context, size_t max_events, size_t* out_drained_count)
{
    TestHostContext* host;
    if (context == NULL || out_drained_count == NULL) {
        return 0;
    }
    host = (TestHostContext*)context;
    *out_drained_count = max_events < host->enqueued_count ? max_events : host->enqueued_count;
    host->drained_count += *out_drained_count;
    return 0;
}

int main(void)
{
    TestHostContext context = { 0U, 0U };
    size_t drained_count = 0U;
    AivmValue syscall_result = aivm_value_void();
    AivmCResult result;
    static const AivmSyscallBinding bindings[] = {
        { "sys.remote.call", host_echo_int }
    };
    AivmHostAbiDescriptor host = {
        .abi_version = AIVM_HOST_ABI_VERSION,
        .min_core_abi_version = AIVM_HOST_ABI_MIN_COMPATIBLE_VERSION,
        .target_id = "test-target",
        .host_name = "test-host",
        .syscall_bindings = bindings,
        .syscall_binding_count = 1U,
        .event_adapter = {
            .context = &context,
            .enqueue = host_enqueue,
            .drain = host_drain
        }
    };
    static const AivmValue syscall_args[] = {
        { .type = AIVM_VAL_STRING, .string_value = "cap.remote" },
        { .type = AIVM_VAL_STRING, .string_value = "echoInt" },
        { .type = AIVM_VAL_INT, .int_value = 42 }
    };
    static const AivmInstruction instructions[] = {
        { .opcode = AIVM_OP_CONST, .operand_int = 0 },
        { .opcode = AIVM_OP_CONST, .operand_int = 1 },
        { .opcode = AIVM_OP_CONST, .operand_int = 2 },
        { .opcode = AIVM_OP_CONST, .operand_int = 3 },
        { .opcode = AIVM_OP_CALL_SYS, .operand_int = 3 },
        { .opcode = AIVM_OP_HALT, .operand_int = 0 }
    };
    static const AivmValue constants[] = {
        { .type = AIVM_VAL_STRING, .string_value = "sys.remote.call" },
        { .type = AIVM_VAL_STRING, .string_value = "cap.remote" },
        { .type = AIVM_VAL_STRING, .string_value = "echoInt" },
        { .type = AIVM_VAL_INT, .int_value = 9 }
    };

    if (expect(aivm_host_abi_version() == AIVM_HOST_ABI_VERSION) != 0) {
        return 1;
    }
    if (expect(aivm_c_abi_version() == AIVM_HOST_ABI_VERSION) != 0) {
        return 1;
    }
    if (expect(aivm_host_abi_check_compatible(aivm_c_abi_version(), &host) == AIVM_HOST_ABI_COMPAT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_host_abi_check_compatible(0U, &host) == AIVM_HOST_ABI_COMPAT_CORE_TOO_OLD) != 0) {
        return 1;
    }
    if (expect(aivm_host_abi_check_compatible(2U, &host) == AIVM_HOST_ABI_COMPAT_CORE_TOO_NEW) != 0) {
        return 1;
    }
    if (expect(aivm_host_abi_check_compatible(aivm_c_abi_version(), NULL) == AIVM_HOST_ABI_COMPAT_INVALID) != 0) {
        return 1;
    }
    if (expect(aivm_host_abi_compatibility_code(AIVM_HOST_ABI_COMPAT_OK)[0] != '\0') != 0) {
        return 1;
    }

    if (expect(aivm_syscall_dispatch_checked(
        host.syscall_bindings,
        host.syscall_binding_count,
        "sys.remote.call",
        syscall_args,
        3U,
        &syscall_result) == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(syscall_result.type == AIVM_VAL_INT && syscall_result.int_value == 42) != 0) {
        return 1;
    }

    if (expect(aivm_runtime_host_enqueue_event(
        &host.event_adapter,
        "Input",
        aivm_value_int(1)) == AIVM_RUNTIME_HOST_EVENT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_runtime_host_drain_events(
        &host.event_adapter,
        1U,
        &drained_count) == AIVM_RUNTIME_HOST_EVENT_OK) != 0) {
        return 1;
    }
    if (expect(context.enqueued_count == 1U && context.drained_count == 1U && drained_count == 1U) != 0) {
        return 1;
    }

    result = aivm_c_execute_instructions_with_syscalls(
        instructions,
        6U,
        constants,
        4U,
        host.syscall_bindings,
        host.syscall_binding_count);
    if (expect(result.ok == 1) != 0) {
        return 1;
    }
    if (expect(result.has_exit_code == 1 && result.exit_code == 9) != 0) {
        return 1;
    }

    return 0;
}
