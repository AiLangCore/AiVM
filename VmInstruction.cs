namespace AiVM.Core;

public sealed class VmInstruction
{
    public required string Op { get; init; }
    public int A { get; init; }
    public int B { get; init; }
    public string S { get; init; } = string.Empty;
}
