using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Expressions;

namespace Simple.Compiler.Syntax.Statements;

public sealed class WhileStatementSyntax : StatementSyntax
{
    public WhileStatementSyntax(SyntaxToken whileKeyword, ExpressionSyntax condition, BlockStatementSyntax body)
    {
        WhileKeyword = whileKeyword;
        Condition = condition;
        Body = body;
        Span = TextSpan.FromBounds(whileKeyword.Span.Start, body.Span.End);
    }

    public SyntaxToken WhileKeyword { get; }
    public ExpressionSyntax Condition { get; }
    public BlockStatementSyntax Body { get; }
    public override TextSpan Span { get; }
}
