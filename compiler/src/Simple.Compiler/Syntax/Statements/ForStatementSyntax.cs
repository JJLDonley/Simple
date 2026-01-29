using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Expressions;

namespace Simple.Compiler.Syntax.Statements;

public sealed class ForStatementSyntax : StatementSyntax
{
    public ForStatementSyntax(
        SyntaxToken forKeyword,
        SyntaxToken openParenToken,
        StatementSyntax? initializer,
        SyntaxToken firstSemicolon,
        ExpressionSyntax? condition,
        SyntaxToken secondSemicolon,
        StatementSyntax? increment,
        SyntaxToken closeParenToken,
        BlockStatementSyntax body)
    {
        ForKeyword = forKeyword;
        OpenParenToken = openParenToken;
        Initializer = initializer;
        FirstSemicolon = firstSemicolon;
        Condition = condition;
        SecondSemicolon = secondSemicolon;
        Increment = increment;
        CloseParenToken = closeParenToken;
        Body = body;
        Span = TextSpan.FromBounds(forKeyword.Span.Start, body.Span.End);
    }

    public SyntaxToken ForKeyword { get; }
    public SyntaxToken OpenParenToken { get; }
    public StatementSyntax? Initializer { get; }
    public SyntaxToken FirstSemicolon { get; }
    public ExpressionSyntax? Condition { get; }
    public SyntaxToken SecondSemicolon { get; }
    public StatementSyntax? Increment { get; }
    public SyntaxToken CloseParenToken { get; }
    public BlockStatementSyntax Body { get; }
    public override TextSpan Span { get; }
}
