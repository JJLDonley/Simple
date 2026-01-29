using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundVariableDeclaration : BoundStatement
{
    public BoundVariableDeclaration(VariableSymbol variable, BoundExpression initializer)
    {
        Variable = variable;
        Initializer = initializer;
    }

    public VariableSymbol Variable { get; }
    public BoundExpression Initializer { get; }
}
