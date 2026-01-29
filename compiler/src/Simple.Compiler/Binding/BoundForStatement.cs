namespace Simple.Compiler.Binding;

public sealed class BoundForStatement : BoundStatement
{
    public BoundForStatement(BoundStatement? initializer, BoundExpression condition, BoundStatement? increment, BoundBlockStatement body)
    {
        Initializer = initializer;
        Condition = condition;
        Increment = increment;
        Body = body;
    }

    public BoundStatement? Initializer { get; }
    public BoundExpression Condition { get; }
    public BoundStatement? Increment { get; }
    public BoundBlockStatement Body { get; }
}
