using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Syntax.Expressions;

public sealed class CallExpressionSyntax : ExpressionSyntax
{
    public CallExpressionSyntax(ExpressionSyntax target, SyntaxToken openParenToken, IReadOnlyList<ExpressionSyntax> arguments, SyntaxToken closeParenToken)
    {
        Target = target;
        OpenParenToken = openParenToken;
        Arguments = arguments;
        CloseParenToken = closeParenToken;
        Span = TextSpan.FromBounds(target.Span.Start, closeParenToken.Span.End);
    }

    public ExpressionSyntax Target { get; }
    public SyntaxToken OpenParenToken { get; }
    public IReadOnlyList<ExpressionSyntax> Arguments { get; }
    public SyntaxToken CloseParenToken { get; }
    public override TextSpan Span { get; }
}
