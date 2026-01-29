using Simple.Compiler.Syntax;

namespace Simple.Compiler.Parsing;

public static class SyntaxFacts
{
    public static int GetUnaryOperatorPrecedence(TokenKind kind)
    {
        return kind switch
        {
            TokenKind.Plus or TokenKind.Minus or TokenKind.Bang => 7,
            _ => 0
        };
    }

    public static int GetBinaryOperatorPrecedence(TokenKind kind)
    {
        return kind switch
        {
            TokenKind.Star or TokenKind.Slash or TokenKind.Percent => 12,
            TokenKind.Plus or TokenKind.Minus => 11,
            TokenKind.ShiftLeft or TokenKind.ShiftRight => 10,
            TokenKind.Less or TokenKind.Greater or TokenKind.LessOrEquals or TokenKind.GreaterOrEquals => 9,
            TokenKind.EqualsEquals or TokenKind.BangEquals => 8,
            TokenKind.Ampersand => 7,
            TokenKind.Caret => 6,
            TokenKind.Pipe => 5,
            TokenKind.AmpersandAmpersand => 4,
            TokenKind.PipePipe => 3,
            _ => 0
        };
    }
}
