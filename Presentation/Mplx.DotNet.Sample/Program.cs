using System.Globalization;
using Mplx.DotNet;
if (args.Length < 1)
{
    Console.Error.WriteLine("Usage: Mplx.DotNet.Sample <source.mplx|module.mplxb> [entry] [i64 arguments...]");
    return 2;
}
try
{
    using var program = Path.GetExtension(args[0]) == ".mplxb"
        ? MplxProgram.Load(File.ReadAllBytes(args[0]))
        : MplxProgram.CompileFile(args[0]);
    Console.WriteLine(program.InfoJson);
    using var instance = program.CreateInstance(MplxJitMode.Auto, cancellable: false);
    var values = args.Skip(2).Select(v => long.Parse(v, CultureInfo.InvariantCulture)).ToArray();
    var result = instance.Invoke(args.Length > 1 ? args[1] : "main", values,
        new(MaxInstructions: 1_000_000));
    Console.WriteLine($"Result: {result.Result}");
    Console.Error.WriteLine($"Instructions: {result.Stats.Instructions}; native executions: {result.Stats.JitExecutions}");
    return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine(error.Message);
    return 1;
}
