using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax;

public sealed class SyntaxToken
{
    public SyntaxToken(TokenKind kind, string text, object? value, TextSpan span)
    {
        Kind = kind;
        Text = text;
        Value = value;
        Span = span;
    }

    public TokenKind Kind { get; }
    public string Text { get; }
    public object? Value { get; }
    public TextSpan Span { get; }
}
