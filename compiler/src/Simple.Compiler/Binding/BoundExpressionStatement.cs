namespace Simple.Compiler.Binding;

public sealed class BoundExpressionStatement : BoundStatement
{
    public BoundExpressionStatement(BoundExpression expression)
    {
        Expression = expression;
    }

    public BoundExpression Expression { get; }
}
