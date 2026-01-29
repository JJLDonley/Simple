namespace Simple.Compiler.Symbols;

public sealed class ParameterSymbol : VariableSymbol
{
    public ParameterSymbol(string name, TypeSymbol type, bool isReadOnly)
        : base(name, type, isReadOnly, isGlobal: false)
    {
    }
}
