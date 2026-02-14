namespace AiVM.Core;

public static class VmSyscalls
{
    public static ISyscallHost Host { get; set; } = new DefaultSyscallHost();

    public static string[] ProcessArgv()
    {
        return Host.ProcessArgv();
    }

    public static string ProcessEnvGet(string name)
    {
        return Host.ProcessEnvGet(name);
    }

    public static void ConsoleWriteErrLine(string text)
    {
        Host.ConsoleWriteErrLine(text);
    }

    public static void ConsoleWrite(string text)
    {
        Host.ConsoleWrite(text);
    }

    public static string ProcessCwd()
    {
        return Host.ProcessCwd();
    }

    public static void ConsolePrintLine(string text)
    {
        Host.ConsolePrintLine(text);
    }

    public static void IoPrint(string text)
    {
        Host.IoPrint(text);
    }

    public static void IoWrite(string text)
    {
        Host.IoWrite(text);
    }

    public static string IoReadLine()
    {
        return Host.IoReadLine();
    }

    public static string IoReadAllStdin()
    {
        return Host.IoReadAllStdin();
    }

    public static string IoReadFile(string path)
    {
        return Host.IoReadFile(path);
    }

    public static bool IoFileExists(string path)
    {
        return Host.IoFileExists(path);
    }

    public static bool IoPathExists(string path)
    {
        return Host.IoPathExists(path);
    }

    public static void IoMakeDir(string path)
    {
        Host.IoMakeDir(path);
    }

    public static void IoWriteFile(string path, string text)
    {
        Host.IoWriteFile(path, text);
    }

    public static string FsReadFile(string path)
    {
        return Host.FsReadFile(path);
    }

    public static bool FsFileExists(string path)
    {
        return Host.FsFileExists(path);
    }

    public static bool FsPathExists(string path)
    {
        return Host.FsPathExists(path);
    }

    public static void FsWriteFile(string path, string text)
    {
        Host.FsWriteFile(path, text);
    }

    public static void FsMakeDir(string path)
    {
        Host.FsMakeDir(path);
    }

    public static int StrUtf8ByteCount(string text)
    {
        return Host.StrUtf8ByteCount(text);
    }

    public static string HttpGet(string url)
    {
        return Host.HttpGet(url);
    }

    public static string Platform()
    {
        return Host.Platform();
    }

    public static string Architecture()
    {
        return Host.Architecture();
    }

    public static string OsVersion()
    {
        return Host.OsVersion();
    }

    public static string Runtime()
    {
        return Host.Runtime();
    }

    public static int NetListen(VmNetworkState state, int port)
    {
        return Host.NetListen(state, port);
    }

    public static int NetListenTls(VmNetworkState state, int port, string certPath, string keyPath)
    {
        return Host.NetListenTls(state, port, certPath, keyPath);
    }

    public static int NetAccept(VmNetworkState state, int listenerHandle)
    {
        return Host.NetAccept(state, listenerHandle);
    }

    public static string NetReadHeaders(VmNetworkState state, int connectionHandle)
    {
        return Host.NetReadHeaders(state, connectionHandle);
    }

    public static bool NetWrite(VmNetworkState state, int connectionHandle, string text)
    {
        return Host.NetWrite(state, connectionHandle, text);
    }

    public static void NetClose(VmNetworkState state, int handle)
    {
        Host.NetClose(state, handle);
    }

    public static void StdoutWriteLine(string text)
    {
        Host.StdoutWriteLine(text);
    }

    public static void ProcessExit(int code)
    {
        Host.ProcessExit(code);
    }
}
