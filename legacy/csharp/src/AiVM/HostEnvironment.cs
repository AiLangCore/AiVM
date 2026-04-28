namespace AiVM.Core;

public static class HostEnvironment
{
    public static string BaseDirectory => AppContext.BaseDirectory;

    public static string? ProcessPath => Environment.ProcessPath;

    public static string? GetEnvironmentVariable(string name) => Environment.GetEnvironmentVariable(name);
}
