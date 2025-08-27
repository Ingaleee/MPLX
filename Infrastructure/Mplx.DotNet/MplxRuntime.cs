using System;
using System.Runtime.InteropServices;

namespace Mplx.DotNet;

public static class MplxRuntime
{
    private const string Dll = "mplx_native";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_run_from_source")]
    private static extern int _RunFromSource(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string entryUtf8,
        out long result,
        out IntPtr errorUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_check_source")]
    private static extern int _CheckSource(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string sourceUtf8,
        out IntPtr jsonUtf8,
        out IntPtr errorUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_free")]
    private static extern void _Free(IntPtr ptr);

    private static string? PtrToUtf8AndFree(IntPtr p)
    {
        if (p == IntPtr.Zero) return null;
        try { return Marshal.PtrToStringUTF8(p); }
        finally { _Free(p); }
    }

    public static long RunFromSource(string source, string entry = "main")
    {
        var rc = _RunFromSource(source, entry, out var result, out var errPtr);
        var err = PtrToUtf8AndFree(errPtr);
        if (rc != 0) throw new InvalidOperationException($"mplx_run_from_source rc={rc}: {err}");
        return result;
    }

    public static string CheckSource(string source)
    {
        var rc = _CheckSource(source, out var jsonPtr, out var errPtr);
        var json = PtrToUtf8AndFree(jsonPtr);
        var err  = PtrToUtf8AndFree(errPtr);
        if (rc != 0) throw new InvalidOperationException($"mplx_check_source rc={rc}: {err}");
        return json ?? "{}";
    }
}