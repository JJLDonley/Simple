using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundCallExpression : BoundExpression
{
    public BoundCallExpression(FunctionSymbol function, IReadOnlyList<BoundExpression> arguments)
        : base(function.ReturnType)
    {
        Function = function;
        Arguments = arguments;
    }

    public FunctionSymbol Function { get; }
    public IReadOnlyList<BoundExpression> Arguments { get; }
}
