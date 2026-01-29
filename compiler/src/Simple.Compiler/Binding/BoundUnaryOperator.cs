using Simple.Compiler.Syntax;
using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundUnaryOperator
{
    private BoundUnaryOperator(TokenKind syntaxKind, TypeSymbol operandType, TypeSymbol resultType)
    {
        SyntaxKind = syntaxKind;
        OperandType = operandType;
        ResultType = resultType;
    }

    public TokenKind SyntaxKind { get; }
    public TypeSymbol OperandType { get; }
    public TypeSymbol ResultType { get; }

    private static readonly BoundUnaryOperator[] Operators =
    {
        new BoundUnaryOperator(TokenKind.Plus, TypeSymbol.I32, TypeSymbol.I32),
        new BoundUnaryOperator(TokenKind.Minus, TypeSymbol.I32, TypeSymbol.I32),
        new BoundUnaryOperator(TokenKind.Plus, TypeSymbol.F64, TypeSymbol.F64),
        new BoundUnaryOperator(TokenKind.Minus, TypeSymbol.F64, TypeSymbol.F64),
        new BoundUnaryOperator(TokenKind.Bang, TypeSymbol.Bool, TypeSymbol.Bool),
    };

    public static BoundUnaryOperator? Bind(TokenKind kind, TypeSymbol operandType)
    {
        return Operators.FirstOrDefault(op => op.SyntaxKind == kind && op.OperandType == operandType);
    }
}
