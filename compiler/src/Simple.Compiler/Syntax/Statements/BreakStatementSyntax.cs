using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Statements;

public sealed class BreakStatementSyntax : StatementSyntax
{
    public BreakStatementSyntax(SyntaxToken breakKeyword)
    {
        BreakKeyword = breakKeyword;
        Span = breakKeyword.Span;
    }

    public SyntaxToken BreakKeyword { get; }
    public override TextSpan Span { get; }
}
