namespace Simple.Compiler.Symbols;

public class VariableSymbol : Symbol
{
    public VariableSymbol(string name, TypeSymbol type, bool isReadOnly, bool isGlobal)
        : base(name)
    {
        Type = type;
        IsReadOnly = isReadOnly;
        IsGlobal = isGlobal;
    }

    public TypeSymbol Type { get; }
    public bool IsReadOnly { get; }
    public bool IsGlobal { get; }
}
