using System.Runtime.InteropServices;

namespace AiVM.Core;

public static class VmSyscallDispatcher
{
    public static bool SupportsTarget(string target)
    {
        return SyscallRegistry.TryResolve(target, out _);
    }

    public static bool TryGetExpectedArity(string target, out int arity)
    {
        if (!SyscallRegistry.TryResolve(target, out var id))
        {
            arity = -1;
            return false;
        }
        return TryGetExpectedArity(id, out arity);
    }

    public static bool TryGetExpectedArity(SyscallId id, out int arity)
    {
        arity = id switch
        {
            SyscallId.NetListen => 1,
            SyscallId.NetListenTls => 3,
            SyscallId.NetAccept => 1,
            SyscallId.NetReadHeaders => 1,
            SyscallId.NetWrite => 2,
            SyscallId.NetClose => 1,
            SyscallId.ConsoleWrite => 1,
            SyscallId.ConsoleWriteLine => 1,
            SyscallId.ConsoleReadLine => 0,
            SyscallId.ConsoleReadAllStdin => 0,
            SyscallId.ConsoleWriteErrLine => 1,
            SyscallId.ProcessCwd => 0,
            SyscallId.ProcessEnvGet => 1,
            SyscallId.TimeNowUnixMs => 0,
            SyscallId.TimeMonotonicMs => 0,
            SyscallId.TimeSleepMs => 1,
            SyscallId.StdoutWriteLine => 1,
            SyscallId.ProcExit => 1,
            SyscallId.ProcessArgv => 0,
            SyscallId.FsReadFile => 1,
            SyscallId.FsFileExists => 1,
            SyscallId.FsReadDir => 1,
            SyscallId.FsStat => 1,
            SyscallId.FsPathExists => 1,
            SyscallId.FsWriteFile => 2,
            SyscallId.FsMakeDir => 1,
            SyscallId.StrUtf8ByteCount => 1,
            SyscallId.HttpGet => 1,
            SyscallId.Platform => 0,
            SyscallId.Arch => 0,
            SyscallId.OsVersion => 0,
            SyscallId.Runtime => 0,
            _ => -1
        };

        return arity >= 0;
    }

    public static bool TryInvoke(string target, IReadOnlyList<SysValue> args, VmNetworkState network, out SysValue result)
    {
        if (SyscallRegistry.TryResolve(target, out var id))
        {
            return TryInvoke(id, AsSpan(args), network, out result);
        }
        result = SysValue.Unknown();
        return false;
    }

    public static bool TryInvoke(SyscallId id, IReadOnlyList<SysValue> args, VmNetworkState network, out SysValue result)
    {
        return TryInvoke(id, AsSpan(args), network, out result);
    }

    public static bool TryInvoke(SyscallId id, ReadOnlySpan<SysValue> args, VmNetworkState network, out SysValue result)
    {
        result = SysValue.Unknown();
        var target = GetTargetName(id);
        if (target is null)
        {
            return false;
        }

        switch (target)
        {
            case "sys.net_listen":
                if (!TryGetInt(args, 0, 1, out var listenPort))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetListen(network, listenPort));
                return true;

            case "sys.net_listen_tls":
                if (!TryGetInt(args, 0, 3, out var tlsPort) ||
                    !TryGetString(args, 1, 3, out var certPath) ||
                    !TryGetString(args, 2, 3, out var keyPath))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetListenTls(network, tlsPort, certPath, keyPath));
                return true;

