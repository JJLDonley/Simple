using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundLiteralExpression : BoundExpression
{
    public BoundLiteralExpression(object? value, TypeSymbol type)
        : base(type)
    {
        Value = value;
    }

    public object? Value { get; }
}
