using System.Collections.Concurrent;
using Keire.UI;

namespace Keire.Editor;

[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
public sealed class MenuItemAttribute : Attribute
{
    public MenuItemAttribute(string path, int priority = 0, bool validate = false)
    {
        Path = string.IsNullOrWhiteSpace(path)
            ? throw new ArgumentException("Editor menu paths cannot be empty.", nameof(path))
            : path.Trim();
        Priority = priority;
        Validate = validate;
    }

    public string Path { get; }
    public int Priority { get; }
    public bool Validate { get; }
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class EditorWindowAttribute(string title, bool defaultVisible = false) : Attribute
{
    public string Title { get; } = string.IsNullOrWhiteSpace(title)
        ? throw new ArgumentException("Editor window titles cannot be empty.", nameof(title))
        : title.Trim();
    public bool DefaultVisible { get; } = defaultVisible;
}

public abstract class EditorWindow : EditorExtension
{
    protected EditorWindow() => RootVisualElement = new VisualElement();

    public VisualElement RootVisualElement { get; }
    public bool IsVisible { get; internal set; }
    public virtual void CreateGUI() { }
    public virtual void Update() { }
}

public abstract class SettingsProvider : EditorExtension
{
    protected SettingsProvider(string path)
    {
        Path = string.IsNullOrWhiteSpace(path)
            ? throw new ArgumentException("Settings paths cannot be empty.", nameof(path))
            : path.Trim();
    }

    public string Path { get; }
    public abstract VisualElement CreateSettingsGUI();
}

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class EditorToolAttribute(string displayName, params Type[] targetTypes) : Attribute
{
    public string DisplayName { get; } = string.IsNullOrWhiteSpace(displayName)
        ? throw new ArgumentException("Editor tool names cannot be empty.", nameof(displayName))
        : displayName.Trim();
    public IReadOnlyList<Type> TargetTypes { get; } = targetTypes ?? throw new ArgumentNullException(nameof(targetTypes));
}

public readonly record struct SceneToolContext(IReadOnlyList<EngineObject> Selection, CancellationToken LifetimeToken);

public abstract class EditorTool : EditorExtension
{
    public virtual void OnActivated(SceneToolContext context) { }
    public virtual void OnSceneGUI(SceneToolContext context) { }
    public virtual void OnDeactivated(SceneToolContext context) { }
}

public enum GizmoSelection : byte
{
    Always,
    Selected,
    Active
}

[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
public sealed class DrawGizmoAttribute(Type targetType, GizmoSelection selection = GizmoSelection.Always) : Attribute
{
    public Type TargetType { get; } = targetType ?? throw new ArgumentNullException(nameof(targetType));
    public GizmoSelection Selection { get; } = selection;
}

public static class Selection
{
    private static IReadOnlyList<EngineObject> s_objects = [];

    public static event Action? Changed;
    public static IReadOnlyList<EngineObject> Objects => s_objects;
    public static EngineObject? ActiveObject => s_objects.FirstOrDefault();

    internal static void Publish(IEnumerable<EngineObject> objects)
    {
        s_objects = objects?.Where(value => value is not null).Distinct().ToArray() ?? [];
        Changed?.Invoke();
    }
}

public static class EditorApplication
{
    public static event Action? Update;
    public static event Action? ProjectChanged;
    public static event Action? ExtensionsReloading;
    public static event Action? ExtensionsReloaded;

    public static bool IsPlaying { get; internal set; }
    public static bool IsCompiling { get; internal set; }

    internal static IReadOnlyList<Exception> PublishUpdate() => Publish(Update);
    internal static IReadOnlyList<Exception> PublishProjectChanged() => Publish(ProjectChanged);
    internal static IReadOnlyList<Exception> PublishReloading() => Publish(ExtensionsReloading);
    internal static IReadOnlyList<Exception> PublishReloaded() => Publish(ExtensionsReloaded);

    private static IReadOnlyList<Exception> Publish(Action? callbacks)
    {
        if (callbacks is null)
            return [];
        var failures = new List<Exception>();
        foreach (Action callback in callbacks.GetInvocationList().Cast<Action>())
        {
            try
            {
                callback();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
        return failures;
    }
}

public static class Undo
{
    internal static Func<string, Action, bool>? ApplyProvider;

    public static bool Perform(string name, Action mutation)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(mutation);
        return ApplyProvider?.Invoke(name, mutation) ?? PerformLocal(mutation);
    }

    private static bool PerformLocal(Action mutation)
    {
        mutation();
        return true;
    }
}

public static class EditorPreferences
{
    private static readonly ConcurrentDictionary<string, string> Values = new(StringComparer.Ordinal);

    public static string GetString(string key, string defaultValue = "") =>
        Values.GetValueOrDefault(ValidateKey(key), defaultValue);

    public static void SetString(string key, string value) => Values[ValidateKey(key)] = value ?? string.Empty;
    public static bool DeleteKey(string key) => Values.TryRemove(ValidateKey(key), out _);

    private static string ValidateKey(string key) => string.IsNullOrWhiteSpace(key)
        ? throw new ArgumentException("Editor preference keys cannot be empty.", nameof(key))
        : key.Trim();
}

public abstract class ProjectSettingsSingleton<T> where T : ProjectSettingsSingleton<T>, new()
{
    private static readonly Lazy<T> Value = new(static () => new T(), LazyThreadSafetyMode.ExecutionAndPublication);
    public static T Instance => Value.Value;
}

public static class AssetDatabase
{
    internal static IAssetDatabaseBridge? Bridge;

    public static AssetId CreateAsset(string projectRelativePath, ReadOnlyMemory<byte> source) =>
        RequireBridge().CreateAsset(projectRelativePath, source);
    public static void MoveAsset(AssetId asset, string destination) => RequireBridge().MoveAsset(asset, destination);
    public static void RenameAsset(AssetId asset, string name) => RequireBridge().RenameAsset(asset, name);
    public static void TrashAsset(AssetId asset) => RequireBridge().TrashAsset(asset);
    public static void RequestReimport(AssetId asset) => RequireBridge().RequestReimport(asset);

    private static IAssetDatabaseBridge RequireBridge() => Bridge ??
        throw new InvalidOperationException("The editor AssetDatabase bridge is unavailable.");
}

internal interface IAssetDatabaseBridge
{
    AssetId CreateAsset(string projectRelativePath, ReadOnlyMemory<byte> source);
    void MoveAsset(AssetId asset, string destination);
    void RenameAsset(AssetId asset, string name);
    void TrashAsset(AssetId asset);
    void RequestReimport(AssetId asset);
}
