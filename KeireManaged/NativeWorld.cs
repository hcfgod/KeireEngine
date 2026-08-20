using System.Runtime.InteropServices;
using System.Text;

namespace Keire;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneLoadStatus
{
    internal ulong SceneHigh;
    internal ulong SceneLow;
    internal float Progress;
    internal byte Mode;
    internal byte State;

    internal readonly AssetId Scene => new(SceneHigh, SceneLow);
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAssetId
{
    internal ulong High;
    internal ulong Low;

    internal readonly AssetId Value => new(High, Low);
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeRenderEnvironment
{
    internal Color AmbientColor;
    internal float AmbientIntensity;
    internal float Exposure;
    internal ulong EnvironmentHigh;
    internal ulong EnvironmentLow;
    internal float EnvironmentRotationDegrees;
    internal float EnvironmentDiffuseIntensity;
    internal float EnvironmentSpecularIntensity;
    internal byte SkyVisibleValue;
    internal float DirectionalShadowDistance;
    internal uint DirectionalShadowCascadeCount;
    internal uint DirectionalShadowResolution;
    internal float DirectionalShadowSplitLambda;

    internal readonly RenderEnvironmentSettings Settings => new()
    {
        AmbientColor = AmbientColor,
        AmbientIntensity = AmbientIntensity,
        Exposure = Exposure,
        Environment = new AssetReference<Texture>(new AssetId(EnvironmentHigh, EnvironmentLow)),
        EnvironmentRotationDegrees = EnvironmentRotationDegrees,
        EnvironmentDiffuseIntensity = EnvironmentDiffuseIntensity,
        EnvironmentSpecularIntensity = EnvironmentSpecularIntensity,
        SkyVisible = SkyVisibleValue != 0,
        DirectionalShadowDistance = DirectionalShadowDistance,
        DirectionalShadowCascadeCount = DirectionalShadowCascadeCount,
        DirectionalShadowResolution = DirectionalShadowResolution,
        DirectionalShadowSplitLambda = DirectionalShadowSplitLambda
    };

    internal static NativeRenderEnvironment From(RenderEnvironmentSettings settings) => new()
    {
        AmbientColor = settings.AmbientColor,
        AmbientIntensity = settings.AmbientIntensity,
        Exposure = settings.Exposure,
        EnvironmentHigh = settings.Environment.Id.High,
        EnvironmentLow = settings.Environment.Id.Low,
        EnvironmentRotationDegrees = settings.EnvironmentRotationDegrees,
        EnvironmentDiffuseIntensity = settings.EnvironmentDiffuseIntensity,
        EnvironmentSpecularIntensity = settings.EnvironmentSpecularIntensity,
        SkyVisibleValue = settings.SkyVisible ? (byte)1 : (byte)0,
        DirectionalShadowDistance = settings.DirectionalShadowDistance,
        DirectionalShadowCascadeCount = settings.DirectionalShadowCascadeCount,
        DirectionalShadowResolution = settings.DirectionalShadowResolution,
        DirectionalShadowSplitLambda = settings.DirectionalShadowSplitLambda
    };
}

internal static unsafe class NativeWorld
{
#pragma warning disable CS0649
    internal static delegate* unmanaged<ulong, ulong, byte, ulong> BeginSceneLoadIcall;
    internal static delegate* unmanaged<ulong, NativeSceneLoadStatus*, byte> GetSceneLoadStatusIcall;
    internal static delegate* unmanaged<ulong, byte*, int, int> GetSceneLoadDiagnosticIcall;
    internal static delegate* unmanaged<ulong, byte> CancelSceneLoadIcall;
    internal static delegate* unmanaged<NativeAssetId*, byte> GetActiveSceneIcall;
    internal static delegate* unmanaged<NativeAssetId*, int, int> GetLoadedScenesIcall;
    internal static delegate* unmanaged<NativeRenderEnvironment*, byte> GetRenderEnvironmentIcall;
    internal static delegate* unmanaged<NativeRenderEnvironment*, byte> SetRenderEnvironmentIcall;
#pragma warning restore CS0649

    internal static ulong BeginSceneLoad(AssetId scene, SceneLoadMode mode)
    {
        if (BeginSceneLoadIcall == null)
            throw Unbound();
        return BeginSceneLoadIcall(scene.High, scene.Low, (byte)mode);
    }

    internal static NativeSceneLoadStatus GetSceneLoadStatus(ulong operation)
    {
        if (GetSceneLoadStatusIcall == null)
            throw Unbound();
        NativeSceneLoadStatus status = default;
        if (GetSceneLoadStatusIcall(operation, &status) == 0)
            throw new InvalidOperationException("The scene load operation is no longer available.");
        return status;
    }

    internal static string GetSceneLoadDiagnostic(ulong operation)
    {
        if (GetSceneLoadDiagnosticIcall == null)
            throw Unbound();
        int length = GetSceneLoadDiagnosticIcall(operation, null, 0);
        if (length < 0 || length > 64 * 1024)
            throw new InvalidOperationException("The native runtime returned an invalid scene diagnostic length.");
        if (length == 0)
            return string.Empty;
        var bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            if (GetSceneLoadDiagnosticIcall(operation, destination, bytes.Length) != bytes.Length)
                throw new InvalidOperationException("The scene diagnostic changed while it was being read.");
        }
        return Encoding.UTF8.GetString(bytes);
    }

    internal static bool CancelSceneLoad(ulong operation)
    {
        if (CancelSceneLoadIcall == null)
            throw Unbound();
        return CancelSceneLoadIcall(operation) != 0;
    }

    internal static AssetId GetActiveScene()
    {
        if (GetActiveSceneIcall == null)
            throw Unbound();
        NativeAssetId scene = default;
        return GetActiveSceneIcall(&scene) != 0 ? scene.Value : default;
    }

    internal static AssetId[] GetLoadedScenes()
    {
        if (GetLoadedScenesIcall == null)
            throw Unbound();
        int count = GetLoadedScenesIcall(null, 0);
        if (count < 0 || count > 1024)
            throw new InvalidOperationException("The native runtime returned an invalid loaded-scene count.");
        if (count == 0)
            return [];
        var native = new NativeAssetId[count];
        fixed (NativeAssetId* destination = native)
        {
            if (GetLoadedScenesIcall(destination, native.Length) != native.Length)
                throw new InvalidOperationException("The loaded-scene list changed while it was being read.");
        }
        var result = new AssetId[native.Length];
        for (int index = 0; index < native.Length; ++index)
            result[index] = native[index].Value;
        return result;
    }

    internal static RenderEnvironmentSettings GetRenderEnvironment()
    {
        if (GetRenderEnvironmentIcall == null)
            throw Unbound();
        NativeRenderEnvironment value = default;
        if (GetRenderEnvironmentIcall(&value) == 0)
            throw new InvalidOperationException("Render settings are unavailable in the current runtime context.");
        return value.Settings;
    }

    internal static void SetRenderEnvironment(RenderEnvironmentSettings settings)
    {
        if (SetRenderEnvironmentIcall == null)
            throw Unbound();
        NativeRenderEnvironment value = NativeRenderEnvironment.From(settings);
        if (SetRenderEnvironmentIcall(&value) == 0)
            throw new InvalidOperationException("The native runtime rejected the render settings transaction.");
    }

    private static InvalidOperationException Unbound() => new("Kéire managed world services are not attached.");
}
