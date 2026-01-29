using Simple.Compiler.Module.Diagnostics;
using Simple.Compiler.Symbols;
using Simple.Compiler.Syntax;
using Simple.Compiler.Syntax.Declarations;
using Simple.Compiler.Syntax.Expressions;
using Simple.Compiler.Syntax.Statements;
using Simple.Compiler.Syntax.Types;

namespace Simple.Compiler.Binding;

public sealed class Binder
{
    private readonly DiagnosticBag _diagnostics = new();
    private BoundScope _scope;
    private FunctionSymbol? _currentFunction;
    private int _loopDepth;

    public Binder(BoundScope parentScope)
    {
        _scope = parentScope;
    }

    public DiagnosticBag Diagnostics => _diagnostics;

    public static BoundProgram BindProgram(ProgramSyntax program, out DiagnosticBag diagnostics)
    {
        var globalScope = new BoundScope(null);
        DeclareBuiltIns(globalScope);

        var binder = new Binder(globalScope);
        var declarations = new List<(DeclarationSyntax syntax, Symbol symbol)>();

        foreach (var declaration in program.Declarations)
        {
            switch (declaration)
            {
                case VariableDeclarationSyntax variableDeclaration:
                    var variable = binder.BindVariableSymbol(variableDeclaration, isGlobal: true);
                    if (variable is not null)
                    {
                        declarations.Add((declaration, variable));
                    }
                    break;
                case ProcedureDeclarationSyntax procedureDeclaration:
                    var function = binder.BindFunctionSymbol(procedureDeclaration);
                    if (function is not null)
                    {
                        declarations.Add((declaration, function));
                    }
                    break;
                default:
                    binder._diagnostics.Report("BND000", declaration.Span, "Unsupported declaration in Phase 1.");
                    break;
            }
        }

        var globalStatements = new List<BoundStatement>();
        var functionBodies = new Dictionary<FunctionSymbol, BoundBlockStatement>();

        foreach (var (syntax, symbol) in declarations)
        {
            switch (syntax)
            {
                case VariableDeclarationSyntax variableDeclaration:
                    var boundVariable = (VariableSymbol)symbol;
                    var initializer = binder.BindExpression(variableDeclaration.Initializer);
                    if (initializer.Type != boundVariable.Type && initializer.Type != TypeSymbol.Error)
                    {
                        binder._diagnostics.Report("BND001", variableDeclaration.Initializer.Span, "Initializer type does not match variable type.");
                    }
                    globalStatements.Add(new BoundVariableDeclaration(boundVariable, initializer));
                    break;
                case ProcedureDeclarationSyntax procedureDeclaration:
                    var function = (FunctionSymbol)symbol;
                    var body = binder.BindFunctionBody(function, procedureDeclaration);
                    functionBodies[function] = body;
                    break;
            }
        }

        diagnostics = binder._diagnostics;
        return new BoundProgram(globalStatements, functionBodies);
    }

    private static void DeclareBuiltIns(BoundScope scope)
    {
        var print = new FunctionSymbol("print", new[]
        {
            new ParameterSymbol("value", TypeSymbol.String, isReadOnly: true)
        }, TypeSymbol.Void, isBuiltIn: true);

        var println = new FunctionSymbol("println", new[]
        {
            new ParameterSymbol("value", TypeSymbol.String, isReadOnly: true)
        }, TypeSymbol.Void, isBuiltIn: true);

        scope.TryDeclare(print);
        scope.TryDeclare(println);
    }

    private VariableSymbol? BindVariableSymbol(VariableDeclarationSyntax syntax, bool isGlobal)
    {
        var name = syntax.Identifier.Text;
        var type = BindType(syntax.Type);
        var isReadOnly = syntax.MutabilityToken.Kind == TokenKind.DoubleColon;
        var symbol = new VariableSymbol(name, type, isReadOnly, isGlobal);

        if (!_scope.TryDeclare(symbol))
        {
            _diagnostics.Report("BND002", syntax.Identifier.Span, $"Variable '{name}' already declared.");
            return null;
        }

        return symbol;
    }

    private FunctionSymbol? BindFunctionSymbol(ProcedureDeclarationSyntax syntax)
    {
        var name = syntax.Identifier.Text;
        var parameters = new List<ParameterSymbol>();

        foreach (var param in syntax.Parameters)
        {
            var paramType = BindType(param.Type);
            var isReadOnly = param.MutabilityToken.Kind == TokenKind.DoubleColon;
            parameters.Add(new ParameterSymbol(param.Identifier.Text, paramType, isReadOnly));
        }

        var returnType = BindType(syntax.ReturnType);
        var symbol = new FunctionSymbol(name, parameters, returnType, isBuiltIn: false);

        if (!_scope.TryDeclare(symbol))
        {
            _diagnostics.Report("BND003", syntax.Identifier.Span, $"Procedure '{name}' already declared.");
            return null;
        }

        return symbol;
    }

