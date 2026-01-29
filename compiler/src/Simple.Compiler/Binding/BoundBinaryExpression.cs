using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundBinaryExpression : BoundExpression
{
    public BoundBinaryExpression(BoundExpression left, BoundBinaryOperator op, BoundExpression right)
        : base(op.ResultType)
    {
        Left = left;
        Operator = op;
        Right = right;
    }

    public BoundExpression Left { get; }
    public BoundBinaryOperator Operator { get; }
    public BoundExpression Right { get; }
}
