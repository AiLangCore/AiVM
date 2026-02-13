namespace AiVM.Core;

public enum VmValueKind
{
    Unknown = 0,
    String = 1,
    Int = 2,
    Bool = 3,
    Node = 4,
    Void = 5
}

public static class SyscallContracts
{
    public static bool IsSysTarget(string target) => target.StartsWith("sys.", StringComparison.Ordinal);

    public static bool TryValidate(
        string target,
        IReadOnlyList<VmValueKind> argKinds,
        Action<string, string> addDiagnostic,
        out VmValueKind returnKind)
    {
        returnKind = VmValueKind.Unknown;
        switch (target)
        {
            case "sys.net_listen":
                ValidateArityAndType(argKinds, 1, VmValueKind.Int, "VAL123", "sys.net_listen expects 1 argument.", "VAL124", "sys.net_listen arg must be int.", addDiagnostic);
                returnKind = VmValueKind.Int;
                return true;
            case "sys.net_listen_tls":
                ValidateArityAndTypes(
                    argKinds,
                    3,
                    new[]
                    {
                        (VmValueKind.Int, "VAL141", "sys.net_listen_tls arg 1 must be int."),
                        (VmValueKind.String, "VAL142", "sys.net_listen_tls arg 2 must be string."),
                        (VmValueKind.String, "VAL143", "sys.net_listen_tls arg 3 must be string.")
                    },
                    "VAL140",
                    "sys.net_listen_tls expects 3 arguments.",
                    addDiagnostic);
                returnKind = VmValueKind.Int;
                return true;
            case "sys.net_accept":
                ValidateArityAndType(argKinds, 1, VmValueKind.Int, "VAL125", "sys.net_accept expects 1 argument.", "VAL126", "sys.net_accept arg must be int.", addDiagnostic);
                returnKind = VmValueKind.Int;
                return true;
            case "sys.net_readHeaders":
                ValidateArityAndType(argKinds, 1, VmValueKind.Int, "VAL127", "sys.net_readHeaders expects 1 argument.", "VAL128", "sys.net_readHeaders arg must be int.", addDiagnostic);
                returnKind = VmValueKind.String;
                return true;
            case "sys.net_write":
                ValidateArityAndTypes(
                    argKinds,
                    2,
                    new[]
                    {
                        (VmValueKind.Int, "VAL130", "sys.net_write arg 1 must be int."),
                        (VmValueKind.String, "VAL131", "sys.net_write arg 2 must be string.")
                    },
                    "VAL129",
                    "sys.net_write expects 2 arguments.",
                    addDiagnostic);
                returnKind = VmValueKind.Void;
                return true;
            case "sys.net_close":
                ValidateArityAndType(argKinds, 1, VmValueKind.Int, "VAL132", "sys.net_close expects 1 argument.", "VAL133", "sys.net_close arg must be int.", addDiagnostic);
                returnKind = VmValueKind.Void;
                return true;
            case "sys.stdout_writeLine":
                ValidateArityAndType(argKinds, 1, VmValueKind.String, "VAL134", "sys.stdout_writeLine expects 1 argument.", "VAL135", "sys.stdout_writeLine arg must be string.", addDiagnostic);
                returnKind = VmValueKind.Void;
                return true;
            case "sys.proc_exit":
                ValidateArityAndType(argKinds, 1, VmValueKind.Int, "VAL136", "sys.proc_exit expects 1 argument.", "VAL137", "sys.proc_exit arg must be int.", addDiagnostic);
                returnKind = VmValueKind.Void;
                return true;
            case "sys.fs_readFile":
                ValidateArityAndType(argKinds, 1, VmValueKind.String, "VAL138", "sys.fs_readFile expects 1 argument.", "VAL139", "sys.fs_readFile arg must be string.", addDiagnostic);
                returnKind = VmValueKind.String;
                return true;
            case "sys.fs_fileExists":
                ValidateArityAndType(argKinds, 1, VmValueKind.String, "VAL140", "sys.fs_fileExists expects 1 argument.", "VAL141", "sys.fs_fileExists arg must be string.", addDiagnostic);
                returnKind = VmValueKind.Bool;
                return true;
            case "sys.str_utf8ByteCount":
                ValidateArityAndType(argKinds, 1, VmValueKind.String, "VAL142", "sys.str_utf8ByteCount expects 1 argument.", "VAL143", "sys.str_utf8ByteCount arg must be string.", addDiagnostic);
                returnKind = VmValueKind.Int;
                return true;
            case "sys.http_get":
                ValidateArityAndType(argKinds, 1, VmValueKind.String, "VAL148", "sys.http_get expects 1 argument.", "VAL149", "sys.http_get arg must be string.", addDiagnostic);
                returnKind = VmValueKind.String;
                return true;
            case "sys.platform":
                ValidateArity(argKinds, 0, "VAL150", "sys.platform expects 0 arguments.", addDiagnostic);
                returnKind = VmValueKind.String;
                return true;
            case "sys.arch":
                ValidateArity(argKinds, 0, "VAL151", "sys.arch expects 0 arguments.", addDiagnostic);
                returnKind = VmValueKind.String;
                return true;
            case "sys.os_version":
                ValidateArity(argKinds, 0, "VAL152", "sys.os_version expects 0 arguments.", addDiagnostic);
                returnKind = VmValueKind.String;
                return true;
            case "sys.runtime":
                ValidateArity(argKinds, 0, "VAL153", "sys.runtime expects 0 arguments.", addDiagnostic);
                returnKind = VmValueKind.String;
                return true;
            case "sys.vm_run":
                ValidateArityAndTypes(
                    argKinds,
                    3,
                    new[]
                    {
                        (VmValueKind.Node, "VAL145", "sys.vm_run arg 1 must be node."),
                        (VmValueKind.String, "VAL146", "sys.vm_run arg 2 must be string."),
                        (VmValueKind.Node, "VAL147", "sys.vm_run arg 3 must be node.")
                    },
                    "VAL144",
                    "sys.vm_run expects 3 arguments.",
                    addDiagnostic);
                returnKind = VmValueKind.Unknown;
                return true;
            default:
                return false;
        }
    }

