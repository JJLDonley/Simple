using System.Reflection;
using System.Reflection.Emit;
using Simple.Compiler.Binding;
using Simple.Compiler.Module.Diagnostics;
using Simple.Compiler.Symbols;
using Simple.Compiler.Syntax;

namespace Simple.Compiler.CodeGen;

public sealed class CodeGenerator
{
    private readonly DiagnosticBag _diagnostics = new();
    private readonly BoundProgram _program;
    private readonly Dictionary<VariableSymbol, FieldBuilder> _globalFields = new();
    private readonly Dictionary<FunctionSymbol, MethodBuilder> _methods = new();

    public CodeGenerator(BoundProgram program)
    {
        _program = program;
    }

    public DiagnosticBag Diagnostics => _diagnostics;

    public void Emit(string assemblyName, string outputPath)
    {
        var name = new AssemblyName(assemblyName);

        var assemblyBuilder = CreateAssemblyBuilder(name);
        var moduleBuilder = assemblyBuilder.DefineDynamicModule(name.Name!);

        var programType = moduleBuilder.DefineType("Program", TypeAttributes.Public | TypeAttributes.Sealed | TypeAttributes.Abstract);

        DefineGlobalFields(programType);
        DefineMethods(programType);
        DefineGlobalInitializer(programType);

        var mainMethod = FindMainMethod();
        if (mainMethod is null)
        {
            _diagnostics.Report("EMT001", new Simple.Compiler.Module.Text.TextSpan(0, 0), "Missing entry point: main : i32 ().");
        }

        var createdType = programType.CreateTypeInfo();

        if (mainMethod is not null)
        {
            TrySetEntryPoint(assemblyBuilder, mainMethod);
        }

        PersistAssembly(assemblyBuilder, outputPath);
    }

    private static AssemblyBuilder CreateAssemblyBuilder(AssemblyName name)
    {
        var builderType = typeof(AssemblyBuilder).Assembly.GetType("System.Reflection.Emit.PersistedAssemblyBuilder");
        if (builderType is not null)
        {
            return (AssemblyBuilder)Activator.CreateInstance(builderType, name, null)!;
        }

        return AssemblyBuilder.DefineDynamicAssembly(name, AssemblyBuilderAccess.Run);
    }

    private void PersistAssembly(AssemblyBuilder builder, string outputPath)
    {
        var saveMethod = builder.GetType().GetMethod("Save", new[] { typeof(string) });
        if (saveMethod is null)
        {
            _diagnostics.Report("EMT002", new Simple.Compiler.Module.Text.TextSpan(0, 0), "PersistedAssemblyBuilder is not available; cannot write output file.");
            return;
        }

        saveMethod.Invoke(builder, new object?[] { outputPath });
    }

    private static void TrySetEntryPoint(AssemblyBuilder builder, MethodInfo method)
    {
        var peFileKindsType = builder.GetType().Assembly.GetType("System.Reflection.Emit.PEFileKinds");
        if (peFileKindsType is null)
        {
            return;
        }

        var setEntryPoint = builder.GetType().GetMethod("SetEntryPoint", new[] { typeof(MethodInfo), peFileKindsType });
        if (setEntryPoint is null)
        {
            return;
        }

        var kind = Enum.Parse(peFileKindsType, "ConsoleApplication");
        setEntryPoint.Invoke(builder, new[] { method, kind });
    }

    private void DefineGlobalFields(TypeBuilder programType)
    {
        foreach (var statement in _program.GlobalStatements)
        {
            if (statement is not BoundVariableDeclaration declaration)
            {
                continue;
            }

            var fieldType = MapType(declaration.Variable.Type);
            var field = programType.DefineField(declaration.Variable.Name, fieldType, FieldAttributes.Public | FieldAttributes.Static);
            _globalFields[declaration.Variable] = field;
        }
    }

