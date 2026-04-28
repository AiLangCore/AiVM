namespace AiVM.Core;

public sealed class VmRuntimeException : Exception
{
    public VmRuntimeException(string code, string message, string nodeId)
        : base(message)
    {
        Code = code;
        NodeId = nodeId;
    }

    public string Code { get; }

    public string NodeId { get; }
}
