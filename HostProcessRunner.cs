using System.Diagnostics;

namespace AiVM.Core;

public static class HostProcessRunner
{
    public sealed record ProcessResult(int ExitCode, byte[] Stdout, string Stderr);

    public static ProcessResult? RunWithStdIn(string fileName, string arguments, string stdin)
    {
        var psi = new ProcessStartInfo
        {
            FileName = fileName,
            Arguments = arguments,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };

        using var process = Process.Start(psi);
        if (process is null)
        {
            return null;
        }

        process.StandardInput.Write(stdin);
        process.StandardInput.Close();

        var stderrTask = process.StandardError.ReadToEndAsync();
        using var stdout = new MemoryStream();
        process.StandardOutput.BaseStream.CopyTo(stdout);
        process.WaitForExit();
        var stderr = stderrTask.GetAwaiter().GetResult();
        return new ProcessResult(process.ExitCode, stdout.ToArray(), stderr);
    }
}