    private void DefineMethods(TypeBuilder programType)
    {
        foreach (var entry in _program.Functions)
        {
            var function = entry.Key;
            if (function.IsBuiltIn)
            {
                continue;
            }

            var returnType = MapType(function.ReturnType);
            var parameterTypes = function.Parameters.Select(p => MapType(p.Type)).ToArray();
            var method = programType.DefineMethod(function.Name, MethodAttributes.Public | MethodAttributes.Static, returnType, parameterTypes);
            _methods[function] = method;
        }

        foreach (var entry in _program.Functions)
        {
            var function = entry.Key;
            if (function.IsBuiltIn)
            {
                continue;
            }

            var body = entry.Value;
            var method = _methods[function];
            var emitter = new MethodEmitter(_globalFields, _methods, function, method.GetILGenerator());
            emitter.EmitBlock(body);
        }
    }

    private void DefineGlobalInitializer(TypeBuilder programType)
    {
        if (_program.GlobalStatements.Count == 0)
        {
            return;
        }

        var ctor = programType.DefineConstructor(MethodAttributes.Private | MethodAttributes.Static, CallingConventions.Standard, Type.EmptyTypes);
        var emitter = new MethodEmitter(_globalFields, _methods, null, ctor.GetILGenerator());
        foreach (var statement in _program.GlobalStatements)
        {
            emitter.EmitStatement(statement);
        }

        emitter.EmitReturn();
    }

    private MethodInfo? FindMainMethod()
    {
        foreach (var entry in _methods)
        {
            var function = entry.Key;
            if (function.Name == "main" && function.Parameters.Count == 0 && function.ReturnType == TypeSymbol.I32)
            {
                return entry.Value;
            }
        }

        return null;
    }

    private static Type MapType(TypeSymbol type)
    {
        if (type == TypeSymbol.I32)
        {
            return typeof(int);
        }

        if (type == TypeSymbol.F64)
        {
            return typeof(double);
        }

        if (type == TypeSymbol.Bool)
        {
            return typeof(bool);
        }

        if (type == TypeSymbol.String)
        {
            return typeof(string);
        }

        if (type == TypeSymbol.Char)
        {
            return typeof(char);
        }

        if (type == TypeSymbol.Void)
        {
            return typeof(void);
        }

        return typeof(object);
    }

    private sealed class MethodEmitter
    {
        private readonly Dictionary<VariableSymbol, FieldBuilder> _globalFields;
        private readonly Dictionary<FunctionSymbol, MethodBuilder> _methods;
        private readonly Dictionary<VariableSymbol, LocalBuilder> _locals = new();
        private readonly FunctionSymbol? _function;
        private readonly ILGenerator _il;

        public MethodEmitter(
            Dictionary<VariableSymbol, FieldBuilder> globalFields,
            Dictionary<FunctionSymbol, MethodBuilder> methods,
            FunctionSymbol? function,
            ILGenerator il)
        {
            _globalFields = globalFields;
            _methods = methods;
            _function = function;
            _il = il;
        }

        public void EmitBlock(BoundBlockStatement block)
        {
            foreach (var statement in block.Statements)
            {
                EmitStatement(statement);
            }

            EmitReturnIfNeeded();
        }

        public void EmitStatement(BoundStatement statement)
        {
            switch (statement)
            {
                case BoundBlockStatement block:
                    EmitBlock(block);
                    break;
                case BoundVariableDeclaration declaration:
                    EmitVariableDeclaration(declaration);
                    break;
                case BoundExpressionStatement expressionStatement:
                    EmitExpression(expressionStatement.Expression);
                    if (expressionStatement.Expression.Type != TypeSymbol.Void)
                    {
                        _il.Emit(OpCodes.Pop);
                    }
                    break;
                case BoundReturnStatement returnStatement:
                    EmitReturnStatement(returnStatement);
                    break;
                case BoundIfStatement ifStatement:
                    EmitIfStatement(ifStatement);
                    break;
                case BoundAssignmentStatement assignment:
                    EmitAssignment(assignment);
                    break;
            }
        }

        public void EmitReturn()
        {
            _il.Emit(OpCodes.Ret);
        }

