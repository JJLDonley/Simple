using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Expressions;

namespace Simple.Compiler.Syntax.Statements;

public sealed class ReturnStatementSyntax : StatementSyntax
{
    public ReturnStatementSyntax(SyntaxToken returnKeyword, ExpressionSyntax? expression)
    {
        ReturnKeyword = returnKeyword;
        Expression = expression;
        Span = expression is null
            ? returnKeyword.Span
            : TextSpan.FromBounds(returnKeyword.Span.Start, expression.Span.End);
    }

    public SyntaxToken ReturnKeyword { get; }
    public ExpressionSyntax? Expression { get; }
    public override TextSpan Span { get; }
}
