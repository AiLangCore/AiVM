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
            SyscallId.NetTcpListen => 2,
            SyscallId.NetTcpListenTls => 4,
            SyscallId.NetTcpAccept => 1,
            SyscallId.NetTcpRead => 2,
            SyscallId.NetTcpWrite => 2,
            SyscallId.NetUdpBind => 2,
            SyscallId.NetUdpRecv => 2,
            SyscallId.NetUdpSend => 4,
            SyscallId.UiCreateWindow => 3,
            SyscallId.UiBeginFrame => 1,
            SyscallId.UiDrawRect => 6,
            SyscallId.UiDrawText => 6,
            SyscallId.UiEndFrame => 1,
            SyscallId.UiPollEvent => 1,
            SyscallId.UiPresent => 1,
            SyscallId.UiCloseWindow => 1,
            SyscallId.CryptoBase64Encode => 1,
            SyscallId.CryptoBase64Decode => 1,
            SyscallId.CryptoSha1 => 1,
            SyscallId.CryptoSha256 => 1,
            SyscallId.CryptoHmacSha256 => 2,
            SyscallId.CryptoRandomBytes => 1,
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

            case SyscallId.NetTcpListen:
                if (!TryGetString(args, 0, 2, out var tcpListenHost) ||
                    !TryGetInt(args, 1, 2, out var tcpListenPort))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetTcpListen(network, tcpListenHost, tcpListenPort));
                return true;

            case SyscallId.NetTcpListenTls:
                if (!TryGetString(args, 0, 4, out var tcpTlsHost) ||
                    !TryGetInt(args, 1, 4, out var tcpTlsPort) ||
                    !TryGetString(args, 2, 4, out var tcpCertPath) ||
                    !TryGetString(args, 3, 4, out var tcpKeyPath))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetTcpListenTls(network, tcpTlsHost, tcpTlsPort, tcpCertPath, tcpKeyPath));
                return true;

            case SyscallId.NetTcpAccept:
                if (!TryGetInt(args, 0, 1, out var tcpAcceptListenerHandle))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetTcpAccept(network, tcpAcceptListenerHandle));
                return true;

            case SyscallId.NetTcpRead:
                if (!TryGetInt(args, 0, 2, out var tcpReadConnectionHandle) ||
                    !TryGetInt(args, 1, 2, out var tcpReadMaxBytes))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.NetTcpRead(network, tcpReadConnectionHandle, tcpReadMaxBytes));
                return true;

            case SyscallId.NetTcpWrite:
                if (!TryGetInt(args, 0, 2, out var tcpWriteConnectionHandle) ||
                    !TryGetString(args, 1, 2, out var tcpWriteData))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetTcpWrite(network, tcpWriteConnectionHandle, tcpWriteData));
                return true;

            case SyscallId.NetUdpBind:
                if (!TryGetString(args, 0, 2, out var udpBindHost) ||
                    !TryGetInt(args, 1, 2, out var udpBindPort))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetUdpBind(network, udpBindHost, udpBindPort));
                return true;

            case SyscallId.NetUdpSend:
                if (!TryGetInt(args, 0, 4, out var udpSendHandle) ||
                    !TryGetString(args, 1, 4, out var udpSendHost) ||
                    !TryGetInt(args, 2, 4, out var udpSendPort) ||
                    !TryGetString(args, 3, 4, out var udpSendData))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.NetUdpSend(network, udpSendHandle, udpSendHost, udpSendPort, udpSendData));
                return true;

            case SyscallId.UiCreateWindow:
                if (!TryGetString(args, 0, 3, out var uiTitle) ||
                    !TryGetInt(args, 1, 3, out var uiWidth) ||
                    !TryGetInt(args, 2, 3, out var uiHeight))
                {
                    return true;
                }
                result = SysValue.Int(VmSyscalls.UiCreateWindow(uiTitle, uiWidth, uiHeight));
                return true;

            case SyscallId.UiBeginFrame:
                if (!TryGetInt(args, 0, 1, out var uiBeginHandle))
                {
                    return true;
                }
                VmSyscalls.UiBeginFrame(uiBeginHandle);
                result = SysValue.Void();
                return true;

            case SyscallId.UiDrawRect:
                if (!TryGetInt(args, 0, 6, out var uiRectHandle) ||
                    !TryGetInt(args, 1, 6, out var uiRectX) ||
                    !TryGetInt(args, 2, 6, out var uiRectY) ||
                    !TryGetInt(args, 3, 6, out var uiRectW) ||
                    !TryGetInt(args, 4, 6, out var uiRectH) ||
                    !TryGetString(args, 5, 6, out var uiRectColor))
                {
                    return true;
                }
                VmSyscalls.UiDrawRect(uiRectHandle, uiRectX, uiRectY, uiRectW, uiRectH, uiRectColor);
                result = SysValue.Void();
                return true;

            case SyscallId.UiDrawText:
                if (!TryGetInt(args, 0, 6, out var uiTextHandle) ||
                    !TryGetInt(args, 1, 6, out var uiTextX) ||
                    !TryGetInt(args, 2, 6, out var uiTextY) ||
                    !TryGetString(args, 3, 6, out var uiTextValue) ||
                    !TryGetString(args, 4, 6, out var uiTextColor) ||
                    !TryGetInt(args, 5, 6, out var uiTextSize))
                {
                    return true;
                }
                VmSyscalls.UiDrawText(uiTextHandle, uiTextX, uiTextY, uiTextValue, uiTextColor, uiTextSize);
                result = SysValue.Void();
                return true;

            case SyscallId.UiEndFrame:
                if (!TryGetInt(args, 0, 1, out var uiEndHandle))
                {
                    return true;
                }
                VmSyscalls.UiEndFrame(uiEndHandle);
                result = SysValue.Void();
                return true;

            case SyscallId.UiPresent:
                if (!TryGetInt(args, 0, 1, out var uiPresentHandle))
                {
                    return true;
                }
                VmSyscalls.UiPresent(uiPresentHandle);
                result = SysValue.Void();
                return true;

            case SyscallId.UiCloseWindow:
                if (!TryGetInt(args, 0, 1, out var uiCloseHandle))
                {
                    return true;
                }
                VmSyscalls.UiCloseWindow(uiCloseHandle);
                result = SysValue.Void();
                return true;

            case SyscallId.CryptoBase64Encode:
                if (!TryGetString(args, 0, 1, out var base64EncodeText))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.CryptoBase64Encode(base64EncodeText));
                return true;

            case SyscallId.CryptoBase64Decode:
                if (!TryGetString(args, 0, 1, out var base64DecodeText))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.CryptoBase64Decode(base64DecodeText));
                return true;

            case SyscallId.CryptoSha1:
                if (!TryGetString(args, 0, 1, out var sha1Text))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.CryptoSha1(sha1Text));
                return true;

            case SyscallId.CryptoSha256:
                if (!TryGetString(args, 0, 1, out var sha256Text))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.CryptoSha256(sha256Text));
                return true;

            case SyscallId.CryptoHmacSha256:
                if (!TryGetString(args, 0, 2, out var hmacKey) ||
                    !TryGetString(args, 1, 2, out var hmacText))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.CryptoHmacSha256(hmacKey, hmacText));
                return true;

            case SyscallId.CryptoRandomBytes:
                if (!TryGetInt(args, 0, 1, out var randomCount))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.CryptoRandomBytes(randomCount));
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
