namespace Simple.Compiler.Binding;

public sealed class BoundWhileStatement : BoundStatement
{
    public BoundWhileStatement(BoundExpression condition, BoundBlockStatement body)
    {
        Condition = condition;
        Body = body;
    }

    public BoundExpression Condition { get; }
    public BoundBlockStatement Body { get; }
}
