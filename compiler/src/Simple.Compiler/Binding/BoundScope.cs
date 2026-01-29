using Simple.Compiler.Symbols;

namespace Simple.Compiler.Binding;

public sealed class BoundScope
{
    private readonly Dictionary<string, Symbol> _symbols = new(StringComparer.Ordinal);

    public BoundScope(BoundScope? parent)
    {
        Parent = parent;
    }

    public BoundScope? Parent { get; }

    public bool TryDeclare(Symbol symbol)
    {
        if (_symbols.ContainsKey(symbol.Name))
        {
            return false;
        }

        _symbols[symbol.Name] = symbol;
        return true;
    }

    public Symbol? TryLookup(string name)
    {
        if (_symbols.TryGetValue(name, out var symbol))
        {
            return symbol;
        }

        return Parent?.TryLookup(name);
    }

    public IEnumerable<Symbol> GetDeclaredSymbols()
    {
        return _symbols.Values;
    }
}
