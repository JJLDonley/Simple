using Simple.Compiler.Lexing;
using Simple.Compiler.Module.Diagnostics;
using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax;
using Simple.Compiler.Syntax.Declarations;
using Simple.Compiler.Syntax.Expressions;
using Simple.Compiler.Syntax.Statements;
using Simple.Compiler.Syntax.Types;

namespace Simple.Compiler.Parsing;

public sealed class Parser
{
    private readonly DiagnosticBag _diagnostics = new();
    private readonly List<SyntaxToken> _tokens;
    private int _position;

    public Parser(SourceText text)
    {
        var lexer = new Lexer(text);
        _tokens = lexer.Lex().ToList();
        _diagnostics.AddRange(lexer.Diagnostics);
    }

    public DiagnosticBag Diagnostics => _diagnostics;

    public ProgramSyntax ParseProgram()
    {
        var declarations = new List<DeclarationSyntax>();

        while (Current.Kind != TokenKind.EndOfFile)
        {
            var declaration = ParseDeclaration();
            declarations.Add(declaration);
        }

        return new ProgramSyntax(declarations);
    }

    private DeclarationSyntax ParseDeclaration()
    {
        var identifier = Match(TokenKind.Identifier, "PAR001", "Expected identifier.");
        var mutability = ParseMutabilityToken();
        var type = ParseType();

        if (Current.Kind == TokenKind.OpenParen)
        {
            return ParseProcedureDeclaration(identifier, mutability, type);
        }

        var equals = Match(TokenKind.Equals, "PAR002", "Expected '=' for variable declaration.");
        var initializer = ParseExpression();
        return new VariableDeclarationSyntax(identifier, mutability, type, equals, initializer);
    }

    private ProcedureDeclarationSyntax ParseProcedureDeclaration(SyntaxToken identifier, SyntaxToken mutabilityToken, TypeSyntax returnType)
    {
        var openParen = Match(TokenKind.OpenParen, "PAR003", "Expected '(' after return type.");
        var parameters = new List<ParameterSyntax>();

        if (Current.Kind != TokenKind.CloseParen)
        {
            while (true)
            {
                var paramIdentifier = Match(TokenKind.Identifier, "PAR004", "Expected parameter name.");
                var paramMutability = ParseMutabilityToken();
                var paramType = ParseType();
                parameters.Add(new ParameterSyntax(paramIdentifier, paramMutability, paramType));

                if (Current.Kind != TokenKind.Comma)
                {
                    break;
                }

                NextToken();
            }
        }

        var closeParen = Match(TokenKind.CloseParen, "PAR005", "Expected ')' after parameters.");
        var body = ParseBlockStatement();
        return new ProcedureDeclarationSyntax(identifier, mutabilityToken, returnType, openParen, parameters, closeParen, body);
    }

    private TypeSyntax ParseType()
    {
        var identifier = Match(TokenKind.Identifier, "PAR006", "Expected type name.");
        return new NamedTypeSyntax(identifier);
    }

    private BlockStatementSyntax ParseBlockStatement()
    {
        var openBrace = Match(TokenKind.OpenBrace, "PAR007", "Expected '{'.");
        var statements = new List<StatementSyntax>();

        while (Current.Kind != TokenKind.CloseBrace && Current.Kind != TokenKind.EndOfFile)
        {
            while (Current.Kind == TokenKind.Semicolon)
            {
                NextToken();
            }

            if (Current.Kind == TokenKind.CloseBrace || Current.Kind == TokenKind.EndOfFile)
            {
                break;
            }

            statements.Add(ParseStatement());
        }

        var closeBrace = Match(TokenKind.CloseBrace, "PAR008", "Expected '}'.");
        return new BlockStatementSyntax(openBrace, statements, closeBrace);
    }

    private StatementSyntax ParseStatement()
    {
        if (Current.Kind == TokenKind.ReturnKeyword)
        {
            return ParseReturnStatement();
        }

        if (Current.Kind == TokenKind.Identifier && (Peek(1).Kind == TokenKind.Colon || Peek(1).Kind == TokenKind.DoubleColon))
        {
            return ParseVariableDeclarationStatement();
        }

        if (Current.Kind == TokenKind.Identifier && IsAssignmentOperator(Peek(1).Kind))
        {
            return ParseAssignmentStatement();
        }

        var expression = ParseExpression();
        if (Current.Kind == TokenKind.OpenBrace)
        {
            var body = ParseBlockStatement();
            return new IfStatementSyntax(expression, body);
        }

        return new ExpressionStatementSyntax(expression);
    }

    private StatementSyntax ParseVariableDeclarationStatement()
    {
        var identifier = Match(TokenKind.Identifier, "PAR009", "Expected identifier.");
        var mutability = ParseMutabilityToken();
        var type = ParseType();
        var equals = Match(TokenKind.Equals, "PAR010", "Expected '=' for variable declaration.");
        var initializer = ParseExpression();
        return new VariableDeclarationStatementSyntax(identifier, mutability, type, equals, initializer);
    }

