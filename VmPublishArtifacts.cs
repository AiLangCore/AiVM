namespace AiVM.Core;

public static class VmPublishArtifacts
{
    public static bool TryWriteLibrary(string publishDir, string libraryPath, string canonicalProgramText, out string errorMessage)
    {
        try
        {
            HostFileSystem.EnsureDirectory(publishDir);
            HostFileSystem.WriteAllText(libraryPath, canonicalProgramText);
            errorMessage = string.Empty;
            return true;
        }
        catch (Exception ex)
        {
            errorMessage = ex.Message;
            return false;
        }
    }

    public static bool TryWriteBundleExecutable(
        string publishDir,
        string bundlePath,
        string outputBinaryPath,
        string bytecodeText,
        out string errorCode,
        out string errorMessage)
    {
        try
        {
            HostFileSystem.EnsureDirectory(publishDir);
            HostFileSystem.WriteAllText(bundlePath, bytecodeText);

            var sourceBinary = HostExecutableLocator.ResolveHostBinaryPath();
            if (sourceBinary is null)
            {
                errorCode = "PUB004";
                errorMessage = "host executable not found.";
                return false;
            }

            if (!BundlePublisher.TryWriteEmbeddedBytecodeExecutable(sourceBinary, outputBinaryPath, bytecodeText, out var bundleWriteError))
            {
                errorCode = "PUB003";
                errorMessage = bundleWriteError;
                return false;
            }

            errorCode = string.Empty;
            errorMessage = string.Empty;
            return true;
        }
        catch (Exception ex)
        {
            errorCode = "PUB003";
            errorMessage = ex.Message;
            return false;
        }
    }
}
