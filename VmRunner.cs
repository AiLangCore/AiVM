namespace AiVM.Core;

public static class VmRunner
{
    private sealed class VmAsyncState<TValue>
    {
        public int NextTaskHandle { get; set; } = 1;
        public Dictionary<int, TValue> CompletedTasks { get; } = new();
        public int NextParNodeId { get; set; } = 1;
    }

    private sealed class ParContext<TValue>
    {
        public required int ExpectedCount { get; init; }
        public required List<TValue> Values { get; init; }
    }

    public static TValue Run<TValue, TNode>(
        VmProgram<TValue> vm,
        int entryFunctionIndex,
        List<TValue> args,
        IVmExecutionAdapter<TValue, TNode> adapter)
    {
        return ExecuteFunction(vm, entryFunctionIndex, args, adapter, new VmAsyncState<TValue>());
    }

    private static TValue ExecuteFunction<TValue, TNode>(
        VmProgram<TValue> vm,
        int functionIndex,
        List<TValue> args,
        IVmExecutionAdapter<TValue, TNode> adapter,
        VmAsyncState<TValue> asyncState)
    {
        if (functionIndex < 0 || functionIndex >= vm.Functions.Count)
        {
            throw new VmRuntimeException("VM001", "Invalid function index.", "vm");
        }

        var function = vm.Functions[functionIndex];
        if (args.Count != function.Params.Count)
        {
            throw new VmRuntimeException("VM001", $"Arity mismatch for {function.Name}.", function.Name);
        }

        var locals = new TValue[function.Locals.Count];
        for (var i = 0; i < locals.Length; i++)
        {
            locals[i] = adapter.UnknownValue;
        }
        for (var i = 0; i < function.Params.Count; i++)
        {
            locals[i] = args[i];
        }

        var stack = new List<TValue>();
        var parStack = new Stack<ParContext<TValue>>();
        var pc = 0;
        while (pc < function.Instructions.Count)
        {
            var inst = function.Instructions[pc];
            adapter.TraceInstruction(function.Name, pc, inst.Op);
            switch (inst.Op)
            {
                case "CONST":
                    if (inst.A < 0 || inst.A >= vm.Constants.Count)
                    {
                        throw new VmRuntimeException("VM001", "Invalid CONST index.", function.Name);
                    }
                    stack.Add(vm.Constants[inst.A]);
                    pc++;
                    break;
                case "LOAD_LOCAL":
                    if (inst.A < 0 || inst.A >= locals.Length)
                    {
                        throw new VmRuntimeException("VM001", "Invalid local slot.", function.Name);
                    }
                    stack.Add(locals[inst.A]);
                    pc++;
                    break;
                case "STORE_LOCAL":
                    if (inst.A < 0 || inst.A >= locals.Length)
                    {
                        throw new VmRuntimeException("VM001", "Invalid local slot.", function.Name);
                    }
                    locals[inst.A] = Pop(stack, function.Name);
                    pc++;
                    break;
                case "POP":
                    _ = Pop(stack, function.Name);
                    pc++;
                    break;
                case "EQ":
                {
                    var right = Pop(stack, function.Name);
                    var left = Pop(stack, function.Name);
                    stack.Add(adapter.FromBool(adapter.ValueEquals(left, right)));
                    pc++;
                    break;
                }
                case "ADD_INT":
                {
                    var right = Pop(stack, function.Name);
                    var left = Pop(stack, function.Name);
                    if (!adapter.TryGetInt(left, out var l) || !adapter.TryGetInt(right, out var r))
                    {
                        throw new VmRuntimeException("VM001", "ADD_INT requires int operands.", function.Name);
                    }
                    stack.Add(adapter.FromInt(l + r));
                    pc++;
                    break;
                }
                case "STR_CONCAT":
                {
                    var right = Pop(stack, function.Name);
                    var left = Pop(stack, function.Name);
                    if (!adapter.TryGetString(left, out var l) || !adapter.TryGetString(right, out var r) || l is null || r is null)
                    {
                        throw new VmRuntimeException("VM001", "STR_CONCAT requires string operands.", function.Name);
                    }
                    stack.Add(adapter.FromString(l + r));
                    pc++;
                    break;
                }
                case "TO_STRING":
                {
                    var value = Pop(stack, function.Name);
                    stack.Add(adapter.FromString(adapter.ValueToDisplayString(value)));
                    pc++;
                    break;
                }
                case "STR_ESCAPE":
                {
                    var value = Pop(stack, function.Name);
                    if (!adapter.TryGetString(value, out var text) || text is null)
                    {
                        throw new VmRuntimeException("VM001", "STR_ESCAPE requires string operand.", function.Name);
                    }
                    stack.Add(adapter.FromString(adapter.EscapeString(text)));
                    pc++;
                    break;
                }
                case "NODE_KIND":
                {
                    var value = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(value, out var node) || node is null)
                    {
                        throw new VmRuntimeException("VM001", "NODE_KIND requires node operand.", function.Name);
                    }
                    stack.Add(adapter.FromString(adapter.NodeKind(node)));
                    pc++;
                    break;
                }
                case "NODE_ID":
                {
                    var value = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(value, out var node) || node is null)
                    {
                        throw new VmRuntimeException("VM001", "NODE_ID requires node operand.", function.Name);
                    }
                    stack.Add(adapter.FromString(adapter.NodeId(node)));
                    pc++;
                    break;
                }
                case "ATTR_COUNT":
                {
                    var value = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(value, out var node) || node is null)
                    {
                        throw new VmRuntimeException("VM001", "ATTR_COUNT requires node operand.", function.Name);
                    }
                    stack.Add(adapter.FromInt(adapter.OrderedAttrs(node).Count));
                    pc++;
                    break;
                }
                case "ATTR_KEY":
                {
                    var indexValue = Pop(stack, function.Name);
                    var nodeValue = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(nodeValue, out var node) || node is null || !adapter.TryGetInt(indexValue, out var index))
                    {
                        throw new VmRuntimeException("VM001", "ATTR_KEY requires (node,int).", function.Name);
                    }
                    var attrs = adapter.OrderedAttrs(node);
                    stack.Add(adapter.FromString(index >= 0 && index < attrs.Count ? attrs[index].Key : string.Empty));
                    pc++;
                    break;
                }
                case "ATTR_VALUE_KIND":
                {
                    var indexValue = Pop(stack, function.Name);
                    var nodeValue = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(nodeValue, out var node) || node is null || !adapter.TryGetInt(indexValue, out var index))
                    {
                        throw new VmRuntimeException("VM001", "ATTR_VALUE_KIND requires (node,int).", function.Name);
                    }
                    var attrs = adapter.OrderedAttrs(node);
                    var kind = index >= 0 && index < attrs.Count ? AttrKindText(attrs[index].Value.Kind) : string.Empty;
                    stack.Add(adapter.FromString(kind));
                    pc++;
                    break;
                }
                case "ATTR_VALUE_STRING":
                {
                    var indexValue = Pop(stack, function.Name);
                    var nodeValue = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(nodeValue, out var node) || node is null || !adapter.TryGetInt(indexValue, out var index))
                    {
                        throw new VmRuntimeException("VM001", "ATTR_VALUE_STRING requires (node,int).", function.Name);
                    }
                    var attrs = adapter.OrderedAttrs(node);
                    if (index < 0 || index >= attrs.Count)
                    {
                        stack.Add(adapter.FromString(string.Empty));
                    }
                    else
                    {
                        var attr = attrs[index].Value;
                        stack.Add(attr.Kind is VmAttrKind.String or VmAttrKind.Identifier
                            ? adapter.FromString(attr.StringValue)
                            : adapter.FromString(string.Empty));
                    }
                    pc++;
                    break;
                }
                case "ATTR_VALUE_INT":
                {
                    var indexValue = Pop(stack, function.Name);
                    var nodeValue = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(nodeValue, out var node) || node is null || !adapter.TryGetInt(indexValue, out var index))
                    {
                        throw new VmRuntimeException("VM001", "ATTR_VALUE_INT requires (node,int).", function.Name);
                    }
                    var attrs = adapter.OrderedAttrs(node);
                    stack.Add(index < 0 || index >= attrs.Count || attrs[index].Value.Kind != VmAttrKind.Int
                        ? adapter.FromInt(0)
                        : adapter.FromInt(attrs[index].Value.IntValue));
                    pc++;
                    break;
                }
                case "ATTR_VALUE_BOOL":
                {
                    var indexValue = Pop(stack, function.Name);
                    var nodeValue = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(nodeValue, out var node) || node is null || !adapter.TryGetInt(indexValue, out var index))
                    {
                        throw new VmRuntimeException("VM001", "ATTR_VALUE_BOOL requires (node,int).", function.Name);
                    }
                    var attrs = adapter.OrderedAttrs(node);
                    stack.Add(index < 0 || index >= attrs.Count || attrs[index].Value.Kind != VmAttrKind.Bool
                        ? adapter.FromBool(false)
                        : adapter.FromBool(attrs[index].Value.BoolValue));
                    pc++;
                    break;
                }
                case "CHILD_COUNT":
                {
                    var value = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(value, out var node) || node is null)
                    {
                        throw new VmRuntimeException("VM001", "CHILD_COUNT requires node operand.", function.Name);
                    }
                    stack.Add(adapter.FromInt(adapter.NodeChildren(node).Count));
                    pc++;
                    break;
                }
                case "CHILD_AT":
                {
                    var indexValue = Pop(stack, function.Name);
                    var nodeValue = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(nodeValue, out var node) || node is null || !adapter.TryGetInt(indexValue, out var index))
                    {
                        throw new VmRuntimeException("VM001", "CHILD_AT requires (node,int).", function.Name);
                    }
                    var children = adapter.NodeChildren(node);
                    if (index < 0 || index >= children.Count)
                    {
                        throw new VmRuntimeException("VM001", "CHILD_AT index out of range.", function.Name);
                    }
                    stack.Add(adapter.FromNode(children[index]));
                    pc++;
                    break;
                }
                case "MAKE_BLOCK":
                {
                    var idValue = Pop(stack, function.Name);
                    if (!adapter.TryGetString(idValue, out var id) || id is null)
                    {
                        throw new VmRuntimeException("VM001", "MAKE_BLOCK requires string id.", function.Name);
                    }
                    stack.Add(adapter.FromNode(adapter.CreateNode(
                        "Block",
                        id,
                        new Dictionary<string, VmAttr>(StringComparer.Ordinal),
                        new List<TNode>())));
                    pc++;
                    break;
                }
                case "APPEND_CHILD":
                {
                    var childValue = Pop(stack, function.Name);
                    var nodeValue = Pop(stack, function.Name);
                    if (!adapter.TryGetNode(nodeValue, out var baseNode) || baseNode is null ||
                        !adapter.TryGetNode(childValue, out var childNode) || childNode is null)
                    {
                        throw new VmRuntimeException("VM001", "APPEND_CHILD requires (node,node).", function.Name);
                    }
                    var attrs = adapter.OrderedAttrs(baseNode)
                        .ToDictionary(x => x.Key, x => x.Value, StringComparer.Ordinal);
                    var children = new List<TNode>(adapter.NodeChildren(baseNode)) { childNode };
                    stack.Add(adapter.FromNode(adapter.CreateNode(
                        adapter.NodeKind(baseNode),
                        adapter.NodeId(baseNode),
                        attrs,
                        children)));
                    pc++;
                    break;
                }
                case "MAKE_ERR":
                {
                    var nodeIdValue = Pop(stack, function.Name);
                    var messageValue = Pop(stack, function.Name);
                    var codeValue = Pop(stack, function.Name);
                    var idValue = Pop(stack, function.Name);
                    if (!adapter.TryGetString(idValue, out var id) || id is null ||
                        !adapter.TryGetString(codeValue, out var code) || code is null ||
                        !adapter.TryGetString(messageValue, out var message) || message is null ||
                        !adapter.TryGetString(nodeIdValue, out var nodeId) || nodeId is null)
                    {
                        throw new VmRuntimeException("VM001", "MAKE_ERR requires (string,string,string,string).", function.Name);
                    }

                    stack.Add(adapter.FromNode(adapter.CreateNode(
                        "Err",
                        id,
                        new Dictionary<string, VmAttr>(StringComparer.Ordinal)
                        {
                            ["code"] = VmAttr.Identifier(code),
                            ["message"] = VmAttr.String(message),
                            ["nodeId"] = VmAttr.Identifier(nodeId)
                        },
                        new List<TNode>())));
                    pc++;
                    break;
                }
                case "MAKE_LIT_STRING":
                {
                    var textValue = Pop(stack, function.Name);
                    var idValue = Pop(stack, function.Name);
                    if (!adapter.TryGetString(idValue, out var id) || id is null ||
                        !adapter.TryGetString(textValue, out var text) || text is null)
                    {
                        throw new VmRuntimeException("VM001", "MAKE_LIT_STRING requires (string,string).", function.Name);
                    }
                    stack.Add(adapter.FromNode(adapter.CreateNode(
                        "Lit",
                        id,
                        new Dictionary<string, VmAttr>(StringComparer.Ordinal)
                        {
                            ["value"] = VmAttr.String(text)
                        },
                        new List<TNode>())));
                    pc++;
                    break;
                }
                case "MAKE_NODE":
                {
                    if (inst.A < 0 || inst.A >= vm.Constants.Count)
                    {
                        throw new VmRuntimeException("VM001", "Invalid MAKE_NODE template index.", function.Name);
                    }

                    var templateValue = vm.Constants[inst.A];
                    if (!adapter.TryGetNode(templateValue, out var templateNode) || templateNode is null)
                    {
                        throw new VmRuntimeException("VM001", "MAKE_NODE template must be node constant.", function.Name);
                    }

                    var argsForNode = PopArgs(stack, inst.B, function.Name);
                    var attrs = adapter.OrderedAttrs(templateNode)
                        .ToDictionary(x => x.Key, x => x.Value, StringComparer.Ordinal);
                    var children = new List<TNode>(argsForNode.Count);
                    foreach (var arg in argsForNode)
                    {
                        children.Add(adapter.ValueToNode(arg));
                    }
                    stack.Add(adapter.FromNode(adapter.CreateNode(
                        adapter.NodeKind(templateNode),
                        adapter.NodeId(templateNode),
                        attrs,
                        children)));
                    pc++;
                    break;
                }
                case "JUMP":
                    pc = inst.A;
                    break;
                case "JUMP_IF_FALSE":
                {
                    var cond = Pop(stack, function.Name);
                    if (!adapter.TryGetBool(cond, out var isTrue))
                    {
                        throw new VmRuntimeException("VM001", "JUMP_IF_FALSE requires bool.", function.Name);
                    }
                    pc = isTrue ? pc + 1 : inst.A;
                    break;
                }
                case "CALL":
                {
                    var callArgs = PopArgs(stack, inst.B, function.Name);
                    var result = ExecuteFunction(vm, inst.A, callArgs, adapter, asyncState);
                    stack.Add(result);
                    pc++;
                    break;
                }
                case "ASYNC_CALL":
                {
                    var callArgs = PopArgs(stack, inst.B, function.Name);
                    var result = ExecuteFunction(vm, inst.A, callArgs, adapter, asyncState);
                    var handle = asyncState.NextTaskHandle++;
                    asyncState.CompletedTasks[handle] = result;
                    stack.Add(adapter.FromInt(handle));
                    pc++;
                    break;
                }
                case "AWAIT":
                {
                    var handleValue = Pop(stack, function.Name);
                    if (!adapter.TryGetInt(handleValue, out var handle) ||
                        !asyncState.CompletedTasks.TryGetValue(handle, out var result))
                    {
                        throw new VmRuntimeException("VM001", "AWAIT requires valid task handle.", function.Name);
                    }
                    stack.Add(result);
                    pc++;
                    break;
                }
                case "PAR_BEGIN":
                {
                    if (inst.A < 0)
                    {
                        throw new VmRuntimeException("VM001", "PAR_BEGIN requires non-negative branch count.", function.Name);
                    }
                    parStack.Push(new ParContext<TValue>
                    {
                        ExpectedCount = inst.A,
                        Values = new List<TValue>(inst.A)
                    });
                    pc++;
                    break;
                }
                case "PAR_FORK":
                {
                    if (parStack.Count == 0)
                    {
                        throw new VmRuntimeException("VM001", "PAR_FORK requires active Par context.", function.Name);
                    }
                    var value = Pop(stack, function.Name);
                    parStack.Peek().Values.Add(value);
                    pc++;
                    break;
                }
                case "PAR_JOIN":
                {
                    if (parStack.Count == 0)
                    {
                        throw new VmRuntimeException("VM001", "PAR_JOIN requires active Par context.", function.Name);
                    }
                    var par = parStack.Pop();
                    if (par.ExpectedCount != inst.A || par.Values.Count != inst.A)
                    {
                        throw new VmRuntimeException("VM001", "PAR_JOIN branch count mismatch.", function.Name);
                    }
                    var children = new List<TNode>(par.Values.Count);
                    foreach (var value in par.Values)
                    {
                        children.Add(adapter.ValueToNode(value));
                    }
                    var id = $"par_{asyncState.NextParNodeId++}";
                    stack.Add(adapter.FromNode(adapter.CreateNode(
                        "Block",
                        id,
                        new Dictionary<string, VmAttr>(StringComparer.Ordinal),
                        children)));
                    pc++;
                    break;
                }
                case "PAR_CANCEL":
                    // Deterministic no-op for current structured async model.
                    pc++;
                    break;
                case "CALL_SYS":
                {
                    var callArgs = PopArgs(stack, inst.A, function.Name);
                    var result = adapter.ExecuteCall(inst.S, callArgs);
                    if (adapter.IsUnknown(result))
                    {
                        throw new VmRuntimeException("VM001", $"Unsupported call target in bytecode mode: {inst.S}.", inst.S);
                    }
                    stack.Add(result);
                    pc++;
                    break;
                }
                case "RETURN":
                    return stack.Count == 0 ? adapter.VoidValue : Pop(stack, function.Name);
                default:
                    throw new VmRuntimeException("VM001", $"Unsupported opcode: {inst.Op}.", function.Name);
            }
        }

        return adapter.VoidValue;
    }

    private static string AttrKindText(VmAttrKind kind)
    {
        return kind switch
        {
            VmAttrKind.Identifier => "identifier",
            VmAttrKind.String => "string",
            VmAttrKind.Int => "int",
            VmAttrKind.Bool => "bool",
            _ => string.Empty
        };
    }

    private static TValue Pop<TValue>(List<TValue> stack, string nodeId)
    {
        if (stack.Count == 0)
        {
            throw new VmRuntimeException("VM001", "Stack underflow.", nodeId);
        }
        var index = stack.Count - 1;
        var value = stack[index];
        stack.RemoveAt(index);
        return value;
    }

    private static List<TValue> PopArgs<TValue>(List<TValue> stack, int argc, string nodeId)
    {
        if (argc < 0 || stack.Count < argc)
        {
            throw new VmRuntimeException("VM001", "Invalid call argument count.", nodeId);
        }

        var args = new List<TValue>(argc);
        for (var i = 0; i < argc; i++)
        {
            args.Add(Pop(stack, nodeId));
        }
        args.Reverse();
        return args;
    }
}
