namespace AiVM.Core;

public static class VmProgramLoader
{
    public static VmProgram<TValue> Load<TNode, TValue>(TNode node, IVmBytecodeAdapter<TNode, TValue> adapter)
    {
        if (!string.Equals(adapter.GetNodeKind(node), "Bytecode", StringComparison.Ordinal))
        {
            throw new VmRuntimeException("VM001", "Expected Bytecode node.", adapter.GetNodeId(node));
        }

        RequireStringAttr(adapter, node, "magic", "AIBC", "Unsupported bytecode magic.");
        RequireStringAttr(adapter, node, "format", "AiBC1", "Unsupported bytecode format.");

        var versionAttr = adapter.GetAttr(node, "version");
        if (versionAttr.Kind != VmAttrKind.Int || versionAttr.IntValue != 1)
        {
            throw new VmRuntimeException("VM001", "Unsupported bytecode version.", adapter.GetNodeId(node));
        }

        var flagsAttr = adapter.GetAttr(node, "flags");
        if (flagsAttr.Kind != VmAttrKind.Int)
        {
            throw new VmRuntimeException("VM001", "Invalid bytecode flags.", adapter.GetNodeId(node));
        }

        var constants = new List<TValue>();
        var functions = new List<VmFunction>();
        foreach (var child in adapter.GetChildren(node))
        {
            var childKind = adapter.GetNodeKind(child);
            if (string.Equals(childKind, "Const", StringComparison.Ordinal))
            {
                var kindAttr = adapter.GetAttr(child, "kind");
                if (kindAttr.Kind != VmAttrKind.Identifier)
                {
                    throw new VmRuntimeException("VM001", "Invalid Const node.", adapter.GetNodeId(child));
                }

                var valueAttr = adapter.GetAttr(child, "value");
                var kind = kindAttr.StringValue;
                constants.Add(kind switch
                {
                    "string" when valueAttr.Kind == VmAttrKind.String => adapter.FromString(valueAttr.StringValue),
                    "int" when valueAttr.Kind == VmAttrKind.Int => adapter.FromInt(valueAttr.IntValue),
                    "bool" when valueAttr.Kind == VmAttrKind.Bool => adapter.FromBool(valueAttr.BoolValue),
                    "node" when valueAttr.Kind == VmAttrKind.String => adapter.FromEncodedNodeConstant(valueAttr.StringValue, adapter.GetNodeId(child)),
                    "null" => adapter.FromNull(),
                    _ => throw new VmRuntimeException("VM001", "Unsupported constant kind.", adapter.GetNodeId(child))
                });
                continue;
            }

            if (string.Equals(childKind, "Func", StringComparison.Ordinal))
            {
                var nameAttr = adapter.GetAttr(child, "name");
                if (nameAttr.Kind != VmAttrKind.Identifier)
                {
                    throw new VmRuntimeException("VM001", "Func missing name.", adapter.GetNodeId(child));
                }

                var paramText = adapter.GetAttr(child, "params");
                var localText = adapter.GetAttr(child, "locals");
                var parameters = SplitCsv(paramText.Kind == VmAttrKind.String ? paramText.StringValue : string.Empty);
                var locals = SplitCsv(localText.Kind == VmAttrKind.String ? localText.StringValue : string.Empty);

                var instructions = new List<VmInstruction>();
                foreach (var instNode in adapter.GetChildren(child))
                {
                    if (!string.Equals(adapter.GetNodeKind(instNode), "Inst", StringComparison.Ordinal))
                    {
                        throw new VmRuntimeException("VM001", "Func contains non-instruction child.", adapter.GetNodeId(instNode));
                    }

                    var opAttr = adapter.GetAttr(instNode, "op");
                    if (opAttr.Kind != VmAttrKind.Identifier)
                    {
                        throw new VmRuntimeException("VM001", "Instruction missing op.", adapter.GetNodeId(instNode));
                    }

                    var aAttr = adapter.GetAttr(instNode, "a");
                    var bAttr = adapter.GetAttr(instNode, "b");
                    var sAttr = adapter.GetAttr(instNode, "s");
                    instructions.Add(new VmInstruction
                    {
                        Op = opAttr.StringValue,
                        A = aAttr.Kind == VmAttrKind.Int ? aAttr.IntValue : 0,
                        B = bAttr.Kind == VmAttrKind.Int ? bAttr.IntValue : 0,
                        S = sAttr.Kind == VmAttrKind.String ? sAttr.StringValue : string.Empty
                    });
                }

                functions.Add(new VmFunction
                {
                    Name = nameAttr.StringValue,
                    Params = parameters,
                    Locals = locals,
                    Instructions = instructions
                });
                continue;
            }

            throw new VmRuntimeException("VM001", "Unsupported Bytecode section.", adapter.GetNodeId(child));
        }

        var functionIndexByName = new Dictionary<string, int>(StringComparer.Ordinal);
        for (var i = 0; i < functions.Count; i++)
        {
            functionIndexByName[functions[i].Name] = i;
        }

        return new VmProgram<TValue>
        {
            Constants = constants,
            Functions = functions,
            FunctionIndexByName = functionIndexByName
        };
    }

    private static void RequireStringAttr<TNode, TValue>(
        IVmBytecodeAdapter<TNode, TValue> adapter,
        TNode node,
        string name,
        string expected,
        string error)
    {
        var attr = adapter.GetAttr(node, name);
        if (attr.Kind != VmAttrKind.String || !string.Equals(attr.StringValue, expected, StringComparison.Ordinal))
        {
            throw new VmRuntimeException("VM001", error, adapter.GetNodeId(node));
        }
    }

    private static List<string> SplitCsv(string text)
    {
        if (string.IsNullOrEmpty(text))
        {
            return new List<string>();
        }

        return text
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .ToList();
    }
}
