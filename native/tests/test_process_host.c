#define AIRUN_ALLOW_INTERNAL_UI_FALLBACK 1
#define main airun_embedded_main_for_test
#include "../../../AiCLI/native/airun.c"
#undef main

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d\n", __LINE__); \
            native_process_cleanup_all(); \
            return 1; \
        } \
    } while (0)

int main(void)
{
    AivmValue start_args[4];
    AivmValue poll_args[1];
    AivmValue wait_args[1];
    AivmValue result;
    AivmSyscallStatus status;
    int handle;

    native_process_cleanup_all();

    #ifdef _WIN32
    start_args[0] = aivm_value_string("ping");
    start_args[1] = aivm_value_string("-n 2 127.0.0.1");
    #else
    start_args[0] = aivm_value_string("sleep");
    start_args[1] = aivm_value_string("1");
    #endif
    start_args[2] = aivm_value_string("");
    start_args[3] = aivm_value_string("");
    status = native_syscall_process_start("sys.process_start", start_args, 4U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    handle = (int)result.int_value;
    CHECK(handle > 0);

    poll_args[0] = aivm_value_int(handle);
    status = native_syscall_process_poll("sys.process_poll", poll_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == NATIVE_PROCESS_STATUS_PENDING);

    wait_args[0] = aivm_value_int(handle);
    status = native_syscall_process_wait("sys.process_wait", wait_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == NATIVE_PROCESS_STATUS_OK);

    status = native_syscall_process_exit_code("sys.process_exitCode", wait_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == 0);

    start_args[0] = aivm_value_string("command_that_does_not_exist__aivm");
    start_args[1] = aivm_value_string("");
    start_args[2] = aivm_value_string("");
    start_args[3] = aivm_value_string("");
    status = native_syscall_process_start("sys.process_start", start_args, 4U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == -1);

    poll_args[0] = aivm_value_int(-1);
    status = native_syscall_process_poll("sys.process_poll", poll_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == NATIVE_PROCESS_STATUS_UNKNOWN);

    wait_args[0] = aivm_value_int(-1);
    status = native_syscall_process_wait("sys.process_wait", wait_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == NATIVE_PROCESS_STATUS_UNKNOWN);

    #ifdef _WIN32
    start_args[0] = aivm_value_string("ping");
    start_args[1] = aivm_value_string("-n 10 127.0.0.1");
    #else
    start_args[0] = aivm_value_string("sleep");
    start_args[1] = aivm_value_string("10");
    #endif
    start_args[2] = aivm_value_string("");
    start_args[3] = aivm_value_string("");
    status = native_syscall_process_start("sys.process_start", start_args, 4U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    handle = (int)result.int_value;
    CHECK(handle > 0);

    wait_args[0] = aivm_value_int(handle);
    status = native_syscall_process_kill("sys.process_kill", wait_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 1);

    poll_args[0] = aivm_value_int(handle);
    status = native_syscall_process_poll("sys.process_poll", poll_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == NATIVE_PROCESS_STATUS_CANCELED);

    status = native_syscall_process_wait("sys.process_wait", wait_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == NATIVE_PROCESS_STATUS_CANCELED);

    native_process_cleanup_all();
    return 0;
}
