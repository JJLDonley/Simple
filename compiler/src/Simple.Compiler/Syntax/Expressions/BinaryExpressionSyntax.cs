using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Expressions;

public sealed class BinaryExpressionSyntax : ExpressionSyntax
{
    public BinaryExpressionSyntax(ExpressionSyntax left, SyntaxToken operatorToken, ExpressionSyntax right)
    {
        Left = left;
        OperatorToken = operatorToken;
        Right = right;
        Span = TextSpan.FromBounds(left.Span.Start, right.Span.End);
    }

    public ExpressionSyntax Left { get; }
    public SyntaxToken OperatorToken { get; }
    public ExpressionSyntax Right { get; }
    public override TextSpan Span { get; }
}
