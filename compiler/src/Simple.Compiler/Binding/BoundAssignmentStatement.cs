using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundAssignmentStatement : BoundStatement
{
    public BoundAssignmentStatement(VariableSymbol variable, BoundExpression expression)
    {
        Variable = variable;
        Expression = expression;
    }

    public VariableSymbol Variable { get; }
    public BoundExpression Expression { get; }
}
