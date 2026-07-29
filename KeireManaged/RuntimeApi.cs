namespace Keire;

public enum LogLevel : byte
{
    Trace,
    Debug,
    Information,
    Warning,
    Error,
    Critical
}

public readonly record struct RaycastHit(Entity Entity, Vector3 Point, Vector3 Normal, float Distance);
public readonly record struct NavigationPath(IReadOnlyList<Vector3> Points, ulong MeshRevision);
public readonly record struct PrefabInstance(Entity Root, IReadOnlyList<Entity> Entities);

public interface IRuntimeBridge
{
    bool EntityExists(Entity entity);
    string GetEntityName(Entity entity);
    void SetEntityName(Entity entity, string name);
    bool GetEntityActive(Entity entity);
    void SetEntityActive(Entity entity, bool active);
    Entity GetEntityParent(Entity entity);
    void SetEntityParent(Entity entity, Entity parent);
    IReadOnlyList<Entity> GetEntityChildren(Entity entity);
    ComponentHandle GetComponent(Entity entity, ComponentTypeId type);
    ComponentHandle AddComponent(Entity entity, ComponentTypeId type);
    bool RemoveComponent(Entity entity, ComponentTypeId type);
    bool ComponentExists(ComponentHandle component);
    Entity CloneEntity(Entity entity);
    void DestroyEntity(Entity entity);
    Vector3 GetLocalPosition(Entity entity);
    void SetLocalPosition(Entity entity, Vector3 value);
    Quaternion GetLocalRotation(Entity entity);
    void SetLocalRotation(Entity entity, Quaternion value);
    Vector3 GetLocalScale(Entity entity);
    void SetLocalScale(Entity entity, Vector3 value);
    float DeltaTime { get; }
    float FixedDeltaTime { get; }
    float UnscaledDeltaTime { get; }
    double ElapsedTime { get; }
    bool GetInputButton(string action);
    float GetInputAxis(string action);
    IReadOnlyList<RaycastHit> Raycast(Vector3 origin, Vector3 direction, float maximumDistance, uint mask);
    ValueTask<NavigationPath> FindPathAsync(Vector3 start, Vector3 end, uint areaMask, CancellationToken cancellation);
    void SetAnimatorFloat(Entity entity, string parameter, float value);
    void SetAnimatorBool(Entity entity, string parameter, bool value);
    void SetAnimatorTrigger(Entity entity, string parameter);
    void PlayAudio(Entity entity, AssetId clip, float volume);
    void StopAudio(Entity entity);
    PrefabInstance InstantiatePrefab(AssetId prefab, Vector3 position, Quaternion rotation);
    void DrawLine(Vector3 start, Vector3 end, Color color, float duration);
    void WriteLog(LogLevel level, string message);
}

