using System;
using System.Runtime.InteropServices;

namespace Mplx.DotNet;

public sealed record MplxExecutionOptions(ulong MaxInstructions = 10_000_000, ulong MaxFrames = 1024,
                                          ulong MaxValues = 1_000_000);

public static class MplxRuntime
{
    private const string Dll = "mplx_native";

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeOptions
    {
        public uint StructSize;
        public uint AbiVersion;
        public ulong MaxInstructions;
        public ulong MaxFrames;
        public ulong MaxValues;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_run_from_source")]
    private static extern int Run([MarshalAs(UnmanagedType.LPUTF8Str)] string source,
                                  [MarshalAs(UnmanagedType.LPUTF8Str)] string entry, out long result,
                                  out IntPtr error);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_run_from_source_ex")]
    private static extern int RunBounded([MarshalAs(UnmanagedType.LPUTF8Str)] string source,
                                         [MarshalAs(UnmanagedType.LPUTF8Str)] string entry,
                                         in NativeOptions options, out long result, out IntPtr error);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_check_source")]
    private static extern int Check([MarshalAs(UnmanagedType.LPUTF8Str)] string source, out IntPtr json,
                                    out IntPtr error);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_free")]
    private static extern void Free(IntPtr ptr);

    private static string? ReadAndFree(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero)
            return null;
        try
        {
            return Marshal.PtrToStringUTF8(ptr);
        }
        finally
        {
            Free(ptr);
        }
    }

    public static long RunFromSource(string source, string entry = "main",
                                     MplxExecutionOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(entry);
        if (source.Contains('\0') || entry.Contains('\0'))
            throw new ArgumentException("Source and entry must not contain embedded NUL characters.");
        long result;
        IntPtr errorPtr;
        int code;
        if (options is null)
            code = Run(source, entry, out result, out errorPtr);
        else
        {
            if (options.MaxInstructions == 0 || options.MaxFrames == 0 || options.MaxValues == 0)
                throw new ArgumentOutOfRangeException(nameof(options), "Execution limits must be positive.");
            var native = new NativeOptions
            {
                StructSize = checked((uint)Marshal.SizeOf<NativeOptions>()),
                AbiVersion = 1,
                MaxInstructions = options.MaxInstructions,
                MaxFrames = options.MaxFrames,
                MaxValues = options.MaxValues
            };
            code = RunBounded(source, entry, in native, out result, out errorPtr);
        }
        var error = ReadAndFree(errorPtr);
        if (code != 0)
            throw new InvalidOperationException($"MPLX run failed (code={code}): {error}");
        return result;
    }

    public static string CheckSource(string source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (source.Contains('\0'))
            throw new ArgumentException("Source must not contain embedded NUL characters.");
        ArgumentNullException.ThrowIfNull(source);
        var code = Check(source, out var jsonPtr, out var errorPtr);
        var json = ReadAndFree(jsonPtr);
        var error = ReadAndFree(errorPtr);
        if (code != 0)
            throw new InvalidOperationException($"MPLX check failed (code={code}): {error}");
        return json ?? "{\"diagnostics\":[]}";
    }
}
