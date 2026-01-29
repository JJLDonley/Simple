using System.Collections;
using System.Collections.Generic;
using Simple.Compiler.Module.Text;

namespace Simple.Compiler.Module.Diagnostics;

public sealed class DiagnosticBag : IReadOnlyList<Diagnostic>
{
    private readonly List<Diagnostic> _diagnostics = new();

    public int Count => _diagnostics.Count;
    public Diagnostic this[int index] => _diagnostics[index];

    public void Report(string code, TextSpan span, string message)
    {
        _diagnostics.Add(new Diagnostic(code, span, message));
    }

    public void AddRange(IEnumerable<Diagnostic> diagnostics)
    {
        _diagnostics.AddRange(diagnostics);
    }

    public IEnumerator<Diagnostic> GetEnumerator()
    {
        return _diagnostics.GetEnumerator();
    }

    IEnumerator IEnumerable.GetEnumerator()
    {
        return GetEnumerator();
    }
}
