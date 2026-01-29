using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Module.Diagnostics;

public sealed class Diagnostic
{
    public Diagnostic(string code, TextSpan span, string message)
    {
        Code = code;
        Span = span;
        Message = message;
    }

    public string Code { get; }
    public TextSpan Span { get; }
    public string Message { get; }

    public override string ToString()
    {
        return $"{Code}: {Message}";
    }
}