public static class RuntimeBridge
{
    private sealed class UnboundBridge : IRuntimeBridge
    {
        private static InvalidOperationException Unbound() => new("Kéire managed runtime is not attached.");
        public bool EntityExists(Entity entity) => throw Unbound();
        public string GetEntityName(Entity entity) => throw Unbound();
        public void SetEntityName(Entity entity, string name) => throw Unbound();
        public bool GetEntityActive(Entity entity) => throw Unbound();
        public void SetEntityActive(Entity entity, bool active) => throw Unbound();
        public Entity GetEntityParent(Entity entity) => throw Unbound();
        public void SetEntityParent(Entity entity, Entity parent) => throw Unbound();
        public IReadOnlyList<Entity> GetEntityChildren(Entity entity) => throw Unbound();
        public ComponentHandle GetComponent(Entity entity, ComponentTypeId type) => throw Unbound();
        public ComponentHandle AddComponent(Entity entity, ComponentTypeId type) => throw Unbound();
        public bool RemoveComponent(Entity entity, ComponentTypeId type) => throw Unbound();
        public bool ComponentExists(ComponentHandle component) => throw Unbound();
        public Entity CloneEntity(Entity entity) => throw Unbound();
        public void DestroyEntity(Entity entity) => throw Unbound();
        public Vector3 GetLocalPosition(Entity entity) => throw Unbound();
        public void SetLocalPosition(Entity entity, Vector3 value) => throw Unbound();
        public Quaternion GetLocalRotation(Entity entity) => throw Unbound();
        public void SetLocalRotation(Entity entity, Quaternion value) => throw Unbound();
        public Vector3 GetLocalScale(Entity entity) => throw Unbound();
        public void SetLocalScale(Entity entity, Vector3 value) => throw Unbound();
        public float DeltaTime => throw Unbound();
        public float FixedDeltaTime => throw Unbound();
        public float UnscaledDeltaTime => throw Unbound();
        public double ElapsedTime => throw Unbound();
        public bool GetInputButton(string action) => throw Unbound();
        public float GetInputAxis(string action) => throw Unbound();
        public IReadOnlyList<RaycastHit> Raycast(Vector3 origin, Vector3 direction, float maximumDistance, uint mask) => throw Unbound();
        public ValueTask<NavigationPath> FindPathAsync(Vector3 start, Vector3 end, uint areaMask, CancellationToken cancellation) => throw Unbound();
        public void SetAnimatorFloat(Entity entity, string parameter, float value) => throw Unbound();
        public void SetAnimatorBool(Entity entity, string parameter, bool value) => throw Unbound();
        public void SetAnimatorTrigger(Entity entity, string parameter) => throw Unbound();
        public void PlayAudio(Entity entity, AssetId clip, float volume) => throw Unbound();
        public void StopAudio(Entity entity) => throw Unbound();
        public PrefabInstance InstantiatePrefab(AssetId prefab, Vector3 position, Quaternion rotation) => throw Unbound();
        public void DrawLine(Vector3 start, Vector3 end, Color color, float duration) => throw Unbound();
        public void WriteLog(LogLevel level, string message) => throw Unbound();
    }

    private static IRuntimeBridge s_current = new UnboundBridge();
    public static IRuntimeBridge Current => Volatile.Read(ref s_current);
    public static void Install(IRuntimeBridge bridge) => Interlocked.Exchange(ref s_current, bridge ?? throw new ArgumentNullException(nameof(bridge)));
}

public static class Time
{
    public static float DeltaTime => NativeRuntime.DeltaTime;
    public static float FixedDeltaTime => NativeRuntime.FixedDeltaTime;
    public static float UnscaledDeltaTime => NativeRuntime.UnscaledDeltaTime;
    public static double Elapsed => NativeRuntime.ElapsedTime;
}

public static class Input
{
    public static Vector2 Axis2D(string action) => NativeRuntime.ReadInputAxis2D(action);
    public static bool Held(string action) => (NativeRuntime.ReadInputState(action) & 1) != 0;
    public static bool Pressed(string action) => (NativeRuntime.ReadInputState(action) & 2) != 0;
    public static bool Released(string action) => (NativeRuntime.ReadInputState(action) & 4) != 0;
    public static bool Button(string action) => Held(action);
    public static float Axis(string action) => Axis2D(action).X;
}

public static class Physics
{
    public static bool TryRaycast(Entity context, Vector3 origin, Vector3 direction, out RaycastHit hit,
                                  float maximumDistance = 1000.0f, uint mask = uint.MaxValue,
                                  Entity ignoredEntity = default)
    {
        if (maximumDistance <= 0.0f)
            throw new ArgumentOutOfRangeException(nameof(maximumDistance));
        Vector3 normalized = direction.Normalized;
        if (normalized.LengthSquared <= 0.0f)
            throw new ArgumentException("Raycast direction cannot be zero.", nameof(direction));
        return NativeRuntime.TryRaycast(context, origin, normalized, maximumDistance, mask, ignoredEntity, out hit);
    }

    public static IReadOnlyList<RaycastHit> Raycast(Entity context, Vector3 origin, Vector3 direction,
                                                    float maximumDistance = 1000.0f,
                                                    uint mask = uint.MaxValue) =>
        TryRaycast(context, origin, direction, out RaycastHit hit, maximumDistance, mask)
            ? new[] { hit }
            : Array.Empty<RaycastHit>();
}

public static class Navigation
{
    public static ValueTask<NavigationPath> FindPathAsync(Vector3 start, Vector3 end, uint areaMask = uint.MaxValue,
                                                          CancellationToken cancellation = default) =>
        RuntimeBridge.Current.FindPathAsync(start, end, areaMask, cancellation);
}

public enum AnimatorIkSpace : byte
{
    Model,
    World
}

