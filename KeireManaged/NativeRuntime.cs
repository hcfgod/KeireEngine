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

internal static unsafe class NativeRuntime
{
#pragma warning disable CS0649
    internal static delegate* unmanaged<byte, NativeString, void> WriteLogIcall;
    internal static delegate* unmanaged<ulong, NativeString, void> RegisterProfileNameIcall;
    internal static delegate* unmanaged<ulong, double, double, void> RecordProfileSpanIcall;
    internal static delegate* unmanaged<ulong, double, void> SetProfileCounterIcall;
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
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3> GetLocalScaleIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3, void> SetLocalScaleIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, Vector3> GetWorldPositionIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> EntityExistsIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> GetEntityActiveIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> GetEntityActiveInHierarchyIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, void> SetEntityActiveIcall;
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
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong*, ulong*, void> CloneEntityIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, void> DestroyEntityIcall;
    internal static delegate* unmanaged<ulong, Vector3, Vector3, float, uint, ulong, ulong, NativeRaycastHit*, byte>
        RaycastIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, float, byte> PlayAudioIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, NativeString, float, float, uint, byte, byte,
        float, float, byte> PlayAudioAdvancedIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> StopAudioIcall;
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

    internal static Entity GetEntityParent(Entity entity)
    {
        ulong high = 0;
        ulong low = 0;
        return GetEntityParentIcall(entity.World, entity.Id.High, entity.Id.Low, &high, &low) != 0
            ? new Entity(entity.World, new EntityId(high, low))
            : default;
    }

    internal static void SetEntityParent(Entity entity, Entity parent, bool preserveWorldTransform)
    {
        if (parent.Id.IsValid && parent.World != entity.World)
            throw new ArgumentException("An entity cannot be parented across worlds.", nameof(parent));
        if (SetEntityParentIcall(entity.World, entity.Id.High, entity.Id.Low, parent.Id.High, parent.Id.Low,
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
                children[written++] = new Entity(entity.World, new EntityId(high, low));
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
    internal static Vector3 GetLocalPosition(Entity entity) =>
        GetLocalPositionIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static void SetLocalPosition(Entity entity, Vector3 value) =>
        SetLocalPositionIcall(entity.World, entity.Id.High, entity.Id.Low, value);
    internal static Quaternion GetLocalRotation(Entity entity) =>
        GetLocalRotationIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static void SetLocalRotation(Entity entity, Quaternion value) =>
        SetLocalRotationIcall(entity.World, entity.Id.High, entity.Id.Low, value);
    internal static Vector3 GetLocalScale(Entity entity) =>
        GetLocalScaleIcall(entity.World, entity.Id.High, entity.Id.Low);
    internal static void SetLocalScale(Entity entity, Vector3 value) =>
        SetLocalScaleIcall(entity.World, entity.Id.High, entity.Id.Low, value);
    internal static Vector3 GetWorldPosition(Entity entity) =>
        GetWorldPositionIcall(entity.World, entity.Id.High, entity.Id.Low);

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
        return PlayAudioAdvancedIcall(entity.World, entity.Id.High, entity.Id.Low, clip.High, clip.Low, bus,
                                      options.Gain, options.Pitch, options.Priority, options.Loop ? (byte)1 : (byte)0,
                                      options.Spatial ? (byte)1 : (byte)0, options.MinimumDistance,
                                      options.MaximumDistance) != 0;
    }

    internal static bool StopAudio(Entity entity) =>
        StopAudioIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;

    internal static bool SetUiText(Entity entity, string text)
    {
        using NativeString nativeText = text;
        return SetUiTextIcall(entity.World, entity.Id.High, entity.Id.Low, nativeText) != 0;
    }

    internal static bool ConsumeUiClick(Entity entity) =>
        ConsumeUiClickIcall(entity.World, entity.Id.High, entity.Id.Low) != 0;

    internal static bool TryRaycast(Entity context, Vector3 origin, Vector3 direction, float maximumDistance,
                                    uint mask, Entity ignoredEntity, out RaycastHit hit)
    {
        NativeRaycastHit nativeHit = default;
        bool didHit = RaycastIcall(context.World, origin, direction, maximumDistance, mask,
                                   ignoredEntity.Id.High, ignoredEntity.Id.Low, &nativeHit) != 0;
        hit = didHit
            ? new RaycastHit(new Entity(context.World, new EntityId(nativeHit.EntityHigh, nativeHit.EntityLow)),
                             nativeHit.Point, nativeHit.Normal, nativeHit.Distance)
            : default;
        return didHit;
    }
}
