namespace Simple.Compiler.Symbols;

public sealed class TypeSymbol : Symbol
{
    private TypeSymbol(string name) : base(name)
    {
    }

    public static readonly TypeSymbol I32 = new("i32");
    public static readonly TypeSymbol F64 = new("f64");
    public static readonly TypeSymbol Bool = new("bool");
    public static readonly TypeSymbol String = new("string");
    public static readonly TypeSymbol Char = new("char");
    public static readonly TypeSymbol Void = new("void");
    public static readonly TypeSymbol Error = new("<error>");
}
