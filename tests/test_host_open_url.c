#define AIRUN_ALLOW_INTERNAL_UI_FALLBACK 1
#define AIRUN_TEST_FAKE_OPEN_URL 1
#define main airun_embedded_main_for_test
#include "../../../AiCLI/native/airun.c"
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
    AivmValue args[1];
    AivmValue result;
    AivmSyscallStatus status;

    args[0] = aivm_value_string("https://example.com/maps?q=test");
    status = native_syscall_host_open_url("sys.host.openUrl", args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 1);
    CHECK(strcmp(g_native_open_url_test_scratch, "https://example.com/maps?q=test") == 0);

    args[0] = aivm_value_string("ftp://example.com/");
    status = native_syscall_host_open_url("sys.host.openUrl", args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 0);

    args[0] = aivm_value_string("file:///tmp/nope");
    status = native_syscall_host_open_url("sys.host.openUrl", args, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BOOL && result.bool_value == 0);

    return 0;
}
