using System.Net;
using System.Net.Http;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Runtime.InteropServices;

namespace AiVM.Core;

public class DefaultSyscallHost : ISyscallHost
{
    private static readonly HttpClient HttpClient = new();
    private static readonly string[] EmptyArgv = Array.Empty<string>();

    public virtual string[] ProcessArgv() => EmptyArgv;

    public virtual void ConsolePrintLine(string text) => Console.WriteLine(text);

    public virtual void IoPrint(string text) => Console.WriteLine(text);

    public virtual void IoWrite(string text) => Console.Write(text);

    public virtual string IoReadLine() => Console.ReadLine() ?? string.Empty;

    public virtual string IoReadAllStdin() => Console.In.ReadToEnd();

    public virtual string IoReadFile(string path) => File.ReadAllText(path);

    public virtual bool IoFileExists(string path) => File.Exists(path);

    public virtual bool IoPathExists(string path) => File.Exists(path) || Directory.Exists(path);

    public virtual void IoMakeDir(string path) => Directory.CreateDirectory(path);

    public virtual void IoWriteFile(string path, string text) => File.WriteAllText(path, text);

    public virtual string FsReadFile(string path) => File.ReadAllText(path);

    public virtual bool FsFileExists(string path) => File.Exists(path);

    public virtual int StrUtf8ByteCount(string text) => Encoding.UTF8.GetByteCount(text);

    public virtual string HttpGet(string url)
    {
        try
        {
            var normalizedUrl = url.Replace(" ", "%20", StringComparison.Ordinal);
            return HttpClient.GetStringAsync(normalizedUrl).GetAwaiter().GetResult();
        }
        catch
        {
            return string.Empty;
        }
    }

    public virtual string Platform()
    {
        if (OperatingSystem.IsMacOS())
        {
            return "macos";
        }

        if (OperatingSystem.IsWindows())
        {
            return "windows";
        }

        if (OperatingSystem.IsLinux())
        {
            return "linux";
        }

        return "unknown";
    }

    public virtual string Architecture()
    {
        return RuntimeInformation.OSArchitecture.ToString().ToLowerInvariant();
    }

    public virtual string OsVersion()
    {
        return RuntimeInformation.OSDescription;
    }

    public virtual string Runtime()
    {
        return "airun-dotnet";
    }

    public virtual int NetListen(VmNetworkState state, int port)
    {
        var listener = new TcpListener(IPAddress.Loopback, port);
        listener.Start();
        var handle = state.NextNetHandle++;
        state.NetListeners[handle] = listener;
        return handle;
    }

    public virtual int NetListenTls(VmNetworkState state, int port, string certPath, string keyPath)
    {
        X509Certificate2 certificate;
        try
        {
            certificate = X509Certificate2.CreateFromPemFile(certPath, keyPath);
        }
        catch
        {
            return -1;
        }

        var listener = new TcpListener(IPAddress.Loopback, port);
        listener.Start();
        var handle = state.NextNetHandle++;
        state.NetListeners[handle] = listener;
        state.NetTlsCertificates[handle] = certificate;
        return handle;
    }

    public virtual int NetAccept(VmNetworkState state, int listenerHandle)
    {
        if (!state.NetListeners.TryGetValue(listenerHandle, out var listener))
        {
            return -1;
        }

        var client = listener.AcceptTcpClient();
        var connHandle = state.NextNetHandle++;
        state.NetConnections[connHandle] = client;
        if (state.NetTlsCertificates.TryGetValue(listenerHandle, out var cert))
        {
            var tlsStream = new SslStream(client.GetStream(), leaveInnerStreamOpen: false);
            try
            {
                tlsStream.AuthenticateAsServer(
                    cert,
                    clientCertificateRequired: false,
                    enabledSslProtocols: SslProtocols.Tls12 | SslProtocols.Tls13,
                    checkCertificateRevocation: false);
            }
            catch
            {
                tlsStream.Dispose();
                try { client.Close(); } catch { }
                state.NetConnections.Remove(connHandle);
                return -1;
            }

            state.NetTlsStreams[connHandle] = tlsStream;
        }

        return connHandle;
    }

    public virtual string NetReadHeaders(VmNetworkState state, int connectionHandle)
    {
        if (!state.NetConnections.TryGetValue(connectionHandle, out var client))
        {
            return string.Empty;
        }

        Stream stream = state.NetTlsStreams.TryGetValue(connectionHandle, out var tlsStream)
            ? tlsStream
            : client.GetStream();
        var bytes = new List<byte>(1024);
        var endSeen = 0;
        var pattern = new byte[] { 13, 10, 13, 10 };
        while (bytes.Count < 65536)
        {
            var next = stream.ReadByte();
            if (next < 0)
            {
                break;
            }

            var b = (byte)next;
            bytes.Add(b);
            if (b == pattern[endSeen])
            {
                endSeen++;
                if (endSeen == 4)
                {
                    break;
                }
            }
            else
            {
                endSeen = b == pattern[0] ? 1 : 0;
            }
        }

        var headerText = Encoding.ASCII.GetString(bytes.ToArray());
        var contentLength = ParseContentLength(headerText);
        if (contentLength > 0)
        {
            var bodyBuffer = new byte[contentLength];
            var readTotal = 0;
            while (readTotal < contentLength)
            {
                var read = stream.Read(bodyBuffer, readTotal, contentLength - readTotal);
                if (read <= 0)
                {
                    break;
                }
                readTotal += read;
            }

            if (readTotal > 0)
            {
                headerText += Encoding.UTF8.GetString(bodyBuffer, 0, readTotal);
            }
        }

        return headerText;
    }

    public virtual bool NetWrite(VmNetworkState state, int connectionHandle, string text)
    {
        if (!state.NetConnections.TryGetValue(connectionHandle, out var client))
        {
            return false;
        }

        var bytes = Encoding.UTF8.GetBytes(text);
        Stream stream = state.NetTlsStreams.TryGetValue(connectionHandle, out var tlsStream)
            ? tlsStream
            : client.GetStream();
        stream.Write(bytes, 0, bytes.Length);
        stream.Flush();
        return true;
    }

    public virtual void NetClose(VmNetworkState state, int handle)
    {
        if (state.NetConnections.TryGetValue(handle, out var conn))
        {
            if (state.NetTlsStreams.TryGetValue(handle, out var tlsStream))
            {
                try { tlsStream.Dispose(); } catch { }
                state.NetTlsStreams.Remove(handle);
            }

            try { conn.Close(); } catch { }
            state.NetConnections.Remove(handle);
            return;
        }

        if (state.NetListeners.TryGetValue(handle, out var listener))
        {
            try { listener.Stop(); } catch { }
            state.NetListeners.Remove(handle);
            state.NetTlsCertificates.Remove(handle);
        }
    }

    public virtual void StdoutWriteLine(string text) => Console.WriteLine(text);

    public virtual void ProcessExit(int code) => throw new AosProcessExitException(code);

    private static int ParseContentLength(string raw)
    {
        foreach (var line in raw.Split(new[] { "\r\n", "\n" }, StringSplitOptions.None))
        {
            if (line.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase))
            {
                var value = line["Content-Length:".Length..].Trim();
                if (int.TryParse(value, out var parsed) && parsed > 0)
                {
                    return parsed;
                }

                return 0;
            }
        }

        return 0;
    }
}