            case "sys.net_accept":
                if (!TryGetInt(args, 0, 1, out var acceptHandle))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetAccept(network, acceptHandle));
                return true;

            case "sys.net_readHeaders":
                if (!TryGetInt(args, 0, 1, out var readHandle))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.NetReadHeaders(network, readHandle));
                return true;

            case "sys.net_write":
                if (!TryGetInt(args, 0, 2, out var writeHandle) ||
                    !TryGetString(args, 1, 2, out var writeText))
                {
                    return true;
                }
                result = VmSyscalls.NetWrite(network, writeHandle, writeText) ? SysValue.Void() : SysValue.Unknown();
                return true;

            case "sys.net_close":
                if (!TryGetInt(args, 0, 1, out var closeHandle))
                {
                    return true;
                }
                VmSyscalls.NetClose(network, closeHandle);
                result = SysValue.Void();
                return true;

            case "sys.console_write":
                if (!TryGetString(args, 0, 1, out var writeOutText))
                {
                    return true;
                }
                VmSyscalls.ConsoleWrite(writeOutText);
                result = SysValue.Void();
                return true;

            case "sys.process_cwd":
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.ProcessCwd());
                return true;
            case "sys.process_envGet":
                if (!TryGetString(args, 0, 1, out var envName))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.ProcessEnvGet(envName));
                return true;
            case "sys.time_nowUnixMs":
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.TimeNowUnixMs());
                return true;
            case "sys.time_monotonicMs":
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.TimeMonotonicMs());
                return true;
            case "sys.time_sleepMs":
                if (!TryGetInt(args, 0, 1, out var sleepMs))
                {
                    return true;
                }
                VmSyscalls.TimeSleepMs(sleepMs);
                result = SysValue.Void();
                return true;
            case "sys.console_writeLine":
                if (!TryGetString(args, 0, 1, out var consoleLineText))
                {
                    return true;
                }
                VmSyscalls.ConsolePrintLine(consoleLineText);
                result = SysValue.Void();
                return true;
            case "sys.console_readLine":
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.IoReadLine());
                return true;
            case "sys.console_readAllStdin":
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.IoReadAllStdin());
                return true;
            case "sys.console_writeErrLine":
                if (!TryGetString(args, 0, 1, out var errText))
                {
                    return true;
                }
                VmSyscalls.ConsoleWriteErrLine(errText);
                result = SysValue.Void();
                return true;
            case "sys.stdout_writeLine":
                if (!TryGetString(args, 0, 1, out var outText))
                {
                    return true;
                }
                VmSyscalls.StdoutWriteLine(outText);
                result = SysValue.Void();
                return true;

            case "sys.proc_exit":
                if (!TryGetInt(args, 0, 1, out var exitCode))
                {
                    return true;
                }
                VmSyscalls.ProcessExit(exitCode);
                result = SysValue.Void();
                return true;

            case "sys.fs_readFile":
                if (!TryGetString(args, 0, 1, out var fsPath))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.FsReadFile(fsPath));
                return true;

            case "sys.fs_fileExists":
                if (!TryGetString(args, 0, 1, out var existsPath))
                {
                    return true;
                }
                result = SysValue.Bool(VmSyscalls.FsFileExists(existsPath));
                return true;
            case "sys.fs_readDir":
                if (!TryGetString(args, 0, 1, out var readDirPath))
                {
                    return true;
                }
                _ = VmSyscalls.FsReadDir(readDirPath);
                result = SysValue.Unknown();
                return true;
            case "sys.fs_stat":
                if (!TryGetString(args, 0, 1, out var statPath))
                {
                    return true;
                }
                _ = VmSyscalls.FsStat(statPath);
                result = SysValue.Unknown();
                return true;
            case "sys.fs_pathExists":
                if (!TryGetString(args, 0, 1, out var pathExistsPath))
                {
                    return true;
                }
                result = SysValue.Bool(VmSyscalls.FsPathExists(pathExistsPath));
                return true;
            case "sys.fs_writeFile":
                if (!TryGetString(args, 0, 2, out var writeFilePath) ||
                    !TryGetString(args, 1, 2, out var writeFileText))
                {
                    return true;
                }
                VmSyscalls.FsWriteFile(writeFilePath, writeFileText);
                result = SysValue.Void();
                return true;

            case "sys.fs_makeDir":
                if (!TryGetString(args, 0, 1, out var makeDirPath))
                {
                    return true;
                }
                VmSyscalls.FsMakeDir(makeDirPath);
                result = SysValue.Void();
                return true;

            case "sys.str_utf8ByteCount":
                if (!TryGetString(args, 0, 1, out var utf8Text))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.StrUtf8ByteCount(utf8Text));
                return true;

            case "sys.http_get":
                if (!TryGetString(args, 0, 1, out var url))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.HttpGet(url));
                return true;

            case "sys.platform":
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.Platform());
                return true;

            case "sys.arch":
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.Architecture());
                return true;

            case "sys.os_version":
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.OsVersion());
                return true;

            case "sys.runtime":
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.Runtime());
                return true;

            default:
                return false;
        }
    }

    private static string? GetTargetName(SyscallId id)
    {
        return id switch
        {
            SyscallId.NetListen => "sys.net_listen",
            SyscallId.NetListenTls => "sys.net_listen_tls",
            SyscallId.NetAccept => "sys.net_accept",
            SyscallId.NetReadHeaders => "sys.net_readHeaders",
            SyscallId.NetWrite => "sys.net_write",
            SyscallId.NetClose => "sys.net_close",
            SyscallId.ConsoleWrite => "sys.console_write",
            SyscallId.ConsoleWriteLine => "sys.console_writeLine",
            SyscallId.ConsoleReadLine => "sys.console_readLine",
            SyscallId.ConsoleReadAllStdin => "sys.console_readAllStdin",
            SyscallId.ConsoleWriteErrLine => "sys.console_writeErrLine",
            SyscallId.ProcessCwd => "sys.process_cwd",
            SyscallId.ProcessEnvGet => "sys.process_envGet",
            SyscallId.TimeNowUnixMs => "sys.time_nowUnixMs",
            SyscallId.TimeMonotonicMs => "sys.time_monotonicMs",
            SyscallId.TimeSleepMs => "sys.time_sleepMs",
            SyscallId.StdoutWriteLine => "sys.stdout_writeLine",
            SyscallId.ProcExit => "sys.proc_exit",
            SyscallId.ProcessArgv => "sys.process_argv",
            SyscallId.FsReadFile => "sys.fs_readFile",
            SyscallId.FsFileExists => "sys.fs_fileExists",
            SyscallId.FsReadDir => "sys.fs_readDir",
            SyscallId.FsStat => "sys.fs_stat",
            SyscallId.FsPathExists => "sys.fs_pathExists",
            SyscallId.FsWriteFile => "sys.fs_writeFile",
            SyscallId.FsMakeDir => "sys.fs_makeDir",
            SyscallId.StrUtf8ByteCount => "sys.str_utf8ByteCount",
            SyscallId.HttpGet => "sys.http_get",
            SyscallId.Platform => "sys.platform",
            SyscallId.Arch => "sys.arch",
            SyscallId.OsVersion => "sys.os_version",
            SyscallId.Runtime => "sys.runtime",
            _ => null
        };
    }

    private static bool TryGetInt(ReadOnlySpan<SysValue> args, int index, int expectedCount, out int value)
    {
        value = 0;
        if (args.Length != expectedCount)
        {
            return false;
        }
        if (args[index].Kind != VmValueKind.Int)
        {
            return false;
        }
        value = args[index].IntValue;
        return true;
    }

    private static bool TryGetString(ReadOnlySpan<SysValue> args, int index, int expectedCount, out string value)
    {
        value = string.Empty;
        if (args.Length != expectedCount)
        {
            return false;
        }
        if (args[index].Kind != VmValueKind.String)
        {
            return false;
        }
        value = args[index].StringValue;
        return true;
    }

    private static ReadOnlySpan<SysValue> AsSpan(IReadOnlyList<SysValue> args)
    {
        if (args.Count == 0)
        {
            return ReadOnlySpan<SysValue>.Empty;
        }

        if (args is List<SysValue> list)
        {
            return CollectionsMarshal.AsSpan(list);
        }

        return args.ToArray();
    }
}
