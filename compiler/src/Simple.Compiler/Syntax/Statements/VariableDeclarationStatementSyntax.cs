using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Expressions;
using Simple.Compiler.Syntax.Types;

namespace Simple.Compiler.Syntax.Statements;

public sealed class VariableDeclarationStatementSyntax : StatementSyntax
{
    public VariableDeclarationStatementSyntax(SyntaxToken identifier, SyntaxToken mutabilityToken, TypeSyntax type, SyntaxToken equalsToken, ExpressionSyntax initializer)
    {
        Identifier = identifier;
        MutabilityToken = mutabilityToken;
        Type = type;
        EqualsToken = equalsToken;
        Initializer = initializer;
        Span = TextSpan.FromBounds(identifier.Span.Start, initializer.Span.End);
    }

    public SyntaxToken Identifier { get; }
    public SyntaxToken MutabilityToken { get; }
    public TypeSyntax Type { get; }
    public SyntaxToken EqualsToken { get; }
    public ExpressionSyntax Initializer { get; }
    public override TextSpan Span { get; }
}
