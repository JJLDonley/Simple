using Simple.Compiler.Module.Text;
using Simple.Compiler.Syntax.Statements;
using Simple.Compiler.Syntax.Types;

namespace Simple.Compiler.Syntax.Declarations;

public sealed class ProcedureDeclarationSyntax : DeclarationSyntax
{
    public ProcedureDeclarationSyntax(
        SyntaxToken identifier,
        SyntaxToken mutabilityToken,
        TypeSyntax returnType,
        SyntaxToken openParenToken,
        IReadOnlyList<ParameterSyntax> parameters,
        SyntaxToken closeParenToken,
        BlockStatementSyntax body)
    {
        Identifier = identifier;
        MutabilityToken = mutabilityToken;
        ReturnType = returnType;
        OpenParenToken = openParenToken;
        Parameters = parameters;
        CloseParenToken = closeParenToken;
        Body = body;
        Span = TextSpan.FromBounds(identifier.Span.Start, body.Span.End);
    }

    public SyntaxToken Identifier { get; }
    public SyntaxToken MutabilityToken { get; }
    public TypeSyntax ReturnType { get; }
    public SyntaxToken OpenParenToken { get; }
    public IReadOnlyList<ParameterSyntax> Parameters { get; }
    public SyntaxToken CloseParenToken { get; }
    public BlockStatementSyntax Body { get; }
    public override TextSpan Span { get; }
}
