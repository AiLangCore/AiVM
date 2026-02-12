namespace AiVM.Core;

public static class HostFileSystem
{
    public static string Combine(params string[] parts) => Path.Combine(parts);

    public static string GetFullPath(string path) => Path.GetFullPath(path);

    public static bool IsPathRooted(string path) => Path.IsPathRooted(path);

    public static bool FileExists(string path) => File.Exists(path);

    public static bool DirectoryExists(string path) => Directory.Exists(path);

    public static void EnsureDirectory(string path) => Directory.CreateDirectory(path);

    public static string ReadAllText(string path) => File.ReadAllText(path);

    public static void WriteAllText(string path, string text) => File.WriteAllText(path, text);

    public static string? GetDirectoryName(string path) => Path.GetDirectoryName(path);
}