public static class Animator
{
    public static void SetFloat(Entity entity, string parameter, float value) =>
        NativeRuntime.SetAnimatorFloat(entity, parameter, value);
    public static void SetInteger(Entity entity, string parameter, int value) =>
        NativeRuntime.SetAnimatorInteger(entity, parameter, value);
    public static void SetBool(Entity entity, string parameter, bool value) =>
        NativeRuntime.SetAnimatorBoolean(entity, parameter, value);
    public static void SetTrigger(Entity entity, string parameter) =>
        NativeRuntime.SetAnimatorTrigger(entity, parameter, true);
    public static void ResetTrigger(Entity entity, string parameter) =>
        NativeRuntime.SetAnimatorTrigger(entity, parameter, false);
    public static void SetLayerWeight(Entity entity, string layer, float value) =>
        NativeRuntime.SetAnimatorLayerWeight(entity, layer, value);
    public static void SetTwoBoneIK(Entity entity, string goal, string rootBone, string middleBone, string endBone,
                                    Vector3 target, Vector3 pole, float weight = 1.0f,
                                    AnimatorIkSpace space = AnimatorIkSpace.World) =>
        NativeRuntime.SetAnimatorTwoBoneIk(entity, goal, rootBone, middleBone, endBone, target, pole, weight, space);
    public static void SetFabrikIK(Entity entity, string goal, IReadOnlyList<string> bones, Vector3 target,
                                   float weight = 1.0f, uint maximumIterations = 12, float tolerance = 0.001f,
                                   AnimatorIkSpace space = AnimatorIkSpace.World) =>
        NativeRuntime.SetAnimatorFabrikIk(entity, goal, bones, target, weight, maximumIterations, tolerance, space);
    public static bool ClearIK(Entity entity, string goal) => NativeRuntime.ClearAnimatorIk(entity, goal);

    public static float GetFloat(Entity entity, string parameter) =>
        TryGetFloat(entity, parameter, out float value)
            ? value
            : throw new InvalidOperationException($"Animator float parameter '{parameter}' is unavailable.");

    public static bool TryGetFloat(Entity entity, string parameter, out float value) =>
        NativeRuntime.TryGetAnimatorFloat(entity, parameter, out value);

    public static int GetInteger(Entity entity, string parameter) =>
        TryGetInteger(entity, parameter, out int value)
            ? value
            : throw new InvalidOperationException($"Animator integer parameter '{parameter}' is unavailable.");

    public static bool TryGetInteger(Entity entity, string parameter, out int value) =>
        NativeRuntime.TryGetAnimatorInteger(entity, parameter, out value);

    public static bool GetBool(Entity entity, string parameter) =>
        TryGetBool(entity, parameter, out bool value)
            ? value
            : throw new InvalidOperationException($"Animator boolean parameter '{parameter}' is unavailable.");

    public static bool TryGetBool(Entity entity, string parameter, out bool value) =>
        NativeRuntime.TryGetAnimatorBoolean(entity, parameter, out value);

    public static float GetLayerWeight(Entity entity, string layer) =>
        TryGetLayerWeight(entity, layer, out float value)
            ? value
            : throw new InvalidOperationException($"Animator layer '{layer}' is unavailable.");

    public static bool TryGetLayerWeight(Entity entity, string layer, out float value) =>
        NativeRuntime.TryGetAnimatorLayerWeight(entity, layer, out value);
}

[StableAssetTypeId("4b454952-4541-5544-494f-434c49500001")]
public sealed class AudioClip;

public readonly record struct AudioPlaybackOptions
{
    public AudioPlaybackOptions()
    {
        Bus = "SFX";
        Gain = 1.0f;
        Pitch = 1.0f;
        Priority = 128;
        Loop = false;
        Spatial = true;
        MinimumDistance = 1.0f;
        MaximumDistance = 100.0f;
    }

    public string Bus { get; init; }
    public float Gain { get; init; }
    public float Pitch { get; init; }
    public uint Priority { get; init; }
    public bool Loop { get; init; }
    public bool Spatial { get; init; }
    public float MinimumDistance { get; init; }
    public float MaximumDistance { get; init; }
}