        private void EmitReturnIfNeeded()
        {
            if (_function is null)
            {
                return;
            }

            if (_function.ReturnType == TypeSymbol.Void)
            {
                _il.Emit(OpCodes.Ret);
                return;
            }

            _il.Emit(OpCodes.Ldc_I4_0);
            _il.Emit(OpCodes.Ret);
        }

        private void EmitVariableDeclaration(BoundVariableDeclaration declaration)
        {
            if (declaration.Variable.IsGlobal)
            {
                EmitExpression(declaration.Initializer);
                _il.Emit(OpCodes.Stsfld, _globalFields[declaration.Variable]);
                return;
            }

            var local = _il.DeclareLocal(MapType(declaration.Variable.Type));
            _locals[declaration.Variable] = local;
            EmitExpression(declaration.Initializer);
            _il.Emit(OpCodes.Stloc, local);
        }

        private void EmitAssignment(BoundAssignmentStatement assignment)
        {
            EmitExpression(assignment.Expression);

            if (assignment.Variable.IsGlobal)
            {
                _il.Emit(OpCodes.Stsfld, _globalFields[assignment.Variable]);
                return;
            }

            if (_locals.TryGetValue(assignment.Variable, out var local))
            {
                _il.Emit(OpCodes.Stloc, local);
                return;
            }

            var index = GetParameterIndex(assignment.Variable);
            if (index >= 0)
            {
                _il.Emit(OpCodes.Starg, index);
            }
        }

        private void EmitReturnStatement(BoundReturnStatement statement)
        {
            if (statement.Expression is not null)
            {
                EmitExpression(statement.Expression);
            }
            else if (statement.ExpectedType != TypeSymbol.Void)
            {
                _il.Emit(OpCodes.Ldc_I4_0);
            }

            _il.Emit(OpCodes.Ret);
        }

        private void EmitIfStatement(BoundIfStatement statement)
        {
            EmitExpression(statement.Condition);
            var endLabel = _il.DefineLabel();
            _il.Emit(OpCodes.Brfalse, endLabel);
            EmitStatement(statement.Body);
            _il.MarkLabel(endLabel);
        }

        private void EmitExpression(BoundExpression expression)
        {
            switch (expression)
            {
                case BoundLiteralExpression literal:
                    EmitLiteral(literal);
                    break;
                case BoundVariableExpression variable:
                    EmitVariable(variable);
                    break;
                case BoundUnaryExpression unary:
                    EmitUnary(unary);
                    break;
                case BoundBinaryExpression binary:
                    EmitBinary(binary);
                    break;
                case BoundCallExpression call:
                    EmitCall(call);
                    break;
            }
        }

        private void EmitLiteral(BoundLiteralExpression literal)
        {
            if (literal.Value is null)
            {
                _il.Emit(OpCodes.Ldnull);
                return;
            }

            switch (literal.Value)
            {
                case long intValue:
                    _il.Emit(OpCodes.Ldc_I4, unchecked((int)intValue));
                    break;
                case double doubleValue:
                    _il.Emit(OpCodes.Ldc_R8, doubleValue);
                    break;
                case bool boolValue:
                    _il.Emit(boolValue ? OpCodes.Ldc_I4_1 : OpCodes.Ldc_I4_0);
                    break;
                case string stringValue:
                    _il.Emit(OpCodes.Ldstr, stringValue);
                    break;
                case char charValue:
                    _il.Emit(OpCodes.Ldc_I4, charValue);
                    break;
                default:
                    _il.Emit(OpCodes.Ldnull);
                    break;
            }
        }

        private void EmitVariable(BoundVariableExpression variable)
        {
            if (variable.Variable.IsGlobal)
            {
                _il.Emit(OpCodes.Ldsfld, _globalFields[variable.Variable]);
                return;
            }

            if (_locals.TryGetValue(variable.Variable, out var local))
            {
                _il.Emit(OpCodes.Ldloc, local);
                return;
            }

            var index = GetParameterIndex(variable.Variable);
            if (index >= 0)
            {
                _il.Emit(OpCodes.Ldarg, index);
            }
        }

