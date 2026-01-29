namespace Simple.Compiler.Binding;

public sealed class BoundIfChainStatement : BoundStatement
{
    public BoundIfChainStatement(IReadOnlyList<BoundIfClause> clauses)
    {
        Clauses = clauses;
    }

    public IReadOnlyList<BoundIfClause> Clauses { get; }
}
