using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Expressions;

public sealed class NameExpressionSyntax : ExpressionSyntax
{
    public NameExpressionSyntax(SyntaxToken identifier)
    {
        Identifier = identifier;
        Span = identifier.Span;
    }

    public SyntaxToken Identifier { get; }
    public override TextSpan Span { get; }
}
