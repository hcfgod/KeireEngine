using Keire;

namespace ManagedSdkConsumer;

/// <summary>Compile-time SDK coverage for explicit presentation-asset residency leases.</summary>
public sealed class PresentationAssetResidency : Behaviour
{
    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf001")]
    private AssetReference<AudioClip> _audio = default;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf002")]
    private AssetReference<Material> _material = default;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf003")]
    private AssetReference<MaterialGraph> _materialGraph = default;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf004")]
    private AssetReference<ShaderGraph> _shaderGraph = default;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf005")]
    private AssetReference<VfxEffect> _effect = default;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf006")]
    private AssetReference<ShaderGraphInstance> _shaderGraphInstance = default;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf007")]
    private AssetReference<MaterialInstance> _materialInstance = default;

    [SerializeField, StableFieldId("79fb11a8-7488-4887-87e5-ed09984bf008")]
    private AssetReference<VfxVolume> _volume = default;

    private AssetHandle<AudioClip>? _audioLease;
    private AssetHandle<Material>? _materialLease;
    private AssetHandle<MaterialGraph>? _materialGraphLease;
    private AssetHandle<ShaderGraph>? _shaderGraphLease;
    private AssetHandle<VfxEffect>? _effectLease;
    private AssetHandle<ShaderGraphInstance>? _shaderGraphInstanceLease;
    private AssetHandle<MaterialInstance>? _materialInstanceLease;
    private AssetHandle<VfxVolume>? _volumeLease;

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

    private static AssetHandle<T>? LoadWhenValid<T>(AssetReference<T> reference) where T : class =>
        reference.IsValid ? Assets.LoadRuntime(reference, AssetLoadPriority.High) : null;
}
