namespace Simple.Compiler.Symbols;

public abstract class Symbol
{
    protected Symbol(string name)
    {
        Name = name;
    }

    public string Name { get; }
}
