namespace AiVM.Core;

public readonly record struct EmbeddedPayload(string Text, bool IsBytecode);

public static class EmbeddedBundleLoader
{
    public static bool TryLoadFromCurrentProcess(out EmbeddedPayload payload)
    {
        payload = default;
        var processPath = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(processPath) || !File.Exists(processPath))
        {
            return false;
        }

        var markerAst = System.Text.Encoding.UTF8.GetBytes("\n--AIBUNDLE1--\n");
        var markerBytecode = System.Text.Encoding.UTF8.GetBytes("\n--AIBUNDLE1:BYTECODE--\n");
        var bytes = File.ReadAllBytes(processPath);
        var astIndex = LastIndexOf(bytes, markerAst);
        var bytecodeIndex = LastIndexOf(bytes, markerBytecode);
        if (astIndex < 0 && bytecodeIndex < 0)
        {
            return false;
        }

        var marker = astIndex >= bytecodeIndex ? markerAst : markerBytecode;
        var markerIndex = astIndex >= bytecodeIndex ? astIndex : bytecodeIndex;
        var isBytecode = astIndex < bytecodeIndex;
        var start = markerIndex + marker.Length;
        if (start >= bytes.Length)
        {
            return false;
        }

        var text = System.Text.Encoding.UTF8.GetString(bytes, start, bytes.Length - start);
        payload = new EmbeddedPayload(text, isBytecode);
        return true;
    }

    private static int LastIndexOf(byte[] haystack, byte[] needle)
    {
        for (var i = haystack.Length - needle.Length; i >= 0; i--)
        {
            var match = true;
            for (var j = 0; j < needle.Length; j++)
            {
                if (haystack[i + j] != needle[j])
                {
                    match = false;
                    break;
                }
            }

            if (match)
            {
                return i;
            }
        }

        return -1;
    }
}
