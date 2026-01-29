using Simple.Compiler.Binding;
using Simple.Compiler.Lexing;
using Simple.Compiler.Module.Text;
using Simple.Compiler.Parsing;
using Simple.Compiler.Syntax;
using Simple.Compiler;
using System.Reflection;
using Emit = System.Reflection.Emit;

namespace Simple.Compiler.Tests;

internal static class Program
{
    private static int Main()
    {
        var failures = 0;
        failures += RunTest("Lexer_Keywords", LexerKeywords);
        failures += RunTest("Lexer_Numbers", LexerNumbers);
        failures += RunTest("Parser_VariableDeclaration", ParserVariableDeclaration);
        failures += RunTest("Parser_Procedure", ParserProcedure);
        failures += RunTest("Parser_IfElseChain", ParserIfElseChain);
        failures += RunTest("Parser_While", ParserWhile);
        failures += RunTest("Parser_For", ParserFor);
        failures += RunTest("Binder_TypeMismatch", BinderTypeMismatch);
        failures += RunTest("Binder_PrintCall", BinderPrintCall);
        failures += RunTest("Binder_BreakOutsideLoop", BinderBreakOutsideLoop);
        failures += RunTest("Binder_SkipOutsideLoop", BinderSkipOutsideLoop);
        failures += RunTest("Binder_WhileConditionType", BinderWhileConditionType);
        failures += RunTest("Binder_ForConditionType", BinderForConditionType);
        failures += RunTest("Compilation_Emit_HelloWorld", CompilationEmitHelloWorld);

        Console.WriteLine(failures == 0 ? "All tests passed." : $"{failures} test(s) failed.");
        return failures == 0 ? 0 : 1;
    }

    private static int RunTest(string name, Action test)
    {
        try
        {
            test();
            Console.WriteLine($"[PASS] {name}");
            return 0;
        }
        catch (SkipException ex)
        {
            Console.WriteLine($"[SKIP] {name}: {ex.Message}");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[FAIL] {name}: {ex.Message}");
            return 1;
        }
    }

    private static void LexerKeywords()
    {
        var text = new SourceText("while for break skip return default Fn self Artifact Enum Module Union true false");
        var lexer = new Lexer(text);
        var tokens = lexer.Lex().Where(t => t.Kind != TokenKind.EndOfFile).ToArray();

        Assert.Equal(14, tokens.Length, "Expected 14 tokens.");
        Assert.Equal(TokenKind.WhileKeyword, tokens[0].Kind, "Expected while keyword.");
        Assert.Equal(TokenKind.FalseKeyword, tokens[^1].Kind, "Expected false keyword.");
    }

    private static void LexerNumbers()
    {
        var text = new SourceText("123 3.14 0xFF 0b1010");
        var lexer = new Lexer(text);
        var tokens = lexer.Lex().Where(t => t.Kind == TokenKind.NumberLiteral).ToArray();

        Assert.Equal(4, tokens.Length, "Expected 4 number literals.");
        Assert.Equal(123L, tokens[0].Value, "Expected integer literal.");
        Assert.Equal(3.14, (double)tokens[1].Value!, "Expected float literal.");
    }

    private static void ParserVariableDeclaration()
    {
        var syntax = SyntaxTree.Parse("x : i32 = 10");
        Assert.Equal(0, syntax.Diagnostics.Count, "Expected no parser diagnostics.");
        Assert.Equal(1, syntax.Root.Declarations.Count, "Expected one declaration.");
    }

    private static void ParserProcedure()
    {
        var syntax = SyntaxTree.Parse("main : i32 () { return 0 }");
        Assert.Equal(0, syntax.Diagnostics.Count, "Expected no parser diagnostics.");
        Assert.Equal(1, syntax.Root.Declarations.Count, "Expected one declaration.");
    }

    private static void BinderTypeMismatch()
    {
        var syntax = SyntaxTree.Parse("x : i32 = 3.14");
        var program = Simple.Compiler.Binding.Binder.BindProgram(syntax.Root, out var diagnostics);
        Assert.True(diagnostics.Count > 0, "Expected type mismatch diagnostic.");
    }

