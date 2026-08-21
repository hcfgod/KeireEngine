using System.Runtime.InteropServices;
using System.Text;

namespace Keire;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeRuntimeAssetStatus
{
    internal ulong Revision;
    internal byte State;
    internal byte UsingFallbackValue;

    internal readonly bool UsingFallback => UsingFallbackValue != 0;
}

internal static unsafe class NativeAssets
{
#pragma warning disable CS0649
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, byte, ulong> BeginRuntimeAssetLoadIcall;
    internal static delegate* unmanaged<ulong, NativeRuntimeAssetStatus*, byte> GetRuntimeAssetStatusIcall;
    internal static delegate* unmanaged<ulong, byte, byte*, int, int> GetRuntimeAssetDiagnosticIcall;
    internal static delegate* unmanaged<ulong, byte> ReleaseRuntimeAssetIcall;
#pragma warning restore CS0649

    internal static ulong BeginRuntimeAssetLoad(ulong generation, AssetId asset, AssetId type,
                                                AssetLoadPriority priority)
    {
        if (BeginRuntimeAssetLoadIcall == null)
            throw Unbound();
        return BeginRuntimeAssetLoadIcall(generation, asset.High, asset.Low, type.High, type.Low, (byte)priority);
    }

    internal static NativeRuntimeAssetStatus GetRuntimeAssetStatus(ulong handle)
    {
        if (GetRuntimeAssetStatusIcall == null)
            throw Unbound();
        NativeRuntimeAssetStatus status = default;
        if (GetRuntimeAssetStatusIcall(handle, &status) == 0)
            throw new InvalidOperationException("The runtime asset handle is no longer available.");
        return status;
    }

    internal static AssetLoadDiagnostic GetRuntimeAssetDiagnostic(ulong handle) =>
        new(ReadDiagnosticField(handle, 0), ReadDiagnosticField(handle, 1));

    internal static void ReleaseRuntimeAsset(ulong handle)
    {
        if (ReleaseRuntimeAssetIcall == null)
            return;
        _ = ReleaseRuntimeAssetIcall(handle);
    }

    private static string ReadDiagnosticField(ulong handle, byte field)
    {
        if (GetRuntimeAssetDiagnosticIcall == null)
            throw Unbound();
        int length = GetRuntimeAssetDiagnosticIcall(handle, field, null, 0);
        if (length < 0 || length > 64 * 1024)
            throw new InvalidOperationException("The native runtime returned an invalid asset diagnostic length.");
        if (length == 0)
            return string.Empty;
        var bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            if (GetRuntimeAssetDiagnosticIcall(handle, field, destination, bytes.Length) != bytes.Length)
                throw new InvalidOperationException("The asset diagnostic changed while it was being read.");
        }
        return Encoding.UTF8.GetString(bytes);
    }

    private static InvalidOperationException Unbound() => new("Kéire managed asset services are not attached.");
}
