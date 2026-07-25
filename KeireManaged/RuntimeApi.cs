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
    public static float FixedDeltaTime => RuntimeBridge.Current.FixedDeltaTime;
    public static float UnscaledDeltaTime => RuntimeBridge.Current.UnscaledDeltaTime;
    public static double Elapsed => RuntimeBridge.Current.ElapsedTime;
}

public static class Input
{
    public static Vector2 Axis2D(string action) => NativeRuntime.ReadInputAxis2D(action);
    public static bool Button(string action) => Math.Abs(Axis(action)) >= 0.5f;
    public static float Axis(string action) => Axis2D(action).X;
}

public static class Physics
{
    public static IReadOnlyList<RaycastHit> Raycast(Vector3 origin, Vector3 direction, float maximumDistance = 1000.0f,
                                                    uint mask = uint.MaxValue) =>
        RuntimeBridge.Current.Raycast(origin, direction, maximumDistance, mask);
}

public static class Navigation
{
    public static ValueTask<NavigationPath> FindPathAsync(Vector3 start, Vector3 end, uint areaMask = uint.MaxValue,
                                                          CancellationToken cancellation = default) =>
        RuntimeBridge.Current.FindPathAsync(start, end, areaMask, cancellation);
}

public static class Animator
{
    public static void SetFloat(Entity entity, string parameter, float value) => RuntimeBridge.Current.SetAnimatorFloat(entity, parameter, value);
    public static void SetBool(Entity entity, string parameter, bool value) => RuntimeBridge.Current.SetAnimatorBool(entity, parameter, value);
    public static void SetTrigger(Entity entity, string parameter) => RuntimeBridge.Current.SetAnimatorTrigger(entity, parameter);
}

public static class Audio
{
    public static void Play(Entity entity, AssetId clip, float volume = 1.0f) => RuntimeBridge.Current.PlayAudio(entity, clip, volume);
    public static void Stop(Entity entity) => RuntimeBridge.Current.StopAudio(entity);
}

public static class Prefab
{
    public static PrefabInstance Instantiate(AssetId prefab, Vector3 position = default, Quaternion rotation = default) =>
        RuntimeBridge.Current.InstantiatePrefab(prefab, position, rotation == default ? Quaternion.Identity : rotation);
}

public static class Cursor
{
    public static bool Visible => NativeRuntime.IsCursorVisible;
    public static bool Locked => NativeRuntime.IsCursorLocked;

    public static void Hide() => NativeRuntime.SetCursorVisible(false);
    public static void Show() => NativeRuntime.SetCursorVisible(true);
    public static void Lock() => NativeRuntime.SetCursorLocked(true);
    public static void Unlock() => NativeRuntime.SetCursorLocked(false);
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