    private BoundBlockStatement BindFunctionBody(FunctionSymbol function, ProcedureDeclarationSyntax syntax)
    {
        var previousFunction = _currentFunction;
        _currentFunction = function;

        var previousScope = _scope;
        _scope = new BoundScope(previousScope);

        foreach (var param in function.Parameters)
        {
            _scope.TryDeclare(param);
        }

        var body = BindBlockStatement(syntax.Body);

        _scope = previousScope;
        _currentFunction = previousFunction;

        return body;
    }

    private BoundBlockStatement BindBlockStatement(BlockStatementSyntax syntax)
    {
        var statements = new List<BoundStatement>();
        var previousScope = _scope;
        _scope = new BoundScope(previousScope);

        foreach (var statement in syntax.Statements)
        {
            statements.Add(BindStatement(statement));
        }

        _scope = previousScope;
        return new BoundBlockStatement(statements);
    }

    private BoundStatement BindStatement(StatementSyntax syntax)
    {
        return syntax switch
        {
            BlockStatementSyntax block => BindBlockStatement(block),
            VariableDeclarationStatementSyntax variable => BindVariableDeclarationStatement(variable),
            ExpressionStatementSyntax expression => new BoundExpressionStatement(BindExpression(expression.Expression)),
            ReturnStatementSyntax returnStatement => BindReturnStatement(returnStatement),
            IfStatementSyntax ifStatement => BindIfStatement(ifStatement),
            IfElseChainStatementSyntax ifChain => BindIfElseChainStatement(ifChain),
            WhileStatementSyntax whileStatement => BindWhileStatement(whileStatement),
            ForStatementSyntax forStatement => BindForStatement(forStatement),
            BreakStatementSyntax breakStatement => BindBreakStatement(breakStatement),
            SkipStatementSyntax skipStatement => BindSkipStatement(skipStatement),
            AssignmentStatementSyntax assignment => BindAssignmentStatement(assignment),
            _ => new BoundExpressionStatement(new BoundLiteralExpression(0, TypeSymbol.Error)),
        };
    }

    private BoundStatement BindVariableDeclarationStatement(VariableDeclarationStatementSyntax syntax)
    {
        var name = syntax.Identifier.Text;
        var type = BindType(syntax.Type);
        var isReadOnly = syntax.MutabilityToken.Kind == TokenKind.DoubleColon;
        var symbol = new VariableSymbol(name, type, isReadOnly, isGlobal: false);

        if (!_scope.TryDeclare(symbol))
        {
            _diagnostics.Report("BND004", syntax.Identifier.Span, $"Variable '{name}' already declared.");
        }

        var initializer = BindExpression(syntax.Initializer);
        if (initializer.Type != type && initializer.Type != TypeSymbol.Error)
        {
            _diagnostics.Report("BND005", syntax.Initializer.Span, "Initializer type does not match variable type.");
        }

        return new BoundVariableDeclaration(symbol, initializer);
    }

    private BoundStatement BindAssignmentStatement(AssignmentStatementSyntax syntax)
    {
        var symbol = _scope.TryLookup(syntax.Identifier.Text) as VariableSymbol;
        if (symbol is null)
        {
            _diagnostics.Report("BND006", syntax.Identifier.Span, $"Variable '{syntax.Identifier.Text}' is not declared.");
            return new BoundExpressionStatement(new BoundLiteralExpression(0, TypeSymbol.Error));
        }

        if (symbol.IsReadOnly)
        {
            _diagnostics.Report("BND007", syntax.Identifier.Span, "Cannot assign to immutable variable.");
        }

        if (syntax.OperatorToken.Kind != TokenKind.Equals)
        {
            _diagnostics.Report("BND008", syntax.OperatorToken.Span, "Only '=' assignment is supported in Phase 1.");
        }

        var expression = BindExpression(syntax.Expression);
        if (expression.Type != symbol.Type && expression.Type != TypeSymbol.Error)
        {
            _diagnostics.Report("BND009", syntax.Expression.Span, "Assignment type mismatch.");
        }

        return new BoundAssignmentStatement(symbol, expression);
    }

