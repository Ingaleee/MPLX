using System.Text.Json;
using Mplx.DotNet;
var checks = 0;
void Assert(bool condition, string message)
{
    if (!condition)
        throw new InvalidOperationException(message);
    ++checks;
}
void Fails(string source, string expected, MplxExecutionOptions? options = null)
{
    try
    {
        MplxRuntime.RunFromSource(source, options: options);
    }
    catch (InvalidOperationException error)
    {
        Assert(error.Message.Contains(expected, StringComparison.Ordinal), error.Message);
        return;
    }
    throw new InvalidOperationException("Expected failure: " + expected);
}
for (var i = 0; i < 200; ++i)
    Assert(MplxRuntime.RunFromSource("fn main()->i64{let x=40;return x+2;}") == 42, "Wrong native result.");
using (var json = JsonDocument.Parse(MplxRuntime.CheckSource("fn main()->i64{return missing;}")))
    Assert(json.RootElement.GetProperty("diagnostics").GetArrayLength() > 0, "Missing semantic diagnostic.");
Fails("fn main()->i64{return @;}", "diagnostics");
Fails("fn main()->i64{return 1/0;}", "division by zero");
Fails("fn main()->i64{while(true){}return 0;}", "instruction limit", new(MaxInstructions: 25));
Assert(MplxRuntime.RunFromSource("fn main()->i64{return 7;}", options: new()) == 7, "Options ABI mismatch.");
Assert(MplxRuntime.RunFromSource("fn main()->bool{return bool(42);}") == 1, "bool conversion mismatch.");
Assert(MplxRuntime.RunFromSource("fn f(n:i32)->i32{return n+1;}fn main()->i64{return f(41);}") == 42, "Typed call mismatch.");
Fails("fn main()->i32{return 2147483648;}", "i32 range exceeded");
Fails("fn main()->i64{return true+1;}", "MPLX2014");
Fails("fn f(n:i64)->i64{return f(n+1);}fn main()->i64{return f(0);}", "frame limit", new(MaxFrames: 8));
using (var json = JsonDocument.Parse(MplxRuntime.CheckSource("fn main()->i64{\n  return missing;\n}")))
{
    var diagnostic = json.RootElement.GetProperty("diagnostics")[0];
    Assert(diagnostic.GetProperty("range").GetProperty("start").GetProperty("line").GetInt32() == 2, "Source line lost across C ABI.");
}
foreach (var operation in new Action[] {
    () => MplxRuntime.RunFromSource("fn main()->i64{return 1;}\0"),
    () => MplxRuntime.RunFromSource("fn main()->i64{return 1;}", "main\0other"),
    () => MplxRuntime.CheckSource("fn main()->i64{return 1;}\0") })
{
    var rejected = false;
    try
    {
        operation();
    }
    catch (ArgumentException) { rejected = true; }
    Assert(rejected, "Embedded NUL was silently truncated across P/Invoke.");
}

using (var program = MplxProgram.Compile("fn add(a:i32,b:bool)->i64{return i64(a)+i64(b);}fn bad()->i64{\nreturn 1/0;\n}fn spin()->i64{while(true){}return 0;}fn main()->i64{return 42;}"))
{
    using var imported = MplxProgram.Load(program.Save());
    Assert(program.InfoJson == imported.InfoJson, "Module metadata round trip.");
    using var instance = imported.CreateInstance();
    imported.Dispose();
    Assert(instance.Invoke("add", new long[] { 41, 1 }).Result == 42, "SafeHandle instance/program lifetime.");
    try
    {
        instance.Invoke("add", new long[] { 2147483648, 0 });
        throw new Exception("Expected argument rejection.");
    }
    catch (MplxException error) { Assert(error.Status == MplxStatus.InvalidArgument && error.Function == "add", "Typed export boundary."); }
    try
    {
        instance.Invoke("bad");
        throw new Exception("Expected source trap.");
    }
    catch (MplxException error)
    {
        Assert(error.Status == MplxStatus.RuntimeFailure && error.Function == "bad" && error.ProgramCounter.HasValue, "Structured runtime fault.");
        Assert(error.SourceRange?.Start.Line == 2 && error.Stats?.Instructions > 0, "Fault source range/stats.");
    }
    instance.Cancel();
    try
    {
        instance.Invoke();
        throw new Exception("Expected cancellation.");
    }
    catch (MplxException error) { Assert(error.Status == MplxStatus.Cancelled && error.Stats?.Instructions == 0, "Cancellation latch."); }
    instance.ResetCancellation();
    Assert(instance.Invoke().Result == 42, "Instance recovery.");
    var damaged = program.Save();
    damaged[^1] ^= 1;
    try
    {
        using var invalid = MplxProgram.Load(damaged);
        throw new Exception("Expected corrupt module rejection.");
    }
    catch (MplxException error) { Assert(error.Status == MplxStatus.InvalidModule, "Module integrity."); }
    var concurrent = Enumerable.Range(0, 8).Select(n => Task.Run(() =>
    {
        using var worker = program.CreateInstance(MplxJitMode.Auto, cancellable: false);
        ulong compilations = 0;
        for (var i = 0; i < 50; ++i)
        {
            var invocation = worker.Invoke("add", new long[] { n + i, 1 });
            if (invocation.Result != n + i + 1)
                throw new InvalidOperationException("Concurrent instance result.");
            compilations += invocation.Stats.JitCompilations;
        }
        return compilations;
    })).ToArray();
    var compilationCounts = Task.WhenAll(concurrent).GetAwaiter().GetResult();
    Assert(compilationCounts.Sum(n => (long)n) <= 1, "JIT image was not shared.");
    var execution = Task.Run(() =>
    {
        try
        {
            instance.Invoke("spin", limits: new(MaxInstructions: ulong.MaxValue));
            return MplxStatus.Ok;
        }
        catch (MplxException error) { return error.Status; }
    });
    var deadline = DateTime.UtcNow.AddSeconds(5);
    while (!instance.IsRunning && !execution.IsCompleted && DateTime.UtcNow < deadline)
        Thread.Sleep(1);
    var busy = instance.IsRunning;
    instance.Cancel();
    var cancelled = execution.GetAwaiter().GetResult();
    Assert(busy && cancelled == MplxStatus.Cancelled, "Cross-thread SafeHandle cancellation.");
    instance.ResetCancellation();
    Assert(instance.Invoke().Result == 42, "Recovery after concurrent cancellation.");
    instance.Dispose();
    var disposed = false;
    try
    {
        instance.Invoke();
    }
    catch (ObjectDisposedException) { disposed = true; }
    Assert(disposed, "Disposed instance remained callable.");
}

