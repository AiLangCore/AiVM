namespace AiVM.Core;

public sealed class AosProcessExitException : Exception
{
    public AosProcessExitException(int code)
    {
        Code = code;
    }

    public int Code { get; }
}
