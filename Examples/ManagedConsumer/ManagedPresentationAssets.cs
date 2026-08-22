using Keire;

namespace ManagedSdkConsumer;

/// <summary>Compile-time SDK coverage for explicit presentation-asset residency leases.</summary>
public sealed class PresentationAssetResidency : Behaviour
{
    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf001")]
    private AudioClip? _audio;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf002")]
    private Material? _material;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf003")]
    private MaterialGraph? _materialGraph;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf004")]
    private ShaderGraph? _shaderGraph;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf005")]
    private VfxEffect? _effect;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf006")]
    private ShaderGraphInstance? _shaderGraphInstance;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf007")]
    private MaterialInstance? _materialInstance;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf008")]
    private VfxVolume? _volume;

    private AssetLoadOperation<AudioClip>? _audioLease;
    private AssetLoadOperation<Material>? _materialLease;
    private AssetLoadOperation<MaterialGraph>? _materialGraphLease;
    private AssetLoadOperation<ShaderGraph>? _shaderGraphLease;
    private AssetLoadOperation<VfxEffect>? _effectLease;
    private AssetLoadOperation<ShaderGraphInstance>? _shaderGraphInstanceLease;
    private AssetLoadOperation<MaterialInstance>? _materialInstanceLease;
    private AssetLoadOperation<VfxVolume>? _volumeLease;

    protected override void OnEnable()
    {
        _audioLease = LoadWhenValid(_audio);
        _materialLease = LoadWhenValid(_material);
        _materialGraphLease = LoadWhenValid(_materialGraph);
        _shaderGraphLease = LoadWhenValid(_shaderGraph);
        _effectLease = LoadWhenValid(_effect);
        _shaderGraphInstanceLease = LoadWhenValid(_shaderGraphInstance);
        _materialInstanceLease = LoadWhenValid(_materialInstance);
        _volumeLease = LoadWhenValid(_volume);
    }

    protected override void OnDisable()
    {
        _volumeLease?.Dispose();
        _materialInstanceLease?.Dispose();
        _shaderGraphInstanceLease?.Dispose();
        _effectLease?.Dispose();
        _shaderGraphLease?.Dispose();
        _materialGraphLease?.Dispose();
        _materialLease?.Dispose();
        _audioLease?.Dispose();
        _volumeLease = null;
        _materialInstanceLease = null;
        _shaderGraphInstanceLease = null;
        _effectLease = null;
        _shaderGraphLease = null;
        _materialGraphLease = null;
        _materialLease = null;
        _audioLease = null;
    }

    private static AssetLoadOperation<T>? LoadWhenValid<T>(T? asset) where T : Asset =>
        asset is { IsValid: true } ? Assets.LoadRuntime(asset, AssetLoadPriority.High) : null;
}