var moduleDirectory = Path.Combine(Path.GetTempPath(), "mplx-dotnet-" + Guid.NewGuid());
Directory.CreateDirectory(moduleDirectory);
try
{
    var entry = Path.Combine(moduleDirectory, "main.mplx");
    var dependency = Path.Combine(moduleDirectory, "lib.mplx");
    File.WriteAllText(entry, "import \"lib.mplx\" as lib;fn main()->i64{return lib::value();}");
    File.WriteAllText(dependency, "export fn value()->i64{return 42;}");
    using (var workspace = new MplxWorkspace(moduleDirectory))
    {
        using var firstBuild = workspace.Compile("main.mplx");
        using var secondBuild = workspace.Compile("main.mplx");
        Assert(firstBuild.Statistics.Parsed == 2 && secondBuild.Statistics.ArtifactHit &&
               secondBuild.Statistics.Checked == 0, "Persistent .NET workspace cache.");
        using var firstInstance = firstBuild.Program.CreateInstance(MplxJitMode.On, cancellable: false);
        using var secondInstance = secondBuild.Program.CreateInstance(MplxJitMode.On, cancellable: false);
        var firstCall = firstInstance.Invoke();
        var secondCall = secondInstance.Invoke();
        Assert(firstCall.Result == 42 && secondCall.Result == 42 &&
               firstCall.Stats.JitCompilations + secondCall.Stats.JitCompilations <= 1,
               "Workspace results share native publication.");
        using var updated = workspace.Compile("main.mplx", new Dictionary<string, string>
        {
            ["lib.mplx"] = "export fn value()->i64{return 43;}"
        });
        Assert(updated.Statistics.Checked == 1 && updated.Statistics.SemanticHits == 1,
               "Workspace overlay invalidation.");
        using var updatedInstance = updated.Program.CreateInstance();
        Assert(updatedInstance.Invoke().Result == 43, "Overlay-generated executable.");
        workspace.Dispose();
        Assert(updatedInstance.Invoke().Result == 43, "Workspace disposal preserves executable instances.");
    }
    using var program = MplxProgram.CompileFile(entry);
    using var instance = program.CreateInstance(MplxJitMode.On, cancellable: false);
    Assert(instance.Invoke().Result == 42, "File imports across P/Invoke.");
    using var portable = MplxProgram.Load(program.Save());
    using var loaded = portable.CreateInstance();
    File.Delete(dependency);
    Assert(loaded.Invoke().Result == 42, "Linked module retained dependency code.");
    try
    {
        using var missing = MplxProgram.CompileFile(entry);
        throw new Exception("Expected missing dependency failure.");
    }
    catch (MplxException error)
    {
        Assert(error.Status == MplxStatus.SourceDiagnostics && error.Message.Contains("MPLX4002"),
               "Module graph diagnostics through C ABI.");
    }
    try
    {
        using var outside = MplxProgram.CompileFile(entry, Path.Combine(moduleDirectory, "sub"));
        throw new Exception("Expected root escape rejection.");
    }
    catch (MplxException error)
    {
        Assert(error.Status == MplxStatus.SourceDiagnostics && error.Message.Contains("MPLX4001"),
               "Embedding workspace root confinement.");
    }
}
finally
{
    Directory.Delete(moduleDirectory, recursive: true);
}
Console.WriteLine($".NET/native integration: {checks} checks passed.");
