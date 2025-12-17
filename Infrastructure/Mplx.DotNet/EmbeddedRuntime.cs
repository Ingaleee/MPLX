using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;
namespace Mplx.DotNet;

public enum MplxJitMode
{
    Off, On, Auto
}
public enum MplxStatus
{
    Ok,
    InvalidArgument,
    AllocationFailure,
    RuntimeFailure,
    SourceDiagnostics,
    InvalidModule,
    Busy,
    Cancelled
}
public sealed record MplxExecutionStats(ulong Instructions, ulong PeakOperands, ulong PeakLocals,
                                        ulong PeakFrames, ulong JitCompilations, ulong JitExecutions,
                                        ulong JitFallbacks, ulong NativeCalls);
public sealed record MplxInvocation(long Result, MplxExecutionStats Stats);
public sealed record MplxSourcePosition(uint Offset, uint Line, uint Column);
public sealed record MplxSourceRange(MplxSourcePosition Start, MplxSourcePosition End);
public sealed class MplxException : InvalidOperationException
{
    public MplxStatus Status
    {
        get;
    }
    public int? RuntimeKind
    {
        get;
    }
    public string? Function
    {
        get;
    }
    public uint? ProgramCounter
    {
        get;
    }
    public MplxSourceRange? SourceRange
    {
        get;
    }
    public MplxExecutionStats? Stats
    {
        get;
    }
    internal MplxException(int status, string message, MplxExecutionStats? stats = null) : base(message)
    {
        Status = (MplxStatus)status;
        Stats = stats;
        try
        {
            using var json = JsonDocument.Parse(message);
            var root = json.RootElement;
            if (root.TryGetProperty("kind", out var kind))
                RuntimeKind = kind.GetInt32();
            if (root.TryGetProperty("function", out var function))
                Function = function.GetString();
            if (root.TryGetProperty("pc", out var pc))
                ProgramCounter = pc.GetUInt32();
            if (root.TryGetProperty("range", out var range))
            {
                static MplxSourcePosition Point(JsonElement p) =>
                    new(p.GetProperty("offset").GetUInt32(), p.GetProperty("line").GetUInt32(),
                        p.GetProperty("column").GetUInt32());
                SourceRange = new(Point(range.GetProperty("start")), Point(range.GetProperty("end")));
            }
        }
        catch (JsonException)
        {
        }
    }
}
public sealed class MplxProgram : IDisposable
{
    private readonly ProgramHandle handle;
    internal MplxProgram(ProgramHandle value)
    {
        handle = value;
    }
    public static MplxProgram Compile(string source, bool optimize = true)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (source.Contains('\0'))
            throw new ArgumentException("Source contains embedded NUL.", nameof(source));
        if (Encoding.UTF8.GetByteCount(source) > 1024 * 1024)
            throw new ArgumentOutOfRangeException(nameof(source), "UTF-8 source exceeds 1 MiB.");
        var bytes = Encoding.UTF8.GetBytes(source);
        var status = EmbeddedNative.Compile(bytes, (nuint)bytes.Length, optimize ? 1u : 0u, out var value,
                                            out var error);
        try
        {
            EmbeddedNative.Check(status, error);
            return new(value);
        }
        catch
        {
            value.Dispose();
            throw;
        }
    }
    public static MplxProgram CompileFile(string entry, string? workspaceRoot = null, bool optimize = true)
    {
        ArgumentNullException.ThrowIfNull(entry);
        if (entry.Contains('\0') || workspaceRoot?.Contains('\0') == true)
        {
            throw new ArgumentException("Paths contain embedded NUL.");
        }
        var fullEntry = Path.GetFullPath(entry);
        var root = Path.GetFullPath(workspaceRoot ?? Path.GetDirectoryName(fullEntry)!);
        var status =
            EmbeddedNative.CompileFile(root, fullEntry, optimize ? 1u : 0u, out var value, out var error);
        try
        {
            EmbeddedNative.Check(status, error);
            return new(value);
        }
        catch
        {
            value.Dispose();
            throw;
        }
    }
    public static MplxProgram Load(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length > 64 * 1024 * 1024)
            throw new ArgumentOutOfRangeException(nameof(bytes));
        var data = bytes.ToArray();
        var status = EmbeddedNative.Load(data, (nuint)data.Length, out var value, out var error);
        try
        {
            EmbeddedNative.Check(status, error);
            return new(value);
        }
        catch
        {
            value.Dispose();
            throw;
        }
    }
    public byte[] Save()
    {
        var status = EmbeddedNative.Save(handle, out var bytes, out var size, out var error);
        EmbeddedNative.Check(status, error);
        try
        {
            var result = new byte[checked((int)size)];
            Marshal.Copy(bytes, result, 0, result.Length);
            return result;
        }
        finally
        {
            EmbeddedNative.FreeBuffer(bytes);
        }
    }
    public string InfoJson
    {
        get
        {
            var status = EmbeddedNative.Info(handle, out var text, out var error);
            var result = EmbeddedNative.ReadAndFree(text);
            EmbeddedNative.Check(status, error);
            return result ?? throw new InvalidOperationException("Native program info is missing.");
        }
    }
    public MplxInstance CreateInstance(MplxJitMode jit = MplxJitMode.Off, bool cancellable = true)
    {
        var options = new EmbeddedNative.InstanceOptions
        {
            Size = (uint)Marshal.SizeOf<EmbeddedNative.InstanceOptions>(),
            Version = 1,
            Jit = (uint)jit,
            Flags = cancellable ? 1u : 0u
        };
        var status = EmbeddedNative.CreateInstance(handle, in options, out var value, out var error);
        try
        {
            EmbeddedNative.Check(status, error);
            return new(value);
        }
        catch
        {
            value.Dispose();
            throw;
        }
    }
    public void Dispose() => handle.Dispose();
}
public sealed class MplxInstance : IDisposable
{
    private readonly InstanceHandle handle;
    internal MplxInstance(InstanceHandle value)
    {
        handle = value;
    }
    public MplxInvocation Invoke(string entry = "main", ReadOnlySpan<long> arguments = default,
                                 MplxExecutionOptions? limits = null)
    {
        ArgumentNullException.ThrowIfNull(entry);
        if (entry.Contains('\0'))
            throw new ArgumentException("Entry contains embedded NUL.", nameof(entry));
        if (arguments.Length > 255)
            throw new ArgumentOutOfRangeException(nameof(arguments));
        limits ??= new();
        var options =
            new EmbeddedNative.RuntimeOptions
            {
                Size = (uint)Marshal.SizeOf<EmbeddedNative.RuntimeOptions>(),
                Version = 1,
                Instructions = limits.MaxInstructions,
                Frames = limits.MaxFrames,
                Values = limits.MaxValues
            };
        var stats =
            new EmbeddedNative.Stats { Size = (uint)Marshal.SizeOf<EmbeddedNative.Stats>(), Version = 1 };
        var status = EmbeddedNative.Invoke(handle, entry, arguments.ToArray(), (nuint)arguments.Length,
                                           in options, out var result, ref stats, out var error);
        var snapshot = stats.Snapshot();
        EmbeddedNative.Check(status, error, snapshot);
        return new(result, snapshot);
    }
    public void Cancel()
    {
        var status = EmbeddedNative.Cancel(handle);
        if (status != 0)
            throw new MplxException(status, "Instance does not support cancellation.");
    }
    public bool IsRunning => EmbeddedNative.IsRunning(handle) != 0;
    public void ResetCancellation()
    {
        var status = EmbeddedNative.ResetCancel(handle);
        if (status != 0)
            throw new MplxException(status,
                                    "Cannot reset cancellation while instance is busy or uncancellable.");
    }
    public void Dispose() => handle.Dispose();
}
internal sealed class ProgramHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public ProgramHandle() : base(true)
    {
    }
    protected override bool ReleaseHandle()
    {
        EmbeddedNative.DestroyProgram(handle);
        return true;
    }
}
internal sealed class InstanceHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public InstanceHandle() : base(true)
    {
    }
    protected override bool ReleaseHandle()
    {
        EmbeddedNative.DestroyInstance(handle);
        return true;
    }
}
internal static class EmbeddedNative
{
    private const string Library = "mplx_native";
    [StructLayout(LayoutKind.Sequential)]
    internal struct InstanceOptions
    {
        public uint Size, Version, Jit, Flags;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct RuntimeOptions
    {
        public uint Size, Version;
        public ulong Instructions, Frames, Values;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Stats
    {
        public uint Size, Version;
        public ulong Instructions, Operands, Locals, Frames, Compilations, Executions, Fallbacks, Calls;
        public readonly MplxExecutionStats Snapshot() => new(Instructions, Operands, Locals, Frames,
                                                             Compilations, Executions, Fallbacks, Calls);
    }
    internal static string? ReadAndFree(IntPtr value)
    {
        if (value == IntPtr.Zero)
            return null;
        try
        {
            return Marshal.PtrToStringUTF8(value);
        }
        finally
        {
            Free(value);
        }
    }
    internal static void Check(int status, IntPtr error, MplxExecutionStats? stats = null)
    {
        var message = ReadAndFree(error);
        if (status != 0)
            throw new MplxException(status, message ?? "Native call failed without error text.", stats);
    }
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_free")]
    private static extern void Free(IntPtr value);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_buffer_free")]
    internal static extern void FreeBuffer(IntPtr value);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_program_compile")]
    internal static extern int Compile(byte[] source, nuint size, uint optimize, out ProgramHandle program,
                                       out IntPtr error);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
               EntryPoint = "mplx_program_compile_file")]
    internal static extern int CompileFile([MarshalAs(UnmanagedType.LPUTF8Str)] string root,
                                           [MarshalAs(UnmanagedType.LPUTF8Str)] string entry, uint optimize,
                                           out ProgramHandle program, out IntPtr error);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_program_load")]
    internal static extern int Load(byte[] bytes, nuint size, out ProgramHandle program, out IntPtr error);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_program_save")]
    internal static extern int Save(ProgramHandle program, out IntPtr bytes, out nuint size,
                                    out IntPtr error);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_program_info")]
    internal static extern int Info(ProgramHandle program, out IntPtr json, out IntPtr error);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_program_destroy")]
    internal static extern void DestroyProgram(IntPtr program);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_instance_create")]
    internal static extern int CreateInstance(ProgramHandle program, in InstanceOptions options,
                                              out InstanceHandle instance, out IntPtr error);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_instance_destroy")]
    internal static extern void DestroyInstance(IntPtr instance);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_instance_cancel")]
    internal static extern int Cancel(InstanceHandle instance);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_instance_is_running")]
    internal static extern uint IsRunning(InstanceHandle instance);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl,
               EntryPoint = "mplx_instance_reset_cancel")]
    internal static extern int ResetCancel(InstanceHandle instance);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_instance_invoke")]
    internal static extern int Invoke(InstanceHandle instance,
                                      [MarshalAs(UnmanagedType.LPUTF8Str)] string entry, long[] arguments,
                                      nuint count, in RuntimeOptions options, out long value, ref Stats stats,
                                      out IntPtr error);
}
