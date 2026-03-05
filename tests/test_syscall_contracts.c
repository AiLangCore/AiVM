#include "sys/aivm_syscall_contracts.h"

static int expect(int condition)
{
    return condition ? 0 : 1;
}

int main(void)
{
    AivmValueType return_type;
    const AivmSyscallContract* contract;
    AivmValue draw_rect_args[6];
    AivmValue draw_text_args[6];
    AivmValue draw_line_args[7];
    AivmValue ui_window_args[3];
    AivmValue ui_window_id_arg[1];
    AivmValue console_write_arg[1];
    AivmValue int_arg[1];
    AivmValue fs_write_args[2];
    AivmValue fs_dir_delete_args[2];
    AivmValue crypto_hmac_args[2];
    AivmValue net_int_string_args[2];
    AivmValue net_string_int_args[2];
    AivmValue net_int_int_args[2];
    AivmValue net_listen_tls_args[3];
    AivmValue net_tcp_listen_tls_args[4];
    AivmValue net_udp_send_args[4];
    AivmValue bool_string_string_args[3];
    AivmValue int_string_string_args[3];
    AivmValue str_args[3];
    AivmValue utf8_count_args[1];
    AivmValue bytes_arg[1];
    AivmValue bytes_int_args[2];
    AivmValue bytes_range_args[3];
    AivmValue two_bytes_args[2];
    AivmValue str_pair_args[2];
    AivmValue process_spawn_args[4];
    const uint8_t raw_bytes[3] = { 0x01U, 0x02U, 0x03U };

    draw_rect_args[0] = aivm_value_int(0);
    draw_rect_args[1] = aivm_value_int(0);
    draw_rect_args[2] = aivm_value_int(100);
    draw_rect_args[3] = aivm_value_int(50);
    draw_rect_args[4] = aivm_value_int(1);
    draw_rect_args[5] = aivm_value_string("#fff");

    if (expect(aivm_syscall_contract_validate("sys.ui.drawRect", draw_rect_args, 6U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_string(aivm_contract_status_code(AIVM_CONTRACT_OK)), aivm_value_string("AIVMC000")) == 1) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_VOID) != 0) {
        return 1;
    }
    contract = aivm_syscall_contract_find_by_target("sys.ui.drawRect");
    if (expect(contract != NULL) != 0) {
        return 1;
    }
    if (expect(contract->id == 48U) != 0) {
        return 1;
    }

    console_write_arg[0] = aivm_value_string("hello");
    if (expect(aivm_syscall_contract_validate("sys.console.write", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(6U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.console.writeLine", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(7U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.console.writeErrLine", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(10U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.stdout.writeLine", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(16U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_VOID) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.console.readLine", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(8U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.console.readAllStdin", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(9U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process.cwd", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(11U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process.env.get", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(12U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.platform", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(28U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.arch", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(29U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.os.version", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(30U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.runtime", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(31U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process.args", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(18U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    process_spawn_args[0] = aivm_value_string("echo hi");
    process_spawn_args[1] = aivm_value_node(1);
    process_spawn_args[2] = aivm_value_string(".");
    process_spawn_args[3] = aivm_value_node(1);
    int_arg[0] = aivm_value_int(1);
    if (expect(aivm_syscall_contract_validate("sys.process.spawn", process_spawn_args, 4U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(105U, process_spawn_args, 4U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process.wait", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(106U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process.kill", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(107U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BOOL) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process.stdout.read", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(108U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process.stderr.read", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(109U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process.poll", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(110U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    int_arg[0] = aivm_value_int(1);
    if (expect(aivm_syscall_contract_validate("sys.time.nowUnixMs", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(13U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.time.monotonicMs", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(14U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.time.sleepMs", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(15U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process.exit", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(17U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_VOID) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs.file.read", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(19U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs.file.exists", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(20U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BOOL) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs.dir.list", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(21U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs.path.stat", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(22U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs.path.exists", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(23U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BOOL) != 0) {
        return 1;
    }
    fs_write_args[0] = aivm_value_string("p");
    fs_write_args[1] = aivm_value_bytes(raw_bytes, sizeof(raw_bytes));
    if (expect(aivm_syscall_contract_validate("sys.fs.file.write", fs_write_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(24U, fs_write_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs.dir.create", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(25U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs.file.delete", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(103U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BOOL) != 0) {
        return 1;
    }
    fs_dir_delete_args[0] = aivm_value_string("dir");
    fs_dir_delete_args[1] = aivm_value_bool(1);
    if (expect(aivm_syscall_contract_validate("sys.fs.dir.delete", fs_dir_delete_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(104U, fs_dir_delete_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BOOL) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto.base64Encode", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(37U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto.base64Decode", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(38U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto.sha1", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(39U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto.sha256", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(40U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    crypto_hmac_args[0] = aivm_value_string("k");
    crypto_hmac_args[1] = aivm_value_string("m");
    if (expect(aivm_syscall_contract_validate("sys.crypto.hmacSha256", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(41U, crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto.randomBytes", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(42U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    net_string_int_args[0] = aivm_value_string("127.0.0.1");
    net_string_int_args[1] = aivm_value_int(8080);
    net_int_string_args[0] = aivm_value_int(1);
    net_int_string_args[1] = aivm_value_bytes(raw_bytes, sizeof(raw_bytes));
    net_int_int_args[0] = aivm_value_int(1);
    net_int_int_args[1] = aivm_value_int(1024);
    net_listen_tls_args[0] = aivm_value_int(443);
    net_listen_tls_args[1] = aivm_value_string("cert");
    net_listen_tls_args[2] = aivm_value_string("key");
    net_tcp_listen_tls_args[0] = aivm_value_string("0.0.0.0");
    net_tcp_listen_tls_args[1] = aivm_value_int(443);
    net_tcp_listen_tls_args[2] = aivm_value_string("cert");
    net_tcp_listen_tls_args[3] = aivm_value_string("key");
    net_udp_send_args[0] = aivm_value_int(1);
    net_udp_send_args[1] = aivm_value_string("127.0.0.1");
    net_udp_send_args[2] = aivm_value_int(8080);
    net_udp_send_args[3] = aivm_value_bytes(raw_bytes, sizeof(raw_bytes));
    if (expect(aivm_syscall_contract_validate("sys.net.listen", int_arg, 1U, &return_type) == AIVM_CONTRACT_ERR_UNKNOWN_TARGET) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(0U, int_arg, 1U, &return_type) == AIVM_CONTRACT_ERR_UNKNOWN_ID) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.listen.tls", net_listen_tls_args, 3U, &return_type) == AIVM_CONTRACT_ERR_UNKNOWN_TARGET) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(1U, net_listen_tls_args, 3U, &return_type) == AIVM_CONTRACT_ERR_UNKNOWN_ID) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.accept", int_arg, 1U, &return_type) == AIVM_CONTRACT_ERR_UNKNOWN_TARGET) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.write", net_int_string_args, 2U, &return_type) == AIVM_CONTRACT_ERR_UNKNOWN_TARGET) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.close", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.connect", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(27U, net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.listen", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.listenTls", net_tcp_listen_tls_args, 4U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.accept", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.read", net_int_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.write", net_int_string_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.connectTls", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.connectStart", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.connectTlsStart", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.readStart", net_int_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.tcp.writeStart", net_int_string_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.async.poll", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.async.await", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.async.cancel", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.async.resultInt", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.async.resultBytes", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.async.error", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.udp.bind", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.udp.recv", net_int_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net.udp.send", net_udp_send_args, 4U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker.start", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(73U, crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker.poll", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(74U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker.result", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(75U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker.error", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(76U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker.cancel", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(77U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.emit", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(78U, crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.mode", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(79U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    str_args[0] = aivm_value_int(1);
    str_args[1] = aivm_value_int(2);
    str_args[2] = aivm_value_int(3);
    if (expect(aivm_syscall_contract_validate("sys.debug.captureFrameBegin", str_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(80U, str_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.captureFrameEnd", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.captureDraw", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.captureInput", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.captureState", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.replayLoad", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.replayNext", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    bool_string_string_args[0] = aivm_value_bool(1);
    bool_string_string_args[1] = aivm_value_string("m");
    bool_string_string_args[2] = aivm_value_string("n");
    int_string_string_args[0] = aivm_value_int(1);
    int_string_string_args[1] = aivm_value_string("m");
    int_string_string_args[2] = aivm_value_string("n");
    if (expect(aivm_syscall_contract_validate("sys.debug.assert", bool_string_string_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.artifactWrite", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.traceAsync", int_string_string_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug.taskReclaimStats", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(115U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_NODE) != 0) {
        return 1;
    }

    ui_window_args[0] = aivm_value_string("Hello");
    ui_window_args[1] = aivm_value_int(800);
    ui_window_args[2] = aivm_value_int(600);
    if (expect(aivm_syscall_contract_validate("sys.ui.createWindow", ui_window_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(46U, ui_window_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui.createWindow", ui_window_args, 2U, &return_type) == AIVM_CONTRACT_ERR_ARG_COUNT) != 0) {
        return 1;
    }

    ui_window_id_arg[0] = aivm_value_int(123);
    if (expect(aivm_syscall_contract_validate("sys.ui.beginFrame", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(47U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui.waitFrame", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(72U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui.endFrame", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(50U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui.present", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(52U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui.closeWindow", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(53U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }

    if (expect(aivm_syscall_contract_validate("sys.ui.pollEvent", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(51U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui.getWindowSize", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(58U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }

    draw_text_args[0] = aivm_value_int(10);
    draw_text_args[1] = aivm_value_int(20);
    draw_text_args[2] = aivm_value_int(0);
    draw_text_args[3] = aivm_value_string("hello");
    draw_text_args[4] = aivm_value_string("#000");
    draw_text_args[5] = aivm_value_int(14);

    if (expect(aivm_syscall_contract_validate("sys.ui.drawText", draw_text_args, 6U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }

    if (expect(aivm_syscall_contract_validate("sys.ui.drawText", draw_rect_args, 6U, &return_type) == AIVM_CONTRACT_ERR_ARG_TYPE) != 0) {
        return 1;
    }

    if (expect(aivm_syscall_contract_validate("sys.ui.drawText", draw_rect_args, 4U, &return_type) == AIVM_CONTRACT_ERR_ARG_COUNT) != 0) {
        return 1;
    }

    if (expect(aivm_syscall_contract_validate("sys.ui.drawText", NULL, 6U, &return_type) == AIVM_CONTRACT_ERR_ARG_TYPE) != 0) {
        return 1;
    }

    draw_text_args[3] = aivm_value_int(1);
    if (expect(aivm_syscall_contract_validate("sys.ui.drawText", draw_text_args, 6U, &return_type) == AIVM_CONTRACT_ERR_ARG_TYPE) != 0) {
        return 1;
    }
    draw_text_args[3] = aivm_value_string("hello");

    if (expect(aivm_syscall_contract_validate("sys.unknown", draw_text_args, 6U, &return_type) == AIVM_CONTRACT_ERR_UNKNOWN_TARGET) != 0) {
        return 1;
    }

    draw_line_args[0] = aivm_value_int(0);
    draw_line_args[1] = aivm_value_int(0);
    draw_line_args[2] = aivm_value_int(20);
    draw_line_args[3] = aivm_value_int(20);
    draw_line_args[4] = aivm_value_int(2);
    draw_line_args[5] = aivm_value_string("#f00");
    draw_line_args[6] = aivm_value_int(255);
    if (expect(aivm_syscall_contract_validate("sys.ui.drawLine", draw_line_args, 7U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(54U, draw_line_args, 7U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }

    utf8_count_args[0] = aivm_value_string("abc");
    if (expect(aivm_syscall_contract_validate("sys.str.utf8ByteCount", utf8_count_args, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(26U, utf8_count_args, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }

    str_args[0] = aivm_value_string("hello");
    str_args[1] = aivm_value_int(1);
    str_args[2] = aivm_value_int(3);
    if (expect(aivm_syscall_contract_validate("sys.str.substring", str_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(59U, str_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(60U, str_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    int_arg[0] = aivm_value_int(0x263A);
    if (expect(aivm_syscall_contract_validate("sys.str.fromCodePoint", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(111U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    console_write_arg[0] = aivm_value_string("263A");
    if (expect(aivm_syscall_contract_validate("sys.str.decodeUnicodeHex4", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(112U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    str_pair_args[0] = aivm_value_string("D83D");
    str_pair_args[1] = aivm_value_string("DE42");
    if (expect(aivm_syscall_contract_validate("sys.str.decodeUnicodeSurrogatePairHex4", str_pair_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(113U, str_pair_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    bytes_arg[0] = aivm_value_bytes(raw_bytes, sizeof(raw_bytes));
    if (expect(aivm_syscall_contract_validate("sys.bytes.length", bytes_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(97U, bytes_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.bytes.toBase64", bytes_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.bytes.toUtf8String", bytes_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(114U, bytes_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.bytes.fromBase64", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    bytes_int_args[0] = bytes_arg[0];
    bytes_int_args[1] = aivm_value_int(1);
    if (expect(aivm_syscall_contract_validate("sys.bytes.at", bytes_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(100U, bytes_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    bytes_range_args[0] = bytes_arg[0];
    bytes_range_args[1] = aivm_value_int(0);
    bytes_range_args[2] = aivm_value_int(2);
    if (expect(aivm_syscall_contract_validate("sys.bytes.slice", bytes_range_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(101U, bytes_range_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    two_bytes_args[0] = bytes_arg[0];
    two_bytes_args[1] = bytes_arg[0];
    if (expect(aivm_syscall_contract_validate("sys.bytes.concat", two_bytes_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BYTES) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(102U, two_bytes_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    str_args[1] = aivm_value_string("bad");
    if (expect(aivm_syscall_contract_validate("sys.str.remove", str_args, 3U, &return_type) == AIVM_CONTRACT_ERR_ARG_TYPE) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(99U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_ERR_ARG_TYPE) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(100U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_ERR_ARG_COUNT) != 0) {
        return 1;
    }

    if (expect(aivm_syscall_contract_validate_id(49U, draw_text_args, 3U, &return_type) == AIVM_CONTRACT_ERR_ARG_COUNT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(49U, draw_text_args, 6U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    contract = aivm_syscall_contract_find_by_id(58U);
    if (expect(contract != NULL) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_string(contract->target), aivm_value_string("sys.ui.getWindowSize")) == 1) != 0) {
        return 1;
    }
    if (expect(contract->arg_count == 1U) != 0) {
        return 1;
    }
    if (expect(contract->return_type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(9999U, draw_text_args, 3U, &return_type) == AIVM_CONTRACT_ERR_UNKNOWN_ID) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(
            aivm_value_string(aivm_contract_status_message(AIVM_CONTRACT_ERR_UNKNOWN_ID)),
            aivm_value_string("Syscall contract ID was not found.")) == 1) != 0) {
        return 1;
    }

    return 0;
}
