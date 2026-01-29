using Simple.Compiler.Syntax;
using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundBinaryOperator
{
    private BoundBinaryOperator(TokenKind syntaxKind, TypeSymbol leftType, TypeSymbol rightType, TypeSymbol resultType)
    {
        SyntaxKind = syntaxKind;
        LeftType = leftType;
        RightType = rightType;
        ResultType = resultType;
    }

    public TokenKind SyntaxKind { get; }
    public TypeSymbol LeftType { get; }
    public TypeSymbol RightType { get; }
    public TypeSymbol ResultType { get; }

    private static readonly BoundBinaryOperator[] Operators =
    {
        new BoundBinaryOperator(TokenKind.Plus, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.I32),
        new BoundBinaryOperator(TokenKind.Minus, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.I32),
        new BoundBinaryOperator(TokenKind.Star, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.I32),
        new BoundBinaryOperator(TokenKind.Slash, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.I32),
        new BoundBinaryOperator(TokenKind.Percent, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.I32),
        new BoundBinaryOperator(TokenKind.Plus, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.F64),
        new BoundBinaryOperator(TokenKind.Minus, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.F64),
        new BoundBinaryOperator(TokenKind.Star, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.F64),
        new BoundBinaryOperator(TokenKind.Slash, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.F64),
        new BoundBinaryOperator(TokenKind.Percent, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.F64),

        new BoundBinaryOperator(TokenKind.EqualsEquals, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.BangEquals, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.Less, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.LessOrEquals, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.Greater, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.GreaterOrEquals, TypeSymbol.I32, TypeSymbol.I32, TypeSymbol.Bool),

        new BoundBinaryOperator(TokenKind.EqualsEquals, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.BangEquals, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.Less, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.LessOrEquals, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.Greater, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.GreaterOrEquals, TypeSymbol.F64, TypeSymbol.F64, TypeSymbol.Bool),

        new BoundBinaryOperator(TokenKind.EqualsEquals, TypeSymbol.Bool, TypeSymbol.Bool, TypeSymbol.Bool),
        new BoundBinaryOperator(TokenKind.BangEquals, TypeSymbol.Bool, TypeSymbol.Bool, TypeSymbol.Bool),
    };

    public static BoundBinaryOperator? Bind(TokenKind kind, TypeSymbol leftType, TypeSymbol rightType)
    {
        return Operators.FirstOrDefault(op => op.SyntaxKind == kind && op.LeftType == leftType && op.RightType == rightType);
    }
}
