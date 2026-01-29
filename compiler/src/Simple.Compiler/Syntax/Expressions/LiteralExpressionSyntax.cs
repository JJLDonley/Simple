using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Expressions;

public sealed class LiteralExpressionSyntax : ExpressionSyntax
{
    public LiteralExpressionSyntax(SyntaxToken literalToken)
    {
        LiteralToken = literalToken;
        Span = literalToken.Span;
    }

    public SyntaxToken LiteralToken { get; }
    public override TextSpan Span { get; }
}
