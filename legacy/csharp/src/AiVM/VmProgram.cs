namespace AiVM.Core;

public sealed class VmProgram<TValue>
{
    public required List<TValue> Constants { get; init; }
    public required List<VmFunction> Functions { get; init; }
    public required Dictionary<string, int> FunctionIndexByName { get; init; }
}
