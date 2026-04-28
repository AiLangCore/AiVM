namespace AiVM.Core;

public static class HostExecutableLocator
{
    public static string? ResolveHostBinaryPath()
    {
        var processPath = Environment.ProcessPath;
        if (!string.IsNullOrWhiteSpace(processPath) && File.Exists(processPath))
        {
            return processPath;
        }

        var candidates = new[]
        {
            Path.Combine(AppContext.BaseDirectory, "airun"),
            Path.Combine(Directory.GetCurrentDirectory(), "tools", "airun")
        };

        foreach (var candidate in candidates)
        {
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return null;
    }
}
