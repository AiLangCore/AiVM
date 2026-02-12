namespace AiVM.Core;

public static class HostConsole
{
    public static TextReader In
    {
        get => Console.In;
        set => Console.SetIn(value);
    }

    public static TextWriter Out
    {
        get => Console.Out;
        set => Console.SetOut(value);
    }

    public static void WriteLine(string text) => Console.WriteLine(text);
}