    private BoundStatement BindReturnStatement(ReturnStatementSyntax syntax)
    {
        var expected = _currentFunction?.ReturnType ?? TypeSymbol.Void;
        if (syntax.Expression is null)
        {
            if (expected != TypeSymbol.Void)
            {
                _diagnostics.Report("BND010", syntax.Span, "Return expression required.");
            }

            return new BoundReturnStatement(null, expected);
        }

        var expression = BindExpression(syntax.Expression);
        if (expected == TypeSymbol.Void)
        {
            _diagnostics.Report("BND011", syntax.Expression.Span, "Return expression not allowed in void procedure.");
        }
        else if (expression.Type != expected && expression.Type != TypeSymbol.Error)
        {
            _diagnostics.Report("BND012", syntax.Expression.Span, "Return type mismatch.");
        }

        return new BoundReturnStatement(expression, expected);
    }

    private BoundStatement BindIfStatement(IfStatementSyntax syntax)
    {
        var condition = BindExpression(syntax.Condition);
        if (condition.Type != TypeSymbol.Bool && condition.Type != TypeSymbol.Error)
        {
            _diagnostics.Report("BND013", syntax.Condition.Span, "If condition must be bool.");
        }

        var body = BindBlockStatement(syntax.Body);
        return new BoundIfStatement(condition, body);
    }

    private BoundStatement BindIfElseChainStatement(IfElseChainStatementSyntax syntax)
    {
        var clauses = new List<BoundIfClause>();
        var sawDefault = false;

        foreach (var clause in syntax.Clauses)
        {
            if (clause.IsDefault)
            {
                if (sawDefault)
                {
                    _diagnostics.Report("BND030", clause.Span, "Only one default clause is allowed.");
                }

                sawDefault = true;
                var body = BindBlockStatement(clause.Body);
                clauses.Add(new BoundIfClause(null, body));
                continue;
            }

            if (sawDefault)
            {
                _diagnostics.Report("BND031", clause.Span, "Default clause must be last.");
            }

            var condition = BindExpression(clause.Condition!);
            if (condition.Type != TypeSymbol.Bool && condition.Type != TypeSymbol.Error)
            {
                _diagnostics.Report("BND032", clause.Condition!.Span, "If-else chain condition must be bool.");
            }

            var body = BindBlockStatement(clause.Body);
            clauses.Add(new BoundIfClause(condition, body));
        }

        return new BoundIfChainStatement(clauses);
    }

    private BoundStatement BindWhileStatement(WhileStatementSyntax syntax)
    {
        var condition = BindExpression(syntax.Condition);
        if (condition.Type != TypeSymbol.Bool && condition.Type != TypeSymbol.Error)
        {
            _diagnostics.Report("BND033", syntax.Condition.Span, "While condition must be bool.");
        }

        _loopDepth++;
        var body = BindBlockStatement(syntax.Body);
        _loopDepth--;
        return new BoundWhileStatement(condition, body);
    }

    private BoundStatement BindForStatement(ForStatementSyntax syntax)
    {
        var previousScope = _scope;
        _scope = new BoundScope(previousScope);

        BoundStatement? initializer = null;
        if (syntax.Initializer is not null)
        {
            initializer = BindStatement(syntax.Initializer);
        }

        var condition = syntax.Condition is null
            ? new BoundLiteralExpression(true, TypeSymbol.Bool)
            : BindExpression(syntax.Condition);

        if (condition.Type != TypeSymbol.Bool && condition.Type != TypeSymbol.Error)
        {
            _diagnostics.Report("BND034", syntax.Condition?.Span ?? syntax.SecondSemicolon.Span, "For condition must be bool.");
        }

        BoundStatement? increment = null;
        if (syntax.Increment is not null)
        {
            increment = BindStatement(syntax.Increment);
        }

        _loopDepth++;
        var body = BindBlockStatement(syntax.Body);
        _loopDepth--;

        _scope = previousScope;

        return new BoundForStatement(initializer, condition, increment, body);
    }

    private BoundStatement BindBreakStatement(BreakStatementSyntax syntax)
    {
        if (_loopDepth == 0)
        {
            _diagnostics.Report("BND035", syntax.Span, "break is only valid inside a loop.");
        }

        return new BoundBreakStatement();
    }

    private BoundStatement BindSkipStatement(SkipStatementSyntax syntax)
    {
        if (_loopDepth == 0)
        {
            _diagnostics.Report("BND036", syntax.Span, "skip is only valid inside a loop.");
        }

        return new BoundSkipStatement();
    }

