#define AIRUN_ALLOW_INTERNAL_UI_FALLBACK 1
#define main airun_embedded_main_for_test
#include "../ailang_cli/ailang.c"
#undef main

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL line %d\n", __LINE__); \
            return 1; \
        } \
    } while (0)

int main(void)
{
    const char* png_base64 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIW2P4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==";
    AivmValue one_arg[1];
    AivmValue two_args[2];
    AivmValue result;
    AivmSyscallStatus status;
    int64_t worker_ok = -1;
    int64_t worker_fail = -1;
    int64_t worker_sleep = -1;

    native_worker_reset_all();

    {
        size_t png_len = 0U;
        size_t rgba_len = 0U;
        CHECK(native_bytes_from_base64(png_base64, NULL, 0U, &png_len) == 1);
        CHECK(png_len > 0U && png_len < NATIVE_BYTES_SCRATCH_CAPACITY);
        CHECK(native_bytes_from_base64(png_base64, g_native_bytes_scratch, png_len, &png_len) == 1);
        two_args[0] = aivm_value_bytes(g_native_bytes_scratch, png_len);
        two_args[1] = aivm_value_string("image/png");
        status = native_syscall_image_decode_to_rgba_base64("sys.image.decodeToRgbaBase64", two_args, 2U, &result);
#if defined(__APPLE__) || defined(_WIN32)
        CHECK(status == AIVM_SYSCALL_OK);
        CHECK(result.type == AIVM_VAL_STRING);
        CHECK(result.string_value != NULL && strlen(result.string_value) > 0U);
        CHECK(native_bytes_from_base64(result.string_value, NULL, 0U, &rgba_len) == 1);
        CHECK(rgba_len == 4U);
#else
        CHECK(status == AIVM_SYSCALL_ERR_INVALID);
#endif
    }

    two_args[0] = aivm_value_string("echo");
    two_args[1] = aivm_value_string("worker-ok");
    status = native_syscall_worker_start("sys.worker.start", two_args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value > 0);
    worker_ok = result.int_value;

    two_args[0] = aivm_value_string("fail");
    two_args[1] = aivm_value_string("worker-fail");
    status = native_syscall_worker_start("sys.worker.start", two_args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value > 0);
    worker_fail = result.int_value;

    two_args[0] = aivm_value_string("sleep");
    two_args[1] = aivm_value_string("2");
    status = native_syscall_worker_start("sys.worker.start", two_args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value > 0);
    worker_sleep = result.int_value;

    one_arg[0] = aivm_value_int(worker_ok);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 0);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 1);
    status = native_syscall_worker_result("sys.worker.result", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "worker-ok") == 0);
    CHECK(g_native_workers[(size_t)(worker_ok - 1)].heap != NULL);
    CHECK(g_native_workers[(size_t)(worker_ok - 1)].heap->task_name != NULL);
    CHECK(strcmp(g_native_workers[(size_t)(worker_ok - 1)].heap->task_name, "echo") == 0);
    CHECK(g_native_workers[(size_t)(worker_ok - 1)].heap->payload != NULL);
    CHECK(strcmp(g_native_workers[(size_t)(worker_ok - 1)].heap->payload, "worker-ok") == 0);
    CHECK(g_native_workers[(size_t)(worker_ok - 1)].heap->result != NULL);
    CHECK(g_native_workers[(size_t)(worker_ok - 1)].heap->error == NULL);
    CHECK(g_native_workers[(size_t)(worker_ok - 1)].heap->allocated_bytes >= strlen("echo") + strlen("worker-ok") + strlen("worker-ok") + 3U);
    status = native_syscall_worker_error("sys.worker.error", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "") == 0);

    one_arg[0] = aivm_value_int(worker_fail);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 0);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == -1);
    status = native_syscall_worker_result("sys.worker.result", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "") == 0);
    status = native_syscall_worker_error("sys.worker.error", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "worker-fail") == 0);
    CHECK(g_native_workers[(size_t)(worker_fail - 1)].heap != NULL);
    CHECK(g_native_workers[(size_t)(worker_fail - 1)].heap->result == NULL);
    CHECK(g_native_workers[(size_t)(worker_fail - 1)].heap->error != NULL);
    CHECK(strcmp(g_native_workers[(size_t)(worker_fail - 1)].heap->error, "worker-fail") == 0);

    one_arg[0] = aivm_value_int(worker_sleep);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == 0);
    status = native_syscall_worker_cancel("sys.worker.cancel", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 1);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == -2);
    status = native_syscall_worker_error("sys.worker.error", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "canceled") == 0);
    CHECK(g_native_workers[(size_t)(worker_sleep - 1)].heap != NULL);
    CHECK(g_native_workers[(size_t)(worker_sleep - 1)].heap->result == NULL);
    CHECK(g_native_workers[(size_t)(worker_sleep - 1)].heap->error != NULL);
    CHECK(strcmp(g_native_workers[(size_t)(worker_sleep - 1)].heap->task_name, "cancel") == 0);
    CHECK(strcmp(g_native_workers[(size_t)(worker_sleep - 1)].heap->error, "canceled") == 0);
    status = native_syscall_worker_cancel("sys.worker.cancel", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 0);

    one_arg[0] = aivm_value_int(99999);
    status = native_syscall_worker_poll("sys.worker.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT && result.int_value == -3);
    status = native_syscall_worker_result("sys.worker.result", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "") == 0);
    status = native_syscall_worker_error("sys.worker.error", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_STRING && strcmp(result.string_value, "unknown_worker") == 0);
    status = native_syscall_worker_cancel("sys.worker.cancel", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 0);

    native_worker_reset_all();
    CHECK(native_worker_active_count() == 0U);
    CHECK(g_native_workers[(size_t)(worker_ok - 1)].heap == NULL);
    CHECK(g_native_workers[(size_t)(worker_fail - 1)].heap == NULL);
    CHECK(g_native_workers[(size_t)(worker_sleep - 1)].heap == NULL);

    return 0;
}