        private void EmitUnary(BoundUnaryExpression unary)
        {
            EmitExpression(unary.Operand);
            switch (unary.Operator.SyntaxKind)
            {
                case TokenKind.Minus:
                    _il.Emit(OpCodes.Neg);
                    break;
                case TokenKind.Bang:
                    _il.Emit(OpCodes.Ldc_I4_0);
                    _il.Emit(OpCodes.Ceq);
                    break;
                case TokenKind.Plus:
                    break;
            }
        }

        private void EmitBinary(BoundBinaryExpression binary)
        {
            EmitExpression(binary.Left);
            EmitExpression(binary.Right);

            switch (binary.Operator.SyntaxKind)
            {
                case TokenKind.Plus:
                    _il.Emit(OpCodes.Add);
                    break;
                case TokenKind.Minus:
                    _il.Emit(OpCodes.Sub);
                    break;
                case TokenKind.Star:
                    _il.Emit(OpCodes.Mul);
                    break;
                case TokenKind.Slash:
                    _il.Emit(OpCodes.Div);
                    break;
                case TokenKind.Percent:
                    _il.Emit(OpCodes.Rem);
                    break;
                case TokenKind.EqualsEquals:
                    _il.Emit(OpCodes.Ceq);
                    break;
                case TokenKind.BangEquals:
                    _il.Emit(OpCodes.Ceq);
                    _il.Emit(OpCodes.Ldc_I4_0);
                    _il.Emit(OpCodes.Ceq);
                    break;
                case TokenKind.Less:
                    _il.Emit(OpCodes.Clt);
                    break;
                case TokenKind.Greater:
                    _il.Emit(OpCodes.Cgt);
                    break;
                case TokenKind.LessOrEquals:
                    _il.Emit(OpCodes.Cgt);
                    _il.Emit(OpCodes.Ldc_I4_0);
                    _il.Emit(OpCodes.Ceq);
                    break;
                case TokenKind.GreaterOrEquals:
                    _il.Emit(OpCodes.Clt);
                    _il.Emit(OpCodes.Ldc_I4_0);
                    _il.Emit(OpCodes.Ceq);
                    break;
            }
        }

        private void EmitCall(BoundCallExpression call)
        {
            if (call.Function.IsBuiltIn)
            {
                EmitBuiltInCall(call);
                return;
            }

            foreach (var argument in call.Arguments)
            {
                EmitExpression(argument);
            }

            _il.Emit(OpCodes.Call, _methods[call.Function]);
        }

        private void EmitBuiltInCall(BoundCallExpression call)
        {
            foreach (var argument in call.Arguments)
            {
                EmitExpression(argument);
            }

            if (call.Function.Name == "print")
            {
                var method = typeof(Console).GetMethod("Write", new[] { typeof(string) });
                _il.Emit(OpCodes.Call, method!);
                return;
            }

            var println = typeof(Console).GetMethod("WriteLine", new[] { typeof(string) });
            _il.Emit(OpCodes.Call, println!);
        }

        private int GetParameterIndex(VariableSymbol variable)
        {
            if (_function is null)
            {
                return -1;
            }

            for (var i = 0; i < _function.Parameters.Count; i++)
            {
                if (_function.Parameters[i].Name == variable.Name)
                {
                    return i;
                }
            }

            return -1;
        }

        private static Type MapType(TypeSymbol type)
        {
            if (type == TypeSymbol.I32)
            {
                return typeof(int);
            }

            if (type == TypeSymbol.F64)
            {
                return typeof(double);
            }

            if (type == TypeSymbol.Bool)
            {
                return typeof(bool);
            }

            if (type == TypeSymbol.String)
            {
                return typeof(string);
            }

            if (type == TypeSymbol.Char)
            {
                return typeof(char);
            }

            return typeof(void);
        }
    }
}
