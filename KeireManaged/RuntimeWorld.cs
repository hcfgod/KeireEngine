using System.Text;

namespace Keire;

public sealed class SceneAsset : Asset;

public enum SceneLoadMode : byte
{
    Single,
    Additive
}

public enum SceneLoadState : byte
{
    Queued,
    Loading,
    Ready,
    Failed,
    Cancelled
}

public sealed class Scene : EngineObject, IEquatable<Scene>
{
    internal Scene(SceneAsset asset, ulong id) => (Asset, Id) = (asset, id);

    public SceneAsset Asset { get; }
    public ulong Id { get; }

    public override bool IsValid => Asset.IsValid && Id != 0 && IsLoaded;
    public bool HasStableIdentity => Asset.IsValid && Id != 0;
    public bool IsLoaded
    {
        get
        {
            return HasStableIdentity && SceneManager.LoadedScenes.Contains(this);
        }
    }
    public bool IsActive => HasStableIdentity
        ? SceneManager.ActiveScene == this
        : false;

    public bool Equals(Scene? other) => other is not null && Id == other.Id && Asset == other.Asset;
    public override bool Equals(object? value) => value is Scene other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(Asset, Id);
}

public enum SceneQueryScope : byte
{
    Active,
    Loaded,
    Persistent,
    Specific
}

public readonly record struct SceneQuery
{
    private SceneQuery(SceneQueryScope scope, Scene? scene) => (Scope, Scene) = (scope, scene);

    public SceneQueryScope Scope { get; }
    public Scene? Scene { get; }

    public static SceneQuery Active => new(SceneQueryScope.Active, null);
    public static SceneQuery Loaded => new(SceneQueryScope.Loaded, null);
    public static SceneQuery Persistent => new(SceneQueryScope.Persistent, null);

    public static SceneQuery In(Scene scene)
    {
        if (!scene.HasStableIdentity)
            throw new ArgumentException("A specific scene query requires a valid loaded-scene handle.", nameof(scene));
        return new SceneQuery(SceneQueryScope.Specific, scene);
    }
}

public sealed class SceneLoadOperation : CustomYieldInstruction
{
    private readonly ulong _operation;

    internal SceneLoadOperation(ulong operation) =>
        _operation = operation != 0 ? operation : throw new ArgumentOutOfRangeException(nameof(operation));

    private NativeSceneLoadStatus Status => NativeWorld.GetSceneLoadStatus(_operation);

    public Scene? Scene => Status.Handle == 0 ? null : new Scene(Asset.FromId<SceneAsset>(Status.Scene)!, Status.Handle);
    public SceneLoadMode Mode => (SceneLoadMode)Status.Mode;
    public SceneLoadState State => (SceneLoadState)Status.State;
    public float Progress => Status.Progress;
    public bool IsDone => State is SceneLoadState.Ready or SceneLoadState.Failed or SceneLoadState.Cancelled;
    public bool Succeeded => State == SceneLoadState.Ready;
    public string Error => State == SceneLoadState.Failed ? NativeWorld.GetSceneLoadDiagnostic(_operation) : string.Empty;
    public override bool KeepWaiting => !IsDone;

    public bool Cancel() => NativeWorld.CancelSceneLoad(_operation);
}

public static class SceneManager
{
    public static Scene? ActiveScene => NativeWorld.GetActiveScene();
    public static IReadOnlyList<Scene> LoadedScenes => NativeWorld.GetLoadedScenes();

    public static Entity? FindByName(string name) => FindAllByName(name, 1).FirstOrDefault();

    public static IReadOnlyList<Entity> FindAllByName(string name, int maximumResults = 256)
        => FindAllByName(name, SceneQuery.Active, maximumResults);

    public static IReadOnlyList<Entity> FindAllByName(string name, SceneQuery query, int maximumResults = 256)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (Encoding.UTF8.GetByteCount(name) > 256)
            throw new ArgumentException("Entity names cannot exceed 256 UTF-8 bytes.", nameof(name));
        ValidateMaximum(maximumResults);
        ValidateQuery(query);
        return NativeWorld.QueryEntityNames(name, query, maximumResults);
    }

    public static Entity? FindWithTag(string tag) => FindAllWithTag(tag, 1).FirstOrDefault();

    public static IReadOnlyList<Entity> FindAllWithTag(string tag, int maximumResults = 256)
        => FindAllWithTag(tag, SceneQuery.Active, maximumResults);

    public static IReadOnlyList<Entity> FindAllWithTag(string tag, SceneQuery query, int maximumResults = 256)
    {
        EntityTag.Validate(tag, nameof(tag));
        ValidateMaximum(maximumResults);
        ValidateQuery(query);
        return NativeWorld.QueryEntityTags(tag, query, maximumResults);
    }

    public static IReadOnlyList<Entity> FindAllWithComponent<T>(int maximumResults = 256)
        => FindAllWithComponent<T>(SceneQuery.Active, maximumResults);

    public static IReadOnlyList<Entity> FindAllWithComponent<T>(SceneQuery query, int maximumResults = 256)
    {
        ValidateMaximum(maximumResults);
        ValidateQuery(query);
        return NativeWorld.QueryEntityComponents(ComponentType.Of<T>(), query, maximumResults);
    }

    public static SceneLoadOperation LoadSceneAsync(SceneAsset scene,
                                                     SceneLoadMode mode = SceneLoadMode.Single) =>
        LoadSceneAsync(scene.Id, mode);

    public static SceneLoadOperation LoadSceneAsync(AssetId scene, SceneLoadMode mode = SceneLoadMode.Single)
    {
        if (!scene.IsValid)
            throw new ArgumentException("Scene loading requires a valid scene asset.", nameof(scene));
        if (!Enum.IsDefined(mode))
            throw new ArgumentOutOfRangeException(nameof(mode));
        ulong operation = NativeWorld.BeginSceneLoad(scene, mode);
        if (operation == 0)
            throw new InvalidOperationException("The current runtime context rejected the scene load.");
        return new SceneLoadOperation(operation);
    }

    public static bool UnloadScene(Scene scene)
    {
        if (!scene.HasStableIdentity)
            return false;
        return NativeWorld.UnloadScene(scene);
    }

    public static bool SetActiveScene(Scene scene)
    {
        if (!scene.HasStableIdentity)
            return false;
        return NativeWorld.SetActiveScene(scene);
    }

    public static bool Preserve(Entity entity)
    {
        if (entity is null || entity.World == 0 || !entity.Id.IsValid)
            return false;
        return NativeWorld.MakeEntityPersistent(entity);
    }

    private static void ValidateQuery(SceneQuery query)
    {
        if (!Enum.IsDefined(query.Scope) ||
            (query.Scope == SceneQueryScope.Specific ? query.Scene is null || !query.Scene.HasStableIdentity
                                                     : query.Scene is not null))
            throw new ArgumentException("The scene query scope and handle are inconsistent.", nameof(query));
    }

    private static void ValidateMaximum(int maximumResults)
    {
        if (maximumResults is < 1 or > 4096)
            throw new ArgumentOutOfRangeException(nameof(maximumResults), "Scene queries support 1..4096 results.");
    }
}

