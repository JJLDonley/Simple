using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax;

public abstract class SyntaxNode
{
    public abstract TextSpan Span { get; }
}
