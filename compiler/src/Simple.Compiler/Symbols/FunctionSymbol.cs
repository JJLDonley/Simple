namespace Simple.Compiler.Symbols;

public sealed class FunctionSymbol : Symbol
{
    public FunctionSymbol(string name, IReadOnlyList<ParameterSymbol> parameters, TypeSymbol returnType, bool isBuiltIn)
        : base(name)
    {
        Parameters = parameters;
        ReturnType = returnType;
        IsBuiltIn = isBuiltIn;
    }

    public IReadOnlyList<ParameterSymbol> Parameters { get; }
    public TypeSymbol ReturnType { get; }
    public bool IsBuiltIn { get; }
}