public readonly record struct AudioSourceHandle(Entity Entity)
{
    public bool IsValid => Entity.HasComponent<AudioSourceComponent>();
    public bool Play(AssetReference<AudioClip> clip) => Audio.Play(Entity, clip);
    public bool Play(AssetReference<AudioClip> clip, AudioPlaybackOptions options) => Audio.Play(Entity, clip, options);
    public bool Stop() => Audio.Stop(Entity);
}

public static class Audio
{
    public static bool Play(Entity entity, AssetId clip, float volume = 1.0f) =>
        Play(entity, clip, new AudioPlaybackOptions { Gain = volume });

    public static bool Play(Entity entity, AssetReference<AudioClip> clip) =>
        Play(entity, clip.Id, new AudioPlaybackOptions());

    public static bool Play(Entity entity, AssetReference<AudioClip> clip, AudioPlaybackOptions options) =>
        Play(entity, clip.Id, options);

    public static bool Play(Entity entity, AssetId clip, AudioPlaybackOptions options)
    {
        if (!entity.IsValid)
            throw new ArgumentException("Audio playback requires a valid entity.", nameof(entity));
        if (!clip.IsValid)
            throw new ArgumentException("Audio playback requires a valid clip.", nameof(clip));
        if (string.IsNullOrWhiteSpace(options.Bus) || options.Bus.Length > 128)
            throw new ArgumentException("Audio bus names must contain between 1 and 128 characters.", nameof(options));
        if (!float.IsFinite(options.Gain) || options.Gain < 0.0f || options.Gain > 16.0f)
            throw new ArgumentOutOfRangeException(nameof(options), "Audio gain must be between zero and sixteen.");
        if (!float.IsFinite(options.Pitch) || options.Pitch <= 0.01f || options.Pitch > 8.0f)
            throw new ArgumentOutOfRangeException(nameof(options), "Audio pitch must be greater than 0.01 and at most eight.");
        if (options.Priority > 255 || !float.IsFinite(options.MinimumDistance) ||
            !float.IsFinite(options.MaximumDistance) || options.MinimumDistance < 0.0f ||
            options.MaximumDistance <= options.MinimumDistance)
            throw new ArgumentOutOfRangeException(nameof(options), "Audio priority or attenuation range is invalid.");
        return NativeRuntime.PlayAudio(entity, clip, options);
    }

    public static bool Stop(Entity entity) => NativeRuntime.StopAudio(entity);
}

[StableAssetTypeId("4b454952-4556-4658-4546-464543540001")]
public sealed class VfxEffect;

public readonly record struct VfxEmitterHandle(Entity Entity)
{
    public bool IsValid => Entity.IsValid && Entity.HasComponent<VfxEmitterComponent>();
    public bool IsAlive => IsValid && NativeRuntime.IsVfxAlive(Entity);
    public bool Pause() => IsValid && NativeRuntime.PauseVfx(Entity, true);
    public bool Resume() => IsValid && NativeRuntime.PauseVfx(Entity, false);
    public bool Stop() => IsValid && NativeRuntime.StopVfx(Entity);
    public bool Restart(AssetId effect) => IsValid && NativeRuntime.PlayVfx(Entity, effect, true);
    public bool Restart(AssetReference<VfxEffect> effect) => Restart(effect.Id);
}

public static class Vfx
{
    public static VfxEmitterHandle Play(Entity entity, AssetReference<VfxEffect> effect, bool restart = false) =>
        Play(entity, effect.Id, restart);

    public static VfxEmitterHandle Play(Entity entity, AssetId effect, bool restart = false)
    {
        if (!entity.IsValid)
            throw new ArgumentException("VFX playback requires a valid entity.", nameof(entity));
        if (!effect.IsValid)
            throw new ArgumentException("VFX playback requires a valid effect.", nameof(effect));
        return NativeRuntime.PlayVfx(entity, effect, restart) ? new VfxEmitterHandle(entity) : default;
    }

    public static bool Stop(Entity entity) => entity.IsValid && NativeRuntime.StopVfx(entity);
    public static bool Pause(Entity entity) => entity.IsValid && NativeRuntime.PauseVfx(entity, true);
    public static bool Resume(Entity entity) => entity.IsValid && NativeRuntime.PauseVfx(entity, false);
    public static bool IsAlive(Entity entity) => entity.IsValid && NativeRuntime.IsVfxAlive(entity);
}