    private StatementSyntax ParseAssignmentStatement()
    {
        var identifier = Match(TokenKind.Identifier, "PAR011", "Expected identifier.");
        var op = NextToken();
        var expression = ParseExpression();
        return new AssignmentStatementSyntax(identifier, op, expression);
    }

    private ReturnStatementSyntax ParseReturnStatement()
    {
        var keyword = NextToken();
        ExpressionSyntax? expression = null;
        if (Current.Kind != TokenKind.CloseBrace && Current.Kind != TokenKind.EndOfFile)
        {
            expression = ParseExpression();
        }

        return new ReturnStatementSyntax(keyword, expression);
    }

    private ExpressionSyntax ParseExpression()
    {
        return ParseBinaryExpression();
    }

    private ExpressionSyntax ParseBinaryExpression(int parentPrecedence = 0)
    {
        ExpressionSyntax left;
        var unaryPrecedence = SyntaxFacts.GetUnaryOperatorPrecedence(Current.Kind);
        if (unaryPrecedence != 0 && unaryPrecedence >= parentPrecedence)
        {
            var operatorToken = NextToken();
            var operand = ParseBinaryExpression(unaryPrecedence);
            left = new UnaryExpressionSyntax(operatorToken, operand);
        }
        else
        {
            left = ParsePostfixExpression();
        }

        while (true)
        {
            var precedence = SyntaxFacts.GetBinaryOperatorPrecedence(Current.Kind);
            if (precedence == 0 || precedence <= parentPrecedence)
            {
                break;
            }

            var operatorToken = NextToken();
            var right = ParseBinaryExpression(precedence);
            left = new BinaryExpressionSyntax(left, operatorToken, right);
        }

        return left;
    }

    private ExpressionSyntax ParsePostfixExpression()
    {
        var expression = ParsePrimaryExpression();

        while (Current.Kind == TokenKind.OpenParen)
        {
            var openParen = NextToken();
            var arguments = new List<ExpressionSyntax>();

            if (Current.Kind != TokenKind.CloseParen)
            {
                while (true)
                {
                    arguments.Add(ParseExpression());
                    if (Current.Kind != TokenKind.Comma)
                    {
                        break;
                    }

                    NextToken();
                }
            }

            var closeParen = Match(TokenKind.CloseParen, "PAR012", "Expected ')' after arguments.");
            expression = new CallExpressionSyntax(expression, openParen, arguments, closeParen);
        }

        return expression;
    }

    private ExpressionSyntax ParsePrimaryExpression()
    {
        switch (Current.Kind)
        {
            case TokenKind.OpenParen:
            {
                var openParen = NextToken();
                var expression = ParseExpression();
                var closeParen = Match(TokenKind.CloseParen, "PAR013", "Expected ')' after expression.");
                return new ParenthesizedExpressionSyntax(openParen, expression, closeParen);
            }
            case TokenKind.NumberLiteral:
            case TokenKind.StringLiteral:
            case TokenKind.CharLiteral:
            case TokenKind.TrueKeyword:
            case TokenKind.FalseKeyword:
            {
                var literal = NextToken();
                return new LiteralExpressionSyntax(literal);
            }
            case TokenKind.Identifier:
            {
                var identifier = NextToken();
                return new NameExpressionSyntax(identifier);
            }
        }

        var unexpected = NextToken();
        _diagnostics.Report("PAR014", unexpected.Span, "Unexpected token in expression.");
        var placeholder = new SyntaxToken(TokenKind.Identifier, unexpected.Text, null, unexpected.Span);
        return new NameExpressionSyntax(placeholder);
    }

    private SyntaxToken ParseMutabilityToken()
    {
        if (Current.Kind == TokenKind.Colon || Current.Kind == TokenKind.DoubleColon)
        {
            return NextToken();
        }

        return Match(TokenKind.Colon, "PAR015", "Expected ':' or '::'.");
    }

    private SyntaxToken Match(TokenKind kind, string code, string message)
    {
        if (Current.Kind == kind)
        {
            return NextToken();
        }

        _diagnostics.Report(code, Current.Span, message);
        return new SyntaxToken(kind, string.Empty, null, new TextSpan(Current.Span.Start, 0));
    }

    private SyntaxToken NextToken()
    {
        var current = Current;
        _position++;
        return current;
    }

    private SyntaxToken Current => Peek(0);

    private SyntaxToken Peek(int offset)
    {
        var index = _position + offset;
        if (index >= _tokens.Count)
        {
            return _tokens[^1];
        }

        return _tokens[index];
    }

    private static bool IsAssignmentOperator(TokenKind kind)
    {
        return kind is TokenKind.Equals or TokenKind.PlusEquals or TokenKind.MinusEquals or TokenKind.StarEquals
            or TokenKind.SlashEquals or TokenKind.PercentEquals or TokenKind.AmpersandEquals
            or TokenKind.PipeEquals or TokenKind.CaretEquals or TokenKind.ShiftLeftEquals or TokenKind.ShiftRightEquals;
    }
}
