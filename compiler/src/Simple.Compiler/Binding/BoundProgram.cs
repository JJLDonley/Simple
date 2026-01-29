using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundProgram
{
    public BoundProgram(
        IReadOnlyList<BoundStatement> globalStatements,
        IReadOnlyDictionary<FunctionSymbol, BoundBlockStatement> functions)
    {
        GlobalStatements = globalStatements;
        Functions = functions;
    }

    public IReadOnlyList<BoundStatement> GlobalStatements { get; }
    public IReadOnlyDictionary<FunctionSymbol, BoundBlockStatement> Functions { get; }
}
