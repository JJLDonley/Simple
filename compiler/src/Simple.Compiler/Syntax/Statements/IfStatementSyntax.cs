using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Expressions;

namespace Simple.Compiler.Syntax.Statements;

public sealed class IfStatementSyntax : StatementSyntax
{
    public IfStatementSyntax(ExpressionSyntax condition, BlockStatementSyntax body)
    {
        Condition = condition;
        Body = body;
        Span = TextSpan.FromBounds(condition.Span.Start, body.Span.End);
    }

    public ExpressionSyntax Condition { get; }
    public BlockStatementSyntax Body { get; }
    public override TextSpan Span { get; }
}
