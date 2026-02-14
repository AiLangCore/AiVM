namespace AiVM.Core;

public readonly record struct VmUdpPacket(string Host, int Port, string Data);