public readonly record struct RenderEnvironmentSettings
{
    public static RenderEnvironmentSettings Default => new()
    {
        AmbientColor = new Color(0.20f, 0.22f, 0.26f, 1.0f),
        AmbientIntensity = 0.75f,
        Exposure = 1.0f,
        EnvironmentDiffuseIntensity = 1.0f,
        EnvironmentSpecularIntensity = 1.0f,
        SkyVisible = true,
        DirectionalShadowDistance = 100.0f,
        DirectionalShadowCascadeCount = 4,
        DirectionalShadowResolution = 2048,
        DirectionalShadowSplitLambda = 0.65f
    };

    public Color AmbientColor { get; init; }
    public float AmbientIntensity { get; init; }
    public float Exposure { get; init; }
    public Texture? Environment { get; init; }
    public float EnvironmentRotationDegrees { get; init; }
    public float EnvironmentDiffuseIntensity { get; init; }
    public float EnvironmentSpecularIntensity { get; init; }
    public bool SkyVisible { get; init; }
    public float DirectionalShadowDistance { get; init; }
    public uint DirectionalShadowCascadeCount { get; init; }
    public uint DirectionalShadowResolution { get; init; }
    public float DirectionalShadowSplitLambda { get; init; }
}

public static class RenderSettings
{
    public static RenderEnvironmentSettings Current
    {
        get => NativeWorld.GetRenderEnvironment();
        set
        {
            Validate(value);
            NativeWorld.SetRenderEnvironment(value);
        }
    }

    public static Color AmbientColor
    {
        get => Current.AmbientColor;
        set => Current = Current with { AmbientColor = value };
    }

    public static float AmbientIntensity
    {
        get => Current.AmbientIntensity;
        set => Current = Current with { AmbientIntensity = value };
    }

    public static float Exposure
    {
        get => Current.Exposure;
        set => Current = Current with { Exposure = value };
    }

    public static Texture? Environment
    {
        get => Current.Environment;
        set => Current = Current with { Environment = value };
    }

    public static bool SkyVisible
    {
        get => Current.SkyVisible;
        set => Current = Current with { SkyVisible = value };
    }

    private static void Validate(RenderEnvironmentSettings value)
    {
        static bool FiniteUnit(float component) => float.IsFinite(component) && component is >= 0.0f and <= 1.0f;
        Color color = value.AmbientColor;
        if (!FiniteUnit(color.Red) || !FiniteUnit(color.Green) || !FiniteUnit(color.Blue) || !FiniteUnit(color.Alpha))
            throw new ArgumentOutOfRangeException(nameof(value), "Ambient color channels must be finite values in 0..1.");
        if (!float.IsFinite(value.AmbientIntensity) || value.AmbientIntensity is < 0.0f or > 16.0f)
            throw new ArgumentOutOfRangeException(nameof(value), "Ambient intensity must be in 0..16.");
        if (!float.IsFinite(value.Exposure) || value.Exposure is < 0.01f or > 16.0f)
            throw new ArgumentOutOfRangeException(nameof(value), "Exposure must be in 0.01..16.");
        if (!float.IsFinite(value.EnvironmentRotationDegrees) ||
            !float.IsFinite(value.EnvironmentDiffuseIntensity) ||
            value.EnvironmentDiffuseIntensity is < 0.0f or > 16.0f ||
            !float.IsFinite(value.EnvironmentSpecularIntensity) ||
            value.EnvironmentSpecularIntensity is < 0.0f or > 16.0f)
            throw new ArgumentOutOfRangeException(nameof(value), "Environment rotation or intensity is invalid.");
        if (!float.IsFinite(value.DirectionalShadowDistance) ||
            value.DirectionalShadowDistance is <= 0.0f or > 100000.0f ||
            value.DirectionalShadowCascadeCount is < 1 or > 4 ||
            value.DirectionalShadowResolution is < 256 or > 8192 ||
            (value.DirectionalShadowResolution & (value.DirectionalShadowResolution - 1)) != 0 ||
            !float.IsFinite(value.DirectionalShadowSplitLambda) ||
            value.DirectionalShadowSplitLambda is < 0.0f or > 1.0f)
            throw new ArgumentOutOfRangeException(nameof(value), "Directional shadow settings are invalid.");
    }
}
