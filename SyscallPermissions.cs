namespace AiVM.Core;

public static class SyscallPermissions
{
    public static bool TryGetRequiredPermission(SyscallId id, out string permission)
    {
        permission = id switch
        {
            SyscallId.NetListen => "net",
            SyscallId.NetListenTls => "net",
            SyscallId.NetAccept => "net",
            SyscallId.NetReadHeaders => "net",
            SyscallId.NetWrite => "net",
            SyscallId.NetClose => "net",
            SyscallId.NetTcpListen => "net",
            SyscallId.NetTcpListenTls => "net",
            SyscallId.NetTcpAccept => "net",
            SyscallId.NetTcpRead => "net",
            SyscallId.NetTcpWrite => "net",
            SyscallId.ConsoleWrite => "console",
            SyscallId.ConsoleWriteLine => "console",
            SyscallId.ConsoleReadLine => "console",
            SyscallId.ConsoleReadAllStdin => "console",
            SyscallId.ConsoleWriteErrLine => "console",
            SyscallId.StdoutWriteLine => "console",
            SyscallId.ProcessCwd => "process",
            SyscallId.ProcessEnvGet => "process",
            SyscallId.ProcessArgv => "process",
            SyscallId.ProcExit => "process",
            SyscallId.TimeNowUnixMs => "time",
            SyscallId.TimeMonotonicMs => "time",
            SyscallId.TimeSleepMs => "time",
            SyscallId.FsReadFile => "file",
            SyscallId.FsFileExists => "file",
            SyscallId.FsReadDir => "file",
            SyscallId.FsStat => "file",
            SyscallId.FsPathExists => "file",
            SyscallId.FsWriteFile => "file",
            SyscallId.FsMakeDir => "file",
            SyscallId.HttpGet => "net",
            SyscallId.CryptoBase64Encode => "crypto",
            SyscallId.CryptoBase64Decode => "crypto",
            SyscallId.CryptoSha1 => "crypto",
            SyscallId.CryptoSha256 => "crypto",
            SyscallId.CryptoHmacSha256 => "crypto",
            SyscallId.Platform => "process",
            SyscallId.Arch => "process",
            SyscallId.OsVersion => "process",
            SyscallId.Runtime => "process",
            _ => string.Empty
        };

        return permission.Length > 0;
    }

    public static bool HasPermission(IReadOnlySet<string> permissions, SyscallId id)
    {
        if (permissions.Contains("sys"))
        {
            return true;
        }

        return TryGetRequiredPermission(id, out var permission) && permissions.Contains(permission);
    }
}
