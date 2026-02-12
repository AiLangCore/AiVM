namespace AiVM.Core;

public sealed class VmNetworkState
{
    public Dictionary<int, System.Net.Sockets.TcpListener> NetListeners { get; } = new();
    public Dictionary<int, System.Net.Sockets.TcpClient> NetConnections { get; } = new();
    public Dictionary<int, System.Security.Cryptography.X509Certificates.X509Certificate2> NetTlsCertificates { get; } = new();
    public Dictionary<int, System.Net.Security.SslStream> NetTlsStreams { get; } = new();
    public int NextNetHandle { get; set; } = 1;
}
