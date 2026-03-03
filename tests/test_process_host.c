#include <string.h>

#define main airun_embedded_main_for_test
#include "../../../AiCLI/native/airun.c"
#undef main

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmValue start_args[4];
    AivmValue poll_args[1];
    AivmValue wait_args[1];
    AivmValue result;
    AivmSyscallStatus status;
    int handle;

    native_process_cleanup_all();

    start_args[0] = aivm_value_string("sleep");
    start_args[1] = aivm_value_string("1 || ping -n 2 127.0.0.1 >NUL");
    start_args[2] = aivm_value_string("");
    start_args[3] = aivm_value_string("");
    status = native_syscall_process_start("sys.process_start", start_args, 4U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    handle = (int)result.int_value;
    if (expect(handle > 0) != 0) {
        return 1;
    }

    poll_args[0] = aivm_value_int(handle);
    status = native_syscall_process_poll("sys.process_poll", poll_args, 1U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(result.int_value == NATIVE_PROCESS_STATUS_PENDING) != 0) {
        return 1;
    }

    wait_args[0] = aivm_value_int(handle);
    status = native_syscall_process_wait("sys.process_wait", wait_args, 1U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(result.int_value == NATIVE_PROCESS_STATUS_OK) != 0) {
        return 1;
    }

    status = native_syscall_process_exit_code("sys.process_exitCode", wait_args, 1U, &result);
    if (expect(status == AIVM_SYSCALL_OK) != 0) {
        return 1;
    }
    if (expect(result.type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(result.int_value == 0) != 0) {
        return 1;
    }

    native_process_cleanup_all();
    return 0;
}
