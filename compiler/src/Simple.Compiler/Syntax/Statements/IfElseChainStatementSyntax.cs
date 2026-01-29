using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Statements;

public sealed class IfElseChainStatementSyntax : StatementSyntax
{
    public IfElseChainStatementSyntax(IReadOnlyList<IfElseClauseSyntax> clauses)
    {
        Clauses = clauses;
        Span = clauses.Count > 0
            ? TextSpan.FromBounds(clauses[0].Span.Start, clauses[^1].Span.End)
            : new TextSpan(0, 0);
    }

    public IReadOnlyList<IfElseClauseSyntax> Clauses { get; }
    public override TextSpan Span { get; }
}
