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

    public static ProcessResult? Run(
        string fileName,
        IEnumerable<string> arguments,
        string? workingDirectory = null,
        string? stdin = null)
    {
        var psi = new ProcessStartInfo
        {
            FileName = fileName,
            RedirectStandardInput = stdin is not null,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };

        if (!string.IsNullOrEmpty(workingDirectory))
        {
            psi.WorkingDirectory = workingDirectory;
        }

        foreach (var arg in arguments)
        {
            psi.ArgumentList.Add(arg);
        }

        using var process = Process.Start(psi);
        if (process is null)
        {
            return null;
        }

        if (stdin is not null)
        {
            process.StandardInput.Write(stdin);
            process.StandardInput.Close();
        }

        var stderrTask = process.StandardError.ReadToEndAsync();
        using var stdout = new MemoryStream();
        process.StandardOutput.BaseStream.CopyTo(stdout);
        process.WaitForExit();
        var stderr = stderrTask.GetAwaiter().GetResult();
        return new ProcessResult(process.ExitCode, stdout.ToArray(), stderr);
    }
}
