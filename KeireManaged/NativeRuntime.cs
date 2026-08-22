using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Keire;

[StructLayout(LayoutKind.Explicit, Size = 16)]
internal struct NativeString : IDisposable
{
    [FieldOffset(0)]
    private IntPtr _data;

    [FieldOffset(8)]
    private int _disposed;

    public NativeString(string value)
    {
        _data = Marshal.StringToCoTaskMemAuto(value);
        _disposed = 0;
    }

    public static implicit operator NativeString(string value) => new(value);

    public void Dispose()
    {
        if (_disposed != 0)
            return;
        Marshal.FreeCoTaskMem(_data);
        _data = IntPtr.Zero;
        _disposed = 1;
    }
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeRaycastHit
{
    internal ulong EntityHigh;
    internal ulong EntityLow;
    internal Vector3 Point;
    internal Vector3 Normal;
    internal float Distance;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeEntityId
{
    internal ulong High;
    internal ulong Low;
}

internal enum NativeBuiltinPropertyKind : uint
{
    Boolean,
    Integer,
    Scalar,
    Vector2,
    Vector3,
    Vector4,
    Color,
    Asset,
    Entity
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeBuiltinProperty
{
    internal NativeBuiltinPropertyKind Kind;
    internal long Integer;
    internal double Scalar;
    internal Vector4 Vector;
    internal ulong High;
    internal ulong Low;
}

internal enum AudioSourceScalarProperty : byte
{
    Gain,
    Pitch,
    MinimumDistance,
    MaximumDistance,
    Priority
}

internal enum AudioSourceFlagProperty : byte
{
    Loop,
    Spatial,
    PlayOnAwake
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioSourceProperties
{
    internal ulong ClipHigh;
    internal ulong ClipLow;
    internal ulong MixerHigh;
    internal ulong MixerLow;
    internal ulong BusHigh;
    internal ulong BusLow;
    internal float Gain;
    internal float Pitch;
    internal float PositionSeconds;
    internal float DurationSeconds;
    internal float MinimumDistance;
    internal float MaximumDistance;
    internal uint Priority;
    internal byte LoopValue;
    internal byte SpatialValue;
    internal byte PlayOnAwakeValue;
    internal byte PlaybackState;

    internal readonly AssetId Clip => new(ClipHigh, ClipLow);
    internal readonly AssetId Mixer => new(MixerHigh, MixerLow);
    internal readonly AssetId BusId => new(BusHigh, BusLow);
    internal readonly bool Loop => LoopValue != 0;
    internal readonly bool Spatial => SpatialValue != 0;
    internal readonly bool PlayOnAwake => PlayOnAwakeValue != 0;
    internal readonly AudioSourceStatus Status =>
        new((AudioPlaybackState)PlaybackState, PositionSeconds, DurationSeconds);
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioListenerProperties
{
    internal float Gain;
    internal byte PrimaryValue;

    internal readonly bool Primary => PrimaryValue != 0;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAudioReverbZoneProperties
{
    internal ulong MixerHigh;
    internal ulong MixerLow;
    internal ulong SnapshotHigh;
    internal ulong SnapshotLow;
    internal Vector3 BoxHalfExtent;
    internal float SphereRadius;
    internal float BlendDistance;
    internal float ReverbSend;
    internal int Priority;
    internal byte ShapeValue;

    internal readonly AssetId Mixer => new(MixerHigh, MixerLow);
    internal readonly AssetId Snapshot => new(SnapshotHigh, SnapshotLow);
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeAnimatorState
{
    internal float Speed;
    internal float NormalizedTime;
    internal byte Playing;
    internal byte Paused;
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeProceduralLocomotionState
{
    internal Vector3 ActualWorldVelocity;
    internal Vector3 GroundNormal;
    internal float GaitPhase;
    internal float Speed;
    internal float VerticalSpeed;
    internal float LandingIntensity;
    internal byte State;
    internal byte Quality;
    internal byte Grounded;
    internal byte LeftFootPlanted;
    internal byte RightFootPlanted;
}

internal static unsafe class NativeRuntime
{
    private static readonly object ManagedAssetGate = new();
    private static ManagedAssetRegistry? s_managedAssets;
    private static NativeManagedAssetProvider? s_nativeManagedAssetProvider;

    internal static ManagedAssetRegistry? ManagedAssets => Volatile.Read(ref s_managedAssets);
    internal static bool HasManagedAssetLoadProvider => RequestManagedAssetLoadIcall != null;

    internal static bool RequestManagedAssetLoad(ulong generation, AssetId id) =>
        RequestManagedAssetLoadIcall != null &&
        RequestManagedAssetLoadIcall(generation, id.High, id.Low) != 0;

    internal static void CancelManagedAssetLoad(ulong generation, AssetId id)
    {
        if (CancelManagedAssetLoadIcall != null)
            CancelManagedAssetLoadIcall(generation, id.High, id.Low);
    }

    internal static void ReleaseManagedAsset(ulong generation, AssetId id)
    {
        if (ReleaseManagedAssetIcall != null)
            ReleaseManagedAssetIcall(generation, id.High, id.Low);
    }

    internal static void InstallManagedAssetGeneration(ulong generation, int maximumLoadedAssets,
                                                       int maximumInFlightLoads)
    {
        var provider = new NativeManagedAssetProvider(generation);
        InstallManagedAssets(generation, maximumLoadedAssets, maximumInFlightLoads, provider.LoadAsync, provider);
    }

    internal static bool RegisterManagedAsset(ulong generation, ulong high, ulong low, ScriptableObject asset)
    {
        ManagedAssetRegistry? registry = ManagedAssets;
        if (registry is null || registry.Generation != generation)
            return false;
        try
        {
            registry.Register(new AssetId(high, low), asset);
            return true;
        }
        catch (ObjectDisposedException)
        {
            return false;
        }
    }

    internal static bool UnloadManagedAsset(ulong generation, ulong high, ulong low)
    {
        ManagedAssetRegistry? registry = ManagedAssets;
        if (registry is null || registry.Generation != generation)
            return false;
        try
        {
            return registry.Unload(new AssetId(high, low));
        }
        catch (ObjectDisposedException)
        {
            return false;
        }
    }

    internal static bool ReloadManagedAsset(ulong generation, ulong high, ulong low,
                                            ScriptableObject candidate)
    {
        ManagedAssetRegistry? registry = ManagedAssets;
        if (registry is null || registry.Generation != generation)
            return false;
        try
        {
            return registry.Reload(new AssetId(high, low), candidate);
        }
        catch (ObjectDisposedException)
        {
            return false;
        }
    }

    internal static bool CompleteManagedAssetLoad(ulong generation, ulong high, ulong low,
                                                  ScriptableObject asset)
    {
        NativeManagedAssetProvider? provider;
        lock (ManagedAssetGate)
        {
            provider = s_nativeManagedAssetProvider;
        }
        return provider is not null && provider.Generation == generation &&
               provider.Complete(new AssetId(high, low), asset);
    }

    internal static bool FailManagedAssetLoad(ulong generation, ulong high, ulong low, string diagnostic)
    {
        NativeManagedAssetProvider? provider;
        lock (ManagedAssetGate)
        {
            provider = s_nativeManagedAssetProvider;
        }
        return provider is not null && provider.Generation == generation &&
               provider.Fail(new AssetId(high, low), diagnostic);
    }

    internal static void InstallManagedAssetsForTests(ulong generation, int maximumLoadedAssets,
                                                      int maximumInFlightLoads,
                                                      ManagedAssetLoadProvider? provider) =>
        InstallManagedAssets(generation, maximumLoadedAssets, maximumInFlightLoads, provider, null);

    private static void InstallManagedAssets(ulong generation, int maximumLoadedAssets,
                                             int maximumInFlightLoads, ManagedAssetLoadProvider? provider,
                                             NativeManagedAssetProvider? nativeProvider)
    {
        var replacement = new ManagedAssetRegistry(generation, maximumLoadedAssets, maximumInFlightLoads, provider);
        lock (ManagedAssetGate)
        {
            ManagedAssetRegistry? previous = s_managedAssets;
            if (previous is not null && generation <= previous.Generation)
            {
                replacement.Dispose();
                throw new InvalidOperationException(
                    $"Managed asset generation {generation} must be newer than generation {previous.Generation}.");
            }
            try
            {
                previous?.Dispose();
            }
            catch
            {
                replacement.Dispose();
                throw;
            }
            s_nativeManagedAssetProvider = nativeProvider;
            Volatile.Write(ref s_managedAssets, replacement);
        }
    }

    internal static bool ResetManagedAssets(ulong generation)
    {
        lock (ManagedAssetGate)
        {
            ManagedAssetRegistry? current = s_managedAssets;
            if (current is null || current.Generation != generation)
                return false;
            current.Dispose();
            s_nativeManagedAssetProvider = null;
            Volatile.Write(ref s_managedAssets, null);
            return true;
        }
    }

#pragma warning disable CS0649
    internal static delegate* unmanaged<byte, NativeString, void> WriteLogIcall;
    internal static delegate* unmanaged<ulong, NativeString, void> RegisterProfileNameIcall;
    internal static delegate* unmanaged<ulong, double, double, void> RecordProfileSpanIcall;
    internal static delegate* unmanaged<ulong, double, void> SetProfileCounterIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> RequestManagedAssetLoadIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, void> CancelManagedAssetLoadIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, void> ReleaseManagedAssetIcall;
    internal static delegate* unmanaged<ulong*, int, byte, byte, NativeString, IntPtr, IntPtr, ulong>
        SubmitManagedJobIcall;
    internal static delegate* unmanaged<ulong, void> CancelManagedJobIcall;
    internal static delegate* unmanaged<float> DeltaTimeIcall;
    internal static delegate* unmanaged<float> FixedDeltaTimeIcall;
    internal static delegate* unmanaged<float> UnscaledDeltaTimeIcall;
    internal static delegate* unmanaged<double> ElapsedTimeIcall;
    internal static delegate* unmanaged<NativeString, float*, float*, void> InputAxis2DIcall;
    internal static delegate* unmanaged<NativeString, byte> InputStateIcall;
    internal static delegate* unmanaged<byte, void> SetCursorVisibleIcall;
    internal static delegate* unmanaged<byte, void> SetCursorLockedIcall;
    internal static delegate* unmanaged<byte> IsCursorVisibleIcall;
    internal static delegate* unmanaged<byte> IsCursorLockedIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3> GetLocalPositionIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, void> SetLocalPositionIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Quaternion> GetLocalRotationIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Quaternion, void> SetLocalRotationIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, byte> MoveCharacterControllerIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte*, Vector3*, Vector3*, byte>
        GetCharacterControllerStateIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte*, float*, Vector3*, byte*, byte*, byte>
        GetRigidBodyPropertiesIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, byte> SetRigidBodyMotionIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, float, byte> SetRigidBodyMassIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, byte> SetRigidBodyVelocityIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, byte, byte> SetRigidBodyFlagIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, byte, byte> AddRigidBodyForceIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, float, byte> SetAnimatorFloatIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, int, byte> SetAnimatorIntegerIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, byte, byte> SetAnimatorBooleanIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, byte, byte> SetAnimatorTriggerIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, float, byte> SetAnimatorLayerWeightIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, NativeString, float, byte> PlayAnimatorIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, NativeString, float, float, byte>
        CrossFadeAnimatorIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, byte> PauseAnimatorIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> StopAnimatorIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, float, byte> SetAnimatorSpeedIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, float, byte> SetAnimatorFootGroundingWeightIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, Vector3, Vector3, float, float, byte, byte>
        SetProceduralLocomotionIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeProceduralLocomotionState*, byte>
        GetProceduralLocomotionStateIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeAnimatorState*, byte> GetAnimatorStateIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte*, int, int> GetAnimatorStateNameIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, NativeString, NativeString, NativeString,
        Vector3, Vector3, float, byte, byte> SetAnimatorTwoBoneIkIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, NativeString, Vector3, float, uint, float,
        byte, byte> SetAnimatorFabrikIkIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, byte> ClearAnimatorIkIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, float*, byte> TryGetAnimatorFloatIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, int*, byte> TryGetAnimatorIntegerIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, byte*, byte> TryGetAnimatorBooleanIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, float*, byte> TryGetAnimatorLayerWeightIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3> GetLocalScaleIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, void> SetLocalScaleIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3> GetWorldPositionIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3> GetPresentationWorldPositionIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Quaternion> GetPresentationWorldRotationIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, void> ResetPresentationInterpolationIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Quaternion> GetWorldRotationIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, void> SetWorldPositionIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Quaternion, void> SetWorldRotationIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> EntityExistsIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> GetEntityActiveIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> GetEntityActiveInHierarchyIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, void> SetEntityActiveIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, uint> GetEntityLayerIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, uint, byte> SetEntityLayerIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte*, int, int> GetEntityNameIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, byte> SetEntityNameIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong*, ulong*, byte> GetEntityParentIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, byte, byte> SetEntityParentIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, int> GetEntityChildCountIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, int, ulong*, ulong*, byte> GetEntityChildIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, byte> ComponentExistsIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, byte> AddComponentIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, byte> RemoveComponentIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, byte> GetComponentEnabledIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, byte, byte> SetComponentEnabledIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, NativeString, NativeBuiltinProperty*, byte>
        GetBuiltinComponentPropertyIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, NativeString, NativeBuiltinProperty*, byte>
        SetBuiltinComponentPropertyIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, NativeString, byte*, int, int>
        GetBuiltinComponentTextIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, NativeString, NativeString, byte>
        SetBuiltinComponentTextIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong*, ulong*, void> CloneEntityIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, void> DestroyEntityIcall;
    internal static delegate* unmanaged<ulong, Vector3, Vector3, float, uint, ulong, ulong, NativeRaycastHit*, byte>
        RaycastIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, Quaternion, float, float, Vector3, uint, byte,
        ulong, ulong, NativeRaycastHit*, byte> CapsuleCastIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, float, uint, byte, ulong, ulong, NativeEntityId*,
        int, int> OverlapSphereIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, float, byte> PlayAudioIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, ulong, ulong, ulong, ulong, NativeString,
        float, float, uint, byte, byte, float, float, byte> PlayAudioAdvancedIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> StopAudioIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> PlayAudioSourceIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, byte> PauseAudioIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, float, byte> SeekAudioIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeAudioSourceProperties*, byte>
        GetAudioSourcePropertiesIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, byte> SetAudioSourceClipIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, ulong, ulong, byte>
        SetAudioSourceRoutingIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, float, byte> SetAudioSourceScalarIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, byte, byte> SetAudioSourceFlagIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeAudioListenerProperties*, byte>
        GetAudioListenerPropertiesIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeAudioListenerProperties*, byte>
        SetAudioListenerPropertiesIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeAudioReverbZoneProperties*, byte>
        GetAudioReverbZonePropertiesIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeAudioReverbZoneProperties*, byte>
        SetAudioReverbZonePropertiesIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, byte, byte> PlayVfxIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> StopVfxIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, byte> PauseVfxIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> IsVfxAliveIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, uint, byte> SendVfxEventIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, float, float, byte> SetVfxScalarRangeIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, long, long, byte> SetVfxIntegerRangeIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, ulong, ulong, byte>
        SetVfxUnsignedIntegerRangeIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, Vector2, Vector2, byte>
        SetVfxVector2RangeIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, Vector3, Vector3, byte>
        SetVfxVector3RangeIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, Vector4, Vector4, byte>
        SetVfxVector4RangeIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, Color, Color, byte> SetVfxColorRangeIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, byte> SetUiTextIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> ConsumeUiClickIcall;
#pragma warning restore CS0649

    internal static void WriteLog(byte level, string message)
    {
        using NativeString nativeMessage = message;
        WriteLogIcall(level, nativeMessage);
    }

    internal static void RegisterProfileName(ulong id, string name)
    {
        using NativeString nativeName = name;
        RegisterProfileNameIcall(id, nativeName);
    }

    internal static void RecordProfileSpan(ulong id, double startMicroseconds, double durationMicroseconds) =>
        RecordProfileSpanIcall(id, startMicroseconds, durationMicroseconds);

    internal static void SetProfileCounter(ulong id, double value) => SetProfileCounterIcall(id, value);

    internal static ulong SubmitManagedJob(ReadOnlySpan<ulong> dependencies, byte priority, byte jobClass,
                                           string name, IntPtr state, IntPtr callback)
    {
        using NativeString nativeName = name;
        fixed (ulong* dependencyData = dependencies)
            return SubmitManagedJobIcall(dependencyData, dependencies.Length, priority, jobClass, nativeName, state,
                                         callback);
    }

    internal static void CancelManagedJob(ulong id)
    {
        if (CancelManagedJobIcall != null)
            CancelManagedJobIcall(id);
    }

    internal static float DeltaTime => DeltaTimeIcall();
    internal static float FixedDeltaTime => FixedDeltaTimeIcall();
    internal static float UnscaledDeltaTime => UnscaledDeltaTimeIcall();
    internal static double ElapsedTime => ElapsedTimeIcall();

    internal static Vector2 ReadInputAxis2D(string action)
    {
        using NativeString nativeAction = action;
        float x = 0.0f;
        float y = 0.0f;
        InputAxis2DIcall(nativeAction, &x, &y);
        return new Vector2(x, y);
    }

    internal static byte ReadInputState(string action)
    {
        using NativeString nativeAction = action;
        return InputStateIcall(nativeAction);
    }

    internal static void SetCursorVisible(bool visible) => SetCursorVisibleIcall(visible ? (byte)1 : (byte)0);
    internal static void SetCursorLocked(bool locked) => SetCursorLockedIcall(locked ? (byte)1 : (byte)0);
    internal static bool IsCursorVisible => IsCursorVisibleIcall() != 0;
    internal static bool IsCursorLocked => IsCursorLockedIcall() != 0;
    internal static bool EntityExists(Entity entity) =>
        EntityExistsIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;
    internal static bool GetEntityActive(Entity entity) =>
        GetEntityActiveIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;
    internal static bool GetEntityActiveInHierarchy(Entity entity) =>
        GetEntityActiveInHierarchyIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;
    internal static void SetEntityActive(Entity entity, bool active) =>
        SetEntityActiveIcall(entity.World, entity.Id.High, entity.Id.Low, active ? (byte)1 : (byte)0);

    internal static uint GetEntityLayer(Entity entity) =>
        GetEntityLayerIcall(entity.World, entity.Id.High, entity.Id.Low);

    internal static void SetEntityLayer(Entity entity, uint layer)
    {
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(layer, 32U);
        if (SetEntityLayerIcall(entity.World, entity.Id.High, entity.Id.Low, layer) == 0)
            throw new InvalidOperationException("The entity layer could not be changed.");
    }

    internal static string GetEntityName(Entity entity)
    {
        int length = GetEntityNameIcall(entity.World, entity.Id.High, entity.Id.Low, null, 0);
        if (length <= 0)
            return string.Empty;
        byte[] bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            int copiedLength = GetEntityNameIcall(entity.World, entity.Id.High, entity.Id.Low, destination, bytes.Length);
            if (copiedLength < 0)
                return string.Empty;
        }
        return Encoding.UTF8.GetString(bytes);
    }

    internal static void SetEntityName(Entity entity, string name)
    {
        using NativeString nativeName = name;
        if (SetEntityNameIcall(entity.World, entity.Id.High, entity.Id.Low, nativeName) == 0)
            throw new InvalidOperationException("The entity name could not be changed.");
    }

    internal static Entity? GetEntityParent(Entity entity)
    {
        ulong high = 0;
        ulong low = 0;
        return GetEntityParentIcall(entity.World, entity.Id.High, entity.Id.Low, &high, &low) != 0
            ? Entity.FromId(entity.World, new EntityId(high, low))
            : null;
    }

    internal static void SetEntityParent(Entity entity, Entity? parent, bool preserveWorldTransform)
    {
        if (parent is not null && parent.Id.IsValid && parent.World != entity.World)
            throw new ArgumentException("An entity cannot be parented across worlds.", nameof(parent));
        if (SetEntityParentIcall(entity.World, entity.Id.High, entity.Id.Low, parent?.Id.High ?? 0, parent?.Id.Low ?? 0,
                                 preserveWorldTransform ? (byte)1 : (byte)0) == 0)
            throw new InvalidOperationException("The entity parent could not be changed.");
    }

    internal static IReadOnlyList<Entity> GetEntityChildren(Entity entity)
    {
        int count = GetEntityChildCountIcall(entity.World, entity.Id.High, entity.Id.Low);
        if (count <= 0)
            return Array.Empty<Entity>();
        var children = new Entity[count];
        int written = 0;
        for (int index = 0; index < count; ++index)
        {
            ulong high = 0;
            ulong low = 0;
            if (GetEntityChildIcall(entity.World, entity.Id.High, entity.Id.Low, index, &high, &low) != 0)
                children[written++] = Entity.FromId(entity.World, new EntityId(high, low))!;
        }
        return written == children.Length ? children : children[..written];
    }

    internal static bool ComponentExists(Entity entity, ComponentTypeId type) =>
        entity.Id.IsValid && type.IsValid &&
        ComponentExistsIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low) != 0;
    internal static bool AddComponent(Entity entity, ComponentTypeId type) =>
        AddComponentIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low) != 0;
    internal static bool RemoveComponent(Entity entity, ComponentTypeId type) =>
        RemoveComponentIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low) != 0;
    internal static bool GetComponentEnabled(Entity entity, ComponentTypeId type) =>
        GetComponentEnabledIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low) != 0;
    internal static void SetComponentEnabled(Entity entity, ComponentTypeId type, bool enabled)
    {
        if (SetComponentEnabledIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low,
                                     enabled ? (byte)1 : (byte)0) == 0)
            throw new InvalidOperationException("The component enabled state could not be changed.");
    }
    internal static NativeBuiltinProperty GetBuiltinComponentProperty(Component component, string key)
    {
        NativeBuiltinProperty value = default;
        using NativeString nativeKey = key;
        ComponentTypeId type = component.Type;
        Entity entity = component.Entity;
        if (GetBuiltinComponentPropertyIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low,
                                             nativeKey, &value) == 0)
            throw new InvalidOperationException($"Component property '{key}' is unavailable.");
        return value;
    }
    internal static void SetBuiltinComponentProperty(Component component, string key, NativeBuiltinProperty value)
    {
        using NativeString nativeKey = key;
        ComponentTypeId type = component.Type;
        Entity entity = component.Entity;
        if (SetBuiltinComponentPropertyIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low,
                                             nativeKey, &value) == 0)
            throw new InvalidOperationException($"Component property '{key}' could not be changed.");
    }
    internal static string GetBuiltinComponentText(Component component, string key)
    {
        using NativeString nativeKey = key;
        ComponentTypeId type = component.Type;
        Entity entity = component.Entity;
        int length = GetBuiltinComponentTextIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low,
                                                  nativeKey, null, 0);
        if (length <= 0)
            return string.Empty;
        byte[] bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            _ = GetBuiltinComponentTextIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low,
                                             nativeKey, destination, bytes.Length);
        }
        return Encoding.UTF8.GetString(bytes);
    }
    internal static void SetBuiltinComponentText(Component component, string key, string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        using NativeString nativeKey = key;
        using NativeString nativeValue = value;
        ComponentTypeId type = component.Type;
        Entity entity = component.Entity;
        if (SetBuiltinComponentTextIcall(entity.World, entity.Id.High, entity.Id.Low, type.High, type.Low,
                                         nativeKey, nativeValue) == 0)
            throw new InvalidOperationException($"Component property '{key}' could not be changed.");
    }
    internal static Vector3 GetLocalPosition(Entity entity) =>
        GetLocalPositionIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static void SetLocalPosition(Entity entity, Vector3 value) =>
        SetLocalPositionIcall(entity.World, entity.Id.High, entity.Id.Low, value);
    internal static Quaternion GetLocalRotation(Entity entity) =>
        GetLocalRotationIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static void SetLocalRotation(Entity entity, Quaternion value) =>
        SetLocalRotationIcall(entity.World, entity.Id.High, entity.Id.Low, value);
    internal static Quaternion GetWorldRotation(Entity entity) =>
        GetWorldRotationIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static void SetWorldRotation(Entity entity,
                                          Quaternion value) => SetWorldRotationIcall(entity.World, entity.Id.High,
                                                                                     entity.Id.Low, value);
    internal static bool MoveCharacterController(Entity entity, Vector3 displacement) =>
        MoveCharacterControllerIcall(entity.World, entity.Id.High, entity.Id.Low, displacement) != 0;
    internal static bool TryGetCharacterControllerState(Entity entity, out CharacterControllerState state)
    {
        byte grounded = 0;
        Vector3 normal = default;
        Vector3 velocity = default;
        if (GetCharacterControllerStateIcall(entity.World, entity.Id.High, entity.Id.Low, &grounded, &normal,
                                             &velocity) == 0)
        {
            state = default;
            return false;
        }
        state = new CharacterControllerState(grounded != 0, normal, velocity);
        return true;
    }

    internal static bool TryGetRigidBodyProperties(Entity entity, out RigidBodyProperties properties)
    {
        byte motion = default;
        float mass = default;
        Vector3 velocity = default;
        byte continuous = default;
        byte gravity = default;
        if (GetRigidBodyPropertiesIcall(entity.World, entity.Id.High, entity.Id.Low, &motion, &mass, &velocity,
                                        &continuous, &gravity) == 0)
        {
            properties = default;
            return false;
        }
        properties = new RigidBodyProperties((RigidBodyMotion)motion, mass, velocity, continuous != 0, gravity != 0);
        return true;
    }

    internal static void SetRigidBodyMotion(Entity entity, RigidBodyMotion motion) =>
        RequireRigidBodyResult(SetRigidBodyMotionIcall(entity.World, entity.Id.High, entity.Id.Low, (byte)motion));
    internal static void SetRigidBodyMass(Entity entity, float mass) =>
        RequireRigidBodyResult(SetRigidBodyMassIcall(entity.World, entity.Id.High, entity.Id.Low, mass));
    internal static void SetRigidBodyVelocity(Entity entity, Vector3 velocity) =>
        RequireRigidBodyResult(SetRigidBodyVelocityIcall(entity.World, entity.Id.High, entity.Id.Low, velocity));
    internal static void SetRigidBodyFlag(Entity entity, byte property, bool value) => RequireRigidBodyResult(
        SetRigidBodyFlagIcall(entity.World, entity.Id.High, entity.Id.Low, property, value ? (byte)1 : (byte)0));
    internal static void AddRigidBodyForce(Entity entity, Vector3 force, ForceMode mode) =>
        RequireRigidBodyResult(AddRigidBodyForceIcall(entity.World, entity.Id.High, entity.Id.Low, force, (byte)mode));

    private static void RequireRigidBodyResult(byte result)
    {
        if (result == 0)
            throw new InvalidOperationException("The Rigid Body command could not be applied.");
    }

    private static void RequireAnimatorResult(byte result)
    {
        if (result == 0)
            throw new InvalidOperationException("The Animator command could not be applied.");
    }

    internal static void SetAnimatorFloat(Entity entity, string parameter, float value)
    {
        using NativeString nativeParameter = parameter;
        RequireAnimatorResult(SetAnimatorFloatIcall(entity.World, entity.Id.High, entity.Id.Low, nativeParameter,
                                                     value));
    }

    internal static void SetAnimatorInteger(Entity entity, string parameter, int value)
    {
        using NativeString nativeParameter = parameter;
        RequireAnimatorResult(SetAnimatorIntegerIcall(entity.World, entity.Id.High, entity.Id.Low, nativeParameter,
                                                       value));
    }

    internal static void SetAnimatorBoolean(Entity entity, string parameter, bool value)
    {
        using NativeString nativeParameter = parameter;
        RequireAnimatorResult(SetAnimatorBooleanIcall(entity.World, entity.Id.High, entity.Id.Low, nativeParameter,
                                                       value ? (byte)1 : (byte)0));
    }

    internal static void SetAnimatorTrigger(Entity entity, string parameter, bool set)
    {
        using NativeString nativeParameter = parameter;
        RequireAnimatorResult(SetAnimatorTriggerIcall(entity.World, entity.Id.High, entity.Id.Low, nativeParameter,
                                                       set ? (byte)1 : (byte)0));
    }

    internal static void SetAnimatorLayerWeight(Entity entity, string layer, float value)
    {
        using NativeString nativeLayer = layer;
        RequireAnimatorResult(SetAnimatorLayerWeightIcall(entity.World, entity.Id.High, entity.Id.Low, nativeLayer,
                                                           value));
    }

    internal static void PlayAnimator(Entity entity, string state, string layer, float normalizedTime)
    {
        using NativeString nativeState = state;
        using NativeString nativeLayer = layer;
        RequireAnimatorResult(PlayAnimatorIcall(entity.World, entity.Id.High, entity.Id.Low, nativeState, nativeLayer,
                                                normalizedTime));
    }

    internal static void CrossFadeAnimator(Entity entity, string state, string layer, float duration,
                                           float normalizedTime)
    {
        using NativeString nativeState = state;
        using NativeString nativeLayer = layer;
        RequireAnimatorResult(CrossFadeAnimatorIcall(entity.World, entity.Id.High, entity.Id.Low, nativeState,
                                                     nativeLayer, duration, normalizedTime));
    }

    internal static void PauseAnimator(Entity entity, bool paused) =>
        RequireAnimatorResult(PauseAnimatorIcall(entity.World, entity.Id.High, entity.Id.Low, paused ? (byte)1 : (byte)0));

    internal static void StopAnimator(Entity entity) =>
        RequireAnimatorResult(StopAnimatorIcall(entity.World, entity.Id.High, entity.Id.Low));

    internal static void SetAnimatorSpeed(Entity entity, float speed) =>
        RequireAnimatorResult(SetAnimatorSpeedIcall(entity.World, entity.Id.High, entity.Id.Low, speed));

    internal static void SetAnimatorFootGroundingWeight(Entity entity, float weight) =>
        RequireAnimatorResult(SetAnimatorFootGroundingWeightIcall(entity.World, entity.Id.High, entity.Id.Low, weight));

    internal static void SetProceduralLocomotion(Entity entity, ProceduralLocomotionIntent intent) =>
        RequireAnimatorResult(SetProceduralLocomotionIcall(
            entity.World, entity.Id.High, entity.Id.Low, intent.DesiredWorldVelocity, intent.FacingWorldDirection,
            intent.LookWorldDirection, intent.CrouchAmount, intent.RunBlend, intent.JumpRequested ? (byte)1 : (byte)0));

    internal static ProceduralLocomotionState GetProceduralLocomotionState(Entity entity)
    {
        NativeProceduralLocomotionState state = default;
        RequireAnimatorResult(GetProceduralLocomotionStateIcall(entity.World, entity.Id.High, entity.Id.Low, &state));
        return new ProceduralLocomotionState(
            (ProceduralMotionState)state.State, (ProceduralMotionQuality)state.Quality, state.ActualWorldVelocity,
            state.GroundNormal, state.GaitPhase, state.Speed, state.VerticalSpeed, state.LandingIntensity,
            state.Grounded != 0, state.LeftFootPlanted != 0, state.RightFootPlanted != 0);
    }

    internal static AnimatorStateInfo GetAnimatorState(Entity entity)
    {
        NativeAnimatorState state = default;
        RequireAnimatorResult(GetAnimatorStateIcall(entity.World, entity.Id.High, entity.Id.Low, &state));
        int length = GetAnimatorStateNameIcall(entity.World, entity.Id.High, entity.Id.Low, null, 0);
        string name = string.Empty;
        if (length > 0)
        {
            byte[] bytes = new byte[length];
            fixed (byte* destination = bytes)
            {
                int copied = GetAnimatorStateNameIcall(entity.World, entity.Id.High, entity.Id.Low, destination,
                                                       bytes.Length);
                if (copied >= 0)
                    name = Encoding.UTF8.GetString(bytes);
            }
        }
        return new AnimatorStateInfo(name, state.NormalizedTime, state.Playing != 0, state.Paused != 0, state.Speed);
    }

    internal static void SetAnimatorTwoBoneIk(Entity entity, string goal, string rootBone, string middleBone,
                                              string endBone, Vector3 target, Vector3 pole, float weight,
                                              AnimatorIkSpace space)
    {
        using NativeString nativeGoal = goal;
        using NativeString nativeRoot = rootBone;
        using NativeString nativeMiddle = middleBone;
        using NativeString nativeEnd = endBone;
        RequireAnimatorResult(SetAnimatorTwoBoneIkIcall(
            entity.World, entity.Id.High, entity.Id.Low, nativeGoal, nativeRoot, nativeMiddle, nativeEnd, target, pole,
            weight, (byte)space));
    }

    internal static void SetAnimatorFabrikIk(Entity entity, string goal, IReadOnlyList<string> bones, Vector3 target,
                                             float weight, uint maximumIterations, float tolerance,
                                             AnimatorIkSpace space)
    {
        ArgumentNullException.ThrowIfNull(bones);
        const char separator = '\u001f';
        foreach (string bone in bones)
        {
            if (string.IsNullOrWhiteSpace(bone) || bone.Contains(separator))
                throw new ArgumentException("FABRIK bone names must be non-empty and may not contain U+001F.",
                                            nameof(bones));
        }
        using NativeString nativeGoal = goal;
        using NativeString nativeBones = string.Join(separator.ToString(), bones);
        RequireAnimatorResult(SetAnimatorFabrikIkIcall(entity.World, entity.Id.High, entity.Id.Low, nativeGoal,
                                                        nativeBones, target, weight, maximumIterations, tolerance,
                                                        (byte)space));
    }

    internal static bool ClearAnimatorIk(Entity entity, string goal)
    {
        using NativeString nativeGoal = goal;
        return ClearAnimatorIkIcall(entity.World, entity.Id.High, entity.Id.Low, nativeGoal) != 0;
    }

    internal static bool TryGetAnimatorFloat(Entity entity, string parameter, out float value)
    {
        float result = default;
        using NativeString nativeParameter = parameter;
        bool found = TryGetAnimatorFloatIcall(entity.World, entity.Id.High, entity.Id.Low, nativeParameter, &result) != 0;
        value = result;
        return found;
    }

    internal static bool TryGetAnimatorInteger(Entity entity, string parameter, out int value)
    {
        int result = default;
        using NativeString nativeParameter = parameter;
        bool found =
            TryGetAnimatorIntegerIcall(entity.World, entity.Id.High, entity.Id.Low, nativeParameter, &result) != 0;
        value = result;
        return found;
    }

    internal static bool TryGetAnimatorBoolean(Entity entity, string parameter, out bool value)
    {
        byte result = default;
        using NativeString nativeParameter = parameter;
        bool found =
            TryGetAnimatorBooleanIcall(entity.World, entity.Id.High, entity.Id.Low, nativeParameter, &result) != 0;
        value = result != 0;
        return found;
    }

    internal static bool TryGetAnimatorLayerWeight(Entity entity, string layer, out float value)
    {
        float result = default;
        using NativeString nativeLayer = layer;
        bool found =
            TryGetAnimatorLayerWeightIcall(entity.World, entity.Id.High, entity.Id.Low, nativeLayer, &result) != 0;
        value = result;
        return found;
    }

    internal static Vector3 GetLocalScale(Entity entity) =>
        GetLocalScaleIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static void SetLocalScale(Entity entity, Vector3 value) =>
        SetLocalScaleIcall(entity.World, entity.Id.High, entity.Id.Low, value);
    internal static Vector3 GetWorldPosition(Entity entity) =>
        GetWorldPositionIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static Vector3 GetPresentationWorldPosition(Entity entity) =>
        GetPresentationWorldPositionIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static Quaternion GetPresentationWorldRotation(Entity entity) =>
        GetPresentationWorldRotationIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static void ResetPresentationInterpolation(Entity entity) =>
        ResetPresentationInterpolationIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static void SetWorldPosition(Entity entity, Vector3 value) => SetWorldPositionIcall(entity.World,
                                                                                                 entity.Id.High,
                                                                                                 entity.Id.Low, value);

    internal static Entity CloneEntity(Entity entity)
    {
        ulong high = 0;
        ulong low = 0;
        CloneEntityIcall(entity.World, entity.Id.High, entity.Id.Low, &high, &low);
        return new Entity(entity.World, new EntityId(high, low));
    }

    internal static void DestroyEntity(Entity entity) =>
        DestroyEntityIcall(entity.World, entity.Id.High, entity.Id.Low);

    internal static bool PlayAudio(Entity entity, AssetId clip, float gain) =>
        PlayAudioIcall(entity.World, entity.Id.High, entity.Id.Low, clip.High, clip.Low, gain) != 0;

    internal static bool PlayAudio(Entity entity, AssetId clip, AudioPlaybackOptions options)
    {
        using NativeString bus = options.Bus;
        return PlayAudioAdvancedIcall(
            entity.World, entity.Id.High, entity.Id.Low, clip.High, clip.Low, options.Mixer?.Id.High ?? 0,
            options.Mixer?.Id.Low ?? 0, options.BusId.High, options.BusId.Low, bus, options.Gain, options.Pitch,
            options.Priority, options.Loop ? (byte)1 : (byte)0, options.Spatial ? (byte)1 : (byte)0,
            options.MinimumDistance, options.MaximumDistance) != 0;
    }

    internal static bool StopAudio(Entity entity) =>
        StopAudioIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;

    internal static bool PlayAudioSource(Entity entity) =>
        PlayAudioSourceIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;

    internal static bool PauseAudio(Entity entity, bool paused) =>
        PauseAudioIcall(entity.World, entity.Id.High, entity.Id.Low, paused ? (byte)1 : (byte)0) != 0;

    internal static bool SeekAudio(Entity entity, float time) =>
        SeekAudioIcall(entity.World, entity.Id.High, entity.Id.Low, time) != 0;

    internal static NativeAudioSourceProperties GetAudioSourceProperties(Entity entity)
    {
        NativeAudioSourceProperties properties = default;
        if (GetAudioSourcePropertiesIcall(entity.World, entity.Id.High, entity.Id.Low, &properties) == 0)
            throw new InvalidOperationException("The Audio Source is unavailable.");
        return properties;
    }

    internal static void SetAudioSourceClip(Entity entity, AssetId clip)
    {
        if (SetAudioSourceClipIcall(entity.World, entity.Id.High, entity.Id.Low, clip.High, clip.Low) == 0)
            throw new InvalidOperationException("The Audio Source clip could not be changed.");
    }

    internal static void SetAudioSourceRouting(Entity entity, AssetId mixer, AssetId bus)
    {
        if (SetAudioSourceRoutingIcall(entity.World, entity.Id.High, entity.Id.Low, mixer.High, mixer.Low, bus.High,
                                       bus.Low) == 0)
            throw new InvalidOperationException("The Audio Source mixer routing could not be changed.");
    }

    internal static void SetAudioSourceScalar(Entity entity, AudioSourceScalarProperty property, float value)
    {
        if (SetAudioSourceScalarIcall(entity.World, entity.Id.High, entity.Id.Low, (byte)property, value) == 0)
            throw new InvalidOperationException("The Audio Source value could not be changed.");
    }

    internal static void SetAudioSourceFlag(Entity entity, AudioSourceFlagProperty property, bool value)
    {
        if (SetAudioSourceFlagIcall(entity.World, entity.Id.High, entity.Id.Low, (byte)property,
                                    value ? (byte)1 : (byte)0) == 0)
            throw new InvalidOperationException("The Audio Source flag could not be changed.");
    }

    internal static NativeAudioListenerProperties GetAudioListenerProperties(Entity entity)
    {
        NativeAudioListenerProperties properties = default;
        if (GetAudioListenerPropertiesIcall(entity.World, entity.Id.High, entity.Id.Low, &properties) == 0)
            throw new InvalidOperationException("The Audio Listener is unavailable.");
        return properties;
    }

    internal static void SetAudioListenerProperties(Entity entity, NativeAudioListenerProperties properties)
    {
        if (SetAudioListenerPropertiesIcall(entity.World, entity.Id.High, entity.Id.Low, &properties) == 0)
            throw new InvalidOperationException("The Audio Listener could not be changed.");
    }

    internal static NativeAudioReverbZoneProperties GetAudioReverbZoneProperties(Entity entity)
    {
        NativeAudioReverbZoneProperties properties = default;
        if (GetAudioReverbZonePropertiesIcall(entity.World, entity.Id.High, entity.Id.Low, &properties) == 0)
            throw new InvalidOperationException("The Audio Reverb Zone is unavailable.");
        return properties;
    }

    internal static void SetAudioReverbZoneProperties(Entity entity, NativeAudioReverbZoneProperties properties)
    {
        if (SetAudioReverbZonePropertiesIcall(entity.World, entity.Id.High, entity.Id.Low, &properties) == 0)
            throw new InvalidOperationException("The Audio Reverb Zone could not be changed.");
    }

    internal static bool PlayVfx(Entity entity, AssetId effect, bool restart) =>
        PlayVfxIcall(entity.World, entity.Id.High, entity.Id.Low, effect.High, effect.Low, restart ? (byte)1 : (byte)0) !=
        0;

    internal static bool StopVfx(Entity entity) =>
        StopVfxIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;

    internal static bool PauseVfx(Entity entity, bool paused) =>
        PauseVfxIcall(entity.World, entity.Id.High, entity.Id.Low, paused ? (byte)1 : (byte)0) != 0;

    internal static bool IsVfxAlive(Entity entity) =>
        IsVfxAliveIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;

    internal static bool SendVfxEvent(Entity entity, string eventName, uint spawnCount)
    {
        using NativeString nativeEventName = eventName;
        return SendVfxEventIcall(entity.World, entity.Id.High, entity.Id.Low, nativeEventName, spawnCount) != 0;
    }

    internal static bool SetVfxParameter(Entity entity, AssetId parameter,
                                         VfxRange<float> value) => SetVfxScalarRangeIcall(entity.World, entity.Id.High,
                                                                                          entity.Id.Low, parameter.High,
                                                                                          parameter.Low, value.Minimum,
                                                                                          value.Maximum) != 0;

    internal static bool SetVfxParameter(Entity entity, AssetId parameter,
                                         VfxRange<long> value) => SetVfxIntegerRangeIcall(entity.World, entity.Id.High,
                                                                                          entity.Id.Low, parameter.High,
                                                                                          parameter.Low, value.Minimum,
                                                                                          value.Maximum) != 0;

    internal static bool SetVfxParameter(Entity entity, AssetId parameter, VfxRange<ulong> value) =>
        SetVfxUnsignedIntegerRangeIcall(entity.World, entity.Id.High, entity.Id.Low, parameter.High, parameter.Low,
                                        value.Minimum, value.Maximum) != 0;

    internal static bool SetVfxParameter(Entity entity, AssetId parameter, VfxRange<Vector2> value) =>
        SetVfxVector2RangeIcall(entity.World, entity.Id.High, entity.Id.Low, parameter.High, parameter.Low,
                                value.Minimum, value.Maximum) != 0;

    internal static bool SetVfxParameter(Entity entity, AssetId parameter, VfxRange<Vector3> value) =>
        SetVfxVector3RangeIcall(entity.World, entity.Id.High, entity.Id.Low, parameter.High, parameter.Low,
                                value.Minimum, value.Maximum) != 0;

    internal static bool SetVfxParameter(Entity entity, AssetId parameter, VfxRange<Vector4> value) =>
        SetVfxVector4RangeIcall(entity.World, entity.Id.High, entity.Id.Low, parameter.High, parameter.Low,
                                value.Minimum, value.Maximum) != 0;

    internal static bool SetVfxParameter(Entity entity, AssetId parameter,
                                         VfxRange<Color> value) => SetVfxColorRangeIcall(entity.World, entity.Id.High,
                                                                                         entity.Id.Low, parameter.High,
                                                                                         parameter.Low, value.Minimum,
                                                                                         value.Maximum) != 0;

    internal static bool SetUiText(Entity entity, string text)
    {
        using NativeString nativeText = text;
        return SetUiTextIcall(entity.World, entity.Id.High, entity.Id.Low, nativeText) != 0;
    }

    internal static bool ConsumeUiClick(Entity entity) =>
        ConsumeUiClickIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;

    internal static bool TryRaycast(Entity context, Vector3 origin, Vector3 direction, float maximumDistance,
                                    uint mask, Entity? ignoredEntity, out RaycastHit hit)
    {
        NativeRaycastHit nativeHit = default;
        bool didHit = RaycastIcall(context.World, origin, direction, maximumDistance, mask,
                                   ignoredEntity?.Id.High ?? 0, ignoredEntity?.Id.Low ?? 0, &nativeHit) != 0;
        hit = didHit
            ? new RaycastHit(Entity.FromId(context.World, new EntityId(nativeHit.EntityHigh, nativeHit.EntityLow))!,
                             nativeHit.Point, nativeHit.Normal, nativeHit.Distance)
            : default;
        return didHit;
    }

    internal static bool TryCapsuleCast(Entity context, Vector3 origin, Quaternion rotation, float radius,
                                        float height, Vector3 displacement, uint mask, bool includeTriggers,
                                        Entity? ignoredEntity, out RaycastHit hit)
    {
        NativeRaycastHit nativeHit = default;
        bool didHit = CapsuleCastIcall(context.World, context.Id.High, context.Id.Low, origin, rotation, radius, height,
                                       displacement, mask, includeTriggers ? (byte)1 : (byte)0,
                                       ignoredEntity?.Id.High ?? 0, ignoredEntity?.Id.Low ?? 0, &nativeHit) != 0;
        hit = didHit
            ? new RaycastHit(Entity.FromId(context.World, new EntityId(nativeHit.EntityHigh, nativeHit.EntityLow))!,
                             nativeHit.Point, nativeHit.Normal, nativeHit.Distance)
            : default;
        return didHit;
    }

    internal static IReadOnlyList<Entity> OverlapSphere(Entity context, Vector3 center, float radius, uint mask,
                                                         bool includeTriggers, Entity? ignoredEntity)
    {
        int count = OverlapSphereIcall(context.World, context.Id.High, context.Id.Low, center, radius, mask,
                                       includeTriggers ? (byte)1 : (byte)0, ignoredEntity?.Id.High ?? 0,
                                       ignoredEntity?.Id.Low ?? 0, null, 0);
        if (count < 0)
            throw new InvalidOperationException("The native physics world rejected the sphere overlap.");
        if (count == 0)
            return Array.Empty<Entity>();
        var nativeEntities = new NativeEntityId[count];
        fixed (NativeEntityId* destination = nativeEntities)
        {
            int copied = OverlapSphereIcall(context.World, context.Id.High, context.Id.Low, center, radius, mask,
                                            includeTriggers ? (byte)1 : (byte)0, ignoredEntity?.Id.High ?? 0,
                                            ignoredEntity?.Id.Low ?? 0, destination, nativeEntities.Length);
            if (copied != count)
                throw new InvalidOperationException("The native physics overlap changed during one managed call.");
        }
        var result = new Entity[count];
        for (int index = 0; index < count; ++index)
            result[index] = Entity.FromId(context.World,
                new EntityId(nativeEntities[index].High, nativeEntities[index].Low))!;
        return result;
    }
}
