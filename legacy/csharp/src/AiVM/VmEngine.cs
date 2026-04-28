namespace AiVM.Core;

public static class VmEngine
{
    public static TValue Run<TNode, TValue>(
        VmProgram<TValue> vm,
        string entryName,
        List<TValue> args,
        IVmExecutionAdapter<TValue, TNode> executionAdapter)
    {
        if (!vm.FunctionIndexByName.TryGetValue(entryName, out var entryIndex))
        {
            throw new VmRuntimeException("VM001", $"Entry function not found: {entryName}.", entryName);
        }

        return VmRunner.Run(vm, entryIndex, args, executionAdapter);
    }

    public static TValue Run<TNode, TValue>(
        TNode bytecodeNode,
        string entryName,
        List<TValue> args,
        IVmBytecodeAdapter<TNode, TValue> bytecodeAdapter,
        IVmExecutionAdapter<TValue, TNode> executionAdapter)
    {
        var vm = VmProgramLoader.Load(bytecodeNode, bytecodeAdapter);
        return Run(vm, entryName, args, executionAdapter);
    }
}
