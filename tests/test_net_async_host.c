#define AIRUN_ALLOW_INTERNAL_UI_FALLBACK 1
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

static void test_sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)(ms * 1000));
#endif
}

static void close_socket_if_valid(NativeSocket socket_fd)
{
    if (socket_fd != NATIVE_INVALID_SOCKET) {
        native_socket_close(socket_fd);
    }
}

int main(void)
{
    NativeSocket listener = NATIVE_INVALID_SOCKET;
    NativeSocket accepted = NATIVE_INVALID_SOCKET;
    struct sockaddr_in addr;
    socklen_t addr_len = (socklen_t)sizeof(addr);
    int yes = 1;
    AivmValue args[2];
    AivmValue one_arg[1];
    AivmValue result;
    AivmSyscallStatus status;
    int64_t connect_op;
    int64_t read_op;
    int64_t write_op;
    int64_t connection;
    int i;

    native_net_reset();
    CHECK(native_net_platform_init());

    listener = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener != NATIVE_INVALID_SOCKET);
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    CHECK(native_net_socket_set_nonblocking(listener));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    CHECK(bind(listener, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    CHECK(listen(listener, 4) == 0);
    CHECK(getsockname(listener, (struct sockaddr*)&addr, &addr_len) == 0);

    args[0] = aivm_value_string("localhost");
    args[1] = aivm_value_int((int64_t)ntohs(addr.sin_port));
    status = native_syscall_net_start_op("sys.net.tcp.connectStart", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    connect_op = result.int_value;
    CHECK(connect_op > 0);

    one_arg[0] = aivm_value_int(connect_op);
    connection = -1;
    for (i = 0; i < 1000; i += 1) {
        if (accepted == NATIVE_INVALID_SOCKET) {
            accepted = accept(listener, NULL, NULL);
        }
        status = native_syscall_net_async_poll("sys.net.async.poll", one_arg, 1U, &result);
        CHECK(status == AIVM_SYSCALL_OK);
        CHECK(result.type == AIVM_VAL_INT);
        if (result.int_value == 1) {
            break;
        }
        CHECK(result.int_value == 0);
        test_sleep_ms(1);
    }
    CHECK(result.int_value == 1);

    status = native_syscall_net_async_result_int("sys.net.async.resultInt", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    connection = result.int_value;
    CHECK(connection > 0);
    CHECK(accepted != NATIVE_INVALID_SOCKET);

    args[0] = aivm_value_int(connection);
    args[1] = aivm_value_int(16);
    status = native_syscall_net_start_op("sys.net.tcp.readStart", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    read_op = result.int_value;
    CHECK(read_op > 0);

    one_arg[0] = aivm_value_int(read_op);
    status = native_syscall_net_async_poll("sys.net.async.poll", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    CHECK(result.int_value == 0);

#ifdef _WIN32
    CHECK(send(accepted, "OK", 2, 0) == 2);
#else
    CHECK(send(accepted, "OK", 2U, 0) == 2);
#endif
    for (i = 0; i < 1000; i += 1) {
        status = native_syscall_net_async_poll("sys.net.async.poll", one_arg, 1U, &result);
        CHECK(status == AIVM_SYSCALL_OK);
        CHECK(result.type == AIVM_VAL_INT);
        if (result.int_value == 1) {
            break;
        }
        CHECK(result.int_value == 0);
        test_sleep_ms(1);
    }
    CHECK(result.int_value == 1);
    status = native_syscall_net_async_result_bytes("sys.net.async.resultBytes", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_BYTES);
    CHECK(result.bytes_value.length == 2U);
    CHECK(result.bytes_value.data[0] == 'O');
    CHECK(result.bytes_value.data[1] == 'K');

    args[0] = aivm_value_int(connection);
    args[1] = aivm_value_bytes((const uint8_t*)"PING", 4U);
    status = native_syscall_net_start_op("sys.net.tcp.writeStart", args, 2U, &result);
    CHECK(status == AIVM_SYSCALL_OK);
    CHECK(result.type == AIVM_VAL_INT);
    write_op = result.int_value;
    CHECK(write_op > 0);

    one_arg[0] = aivm_value_int(write_op);
    for (i = 0; i < 1000; i += 1) {
        status = native_syscall_net_async_poll("sys.net.async.poll", one_arg, 1U, &result);
        CHECK(status == AIVM_SYSCALL_OK);
        CHECK(result.type == AIVM_VAL_INT);
        if (result.int_value == 1) {
            break;
        }
        CHECK(result.int_value == 0);
        test_sleep_ms(1);
    }
    CHECK(result.int_value == 1);
    {
        uint8_t recv_buf[4];
        size_t received = 0U;
        for (i = 0; i < 1000 && received < sizeof(recv_buf); i += 1) {
            int recv_count = recv(accepted, (char*)recv_buf + received, (int)(sizeof(recv_buf) - received), 0);
            if (recv_count > 0) {
                received += (size_t)recv_count;
                continue;
            }
            if (recv_count < 0 && native_net_socket_would_block()) {
                test_sleep_ms(1);
                continue;
            }
            break;
        }
        CHECK(received == 4U);
        CHECK(memcmp(recv_buf, "PING", 4U) == 0);
    }

    one_arg[0] = aivm_value_int(connection);
    status = native_syscall_net_tcp_close("sys.net.tcp.close", one_arg, 1U, &result);
    CHECK(status == AIVM_SYSCALL_OK);

    close_socket_if_valid(accepted);
    close_socket_if_valid(listener);
    native_net_reset();
    return 0;
}
