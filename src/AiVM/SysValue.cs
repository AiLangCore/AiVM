namespace AiVM.Core;

public readonly record struct SysValue(VmValueKind Kind, string StringValue, int IntValue, bool BoolValue)
{
    public static SysValue Unknown() => new(VmValueKind.Unknown, string.Empty, 0, false);
    public static SysValue Void() => new(VmValueKind.Void, string.Empty, 0, false);
    public static SysValue String(string value) => new(VmValueKind.String, value, 0, false);
    public static SysValue Int(int value) => new(VmValueKind.Int, string.Empty, value, false);
    public static SysValue Bool(bool value) => new(VmValueKind.Bool, string.Empty, 0, value);
}
