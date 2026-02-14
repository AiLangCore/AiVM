namespace AiVM.Core;

public static class VmSyscallDispatcher
{
    public static bool SupportsTarget(string target)
    {
        return target switch
        {
            "sys.net_listen" or
            "sys.net_listen_tls" or
            "sys.net_accept" or
            "sys.net_readHeaders" or
            "sys.net_write" or
            "sys.net_close" or
            "sys.console_write" or
            "sys.console_writeLine" or
            "sys.console_readLine" or
            "sys.console_readAllStdin" or
            "sys.console_writeErrLine" or
            "sys.process_cwd" or
            "sys.stdout_writeLine" or
            "sys.proc_exit" or
            "sys.fs_readFile" or
            "sys.fs_fileExists" or
            "sys.fs_pathExists" or
            "sys.fs_writeFile" or
            "sys.fs_makeDir" or
            "sys.str_utf8ByteCount" or
            "sys.http_get" or
            "sys.platform" or
            "sys.arch" or
            "sys.os_version" or
            "sys.runtime" => true,
            _ => false
        };
    }

    public static bool TryGetExpectedArity(string target, out int arity)
    {
        arity = target switch
        {
            "sys.net_listen" => 1,
            "sys.net_listen_tls" => 3,
            "sys.net_accept" => 1,
            "sys.net_readHeaders" => 1,
            "sys.net_write" => 2,
            "sys.net_close" => 1,
            "sys.console_write" => 1,
            "sys.console_writeLine" => 1,
            "sys.console_readLine" => 0,
            "sys.console_readAllStdin" => 0,
            "sys.console_writeErrLine" => 1,
            "sys.process_cwd" => 0,
            "sys.stdout_writeLine" => 1,
            "sys.proc_exit" => 1,
            "sys.fs_readFile" => 1,
            "sys.fs_fileExists" => 1,
            "sys.fs_pathExists" => 1,
            "sys.fs_writeFile" => 2,
            "sys.fs_makeDir" => 1,
            "sys.str_utf8ByteCount" => 1,
            "sys.http_get" => 1,
            "sys.platform" => 0,
            "sys.arch" => 0,
            "sys.os_version" => 0,
            "sys.runtime" => 0,
            _ => -1
        };
        return arity >= 0;
    }

    public static bool TryInvoke(string target, IReadOnlyList<SysValue> args, VmNetworkState network, out SysValue result)
    {
        result = SysValue.Unknown();
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
                if (args.Count != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.ProcessCwd());
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
                if (args.Count != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.IoReadLine());
                return true;
            case "sys.console_readAllStdin":
                if (args.Count != 0)
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
                if (args.Count != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.Platform());
                return true;

            case "sys.arch":
                if (args.Count != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.Architecture());
                return true;

            case "sys.os_version":
                if (args.Count != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.OsVersion());
                return true;

            case "sys.runtime":
                if (args.Count != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.Runtime());
                return true;

            default:
                return false;
        }
    }

    private static bool TryGetInt(IReadOnlyList<SysValue> args, int index, int expectedCount, out int value)
    {
        value = 0;
        if (args.Count != expectedCount)
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

    private static bool TryGetString(IReadOnlyList<SysValue> args, int index, int expectedCount, out string value)
    {
        value = string.Empty;
        if (args.Count != expectedCount)
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
