using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Statements;

public sealed class SkipStatementSyntax : StatementSyntax
{
    public SkipStatementSyntax(SyntaxToken skipKeyword)
    {
        SkipKeyword = skipKeyword;
        Span = skipKeyword.Span;
    }

    public SyntaxToken SkipKeyword { get; }
    public override TextSpan Span { get; }
}
