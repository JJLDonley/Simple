using Simple.Compiler.Binding;
using Simple.Compiler.Lexing;
using Simple.Compiler.Module.Text;
using Simple.Compiler.Parsing;
using Simple.Compiler.Syntax;
using Simple.Compiler;
using System.Reflection;

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
        failures += RunTest("Binder_TypeMismatch", BinderTypeMismatch);
        failures += RunTest("Binder_PrintCall", BinderPrintCall);
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
        var program = Binder.BindProgram(syntax.Root, out var diagnostics);
        Assert.True(diagnostics.Count > 0, "Expected type mismatch diagnostic.");
    }

    private static void BinderPrintCall()
    {
        var source = "main : i32 () { print(\"hi\"); return 0 }";
        var syntax = SyntaxTree.Parse(source);
        Binder.BindProgram(syntax.Root, out var diagnostics);
        Assert.Equal(0, diagnostics.Count, "Expected no diagnostics for print call.");
    }

    private static void CompilationEmitHelloWorld()
    {
        if (!CodeGenSupport.IsPersistedAssemblyBuilderAvailable())
        {
            throw new SkipException("PersistedAssemblyBuilder not available.");
        }

        var source = "main : i32 () { print(\"Hello, World!\"); return 0 }";
        var compilation = Compilation.Create(source);
        var outputPath = Path.Combine(Path.GetTempPath(), $"simple-test-{Guid.NewGuid():N}.exe");
        var success = compilation.Emit(outputPath, out var diagnostics);

        Assert.True(success, "Expected emit to succeed.");
        Assert.Equal(0, diagnostics.Count, "Expected no diagnostics.");
        Assert.True(File.Exists(outputPath), "Expected output file.");
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
        return typeof(AssemblyBuilder).Assembly.GetType("System.Reflection.Emit.PersistedAssemblyBuilder") is not null;
    }
}
