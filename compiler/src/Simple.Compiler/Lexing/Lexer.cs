using System.Globalization;
using System.Text;
using Simple.Compiler.Module.Diagnostics;
using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax;

namespace Simple.Compiler.Lexing;

public sealed class Lexer
{
    private static readonly Dictionary<string, TokenKind> Keywords = new(StringComparer.Ordinal)
    {
        ["while"] = TokenKind.WhileKeyword,
        ["for"] = TokenKind.ForKeyword,
        ["break"] = TokenKind.BreakKeyword,
        ["skip"] = TokenKind.SkipKeyword,
        ["return"] = TokenKind.ReturnKeyword,
        ["default"] = TokenKind.DefaultKeyword,
        ["Fn"] = TokenKind.FnKeyword,
        ["self"] = TokenKind.SelfKeyword,
        ["Artifact"] = TokenKind.ArtifactKeyword,
        ["Enum"] = TokenKind.EnumKeyword,
        ["Module"] = TokenKind.ModuleKeyword,
        ["Union"] = TokenKind.UnionKeyword,
        ["true"] = TokenKind.TrueKeyword,
        ["false"] = TokenKind.FalseKeyword,
    };

    private readonly DiagnosticBag _diagnostics = new();
    private readonly SourceText _text;
    private int _position;

    public Lexer(SourceText text)
    {
        _text = text;
    }

    public DiagnosticBag Diagnostics => _diagnostics;

    public IReadOnlyList<SyntaxToken> Lex()
    {
        var tokens = new List<SyntaxToken>();

        while (true)
        {
            var token = NextToken();
            if (token.Kind != TokenKind.BadToken)
            {
                tokens.Add(token);
            }

            if (token.Kind == TokenKind.EndOfFile)
            {
                break;
            }
        }

        return tokens;
    }

