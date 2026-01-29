using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Expressions;

public sealed class ParenthesizedExpressionSyntax : ExpressionSyntax
{
    public ParenthesizedExpressionSyntax(SyntaxToken openParenToken, ExpressionSyntax expression, SyntaxToken closeParenToken)
    {
        OpenParenToken = openParenToken;
        Expression = expression;
        CloseParenToken = closeParenToken;
        Span = TextSpan.FromBounds(openParenToken.Span.Start, closeParenToken.Span.End);
    }

    public SyntaxToken OpenParenToken { get; }
    public ExpressionSyntax Expression { get; }
    public SyntaxToken CloseParenToken { get; }
    public override TextSpan Span { get; }
}
