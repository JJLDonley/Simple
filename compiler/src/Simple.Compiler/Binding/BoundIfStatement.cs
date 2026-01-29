namespace Simple.Compiler.Binding;

public sealed class BoundIfStatement : BoundStatement
{
    public BoundIfStatement(BoundExpression condition, BoundBlockStatement body)
    {
        Condition = condition;
        Body = body;
    }

    public BoundExpression Condition { get; }
    public BoundBlockStatement Body { get; }
}