public static class Prefab
{
    public static PrefabInstance Instantiate(AssetId prefab, Vector3 position = default, Quaternion rotation = default) =>
        RuntimeBridge.Current.InstantiatePrefab(prefab, position, rotation == default ? Quaternion.Identity : rotation);
}

public static class Cursor
{
    private static readonly object Sync = new();
    private static int s_CaptureRequests;
    private static int s_VisibilityRequests;
    private static bool s_LegacyVisible = true;
    private static bool s_LegacyLocked;

    public static bool Visible => NativeRuntime.IsCursorVisible;
    public static bool Locked => NativeRuntime.IsCursorLocked;
    public static bool VisibilityRequested
    {
        get => Volatile.Read(ref s_VisibilityRequests) != 0;
    }

    public static IDisposable RequestCapture() => Request(CursorRequestKind.Capture);
    public static IDisposable RequestVisible() => Request(CursorRequestKind.Visible);

    public static void Hide()
    {
        lock (Sync)
        {
            s_LegacyVisible = false;
            Apply();
        }
    }

    public static void Show()
    {
        lock (Sync)
        {
            s_LegacyVisible = true;
            Apply();
        }
    }

    public static void Lock()
    {
        lock (Sync)
        {
            s_LegacyLocked = true;
            Apply();
        }
    }

    public static void Unlock()
    {
        lock (Sync)
        {
            s_LegacyLocked = false;
            Apply();
        }
    }

    private static IDisposable Request(CursorRequestKind kind)
    {
        lock (Sync)
        {
            if (kind == CursorRequestKind.Capture)
                ++s_CaptureRequests;
            else
                ++s_VisibilityRequests;
            Apply();
        }
        return new CursorRequest(kind);
    }

    private static void Release(CursorRequestKind kind)
    {
        lock (Sync)
        {
            if (kind == CursorRequestKind.Capture)
                s_CaptureRequests = Math.Max(0, s_CaptureRequests - 1);
            else
                s_VisibilityRequests = Math.Max(0, s_VisibilityRequests - 1);
            Apply();
        }
    }

    private static void Apply()
    {
        bool visible = s_LegacyVisible;
        bool locked = s_LegacyLocked;
        if (s_VisibilityRequests != 0)
        {
            visible = true;
            locked = false;
        }
        else if (s_CaptureRequests != 0)
        {
            visible = false;
            locked = true;
        }

        NativeRuntime.SetCursorLocked(locked);
        NativeRuntime.SetCursorVisible(visible);
    }

    private enum CursorRequestKind
    {
        Capture,
        Visible
    }

    private sealed class CursorRequest(CursorRequestKind kind) : IDisposable
    {
        private int _disposed;

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
                Release(kind);
        }
    }
}

public static class Debug
{
    public static void Log(object? message) => NativeRuntime.WriteLog(2, message?.ToString() ?? "null");
    public static void Warn(object? message) => NativeRuntime.WriteLog(3, message?.ToString() ?? "null");
    public static void LogWarning(object? message) => Warn(message);
    public static void Error(object? message) => NativeRuntime.WriteLog(4, message?.ToString() ?? "null");
    public static void LogError(object? message) => Error(message);

    public static void LogException(Exception exception) =>
        NativeRuntime.WriteLog(4, (exception ?? throw new ArgumentNullException(nameof(exception))).ToString());

    public static void Assert(bool condition, object? message = null)
    {
        if (!condition)
            NativeRuntime.WriteLog(4, $"Assertion failed: {message ?? "No message provided."}");
    }

    public static void DrawLine(Vector3 start, Vector3 end, Color color, float duration = 0.0f) =>
        RuntimeBridge.Current.DrawLine(start, end, color, duration);
}

public static class Log
{
    public static void Trace(string message) => NativeRuntime.WriteLog(0, message);
    public static void Debug(string message) => NativeRuntime.WriteLog(1, message);
    public static void Info(string message) => NativeRuntime.WriteLog(2, message);
    public static void Warning(string message) => NativeRuntime.WriteLog(3, message);
    public static void Error(string message) => NativeRuntime.WriteLog(4, message);
    public static void Critical(string message) => NativeRuntime.WriteLog(5, message);
}
