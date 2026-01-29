using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Expressions;

namespace Simple.Compiler.Syntax.Statements;

public sealed class IfElseClauseSyntax : SyntaxNode
{
    public IfElseClauseSyntax(SyntaxToken pipeArrowToken, SyntaxToken? defaultKeyword, ExpressionSyntax? condition, BlockStatementSyntax body)
    {
        PipeArrowToken = pipeArrowToken;
        DefaultKeyword = defaultKeyword;
        Condition = condition;
        Body = body;
        Span = TextSpan.FromBounds(pipeArrowToken.Span.Start, body.Span.End);
    }

    public SyntaxToken PipeArrowToken { get; }
    public SyntaxToken? DefaultKeyword { get; }
    public ExpressionSyntax? Condition { get; }
    public BlockStatementSyntax Body { get; }
    public bool IsDefault => DefaultKeyword is not null;
    public override TextSpan Span { get; }
}
