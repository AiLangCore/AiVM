namespace AiVM.Core;

public static class VmSyscallDispatcher
{
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

    public static bool TryInvoke(SyscallId id, ReadOnlySpan<SysValue> args, VmNetworkState network, out SysValue result)
    {
        result = SysValue.Unknown();
        switch (id)
        {
            case SyscallId.NetListen:
                if (!TryGetInt(args, 0, 1, out var listenPort))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetListen(network, listenPort));
                return true;

            case SyscallId.NetListenTls:
                if (!TryGetInt(args, 0, 3, out var tlsPort) ||
                    !TryGetString(args, 1, 3, out var certPath) ||
                    !TryGetString(args, 2, 3, out var keyPath))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetListenTls(network, tlsPort, certPath, keyPath));
                return true;

            case SyscallId.NetAccept:
                if (!TryGetInt(args, 0, 1, out var acceptHandle))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetAccept(network, acceptHandle));
                return true;

            case SyscallId.NetReadHeaders:
                if (!TryGetInt(args, 0, 1, out var readHandle))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.NetReadHeaders(network, readHandle));
                return true;

            case SyscallId.NetWrite:
                if (!TryGetInt(args, 0, 2, out var writeHandle) ||
                    !TryGetString(args, 1, 2, out var writeText))
                {
                    return true;
                }
                result = VmSyscalls.NetWrite(network, writeHandle, writeText) ? SysValue.Void() : SysValue.Unknown();
                return true;

            case SyscallId.NetClose:
                if (!TryGetInt(args, 0, 1, out var closeHandle))
                {
                    return true;
                }
                VmSyscalls.NetClose(network, closeHandle);
                result = SysValue.Void();
                return true;

            case SyscallId.ConsoleWrite:
                if (!TryGetString(args, 0, 1, out var writeOutText))
                {
                    return true;
                }
                VmSyscalls.ConsoleWrite(writeOutText);
                result = SysValue.Void();
                return true;

            case SyscallId.ProcessCwd:
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.ProcessCwd());
                return true;
            case SyscallId.ProcessEnvGet:
                if (!TryGetString(args, 0, 1, out var envName))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.ProcessEnvGet(envName));
                return true;
            case SyscallId.TimeNowUnixMs:
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.TimeNowUnixMs());
                return true;
            case SyscallId.TimeMonotonicMs:
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.TimeMonotonicMs());
                return true;
            case SyscallId.TimeSleepMs:
                if (!TryGetInt(args, 0, 1, out var sleepMs))
                {
                    return true;
                }
                VmSyscalls.TimeSleepMs(sleepMs);
                result = SysValue.Void();
                return true;
            case SyscallId.ConsoleWriteLine:
                if (!TryGetString(args, 0, 1, out var consoleLineText))
                {
                    return true;
                }
                VmSyscalls.ConsolePrintLine(consoleLineText);
                result = SysValue.Void();
                return true;
            case SyscallId.ConsoleReadLine:
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.IoReadLine());
                return true;
            case SyscallId.ConsoleReadAllStdin:
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.IoReadAllStdin());
                return true;
            case SyscallId.ConsoleWriteErrLine:
                if (!TryGetString(args, 0, 1, out var errText))
                {
                    return true;
                }
                VmSyscalls.ConsoleWriteErrLine(errText);
                result = SysValue.Void();
                return true;
            case SyscallId.StdoutWriteLine:
                if (!TryGetString(args, 0, 1, out var outText))
                {
                    return true;
                }
                VmSyscalls.StdoutWriteLine(outText);
                result = SysValue.Void();
                return true;

            case SyscallId.ProcExit:
                if (!TryGetInt(args, 0, 1, out var exitCode))
                {
                    return true;
                }
                VmSyscalls.ProcessExit(exitCode);
                result = SysValue.Void();
                return true;

            case SyscallId.FsReadFile:
                if (!TryGetString(args, 0, 1, out var fsPath))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.FsReadFile(fsPath));
                return true;

            case SyscallId.FsFileExists:
                if (!TryGetString(args, 0, 1, out var existsPath))
                {
                    return true;
                }
                result = SysValue.Bool(VmSyscalls.FsFileExists(existsPath));
                return true;
            case SyscallId.FsReadDir:
                if (!TryGetString(args, 0, 1, out var readDirPath))
                {
                    return true;
                }
                _ = VmSyscalls.FsReadDir(readDirPath);
                result = SysValue.Unknown();
                return true;
            case SyscallId.FsStat:
                if (!TryGetString(args, 0, 1, out var statPath))
                {
                    return true;
                }
                _ = VmSyscalls.FsStat(statPath);
                result = SysValue.Unknown();
                return true;
            case SyscallId.FsPathExists:
                if (!TryGetString(args, 0, 1, out var pathExistsPath))
                {
                    return true;
                }
                result = SysValue.Bool(VmSyscalls.FsPathExists(pathExistsPath));
                return true;
            case SyscallId.FsWriteFile:
                if (!TryGetString(args, 0, 2, out var writeFilePath) ||
                    !TryGetString(args, 1, 2, out var writeFileText))
                {
                    return true;
                }
                VmSyscalls.FsWriteFile(writeFilePath, writeFileText);
                result = SysValue.Void();
                return true;

            case SyscallId.FsMakeDir:
                if (!TryGetString(args, 0, 1, out var makeDirPath))
                {
                    return true;
                }
                VmSyscalls.FsMakeDir(makeDirPath);
                result = SysValue.Void();
                return true;

            case SyscallId.StrUtf8ByteCount:
                if (!TryGetString(args, 0, 1, out var utf8Text))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.StrUtf8ByteCount(utf8Text));
                return true;

            case SyscallId.HttpGet:
                if (!TryGetString(args, 0, 1, out var url))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.HttpGet(url));
                return true;

            case SyscallId.Platform:
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.Platform());
                return true;

            case SyscallId.Arch:
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.Architecture());
                return true;

            case SyscallId.OsVersion:
                if (args.Length != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.OsVersion());
                return true;

            case SyscallId.Runtime:
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

}
