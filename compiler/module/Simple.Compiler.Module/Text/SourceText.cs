using System.Collections.Generic;

namespace Simple.Compiler.Module.Text;

public sealed class SourceText
{
    private readonly int[] _lineStarts;
    private readonly string _text;

    public SourceText(string text)
    {
        _text = text ?? string.Empty;
        _lineStarts = ComputeLineStarts(_text);
    }

    public string Text => _text;
    public int Length => _text.Length;
    public int LineCount => _lineStarts.Length;

    public char this[int index] => index >= 0 && index < _text.Length ? _text[index] : '\0';

    public string Substring(TextSpan span)
    {
        return _text.Substring(span.Start, span.Length);
    }

    public (int line, int column) GetLineColumn(int position)
    {
        var line = GetLineIndex(position);
        var column = position - _lineStarts[line];
        return (line, column);
    }

    public string GetLineText(int lineIndex)
    {
        var span = GetLineSpan(lineIndex);
        return _text.Substring(span.Start, span.Length);
    }

    public TextSpan GetLineSpan(int lineIndex)
    {
        if (lineIndex < 0 || lineIndex >= _lineStarts.Length)
        {
            return new TextSpan(0, 0);
        }

        var start = _lineStarts[lineIndex];
        var end = lineIndex + 1 < _lineStarts.Length ? _lineStarts[lineIndex + 1] : _text.Length;

        if (end > start && _text[end - 1] == '\n')
        {
            end--;
        }

        if (end > start && _text[end - 1] == '\r')
        {
            end--;
        }

        return TextSpan.FromBounds(start, end);
    }

    public int GetLineIndex(int position)
    {
        if (position < 0)
        {
            return 0;
        }

        if (position >= _text.Length)
        {
            return _lineStarts.Length - 1;
        }

        var index = Array.BinarySearch(_lineStarts, position);
        if (index < 0)
        {
            index = ~index - 1;
        }

        return index;
    }

    private static int[] ComputeLineStarts(string text)
    {
        var starts = new List<int> { 0 };
        var position = 0;

        while (position < text.Length)
        {
            var current = text[position];

            if (current == '\r')
            {
                if (position + 1 < text.Length && text[position + 1] == '\n')
                {
                    position++;
                }

                starts.Add(position + 1);
            }
            else if (current == '\n')
            {
                starts.Add(position + 1);
            }

            position++;
        }

        return starts.ToArray();
    }
}
