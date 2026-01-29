using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Expressions;

namespace Simple.Compiler.Syntax.Statements;

public sealed class ExpressionStatementSyntax : StatementSyntax
{
    public ExpressionStatementSyntax(ExpressionSyntax expression)
    {
        Expression = expression;
        Span = expression.Span;
    }

    public ExpressionSyntax Expression { get; }
    public override TextSpan Span { get; }
}
