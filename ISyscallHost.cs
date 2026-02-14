namespace AiVM.Core;

public interface ISyscallHost
{
    string[] ProcessArgv();
    string ProcessEnvGet(string name);
    int TimeNowUnixMs();
    int TimeMonotonicMs();
    void TimeSleepMs(int ms);
    void ConsoleWriteErrLine(string text);
    void ConsoleWrite(string text);
    string ProcessCwd();
    void ConsolePrintLine(string text);
    void IoPrint(string text);
    void IoWrite(string text);
    string IoReadLine();
    string IoReadAllStdin();
    string IoReadFile(string path);
    bool IoFileExists(string path);
    bool IoPathExists(string path);
    void IoMakeDir(string path);
    void IoWriteFile(string path, string text);

    string FsReadFile(string path);
    bool FsFileExists(string path);
    string[] FsReadDir(string path);
    VmFsStat FsStat(string path);
    bool FsPathExists(string path);
    void FsWriteFile(string path, string text);
    void FsMakeDir(string path);
    int StrUtf8ByteCount(string text);
    string HttpGet(string url);
    string Platform();
    string Architecture();
    string OsVersion();
    string Runtime();

    int NetListen(VmNetworkState state, int port);
    int NetListenTls(VmNetworkState state, int port, string certPath, string keyPath);
    int NetAccept(VmNetworkState state, int listenerHandle);
    string NetReadHeaders(VmNetworkState state, int connectionHandle);
    bool NetWrite(VmNetworkState state, int connectionHandle, string text);
    void NetClose(VmNetworkState state, int handle);
    int NetTcpListen(VmNetworkState state, string host, int port);
    int NetTcpListenTls(VmNetworkState state, string host, int port, string certPath, string keyPath);
    int NetTcpAccept(VmNetworkState state, int listenerHandle);
    string NetTcpRead(VmNetworkState state, int connectionHandle, int maxBytes);
    int NetTcpWrite(VmNetworkState state, int connectionHandle, string data);
    int NetUdpBind(VmNetworkState state, string host, int port);
    VmUdpPacket NetUdpRecv(VmNetworkState state, int handle, int maxBytes);
    int NetUdpSend(VmNetworkState state, int handle, string host, int port, string data);
    string CryptoBase64Encode(string text);
    string CryptoBase64Decode(string text);
    string CryptoSha1(string text);
    string CryptoSha256(string text);
    string CryptoHmacSha256(string key, string text);
    string CryptoRandomBytes(int count);

    void StdoutWriteLine(string text);
    void ProcessExit(int code);
}
