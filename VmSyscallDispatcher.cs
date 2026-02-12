namespace AiVM.Core;

public static class VmSyscallDispatcher
{
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

            case "sys.str_utf8ByteCount":
                if (!TryGetString(args, 0, 1, out var utf8Text))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.StrUtf8ByteCount(utf8Text));
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
