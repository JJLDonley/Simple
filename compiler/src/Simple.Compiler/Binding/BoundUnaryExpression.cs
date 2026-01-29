using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundUnaryExpression : BoundExpression
{
    public BoundUnaryExpression(BoundUnaryOperator op, BoundExpression operand)
        : base(op.ResultType)
    {
        Operator = op;
        Operand = operand;
    }

    public BoundUnaryOperator Operator { get; }
    public BoundExpression Operand { get; }
}
