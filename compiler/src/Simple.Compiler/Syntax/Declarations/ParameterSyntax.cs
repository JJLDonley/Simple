using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Types;

namespace Simple.Compiler.Syntax.Declarations;

public sealed class ParameterSyntax : SyntaxNode
{
    public ParameterSyntax(SyntaxToken identifier, SyntaxToken mutabilityToken, TypeSyntax type)
    {
        Identifier = identifier;
        MutabilityToken = mutabilityToken;
        Type = type;
        Span = TextSpan.FromBounds(identifier.Span.Start, type.Span.End);
    }

    public SyntaxToken Identifier { get; }
    public SyntaxToken MutabilityToken { get; }
    public TypeSyntax Type { get; }
    public override TextSpan Span { get; }
}
