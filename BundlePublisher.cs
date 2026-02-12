namespace AiVM.Core;

public static class BundlePublisher
{
    public static bool TryWriteEmbeddedBytecodeExecutable(
        string sourceBinary,
        string outputBinaryPath,
        string bytecodeText,
        out string error)
    {
        error = string.Empty;
        try
        {
            File.Copy(sourceBinary, outputBinaryPath, overwrite: true);
            File.AppendAllText(outputBinaryPath, "\n--AIBUNDLE1:BYTECODE--\n" + bytecodeText);
            if (!OperatingSystem.IsWindows())
            {
                try
                {
                    File.SetUnixFileMode(
                        outputBinaryPath,
                        UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute |
                        UnixFileMode.GroupRead | UnixFileMode.GroupExecute |
                        UnixFileMode.OtherRead | UnixFileMode.OtherExecute);
                }
                catch
                {
                    // Best-effort on non-Unix platforms.
                }
            }

            return true;
        }
        catch (Exception ex)
        {
            error = ex.Message;
            return false;
        }
    }
}