    private static bool IsCompatible(VmValueKind actual, VmValueKind expected)
        => actual == expected || actual == VmValueKind.Unknown;

    private static void ValidateArity(
        IReadOnlyList<VmValueKind> argKinds,
        int expectedArity,
        string arityCode,
        string arityMessage,
        Action<string, string> addDiagnostic)
    {
        if (argKinds.Count != expectedArity)
        {
            addDiagnostic(arityCode, arityMessage);
        }
    }

    private static void ValidateArityAndType(
        IReadOnlyList<VmValueKind> argKinds,
        int expectedArity,
        VmValueKind expectedType,
        string arityCode,
        string arityMessage,
        string typeCode,
        string typeMessage,
        Action<string, string> addDiagnostic)
    {
        if (argKinds.Count != expectedArity)
        {
            addDiagnostic(arityCode, arityMessage);
            return;
        }

        if (!IsCompatible(argKinds[0], expectedType))
        {
            addDiagnostic(typeCode, typeMessage);
        }
    }

    private static void ValidateArityAndTypes(
        IReadOnlyList<VmValueKind> argKinds,
        int expectedArity,
        (VmValueKind type, string code, string message)[] expectedTypes,
        string arityCode,
        string arityMessage,
        Action<string, string> addDiagnostic)
    {
        if (argKinds.Count != expectedArity)
        {
            addDiagnostic(arityCode, arityMessage);
            return;
        }

        for (var i = 0; i < expectedTypes.Length; i++)
        {
            if (!IsCompatible(argKinds[i], expectedTypes[i].type))
            {
                addDiagnostic(expectedTypes[i].code, expectedTypes[i].message);
            }
        }
    }
}
