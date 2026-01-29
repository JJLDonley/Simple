using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Expressions;

namespace Simple.Compiler.Syntax.Statements;

public sealed class AssignmentStatementSyntax : StatementSyntax
{
    public AssignmentStatementSyntax(SyntaxToken identifier, SyntaxToken operatorToken, ExpressionSyntax expression)
    {
        Identifier = identifier;
        OperatorToken = operatorToken;
        Expression = expression;
        Span = TextSpan.FromBounds(identifier.Span.Start, expression.Span.End);
    }

    public SyntaxToken Identifier { get; }
    public SyntaxToken OperatorToken { get; }
    public ExpressionSyntax Expression { get; }
    public override TextSpan Span { get; }
}
