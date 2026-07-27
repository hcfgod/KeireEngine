using System;
using System.Runtime.InteropServices;

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
    internal static delegate* unmanaged<float> DeltaTimeIcall;
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
    internal static delegate* unmanaged<ulong, ulong, ulong, byte, void> SetEntityActiveIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong*, ulong*, void> CloneEntityIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, void> DestroyEntityIcall;
    internal static delegate* unmanaged<ulong, Vector3, Vector3, float, uint, ulong, ulong, NativeRaycastHit*, byte>
        RaycastIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, ulong, ulong, float, byte> PlayAudioIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> StopAudioIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, NativeString, byte> SetUiTextIcall;
    internal static delegate* unmanaged<ulong, ulong, ulong, byte> ConsumeUiClickIcall;
#pragma warning restore CS0649

    internal static void WriteLog(byte level, string message)
    {
        using NativeString nativeMessage = message;
        WriteLogIcall(level, nativeMessage);
    }

    internal static float DeltaTime => DeltaTimeIcall();

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
    internal static void SetEntityActive(Entity entity, bool active) =>
        SetEntityActiveIcall(entity.World, entity.Id.High, entity.Id.Low, active ? (byte)1 : (byte)0);
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
