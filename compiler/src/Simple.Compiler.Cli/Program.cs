using System.Diagnostics;
using Simple.Compiler;
using Simple.Compiler.Module.Text;
using Simple.Compiler.Parsing;

namespace Simple.Compiler.Cli;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            PrintUsage();
            return 1;
        }

        var command = args[0].ToLowerInvariant();
        var inputPath = args[1];

        if (!File.Exists(inputPath))
        {
            Console.Error.WriteLine($"Input file not found: {inputPath}");
            return 1;
        }

        var source = File.ReadAllText(inputPath);
        var syntaxTree = SyntaxTree.Parse(source);
        var compilation = new Compilation(syntaxTree);

        return command switch
        {
            "build" => Build(compilation, inputPath, args.Skip(2).ToArray()),
            "check" => Check(compilation),
            "run" => Run(compilation, inputPath),
            _ => PrintUsageAndFail()
        };
    }

    private static int Build(Compilation compilation, string inputPath, string[] args)
    {
        var outputPath = Path.ChangeExtension(inputPath, ".exe");

        for (var i = 0; i < args.Length; i++)
        {
            if (args[i] == "-o" && i + 1 < args.Length)
            {
                outputPath = args[i + 1];
                i++;
            }
        }

        if (!compilation.Emit(outputPath, out var diagnostics))
        {
            PrintDiagnostics(compilation.SyntaxTree.Text, diagnostics, inputPath);
            return 1;
        }

        Console.WriteLine($"Built: {outputPath}");
        return 0;
    }

    private static int Check(Compilation compilation)
    {
        var diagnostics = compilation.GetDiagnostics();
        if (diagnostics.Count > 0)
        {
            PrintDiagnostics(compilation.SyntaxTree.Text, diagnostics, "<source>");
            return 1;
        }

        Console.WriteLine("No errors.");
        return 0;
    }

    private static int Run(Compilation compilation, string inputPath)
    {
        var outputPath = Path.Combine(Path.GetTempPath(), $"{Path.GetFileNameWithoutExtension(inputPath)}.dll");

        if (!compilation.Emit(outputPath, out var diagnostics))
        {
            PrintDiagnostics(compilation.SyntaxTree.Text, diagnostics, inputPath);
            return 1;
        }

        var process = Process.Start(new ProcessStartInfo
        {
            FileName = "dotnet",
            Arguments = outputPath,
            UseShellExecute = false
        });

        process?.WaitForExit();
        return process?.ExitCode ?? 1;
    }

    private static void PrintDiagnostics(SourceText text, IReadOnlyList<Simple.Compiler.Module.Diagnostics.Diagnostic> diagnostics, string path)
    {
        foreach (var diagnostic in diagnostics)
        {
            var (line, column) = text.GetLineColumn(diagnostic.Span.Start);
            var lineText = text.GetLineText(line);

            Console.Error.WriteLine($"error[{diagnostic.Code}]: {diagnostic.Message}");
            Console.Error.WriteLine($"  --> {Path.GetFileName(path)}:{line + 1}:{column + 1}");
            Console.Error.WriteLine($"  | {lineText}");
        }
    }

    private static void PrintUsage()
    {
        Console.WriteLine("Usage:");
        Console.WriteLine("  simple build <file.simple> -o <output>");
        Console.WriteLine("  simple check <file.simple>");
        Console.WriteLine("  simple run <file.simple>");
    }

    private static int PrintUsageAndFail()
    {
        PrintUsage();
        return 1;
    }
}
