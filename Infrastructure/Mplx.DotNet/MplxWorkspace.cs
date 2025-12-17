using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Microsoft.Win32.SafeHandles;

namespace Mplx.DotNet;

public sealed record MplxBuildStatistics(
    ulong Parsed,
    ulong SyntaxHits,
    ulong Checked,
    ulong SemanticHits,
    ulong Files,
    ulong SourceBytes,
    ulong LinkedFunctions,
    bool ArtifactHit,
    string[] Affected);

public sealed record MplxBuildResult(MplxProgram Program, MplxBuildStatistics Statistics) : IDisposable
{
    public void Dispose()
    {
        Program.Dispose();
    }
}

public sealed class MplxWorkspace : IDisposable
{
    private readonly WorkspaceHandle handle;
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower
    };

    public MplxWorkspace(string root)
    {
        ArgumentNullException.ThrowIfNull(root);
        if (root.Contains('\0'))
        {
            throw new ArgumentException("Root contains embedded NUL.", nameof(root));
        }
        var status = WorkspaceNative.Create(Path.GetFullPath(root), out var value, out var error);
        try
        {
            EmbeddedNative.Check(status, error);
            handle = value;
        }
        catch
        {
            value.Dispose();
            throw;
        }
    }

    public MplxBuildResult Compile(string entry, IReadOnlyDictionary<string, string>? overlays = null,
                                   bool optimize = true)
    {
        ArgumentNullException.ThrowIfNull(entry);
        if (entry.Contains('\0'))
        {
            throw new ArgumentException("Entry contains embedded NUL.", nameof(entry));
        }
        if (overlays?.Count > 256)
        {
            throw new ArgumentOutOfRangeException(nameof(overlays));
        }
        var pins = new List<GCHandle>();
        var paths = new List<IntPtr>();
        var records = new List<WorkspaceNative.Overlay>();
        var total = 0;
        try
        {
            if (overlays is not null)
            {
                foreach (var (path, source) in overlays)
                {
                    ArgumentNullException.ThrowIfNull(path);
                    ArgumentNullException.ThrowIfNull(source);
                    if (path.Contains('\0') || source.Contains('\0'))
                    {
                        throw new ArgumentException("Overlay contains embedded NUL.", nameof(overlays));
                    }
                    var length = Encoding.UTF8.GetByteCount(source);
                    if (length > 1024 * 1024 || total + length > 16 * 1024 * 1024)
                    {
                        throw new ArgumentOutOfRangeException(nameof(overlays), "Overlay source budget exceeded.");
                    }
                    total += length;
                    var bytes = Encoding.UTF8.GetBytes(source);
                    var pin = GCHandle.Alloc(bytes, GCHandleType.Pinned);
                    pins.Add(pin);
                    var nativePath = Marshal.StringToCoTaskMemUTF8(path);
                    paths.Add(nativePath);
                    records.Add(new()
                    {
                        Path = nativePath,
                        Source = pin.AddrOfPinnedObject(),
                        Size = (nuint)bytes.Length
                    });
                }
            }
            var status = WorkspaceNative.Compile(handle, entry, records.ToArray(), (nuint)records.Count,
                optimize ? 1u : 0u, out var program, out var stats, out var error);
            try
            {
                var json = EmbeddedNative.ReadAndFree(stats);
                EmbeddedNative.Check(status, error);
                var statistics = JsonSerializer.Deserialize<MplxBuildStatistics>(json!, JsonOptions)
                    ?? throw new InvalidOperationException("Native build statistics are missing.");
                return new(new MplxProgram(program), statistics);
            }
            catch
            {
                program.Dispose();
                throw;
            }
        }
        finally
        {
            foreach (var pointer in paths)
            {
                Marshal.FreeCoTaskMem(pointer);
            }
            foreach (var pin in pins)
            {
                pin.Free();
            }
        }
    }

    public void Dispose()
    {
        handle.Dispose();
    }
}

internal sealed class WorkspaceHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    public WorkspaceHandle() : base(true) { }

    protected override bool ReleaseHandle()
    {
        WorkspaceNative.Destroy(handle);
        return true;
    }
}

internal static class WorkspaceNative
{
    private const string Library = "mplx_native";

    [StructLayout(LayoutKind.Sequential)]
    internal struct Overlay
    {
        public IntPtr Path;
        public IntPtr Source;
        public nuint Size;
    }

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_workspace_create")]
    internal static extern int Create([MarshalAs(UnmanagedType.LPUTF8Str)] string root,
                                       out WorkspaceHandle workspace, out IntPtr error);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_workspace_destroy")]
    internal static extern void Destroy(IntPtr workspace);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = "mplx_workspace_compile")]
    internal static extern int Compile(WorkspaceHandle workspace,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string entry, [In] Overlay[] overlays, nuint count,
        uint optimize, out ProgramHandle program, out IntPtr statistics, out IntPtr error);
}
