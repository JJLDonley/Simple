namespace Simple.Compiler.Binding;

public sealed class BoundIfClause
{
    public BoundIfClause(BoundExpression? condition, BoundBlockStatement body)
    {
        Condition = condition;
        Body = body;
    }

    public BoundExpression? Condition { get; }
    public BoundBlockStatement Body { get; }
    public bool IsDefault => Condition is null;
}
