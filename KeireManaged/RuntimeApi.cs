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
    ComponentHandle GetComponent(Entity entity, ComponentTypeId type);
    bool ComponentExists(ComponentHandle component);
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
        public ComponentHandle GetComponent(Entity entity, ComponentTypeId type) => throw Unbound();
        public bool ComponentExists(ComponentHandle component) => throw Unbound();
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
    public static float DeltaTime => RuntimeBridge.Current.DeltaTime;
    public static float FixedDeltaTime => RuntimeBridge.Current.FixedDeltaTime;
    public static float UnscaledDeltaTime => RuntimeBridge.Current.UnscaledDeltaTime;
    public static double Elapsed => RuntimeBridge.Current.ElapsedTime;
}

public static class Input
{
    public static bool Button(string action) => RuntimeBridge.Current.GetInputButton(action);
    public static float Axis(string action) => RuntimeBridge.Current.GetInputAxis(action);
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

public static class Debug
{
    public static void DrawLine(Vector3 start, Vector3 end, Color color, float duration = 0.0f) =>
        RuntimeBridge.Current.DrawLine(start, end, color, duration);
}

public static class Log
{
    public static void Trace(string message) => RuntimeBridge.Current.WriteLog(LogLevel.Trace, message);
    public static void Info(string message) => RuntimeBridge.Current.WriteLog(LogLevel.Information, message);
    public static void Warning(string message) => RuntimeBridge.Current.WriteLog(LogLevel.Warning, message);
    public static void Error(string message) => RuntimeBridge.Current.WriteLog(LogLevel.Error, message);
}
