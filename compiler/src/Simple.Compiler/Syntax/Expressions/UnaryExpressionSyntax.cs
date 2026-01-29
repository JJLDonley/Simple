using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Expressions;

public sealed class UnaryExpressionSyntax : ExpressionSyntax
{
    public UnaryExpressionSyntax(SyntaxToken operatorToken, ExpressionSyntax operand)
    {
        OperatorToken = operatorToken;
        Operand = operand;
        Span = TextSpan.FromBounds(operatorToken.Span.Start, operand.Span.End);
    }

    public SyntaxToken OperatorToken { get; }
    public ExpressionSyntax Operand { get; }
    public override TextSpan Span { get; }
}
