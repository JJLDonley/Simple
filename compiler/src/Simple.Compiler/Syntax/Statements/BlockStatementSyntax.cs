using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Statements;

public sealed class BlockStatementSyntax : StatementSyntax
{
    public BlockStatementSyntax(SyntaxToken openBraceToken, IReadOnlyList<StatementSyntax> statements, SyntaxToken closeBraceToken)
    {
        OpenBraceToken = openBraceToken;
        Statements = statements;
        CloseBraceToken = closeBraceToken;
        Span = TextSpan.FromBounds(openBraceToken.Span.Start, closeBraceToken.Span.End);
    }

    public SyntaxToken OpenBraceToken { get; }
    public IReadOnlyList<StatementSyntax> Statements { get; }
    public SyntaxToken CloseBraceToken { get; }
    public override TextSpan Span { get; }
}
