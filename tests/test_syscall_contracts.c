#include "aivm_syscall_contracts.h"

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

    draw_rect_args[0] = aivm_value_int(0);
    draw_rect_args[1] = aivm_value_int(0);
    draw_rect_args[2] = aivm_value_int(100);
    draw_rect_args[3] = aivm_value_int(50);
    draw_rect_args[4] = aivm_value_int(1);
    draw_rect_args[5] = aivm_value_string("#fff");

    if (expect(aivm_syscall_contract_validate("sys.ui_drawRect", draw_rect_args, 6U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_value_equals(aivm_value_string(aivm_contract_status_code(AIVM_CONTRACT_OK)), aivm_value_string("AIVMC000")) == 1) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_VOID) != 0) {
        return 1;
    }
    contract = aivm_syscall_contract_find_by_target("sys.ui_drawRect");
    if (expect(contract != NULL) != 0) {
        return 1;
    }
    if (expect(contract->id == 48U) != 0) {
        return 1;
    }

    console_write_arg[0] = aivm_value_string("hello");
    if (expect(aivm_syscall_contract_validate("sys.console_write", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(6U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.console_writeLine", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(7U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.console_writeErrLine", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(10U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.stdout_writeLine", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(16U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_VOID) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.console_readLine", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(8U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.console_readAllStdin", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(9U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process_cwd", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(11U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.process_envGet", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
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
    if (expect(aivm_syscall_contract_validate("sys.os_version", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
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
    if (expect(aivm_syscall_contract_validate("sys.process_argv", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(18U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    int_arg[0] = aivm_value_int(1);
    if (expect(aivm_syscall_contract_validate("sys.time_nowUnixMs", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(13U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.time_monotonicMs", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(14U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.time_sleepMs", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(15U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.proc_exit", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(17U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_VOID) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs_readFile", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(19U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs_fileExists", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(20U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BOOL) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs_readDir", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(21U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs_stat", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(22U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs_pathExists", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(23U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_BOOL) != 0) {
        return 1;
    }
    fs_write_args[0] = aivm_value_string("p");
    fs_write_args[1] = aivm_value_string("v");
    if (expect(aivm_syscall_contract_validate("sys.fs_writeFile", fs_write_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(24U, fs_write_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.fs_makeDir", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(25U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto_base64Encode", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(37U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto_base64Decode", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(38U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto_sha1", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(39U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto_sha256", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(40U, console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    crypto_hmac_args[0] = aivm_value_string("k");
    crypto_hmac_args[1] = aivm_value_string("m");
    if (expect(aivm_syscall_contract_validate("sys.crypto_hmacSha256", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(41U, crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.crypto_randomBytes", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(42U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_STRING) != 0) {
        return 1;
    }
    net_string_int_args[0] = aivm_value_string("127.0.0.1");
    net_string_int_args[1] = aivm_value_int(8080);
    net_int_string_args[0] = aivm_value_int(1);
    net_int_string_args[1] = aivm_value_string("data");
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
    net_udp_send_args[3] = aivm_value_string("msg");
    if (expect(aivm_syscall_contract_validate("sys.net_listen", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(0U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_listen_tls", net_listen_tls_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(1U, net_listen_tls_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_accept", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_readHeaders", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_write", net_int_string_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_close", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpConnect", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(27U, net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpListen", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpListenTls", net_tcp_listen_tls_args, 4U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpAccept", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpRead", net_int_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpWrite", net_int_string_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpConnectTls", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpConnectStart", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpConnectTlsStart", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpReadStart", net_int_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_tcpWriteStart", net_int_string_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_asyncPoll", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_asyncAwait", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_asyncCancel", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_asyncResultInt", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_asyncResultString", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_asyncError", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_udpBind", net_string_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_udpRecv", net_int_int_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.net_udpSend", net_udp_send_args, 4U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker_start", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(73U, crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker_poll", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(74U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker_result", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(75U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker_error", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(76U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.worker_cancel", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(77U, int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_emit", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(78U, crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_mode", NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(79U, NULL, 0U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    str_args[0] = aivm_value_int(1);
    str_args[1] = aivm_value_int(2);
    str_args[2] = aivm_value_int(3);
    if (expect(aivm_syscall_contract_validate("sys.debug_captureFrameBegin", str_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(80U, str_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_captureFrameEnd", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_captureDraw", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_captureInput", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_captureState", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_replayLoad", console_write_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_replayNext", int_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    bool_string_string_args[0] = aivm_value_bool(1);
    bool_string_string_args[1] = aivm_value_string("m");
    bool_string_string_args[2] = aivm_value_string("n");
    int_string_string_args[0] = aivm_value_int(1);
    int_string_string_args[1] = aivm_value_string("m");
    int_string_string_args[2] = aivm_value_string("n");
    if (expect(aivm_syscall_contract_validate("sys.debug_assert", bool_string_string_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_artifactWrite", crypto_hmac_args, 2U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.debug_traceAsync", int_string_string_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }

    ui_window_args[0] = aivm_value_string("Hello");
    ui_window_args[1] = aivm_value_int(800);
    ui_window_args[2] = aivm_value_int(600);
    if (expect(aivm_syscall_contract_validate("sys.ui_createWindow", ui_window_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(46U, ui_window_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_INT) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui_createWindow", ui_window_args, 2U, &return_type) == AIVM_CONTRACT_ERR_ARG_COUNT) != 0) {
        return 1;
    }

    ui_window_id_arg[0] = aivm_value_int(123);
    if (expect(aivm_syscall_contract_validate("sys.ui_beginFrame", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(47U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui_waitFrame", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(72U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui_endFrame", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(50U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui_present", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(52U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui_closeWindow", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(53U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }

    if (expect(aivm_syscall_contract_validate("sys.ui_pollEvent", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(return_type == AIVM_VAL_NODE) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(51U, ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate("sys.ui_getWindowSize", ui_window_id_arg, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
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

    if (expect(aivm_syscall_contract_validate("sys.ui_drawText", draw_text_args, 6U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }

    if (expect(aivm_syscall_contract_validate("sys.ui_drawText", draw_rect_args, 6U, &return_type) == AIVM_CONTRACT_ERR_ARG_TYPE) != 0) {
        return 1;
    }

    if (expect(aivm_syscall_contract_validate("sys.ui_drawText", draw_rect_args, 4U, &return_type) == AIVM_CONTRACT_ERR_ARG_COUNT) != 0) {
        return 1;
    }

    if (expect(aivm_syscall_contract_validate("sys.ui_drawText", NULL, 6U, &return_type) == AIVM_CONTRACT_ERR_ARG_TYPE) != 0) {
        return 1;
    }

    draw_text_args[3] = aivm_value_int(1);
    if (expect(aivm_syscall_contract_validate("sys.ui_drawText", draw_text_args, 6U, &return_type) == AIVM_CONTRACT_ERR_ARG_TYPE) != 0) {
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
    if (expect(aivm_syscall_contract_validate("sys.ui_drawLine", draw_line_args, 7U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }
    if (expect(aivm_syscall_contract_validate_id(54U, draw_line_args, 7U, &return_type) == AIVM_CONTRACT_OK) != 0) {
        return 1;
    }

    utf8_count_args[0] = aivm_value_string("abc");
    if (expect(aivm_syscall_contract_validate("sys.str_utf8ByteCount", utf8_count_args, 1U, &return_type) == AIVM_CONTRACT_OK) != 0) {
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
    if (expect(aivm_syscall_contract_validate("sys.str_substring", str_args, 3U, &return_type) == AIVM_CONTRACT_OK) != 0) {
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
    str_args[1] = aivm_value_string("bad");
    if (expect(aivm_syscall_contract_validate("sys.str_remove", str_args, 3U, &return_type) == AIVM_CONTRACT_ERR_ARG_TYPE) != 0) {
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
    if (expect(aivm_value_equals(aivm_value_string(contract->target), aivm_value_string("sys.ui_getWindowSize")) == 1) != 0) {
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
