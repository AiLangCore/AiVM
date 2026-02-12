namespace AiVM.Core;

public enum VmAttrKind
{
    Missing = 0,
    Identifier = 1,
    String = 2,
    Int = 3,
    Bool = 4
}

public readonly record struct VmAttr(VmAttrKind Kind, string StringValue, int IntValue, bool BoolValue)
{
    public static VmAttr Missing() => new(VmAttrKind.Missing, string.Empty, 0, false);
    public static VmAttr Identifier(string value) => new(VmAttrKind.Identifier, value, 0, false);
    public static VmAttr String(string value) => new(VmAttrKind.String, value, 0, false);
    public static VmAttr Int(int value) => new(VmAttrKind.Int, string.Empty, value, false);
    public static VmAttr Bool(bool value) => new(VmAttrKind.Bool, string.Empty, 0, value);
}

public interface IVmBytecodeAdapter<TNode, TValue>
{
    string GetNodeKind(TNode node);
    string GetNodeId(TNode node);
    IEnumerable<TNode> GetChildren(TNode node);
    VmAttr GetAttr(TNode node, string key);

    TValue FromString(string value);
    TValue FromInt(int value);
    TValue FromBool(bool value);
    TValue FromNull();
    TValue FromEncodedNodeConstant(string encodedNode, string nodeId);
}