    private SyntaxToken NextToken()
    {
        SkipWhitespaceAndComments();

        var start = _position;
        if (_position >= _text.Length)
        {
            return new SyntaxToken(TokenKind.EndOfFile, string.Empty, null, new TextSpan(_position, 0));
        }

        var current = Current;

        if (char.IsLetter(current) || current == '_')
        {
            return ReadIdentifierOrKeyword();
        }

        if (char.IsDigit(current))
        {
            return ReadNumber();
        }

        if (current == '"')
        {
            return ReadString();
        }

        if (current == '\'')
        {
            return ReadChar();
        }

        switch (current)
        {
            case '(':
                return SingleChar(TokenKind.OpenParen);
            case ')':
                return SingleChar(TokenKind.CloseParen);
            case '{':
                return SingleChar(TokenKind.OpenBrace);
            case '}':
                return SingleChar(TokenKind.CloseBrace);
            case '[':
                return SingleChar(TokenKind.OpenBracket);
            case ']':
                return SingleChar(TokenKind.CloseBracket);
            case ',':
                return SingleChar(TokenKind.Comma);
            case ';':
                return SingleChar(TokenKind.Semicolon);
            case '.':
                return SingleChar(TokenKind.Dot);
            case ':':
                if (Peek(1) == ':')
                {
                    return MultiChar(TokenKind.DoubleColon, 2);
                }
                return SingleChar(TokenKind.Colon);
            case '+':
                if (Peek(1) == '+')
                {
                    return MultiChar(TokenKind.PlusPlus, 2);
                }
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.PlusEquals, 2);
                }
                return SingleChar(TokenKind.Plus);
            case '-':
                if (Peek(1) == '-')
                {
                    return MultiChar(TokenKind.MinusMinus, 2);
                }
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.MinusEquals, 2);
                }
                return SingleChar(TokenKind.Minus);
            case '*':
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.StarEquals, 2);
                }
                return SingleChar(TokenKind.Star);
            case '/':
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.SlashEquals, 2);
                }
                return SingleChar(TokenKind.Slash);
            case '%':
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.PercentEquals, 2);
                }
                return SingleChar(TokenKind.Percent);
            case '!':
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.BangEquals, 2);
                }
                return SingleChar(TokenKind.Bang);
            case '=':
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.EqualsEquals, 2);
                }
                return SingleChar(TokenKind.Equals);
            case '<':
                if (Peek(1) == '<')
                {
                    if (Peek(2) == '=')
                    {
                        return MultiChar(TokenKind.ShiftLeftEquals, 3);
                    }
                    return MultiChar(TokenKind.ShiftLeft, 2);
                }
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.LessOrEquals, 2);
                }
                return SingleChar(TokenKind.Less);
            case '>':
                if (Peek(1) == '>')
                {
                    if (Peek(2) == '=')
                    {
                        return MultiChar(TokenKind.ShiftRightEquals, 3);
                    }
                    return MultiChar(TokenKind.ShiftRight, 2);
                }
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.GreaterOrEquals, 2);
                }
                return SingleChar(TokenKind.Greater);
            case '&':
                if (Peek(1) == '&')
                {
                    return MultiChar(TokenKind.AmpersandAmpersand, 2);
                }
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.AmpersandEquals, 2);
                }
                return SingleChar(TokenKind.Ampersand);
            case '|':
                if (Peek(1) == '|')
                {
                    return MultiChar(TokenKind.PipePipe, 2);
                }
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.PipeEquals, 2);
                }
                if (Peek(1) == '>')
                {
                    return MultiChar(TokenKind.PipeArrow, 2);
                }
                return SingleChar(TokenKind.Pipe);
            case '^':
                if (Peek(1) == '=')
                {
                    return MultiChar(TokenKind.CaretEquals, 2);
                }
                return SingleChar(TokenKind.Caret);
        }

        _diagnostics.Report("LEX001", new TextSpan(start, 1), $"Unexpected character '{current}'.");
        _position++;
        return new SyntaxToken(TokenKind.BadToken, _text.Text.Substring(start, 1), null, new TextSpan(start, 1));
    }

    private void SkipWhitespaceAndComments()
    {
        while (true)
        {
            if (char.IsWhiteSpace(Current))
            {
                _position++;
                continue;
            }

            if (Current == '/' && Peek(1) == '/')
            {
                _position += 2;
                while (Current != '\0' && Current != '\n')
                {
                    _position++;
                }
                continue;
            }

            if (Current == '/' && Peek(1) == '*')
            {
                var start = _position;
                _position += 2;
                while (Current != '\0')
                {
                    if (Current == '*' && Peek(1) == '/')
                    {
                        _position += 2;
                        break;
                    }
                    _position++;
                }

                if (Current == '\0')
                {
                    _diagnostics.Report("LEX002", new TextSpan(start, _position - start), "Unterminated block comment.");
                }

                continue;
            }

            break;
        }
    }

    private SyntaxToken ReadIdentifierOrKeyword()
    {
        var start = _position;
        while (char.IsLetterOrDigit(Current) || Current == '_')
        {
            _position++;
        }

        var text = _text.Text.Substring(start, _position - start);
        if (Keywords.TryGetValue(text, out var kind))
        {
            object? value = kind switch
            {
                TokenKind.TrueKeyword => true,
                TokenKind.FalseKeyword => false,
                _ => null
            };

            return new SyntaxToken(kind, text, value, new TextSpan(start, text.Length));
        }

        return new SyntaxToken(TokenKind.Identifier, text, null, new TextSpan(start, text.Length));
    }

    private SyntaxToken ReadNumber()
    {
        var start = _position;
        var isFloat = false;

        if (Current == '0' && (Peek(1) == 'x' || Peek(1) == 'X'))
        {
            _position += 2;
            while (IsHexDigit(Current))
            {
                _position++;
            }

            var text = _text.Text.Substring(start, _position - start);
            if (long.TryParse(text[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var hexValue))
            {
                return new SyntaxToken(TokenKind.NumberLiteral, text, hexValue, new TextSpan(start, text.Length));
            }

            _diagnostics.Report("LEX003", new TextSpan(start, _position - start), "Invalid hex literal.");
            return new SyntaxToken(TokenKind.NumberLiteral, text, 0L, new TextSpan(start, text.Length));
        }

        if (Current == '0' && (Peek(1) == 'b' || Peek(1) == 'B'))
        {
            _position += 2;
            while (Current == '0' || Current == '1')
            {
                _position++;
            }

            var text = _text.Text.Substring(start, _position - start);
            try
            {
                var value = Convert.ToInt64(text[2..], 2);
                return new SyntaxToken(TokenKind.NumberLiteral, text, value, new TextSpan(start, text.Length));
            }
            catch
            {
                _diagnostics.Report("LEX004", new TextSpan(start, _position - start), "Invalid binary literal.");
                return new SyntaxToken(TokenKind.NumberLiteral, text, 0L, new TextSpan(start, text.Length));
            }
        }

        while (char.IsDigit(Current))
        {
            _position++;
        }

        if (Current == '.' && char.IsDigit(Peek(1)))
        {
            isFloat = true;
            _position++;
            while (char.IsDigit(Current))
            {
                _position++;
            }
        }

        if (Current == 'e' || Current == 'E')
        {
            isFloat = true;
            _position++;
            if (Current == '+' || Current == '-')
            {
                _position++;
            }

            while (char.IsDigit(Current))
            {
                _position++;
            }
        }

        var literalText = _text.Text.Substring(start, _position - start);

        if (isFloat)
        {
            if (double.TryParse(literalText, NumberStyles.Float, CultureInfo.InvariantCulture, out var doubleValue))
            {
                return new SyntaxToken(TokenKind.NumberLiteral, literalText, doubleValue, new TextSpan(start, literalText.Length));
            }

            _diagnostics.Report("LEX005", new TextSpan(start, _position - start), "Invalid float literal.");
            return new SyntaxToken(TokenKind.NumberLiteral, literalText, 0.0, new TextSpan(start, literalText.Length));
        }

        if (long.TryParse(literalText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var intValue))
        {
            return new SyntaxToken(TokenKind.NumberLiteral, literalText, intValue, new TextSpan(start, literalText.Length));
        }

        _diagnostics.Report("LEX006", new TextSpan(start, _position - start), "Invalid integer literal.");
        return new SyntaxToken(TokenKind.NumberLiteral, literalText, 0L, new TextSpan(start, literalText.Length));
    }

    private SyntaxToken ReadString()
    {
        var start = _position;
        _position++;
        var builder = new StringBuilder();

        while (Current != '\0' && Current != '"')
        {
            if (Current == '\\')
            {
                _position++;
                if (Current == '\0')
                {
                    break;
                }

                builder.Append(EscapeChar(Current));
                _position++;
                continue;
            }

            builder.Append(Current);
            _position++;
        }

        if (Current != '"')
        {
            _diagnostics.Report("LEX007", new TextSpan(start, _position - start), "Unterminated string literal.");
        }
        else
        {
            _position++;
        }

        var text = _text.Text.Substring(start, _position - start);
        return new SyntaxToken(TokenKind.StringLiteral, text, builder.ToString(), new TextSpan(start, text.Length));
    }

    private SyntaxToken ReadChar()
    {
        var start = _position;
        _position++;
        char value;

        if (Current == '\\')
        {
            _position++;
            value = EscapeChar(Current);
            _position++;
        }
        else
        {
            value = Current;
            _position++;
        }

        if (Current != '\'')
        {
            _diagnostics.Report("LEX008", new TextSpan(start, _position - start), "Unterminated char literal.");
        }
        else
        {
            _position++;
        }

        var text = _text.Text.Substring(start, _position - start);
        return new SyntaxToken(TokenKind.CharLiteral, text, value, new TextSpan(start, text.Length));
    }

    private SyntaxToken SingleChar(TokenKind kind)
    {
        var start = _position;
        _position++;
        return new SyntaxToken(kind, _text.Text.Substring(start, 1), null, new TextSpan(start, 1));
    }

    private SyntaxToken MultiChar(TokenKind kind, int length)
    {
        var start = _position;
        _position += length;
        return new SyntaxToken(kind, _text.Text.Substring(start, length), null, new TextSpan(start, length));
    }

    private char Current => _position >= _text.Length ? '\0' : _text[_position];

    private char Peek(int offset)
    {
        var index = _position + offset;
        return index >= _text.Length ? '\0' : _text[index];
    }

    private static bool IsHexDigit(char c)
    {
        return char.IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    private static char EscapeChar(char c)
    {
        return c switch
        {
            'n' => '\n',
            'r' => '\r',
            't' => '\t',
            '\\' => '\\',
            '\'' => '\'',
            '"' => '"',
            '0' => '\0',
            _ => c
        };
    }
}
