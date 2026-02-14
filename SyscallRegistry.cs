using System.Collections.Frozen;

namespace AiVM.Core;

public static class SyscallRegistry
{
    private static readonly FrozenDictionary<string, SyscallId> NameToId = new Dictionary<string, SyscallId>(StringComparer.Ordinal)
    {
        ["sys.net_listen"] = SyscallId.NetListen,
        ["sys.net_listen_tls"] = SyscallId.NetListenTls,
        ["sys.net_accept"] = SyscallId.NetAccept,
        ["sys.net_readHeaders"] = SyscallId.NetReadHeaders,
        ["sys.net_write"] = SyscallId.NetWrite,
        ["sys.net_close"] = SyscallId.NetClose,
        ["sys.net_tcpListen"] = SyscallId.NetTcpListen,
        ["sys.net_tcpListenTls"] = SyscallId.NetTcpListenTls,
        ["sys.net_tcpAccept"] = SyscallId.NetTcpAccept,
        ["sys.net_tcpRead"] = SyscallId.NetTcpRead,
        ["sys.net_tcpWrite"] = SyscallId.NetTcpWrite,
        ["sys.crypto_base64Encode"] = SyscallId.CryptoBase64Encode,
        ["sys.crypto_base64Decode"] = SyscallId.CryptoBase64Decode,
        ["sys.crypto_sha1"] = SyscallId.CryptoSha1,
        ["sys.crypto_sha256"] = SyscallId.CryptoSha256,
        ["sys.crypto_hmacSha256"] = SyscallId.CryptoHmacSha256,
        ["sys.console_write"] = SyscallId.ConsoleWrite,
        ["sys.console_writeLine"] = SyscallId.ConsoleWriteLine,
        ["sys.console_readLine"] = SyscallId.ConsoleReadLine,
        ["sys.console_readAllStdin"] = SyscallId.ConsoleReadAllStdin,
        ["sys.console_writeErrLine"] = SyscallId.ConsoleWriteErrLine,
        ["sys.process_cwd"] = SyscallId.ProcessCwd,
        ["sys.process_envGet"] = SyscallId.ProcessEnvGet,
        ["sys.time_nowUnixMs"] = SyscallId.TimeNowUnixMs,
        ["sys.time_monotonicMs"] = SyscallId.TimeMonotonicMs,
        ["sys.time_sleepMs"] = SyscallId.TimeSleepMs,
        ["sys.stdout_writeLine"] = SyscallId.StdoutWriteLine,
        ["sys.proc_exit"] = SyscallId.ProcExit,
        ["sys.process_argv"] = SyscallId.ProcessArgv,
        ["sys.fs_readFile"] = SyscallId.FsReadFile,
        ["sys.fs_fileExists"] = SyscallId.FsFileExists,
        ["sys.fs_readDir"] = SyscallId.FsReadDir,
        ["sys.fs_stat"] = SyscallId.FsStat,
        ["sys.fs_pathExists"] = SyscallId.FsPathExists,
        ["sys.fs_writeFile"] = SyscallId.FsWriteFile,
        ["sys.fs_makeDir"] = SyscallId.FsMakeDir,
        ["sys.str_utf8ByteCount"] = SyscallId.StrUtf8ByteCount,
        ["sys.http_get"] = SyscallId.HttpGet,
        ["sys.platform"] = SyscallId.Platform,
        ["sys.arch"] = SyscallId.Arch,
        ["sys.os_version"] = SyscallId.OsVersion,
        ["sys.runtime"] = SyscallId.Runtime
    }.ToFrozenDictionary(StringComparer.Ordinal);

    public static bool TryResolve(string target, out SyscallId id)
    {
        return NameToId.TryGetValue(target, out id);
    }
}
