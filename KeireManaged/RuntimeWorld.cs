namespace Keire;

public sealed class SceneAsset;

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

public readonly record struct SceneHandle(AssetId Asset)
{
    public bool IsValid => Asset.IsValid;
    public bool IsLoaded => IsValid && SceneManager.LoadedScenes.Contains(this);
    public bool IsActive => IsValid && SceneManager.ActiveScene.Asset == Asset;
}

public sealed class SceneLoadOperation : CustomYieldInstruction
{
    private readonly ulong _operation;

    internal SceneLoadOperation(ulong operation) =>
        _operation = operation != 0 ? operation : throw new ArgumentOutOfRangeException(nameof(operation));

    private NativeSceneLoadStatus Status => NativeWorld.GetSceneLoadStatus(_operation);

    public SceneHandle Scene => new(Status.Scene);
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
    public static SceneHandle ActiveScene => new(NativeWorld.GetActiveScene());
    public static IReadOnlyList<SceneHandle> LoadedScenes =>
        Array.ConvertAll(NativeWorld.GetLoadedScenes(), static scene => new SceneHandle(scene));

    public static SceneLoadOperation LoadSceneAsync(AssetReference<SceneAsset> scene,
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
            throw new InvalidOperationException(
                "The current player context rejected the scene load. Standalone runtime transitions currently require Single mode.");
        return new SceneLoadOperation(operation);
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
    public AssetReference<Texture> Environment { get; init; }
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

    public static AssetReference<Texture> Environment
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
