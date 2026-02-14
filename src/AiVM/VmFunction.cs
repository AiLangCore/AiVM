namespace AiVM.Core;

public sealed class VmFunction
{
    public required string Name { get; init; }
    public required List<string> Params { get; init; }
    public required List<string> Locals { get; init; }
    public required List<VmInstruction> Instructions { get; init; }
}
