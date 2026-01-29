using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundReturnStatement : BoundStatement
{
    public BoundReturnStatement(BoundExpression? expression, TypeSymbol expectedType)
    {
        Expression = expression;
        ExpectedType = expectedType;
    }

    public BoundExpression? Expression { get; }
    public TypeSymbol ExpectedType { get; }
}
