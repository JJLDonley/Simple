using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public abstract class BoundExpression : BoundNode
{
    protected BoundExpression(TypeSymbol type)
    {
        Type = type;
    }

    public TypeSymbol Type { get; }
}
