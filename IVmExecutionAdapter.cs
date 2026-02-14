namespace AiVM.Core;

public interface IVmExecutionAdapter<TValue, TNode>
{
    TValue VoidValue { get; }
    TValue UnknownValue { get; }

    bool IsUnknown(TValue value);
    TValue FromBool(bool value);
    TValue FromInt(int value);
    TValue FromString(string value);
    TValue FromNode(TNode node);

    bool TryGetBool(TValue value, out bool result);
    bool TryGetInt(TValue value, out int result);
    bool TryGetString(TValue value, out string? result);
    bool TryGetNode(TValue value, out TNode? node);

    bool ValueEquals(TValue left, TValue right);
    string ValueToDisplayString(TValue value);
    string EscapeString(string value);

    string NodeKind(TNode node);
    string NodeId(TNode node);
    IReadOnlyList<KeyValuePair<string, VmAttr>> OrderedAttrs(TNode node);
    IReadOnlyList<TNode> NodeChildren(TNode node);
    TNode CreateNode(string kind, string id, IReadOnlyDictionary<string, VmAttr> attrs, IReadOnlyList<TNode> children);
    TNode ValueToNode(TValue value);

    bool TryExecuteSyscall(SyscallId id, ReadOnlySpan<TValue> args, out TValue result);

    void TraceInstruction(string functionName, int pc, string op)
    {
    }
}
