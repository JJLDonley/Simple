using Simple.Compiler.Binding;
using Simple.Compiler.CodeGen;
using Simple.Compiler.Module.Diagnostics;
using Simple.Compiler.Parsing;

namespace Simple.Compiler;

public sealed class Compilation
{
    public Compilation(SyntaxTree syntaxTree)
    {
        SyntaxTree = syntaxTree;
    }

    public SyntaxTree SyntaxTree { get; }

    public bool Emit(string outputPath, out IReadOnlyList<Diagnostic> diagnostics)
    {
        var diagnosticBag = new DiagnosticBag();
        diagnosticBag.AddRange(SyntaxTree.Diagnostics);

        var boundProgram = Binder.BindProgram(SyntaxTree.Root, out var bindingDiagnostics);
        diagnosticBag.AddRange(bindingDiagnostics);

        if (diagnosticBag.Count > 0)
        {
            diagnostics = diagnosticBag.ToArray();
            return false;
        }

        var assemblyName = Path.GetFileNameWithoutExtension(outputPath);
        var generator = new CodeGenerator(boundProgram);
        generator.Emit(assemblyName, outputPath);
        diagnosticBag.AddRange(generator.Diagnostics);

        diagnostics = diagnosticBag.ToArray();
        return diagnosticBag.Count == 0;
    }

    public IReadOnlyList<Diagnostic> GetDiagnostics()
    {
        var diagnosticBag = new DiagnosticBag();
        diagnosticBag.AddRange(SyntaxTree.Diagnostics);
        Binder.BindProgram(SyntaxTree.Root, out var bindingDiagnostics);
        diagnosticBag.AddRange(bindingDiagnostics);
        return diagnosticBag.ToArray();
    }

    public static Compilation Create(string source)
    {
        var syntaxTree = SyntaxTree.Parse(source);
        return new Compilation(syntaxTree);
    }
}
