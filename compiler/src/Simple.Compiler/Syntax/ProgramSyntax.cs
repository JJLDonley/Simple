using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Declarations;

namespace Simple.Compiler.Syntax;

public sealed class ProgramSyntax : SyntaxNode
{
    public ProgramSyntax(IReadOnlyList<DeclarationSyntax> declarations)
    {
        Declarations = declarations;
        Span = declarations.Count > 0
            ? TextSpan.FromBounds(declarations[0].Span.Start, declarations[^1].Span.End)
            : new TextSpan(0, 0);
    }

    public IReadOnlyList<DeclarationSyntax> Declarations { get; }
    public override TextSpan Span { get; }
}
