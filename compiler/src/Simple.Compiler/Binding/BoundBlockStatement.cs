namespace Simple.Compiler.Binding;

public sealed class BoundBlockStatement : BoundStatement
{
    public BoundBlockStatement(IReadOnlyList<BoundStatement> statements)
    {
        Statements = statements;
    }

    public IReadOnlyList<BoundStatement> Statements { get; }
}