    private BoundExpression BindExpression(ExpressionSyntax syntax)
    {
        return syntax switch
        {
            LiteralExpressionSyntax literal => BindLiteralExpression(literal),
            NameExpressionSyntax name => BindNameExpression(name),
            UnaryExpressionSyntax unary => BindUnaryExpression(unary),
            BinaryExpressionSyntax binary => BindBinaryExpression(binary),
            CallExpressionSyntax call => BindCallExpression(call),
            ParenthesizedExpressionSyntax parenthesized => BindExpression(parenthesized.Expression),
            _ => new BoundLiteralExpression(0, TypeSymbol.Error),
        };
    }

    private BoundExpression BindLiteralExpression(LiteralExpressionSyntax syntax)
    {
        var value = syntax.LiteralToken.Value;
        var type = value switch
        {
            long => TypeSymbol.I32,
            double => TypeSymbol.F64,
            bool => TypeSymbol.Bool,
            string => TypeSymbol.String,
            char => TypeSymbol.Char,
            _ => TypeSymbol.Error
        };

        return new BoundLiteralExpression(value, type);
    }

    private BoundExpression BindNameExpression(NameExpressionSyntax syntax)
    {
        var symbol = _scope.TryLookup(syntax.Identifier.Text);
        if (symbol is VariableSymbol variable)
        {
            return new BoundVariableExpression(variable);
        }

        _diagnostics.Report("BND014", syntax.Identifier.Span, $"Unknown identifier '{syntax.Identifier.Text}'.");
        return new BoundLiteralExpression(0, TypeSymbol.Error);
    }

    private BoundExpression BindUnaryExpression(UnaryExpressionSyntax syntax)
    {
        var operand = BindExpression(syntax.Operand);
        var op = BoundUnaryOperator.Bind(syntax.OperatorToken.Kind, operand.Type);
        if (op is null)
        {
            _diagnostics.Report("BND015", syntax.OperatorToken.Span, "Unary operator not defined for type.");
            return new BoundLiteralExpression(0, TypeSymbol.Error);
        }

        return new BoundUnaryExpression(op, operand);
    }

    private BoundExpression BindBinaryExpression(BinaryExpressionSyntax syntax)
    {
        var left = BindExpression(syntax.Left);
        var right = BindExpression(syntax.Right);
        var op = BoundBinaryOperator.Bind(syntax.OperatorToken.Kind, left.Type, right.Type);

        if (op is null)
        {
            _diagnostics.Report("BND016", syntax.OperatorToken.Span, "Binary operator not defined for operands.");
            return new BoundLiteralExpression(0, TypeSymbol.Error);
        }

        return new BoundBinaryExpression(left, op, right);
    }

    private BoundExpression BindCallExpression(CallExpressionSyntax syntax)
    {
        if (syntax.Target is not NameExpressionSyntax target)
        {
            _diagnostics.Report("BND017", syntax.Target.Span, "Only direct calls are supported in Phase 1.");
            return new BoundLiteralExpression(0, TypeSymbol.Error);
        }

        var symbol = _scope.TryLookup(target.Identifier.Text);
        if (symbol is not FunctionSymbol function)
        {
            _diagnostics.Report("BND018", syntax.Target.Span, $"Unknown procedure '{target.Identifier.Text}'.");
            return new BoundLiteralExpression(0, TypeSymbol.Error);
        }

        if (syntax.Arguments.Count != function.Parameters.Count)
        {
            _diagnostics.Report("BND019", syntax.Target.Span, "Argument count mismatch.");
        }

        var arguments = new List<BoundExpression>();
        for (var i = 0; i < syntax.Arguments.Count; i++)
        {
            var argument = BindExpression(syntax.Arguments[i]);
            arguments.Add(argument);

            if (i < function.Parameters.Count)
            {
                var paramType = function.Parameters[i].Type;
                if (argument.Type != paramType && argument.Type != TypeSymbol.Error)
                {
                    _diagnostics.Report("BND020", syntax.Arguments[i].Span, "Argument type mismatch.");
                }
            }
        }

        return new BoundCallExpression(function, arguments);
    }

    private TypeSymbol BindType(TypeSyntax syntax)
    {
        var name = syntax switch
        {
            NamedTypeSyntax named => named.Identifier.Text,
            _ => string.Empty
        };

        var type = name switch
        {
            "i32" => TypeSymbol.I32,
            "f64" => TypeSymbol.F64,
            "bool" => TypeSymbol.Bool,
            "string" => TypeSymbol.String,
            "char" => TypeSymbol.Char,
            "void" => TypeSymbol.Void,
            _ => TypeSymbol.Error
        };

        if (type == TypeSymbol.Error)
        {
            _diagnostics.Report("BND021", syntax.Span, $"Unknown type '{name}'.");
        }

        return type;
    }
}
