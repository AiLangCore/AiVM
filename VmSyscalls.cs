namespace AiVM.Core;

public static class VmSyscalls
{
    public static void ConsolePrintLine(string text)
    {
        Console.WriteLine(text);
    }

    public static void IoPrint(string text)
    {
        Console.WriteLine(text);
    }

    public static void IoWrite(string text)
    {
        Console.Write(text);
    }

    public static string IoReadLine()
    {
        return Console.ReadLine() ?? string.Empty;
    }

    public static string IoReadAllStdin()
    {
        return Console.In.ReadToEnd();
    }

    public static string IoReadFile(string path)
    {
        return File.ReadAllText(path);
    }

    public static bool IoFileExists(string path)
    {
        return File.Exists(path);
    }

    public static bool IoPathExists(string path)
    {
        return File.Exists(path) || Directory.Exists(path);
    }

    public static void IoMakeDir(string path)
    {
        Directory.CreateDirectory(path);
    }

    public static void IoWriteFile(string path, string text)
    {
        File.WriteAllText(path, text);
    }

    public static string FsReadFile(string path)
    {
        return File.ReadAllText(path);
    }

    public static bool FsFileExists(string path)
    {
        return File.Exists(path);
    }

    public static int StrUtf8ByteCount(string text)
    {
        return System.Text.Encoding.UTF8.GetByteCount(text);
    }

    public static int NetListen(VmNetworkState state, int port)
    {
        var listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, port);
        listener.Start();
        var handle = state.NextNetHandle++;
        state.NetListeners[handle] = listener;
        return handle;
    }

    public static int NetListenTls(VmNetworkState state, int port, string certPath, string keyPath)
    {
        System.Security.Cryptography.X509Certificates.X509Certificate2 certificate;
        try
        {
            certificate = System.Security.Cryptography.X509Certificates.X509Certificate2.CreateFromPemFile(certPath, keyPath);
        }
        catch
        {
            return -1;
        }

        var listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, port);
        listener.Start();
        var handle = state.NextNetHandle++;
        state.NetListeners[handle] = listener;
        state.NetTlsCertificates[handle] = certificate;
        return handle;
    }

    public static int NetAccept(VmNetworkState state, int listenerHandle)
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
            var tlsStream = new System.Net.Security.SslStream(client.GetStream(), leaveInnerStreamOpen: false);
            try
            {
                tlsStream.AuthenticateAsServer(
                    cert,
                    clientCertificateRequired: false,
                    enabledSslProtocols: System.Security.Authentication.SslProtocols.Tls12 | System.Security.Authentication.SslProtocols.Tls13,
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

    public static string NetReadHeaders(VmNetworkState state, int connectionHandle)
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

        var headerText = System.Text.Encoding.ASCII.GetString(bytes.ToArray());
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
                headerText += System.Text.Encoding.UTF8.GetString(bodyBuffer, 0, readTotal);
            }
        }

        return headerText;
    }

    public static bool NetWrite(VmNetworkState state, int connectionHandle, string text)
    {
        if (!state.NetConnections.TryGetValue(connectionHandle, out var client))
        {
            return false;
        }

        var bytes = System.Text.Encoding.UTF8.GetBytes(text);
        Stream stream = state.NetTlsStreams.TryGetValue(connectionHandle, out var tlsStream)
            ? tlsStream
            : client.GetStream();
        stream.Write(bytes, 0, bytes.Length);
        stream.Flush();
        return true;
    }

    public static void NetClose(VmNetworkState state, int handle)
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

    public static void StdoutWriteLine(string text)
    {
        Console.WriteLine(text);
    }

    public static void ProcessExit(int code)
    {
        throw new AosProcessExitException(code);
    }

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
