using System;
using System.IO;
using Mplx.DotNet;

class Program
{
    static void Main(string[] args)
    {
        var file = args.Length > 0 ? args[0] : Path.Combine("examples", "hello.mplx");
        var src  = File.ReadAllText(file);

        Console.WriteLine("== Check ==");
        var diag = MplxRuntime.CheckSource(src);
        Console.WriteLine(diag);

        Console.WriteLine("== Run ==");
        var rv = MplxRuntime.RunFromSource(src, "main");
        Console.WriteLine($"Return: {rv}");
    }
}