    private static void BinderPrintCall()
    {
        var source = "main : i32 () { print(\"hi\"); return 0 }";
        var syntax = SyntaxTree.Parse(source);
        Simple.Compiler.Binding.Binder.BindProgram(syntax.Root, out var diagnostics);
        Assert.Equal(0, diagnostics.Count, "Expected no diagnostics for print call.");
    }

    private static void ParserIfElseChain()
    {
        var source = "main : i32 () { |> true { x : i32 = 1 } |> default { x : i32 = 2 } return 0 }";
        var syntax = SyntaxTree.Parse(source);
        Assert.Equal(0, syntax.Diagnostics.Count, "Expected no parser diagnostics.");
        Assert.Equal(1, syntax.Root.Declarations.Count, "Expected one declaration.");
    }

    private static void ParserWhile()
    {
        var source = "main : i32 () { while true { return 0 } }";
        var syntax = SyntaxTree.Parse(source);
        Assert.Equal(0, syntax.Diagnostics.Count, "Expected no parser diagnostics.");
    }

    private static void ParserFor()
    {
        var source = "main : i32 () { for (i : i32 = 0; i < 10; i = i + 1) { skip } return 0 }";
        var syntax = SyntaxTree.Parse(source);
        Assert.Equal(0, syntax.Diagnostics.Count, "Expected no parser diagnostics.");
    }

    private static void BinderBreakOutsideLoop()
    {
        var source = "main : i32 () { break return 0 }";
        var syntax = SyntaxTree.Parse(source);
        Simple.Compiler.Binding.Binder.BindProgram(syntax.Root, out var diagnostics);
        Assert.True(diagnostics.Count > 0, "Expected diagnostic for break outside loop.");
    }

    private static void BinderSkipOutsideLoop()
    {
        var source = "main : i32 () { skip return 0 }";
        var syntax = SyntaxTree.Parse(source);
        Simple.Compiler.Binding.Binder.BindProgram(syntax.Root, out var diagnostics);
        Assert.True(diagnostics.Count > 0, "Expected diagnostic for skip outside loop.");
    }

    private static void BinderWhileConditionType()
    {
        var source = "main : i32 () { while 1 { return 0 } }";
        var syntax = SyntaxTree.Parse(source);
        Simple.Compiler.Binding.Binder.BindProgram(syntax.Root, out var diagnostics);
        Assert.True(diagnostics.Count > 0, "Expected diagnostic for non-bool while condition.");
    }

    private static void BinderForConditionType()
    {
        var source = "main : i32 () { for (; 1; ) { return 0 } }";
        var syntax = SyntaxTree.Parse(source);
        Simple.Compiler.Binding.Binder.BindProgram(syntax.Root, out var diagnostics);
        Assert.True(diagnostics.Count > 0, "Expected diagnostic for non-bool for condition.");
    }

    private static void CompilationEmitHelloWorld()
    {
        var source = "main : i32 () { print(\"Hello, World!\"); return 0 }";
        var compilation = Compilation.Create(source);
        var outputPath = Path.Combine(Path.GetTempPath(), $"simple-test-{Guid.NewGuid():N}.exe");
        var success = compilation.Emit(outputPath, out var diagnostics);

        if (CodeGenSupport.IsPersistedAssemblyBuilderAvailable())
        {
            Assert.True(success, "Expected emit to succeed.");
            Assert.Equal(0, diagnostics.Count, "Expected no diagnostics.");
            Assert.True(File.Exists(outputPath), "Expected output file.");
        }
        else
        {
            Assert.True(!success, "Expected emit to fail without PersistedAssemblyBuilder.");
            Assert.True(diagnostics.Any(d => d.Code == "EMT002"), "Expected EMT002 diagnostic for missing PersistedAssemblyBuilder.");
        }
    }
}

internal static class Assert
{
    public static void Equal<T>(T expected, T actual, string message)
    {
        if (!Equals(expected, actual))
        {
            throw new InvalidOperationException($"{message} Expected: {expected}, Actual: {actual}");
        }
    }

    public static void True(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}

internal sealed class SkipException : Exception
{
    public SkipException(string message) : base(message)
    {
    }
}

internal static class CodeGenSupport
{
    public static bool IsPersistedAssemblyBuilderAvailable()
    {
        return typeof(Emit.AssemblyBuilder).Assembly.GetType("System.Reflection.Emit.PersistedAssemblyBuilder") is not null;
    }
}
