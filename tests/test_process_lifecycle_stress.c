#define AIRUN_ALLOW_INTERNAL_UI_FALLBACK 1
#define main airun_embedded_main_for_test
#include "../../../AiCLI/native/airun.c"
#undef main

#include <stdio.h>
#include <string.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d\n", __LINE__); \
            return 1; \
        } \
    } while (0)

static int spawn_and_wait_zero_exit(void)
{
    AivmValue spawn_args[4];
    AivmValue wait_args[1];
    AivmValue poll_args[1];
    AivmValue result;
    AivmSyscallStatus status;
    int64_t handle;

#ifdef _WIN32
    spawn_args[0] = aivm_value_string("cmd /c exit 0");
#else
    spawn_args[0] = aivm_value_string("true");
#endif
    spawn_args[1] = aivm_value_node(0);
    spawn_args[2] = aivm_value_string("");
    spawn_args[3] = aivm_value_node(0);

    status = native_syscall_process_spawn("sys.process.spawn", spawn_args, 4U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value > 0);
    handle = result.int_value;

    wait_args[0] = aivm_value_int(handle);
    status = native_syscall_process_wait("sys.process.wait", wait_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == 0);

    poll_args[0] = aivm_value_int(handle);
    status = native_syscall_process_poll("sys.process.poll", poll_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == -1);

    return 0;
}

static int spawn_kill_wait_nonzero_exit(void)
{
    AivmValue spawn_args[4];
    AivmValue poll_args[1];
    AivmValue kill_args[1];
    AivmValue result;
    AivmSyscallStatus status;
    int64_t handle;

#ifdef _WIN32
    spawn_args[0] = aivm_value_string("cmd /c ping -n 4 127.0.0.1 >NUL");
#else
    spawn_args[0] = aivm_value_string("sleep 2");
#endif
    spawn_args[1] = aivm_value_node(0);
    spawn_args[2] = aivm_value_string("");
    spawn_args[3] = aivm_value_node(0);

    status = native_syscall_process_spawn("sys.process.spawn", spawn_args, 4U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value > 0);
    handle = result.int_value;

    poll_args[0] = aivm_value_int(handle);
    status = native_syscall_process_poll("sys.process.poll", poll_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);

    kill_args[0] = aivm_value_int(handle);
    status = native_syscall_process_kill("sys.process.kill", kill_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL);
    CHECK(result.bool_value == 1 || result.bool_value == 0);

    status = native_syscall_process_wait("sys.process.wait", kill_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value != 0);

    poll_args[0] = aivm_value_int(handle);
    status = native_syscall_process_poll("sys.process.poll", poll_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == -1);

    return 0;
}

static int spawn_and_wait_nonzero_exit(void)
{
    AivmValue spawn_args[4];
    AivmValue wait_args[1];
    AivmValue poll_args[1];
    AivmValue result;
    AivmSyscallStatus status;
    int64_t handle;

#ifdef _WIN32
    spawn_args[0] = aivm_value_string("cmd /c exit 7");
#else
    spawn_args[0] = aivm_value_string("false");
#endif
    spawn_args[1] = aivm_value_node(0);
    spawn_args[2] = aivm_value_string("");
    spawn_args[3] = aivm_value_node(0);

    status = native_syscall_process_spawn("sys.process.spawn", spawn_args, 4U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value > 0);
    handle = result.int_value;

    wait_args[0] = aivm_value_int(handle);
    status = native_syscall_process_wait("sys.process.wait", wait_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value != 0);

    poll_args[0] = aivm_value_int(handle);
    status = native_syscall_process_poll("sys.process.poll", poll_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == -1);

    return 0;
}

static int interleaved_fail_and_cancel_cleanup_stress(void)
{
    int i;
    int interleave_iterations = (int)(NATIVE_PROCESS_CAPACITY / 4);
    if (interleave_iterations < 8) {
        interleave_iterations = 8;
    }
    if (interleave_iterations > 32) {
        interleave_iterations = 32;
    }

    for (i = 0; i < interleave_iterations; i += 1) {
        if (spawn_and_wait_nonzero_exit() != 0) {
            return 1;
        }
        if (spawn_kill_wait_nonzero_exit() != 0) {
            return 1;
        }
    }

    return 0;
}

#ifdef _WIN32
static int bytes_contains(const uint8_t* haystack, size_t haystack_len, const char* needle)
{
    size_t needle_len;
    size_t i;
    if (haystack == NULL || needle == NULL) {
        return 0;
    }
    needle_len = strlen(needle);
    if (needle_len == 0U) {
        return 1;
    }
    if (haystack_len < needle_len) {
        return 0;
    }
    for (i = 0U; i + needle_len <= haystack_len; i += 1U) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}
#endif

static int spawn_uses_args_node_contract(void)
{
    AivmProgram program;
    AivmVm vm;
    const char* argv_values[2];
    AivmValue spawn_args[4];
    AivmValue read_args[1];
    AivmValue wait_args[1];
    AivmValue result;
    AivmSyscallStatus status;
    int64_t handle;

    aivm_program_clear(&program);
    argv_values[0] = "hello";
    argv_values[1] = "world";
    aivm_init_with_syscalls_and_argv(&vm, &program, NULL, 0U, argv_values, 2U);
    g_native_active_vm = &vm;

#ifdef _WIN32
    spawn_args[0] = aivm_value_string("cmd.exe");
#else
    spawn_args[0] = aivm_value_string("/bin/echo");
#endif
    spawn_args[1] = aivm_value_node(vm.process_argv_node_handle);
    spawn_args[2] = aivm_value_string("");
    spawn_args[3] = aivm_value_node(0);

    status = native_syscall_process_spawn("sys.process.spawn", spawn_args, 4U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value > 0);
    handle = result.int_value;

    wait_args[0] = aivm_value_int(handle);
    status = native_syscall_process_wait("sys.process.wait", wait_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == 0);

    read_args[0] = aivm_value_int(handle);
    status = native_syscall_process_stdout_read("sys.process.stdout.read", read_args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BYTES);
    CHECK(result.bytes_value.data != NULL);
#ifdef _WIN32
    CHECK(result.bytes_value.length >= 11U);
    CHECK(bytes_contains(result.bytes_value.data, result.bytes_value.length, "hello world"));
#else
    CHECK(result.bytes_value.length == 12U);
    CHECK(memcmp(result.bytes_value.data, "hello world\n", 12U) == 0);
#endif
    g_native_active_vm = NULL;
    return 0;
}

int main(void)
{
    int i;
    for (i = 0; i < ((int)NATIVE_PROCESS_CAPACITY * 2); i += 1) {
        if (spawn_and_wait_zero_exit() != 0) {
            return 1;
        }
    }

    for (i = 0; i < ((int)NATIVE_PROCESS_CAPACITY * 2); i += 1) {
        if (spawn_and_wait_nonzero_exit() != 0) {
            return 1;
        }
    }

    if (spawn_kill_wait_nonzero_exit() != 0) {
        return 1;
    }
    if (interleaved_fail_and_cancel_cleanup_stress() != 0) {
        return 1;
    }
    if (spawn_uses_args_node_contract() != 0) {
        return 1;
    }

    return 0;
}
