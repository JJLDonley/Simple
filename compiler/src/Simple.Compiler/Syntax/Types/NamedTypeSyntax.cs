using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Types;

public sealed class NamedTypeSyntax : TypeSyntax
{
    public NamedTypeSyntax(SyntaxToken identifier)
    {
        Identifier = identifier;
        Span = identifier.Span;
    }

    public SyntaxToken Identifier { get; }
    public override TextSpan Span { get; }
}
