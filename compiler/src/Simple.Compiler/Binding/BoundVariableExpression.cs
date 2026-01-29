using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundVariableExpression : BoundExpression
{
    public BoundVariableExpression(VariableSymbol variable)
        : base(variable.Type)
    {
        Variable = variable;
    }

    public VariableSymbol Variable { get; }
}
