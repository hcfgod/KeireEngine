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

[StableAssetTypeId("4b454952-4542-494e-4152-590000000001")]
public sealed class BinaryAsset : Asset;

[StableAssetTypeId("4b454952-4554-4558-5441-535345540001")]
public sealed class TextAsset : Asset;

[StableAssetTypeId("4b454952-4549-4e50-5554-414354494f01")]
public sealed partial class InputActionAsset : Asset;

[StableAssetTypeId("4b454952-4553-4b45-4c45-544f4e000001")]
public sealed class Skeleton : Asset;

[StableAssetTypeId("4b454952-4553-4b49-4e4d-455348000001")]
public sealed class SkinnedMesh : Asset;

[StableAssetTypeId("4b454952-4541-4e49-4d53-4f5552434501")]
public sealed class AnimationSource : Asset;

[StableAssetTypeId("4b454952-4541-5641-5441-524d41534b01")]
public sealed class AvatarMask : Asset;

[StableAssetTypeId("4b454952-4552-4947-4445-460000000001")]
public sealed class RigDefinition : Asset;

[StableAssetTypeId("4b454952-4550-524f-434d-4f54494f4e01")]
public sealed class ProceduralMotionProfile : Asset;

[StableAssetTypeId("4b454952-454c-5441-5252-415900000001")]
public sealed class LightingTextureArray : Asset;

[StableAssetTypeId("4b454952-454c-5056-4153-534554000001")]
public sealed class LightProbeData : Asset;

[StableAssetTypeId("4b454952-454c-5345-5441-535345540001")]
public sealed class LightingSet : Asset;

[StableAssetTypeId("4b454952-454d-4655-4e43-54494f4e0001")]
public sealed class MaterialFunction : Asset;

[StableAssetTypeId("4b454952-4553-4655-4e43-54494f4e0001")]
public sealed class ShaderFunction : Asset;

[StableAssetTypeId("4b454952-454d-4c41-5945-520000000001")]
public sealed class MaterialLayer : Asset;

[StableAssetTypeId("4b454952-454d-4c42-4c45-4e4400000001")]
public sealed class MaterialLayerBlend : Asset;

[StableAssetTypeId("4b454952-4556-4658-5355-424752410001")]
public sealed class VfxSubgraph : Asset;
