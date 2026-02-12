namespace AiVM.Core;

public static class VmCapabilityDispatcher
{
    public static bool TryInvoke(string target, IReadOnlyList<SysValue> args, out SysValue result)
    {
        result = SysValue.Unknown();
        switch (target)
        {
            case "console.print":
                if (!TryGetString(args, 0, 1, out var consoleText))
                {
                    return true;
                }
                VmSyscalls.ConsolePrintLine(consoleText);
                result = SysValue.Void();
                return true;

            case "io.print":
                if (!TryGetString(args, 0, 1, out var printText))
                {
                    return true;
                }
                VmSyscalls.IoPrint(printText);
                result = SysValue.Void();
                return true;

            case "io.write":
                if (!TryGetString(args, 0, 1, out var writeText))
                {
                    return true;
                }
                VmSyscalls.IoWrite(writeText);
                result = SysValue.Void();
                return true;

            case "io.readLine":
                if (args.Count != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.IoReadLine());
                return true;

            case "io.readAllStdin":
                if (args.Count != 0)
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.IoReadAllStdin());
                return true;

            case "io.readFile":
                if (!TryGetString(args, 0, 1, out var readPath))
                {
                    return true;
                }
                result = SysValue.String(VmSyscalls.IoReadFile(readPath));
                return true;

            case "io.fileExists":
                if (!TryGetString(args, 0, 1, out var fileExistsPath))
                {
                    return true;
                }
                result = SysValue.Bool(VmSyscalls.IoFileExists(fileExistsPath));
                return true;

            case "io.pathExists":
                if (!TryGetString(args, 0, 1, out var pathExistsPath))
                {
                    return true;
                }
                result = SysValue.Bool(VmSyscalls.IoPathExists(pathExistsPath));
                return true;

            case "io.makeDir":
                if (!TryGetString(args, 0, 1, out var makeDirPath))
                {
                    return true;
                }
                VmSyscalls.IoMakeDir(makeDirPath);
                result = SysValue.Void();
                return true;

            case "io.writeFile":
                if (!TryGetString(args, 0, 2, out var filePath) || !TryGetString(args, 1, 2, out var fileText))
                {
                    return true;
                }
                VmSyscalls.IoWriteFile(filePath, fileText);
                result = SysValue.Void();
                return true;

            default:
                return false;
        }
    }

    private static bool TryGetString(IReadOnlyList<SysValue> args, int index, int expectedCount, out string value)
    {
        value = string.Empty;
        if (args.Count != expectedCount)
        {
            return false;
        }
        if (args[index].Kind != VmValueKind.String)
        {
            return false;
        }
        value = args[index].StringValue;
        return true;
    }
}
