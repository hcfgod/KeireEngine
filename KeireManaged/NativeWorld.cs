using System.Runtime.InteropServices;
using System.Text;

namespace Keire;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneLoadStatus
{
    internal ulong SceneHigh;
    internal ulong SceneLow;
    internal ulong Handle;
    internal float Progress;
    internal byte Mode;
    internal byte State;

    internal readonly AssetId Scene => new(SceneHigh, SceneLow);
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeSceneHandle
{
    internal ulong High;
    internal ulong Low;
    internal ulong Handle;

    internal readonly Scene? Value
    {
        get
        {
            SceneAsset? asset = Asset.FromId<SceneAsset>(new AssetId(High, Low));
            return asset is null || Handle == 0 ? null : new Scene(asset, Handle);
        }
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeEntityHandle
{
    internal ulong World;
    internal ulong High;
    internal ulong Low;

    internal NativeEntityHandle(ulong world, ulong high, ulong low) => (World, High, Low) = (world, high, low);

    internal readonly Entity Value => Entity.FromId(World, new EntityId(High, Low))!;
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
        Environment = Asset.FromId<Texture>(new AssetId(EnvironmentHigh, EnvironmentLow)),
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
        EnvironmentHigh = settings.Environment?.Id.High ?? 0,
        EnvironmentLow = settings.Environment?.Id.Low ?? 0,
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
    internal static delegate* unmanaged<ulong, byte> UnloadSceneIcall;
    internal static delegate* unmanaged<ulong, byte> SetActiveSceneIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> MakeEntityPersistentIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, Vector3, Quaternion, byte,
        NativeEntityHandle*, byte> InstantiatePrefabIcall;
    internal static delegate* unmanaged<NativeSceneHandle*, byte> GetActiveSceneIcall;
    internal static delegate* unmanaged<NativeSceneHandle*, int, int> GetLoadedScenesIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, int> GetEntityTagCountIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, int, byte*, int, int> GetEntityTagIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, byte> AddEntityTagIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, byte> RemoveEntityTagIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> ClearEntityTagsIcall;
    internal static delegate* unmanaged<NativeString, byte, ulong, int, NativeEntityHandle*, int, int>
        QueryEntityNamesIcall;
    internal static delegate* unmanaged<NativeString, byte, ulong, int, NativeEntityHandle*, int, int>
        QueryEntityTagsIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, ulong, int, NativeEntityHandle*, int, int>
        QueryEntityComponentsIcall;
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

    internal static bool UnloadScene(Scene scene)
    {
        if (UnloadSceneIcall == null)
            throw Unbound();
        return UnloadSceneIcall(scene.Id) != 0;
    }

    internal static bool SetActiveScene(Scene scene)
    {
        if (SetActiveSceneIcall == null)
            throw Unbound();
        return SetActiveSceneIcall(scene.Id) != 0;
    }

    internal static Entity InstantiatePrefab(Prefab prefab, Vector3 position, Quaternion rotation, Entity? parent,
                                             bool active)
    {
        if (InstantiatePrefabIcall == null)
            throw Unbound();
        NativeEntityHandle result = default;
        if (InstantiatePrefabIcall(prefab.Id.High, prefab.Id.Low, parent?.World ?? 0, parent?.Id.High ?? 0,
                                   parent?.Id.Low ?? 0, position, rotation, active ? (byte)1 : (byte)0, &result) == 0)
        {
            throw new InvalidOperationException(
                "The prefab is unavailable or the current runtime rejected prefab instantiation.");
        }
        return result.Value;
    }

    internal static bool MakeEntityPersistent(Entity entity)
    {
        if (MakeEntityPersistentIcall == null)
            throw Unbound();
        return MakeEntityPersistentIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;
    }

    internal static Scene? GetActiveScene()
    {
        if (GetActiveSceneIcall == null)
            throw Unbound();
        NativeSceneHandle scene = default;
        return GetActiveSceneIcall(&scene) != 0 ? scene.Value : null;
    }

    internal static Scene[] GetLoadedScenes()
    {
        if (GetLoadedScenesIcall == null)
            throw Unbound();
        int count = GetLoadedScenesIcall(null, 0);
        if (count < 0 || count > 1024)
            throw new InvalidOperationException("The native runtime returned an invalid loaded-scene count.");
        if (count == 0)
            return [];
        var native = new NativeSceneHandle[count];
        fixed (NativeSceneHandle* destination = native)
        {
            if (GetLoadedScenesIcall(destination, native.Length) != native.Length)
                throw new InvalidOperationException("The loaded-scene list changed while it was being read.");
        }
        var result = new Scene[native.Length];
        for (int index = 0; index < native.Length; ++index)
            result[index] = native[index].Value ??
                throw new InvalidOperationException("The native runtime returned an invalid loaded scene.");
        return result;
    }

    internal static string[] GetEntityTags(Entity entity)
    {
        if (GetEntityTagCountIcall == null || GetEntityTagIcall == null)
            throw Unbound();
        int count = GetEntityTagCountIcall(entity.World, entity.Id.High, entity.Id.Low);
        if (count < 0 || count > 16)
            throw new InvalidOperationException("The native runtime returned an invalid entity-tag count.");
        var result = new string[count];
        for (int index = 0; index < count; ++index)
        {
            int length = GetEntityTagIcall(entity.World, entity.Id.High, entity.Id.Low, index, null, 0);
            if (length < 0 || length > 64)
                throw new InvalidOperationException("The native runtime returned an invalid entity tag.");
            var bytes = new byte[length];
            fixed (byte* destination = bytes)
            {
                if (GetEntityTagIcall(entity.World, entity.Id.High, entity.Id.Low, index, destination, bytes.Length) !=
                    bytes.Length)
                    throw new InvalidOperationException("The entity tag changed while it was being read.");
            }
            result[index] = Encoding.UTF8.GetString(bytes);
        }
        return result;
    }

    internal static bool AddEntityTag(Entity entity, string tag)
    {
        if (AddEntityTagIcall == null)
            throw Unbound();
        using NativeString value = tag;
        return AddEntityTagIcall(entity.World, entity.Id.High, entity.Id.Low, value) != 0;
    }

    internal static bool RemoveEntityTag(Entity entity, string tag)
    {
        if (RemoveEntityTagIcall == null)
            throw Unbound();
        using NativeString value = tag;
        return RemoveEntityTagIcall(entity.World, entity.Id.High, entity.Id.Low, value) != 0;
    }

    internal static void ClearEntityTags(Entity entity)
    {
        if (ClearEntityTagsIcall == null)
            throw Unbound();
        if (ClearEntityTagsIcall(entity.World, entity.Id.High, entity.Id.Low) == 0)
            throw new InvalidOperationException("The entity tags could not be cleared.");
    }

    internal static Entity[] QueryEntityNames(string name, SceneQuery query, int maximum)
    {
        if (QueryEntityNamesIcall == null)
            throw Unbound();
        using NativeString value = name;
        ulong scene = query.Scene?.Id ?? 0;
        int count = QueryEntityNamesIcall(value, (byte)query.Scope, scene, maximum, null, 0);
        NativeEntityHandle[] native = AllocateEntityQuery(count, maximum);
        fixed (NativeEntityHandle* destination = native)
        {
            if (QueryEntityNamesIcall(value, (byte)query.Scope, scene, maximum, destination, native.Length) !=
                native.Length)
                throw new InvalidOperationException("The scene query changed while it was being read.");
        }
        return ConvertEntityQuery(native);
    }

    internal static Entity[] QueryEntityTags(string tag, SceneQuery query, int maximum)
    {
        if (QueryEntityTagsIcall == null)
            throw Unbound();
        using NativeString value = tag;
        ulong scene = query.Scene?.Id ?? 0;
        int count = QueryEntityTagsIcall(value, (byte)query.Scope, scene, maximum, null, 0);
        NativeEntityHandle[] native = AllocateEntityQuery(count, maximum);
        fixed (NativeEntityHandle* destination = native)
        {
            if (QueryEntityTagsIcall(value, (byte)query.Scope, scene, maximum, destination, native.Length) !=
                native.Length)
                throw new InvalidOperationException("The scene query changed while it was being read.");
        }
        return ConvertEntityQuery(native);
    }

    internal static Entity[] QueryEntityComponents(ComponentTypeId component, SceneQuery query, int maximum)
    {
        if (QueryEntityComponentsIcall == null)
            throw Unbound();
        ulong scene = query.Scene?.Id ?? 0;
        int count = QueryEntityComponentsIcall(component.High, component.Low, (byte)query.Scope, scene,
                                                maximum, null, 0);
        NativeEntityHandle[] native = AllocateEntityQuery(count, maximum);
        fixed (NativeEntityHandle* destination = native)
        {
            if (QueryEntityComponentsIcall(component.High, component.Low, (byte)query.Scope, scene, maximum,
                                           destination, native.Length) != native.Length)
                throw new InvalidOperationException("The scene query changed while it was being read.");
        }
        return ConvertEntityQuery(native);
    }

    private static NativeEntityHandle[] AllocateEntityQuery(int count, int maximum)
    {
        if (count < 0 || count > maximum)
            throw new InvalidOperationException("The native runtime returned an invalid scene-query count.");
        return new NativeEntityHandle[count];
    }

    private static Entity[] ConvertEntityQuery(NativeEntityHandle[] native)
    {
        var result = new Entity[native.Length];
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
