using Simple.Compiler.Module.Diagnostics;
using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax;

namespace Simple.Compiler.Parsing;

public sealed class SyntaxTree
{
    private SyntaxTree(SourceText text, ProgramSyntax root, IReadOnlyList<Diagnostic> diagnostics)
    {
        Text = text;
        Root = root;
        Diagnostics = diagnostics;
    }

    public SourceText Text { get; }
    public ProgramSyntax Root { get; }
    public IReadOnlyList<Diagnostic> Diagnostics { get; }

    public static SyntaxTree Parse(string text)
    {
        var sourceText = new SourceText(text);
        var parser = new Parser(sourceText);
        var root = parser.ParseProgram();
        return new SyntaxTree(sourceText, root, parser.Diagnostics.ToArray());
    }

    public static SyntaxTree Parse(SourceText text)
    {
        var parser = new Parser(text);
        var root = parser.ParseProgram();
        return new SyntaxTree(text, root, parser.Diagnostics.ToArray());
    }
}
