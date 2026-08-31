using Keire;

namespace KeireSandbox;

/// <summary>
/// Drives the camera-mounted VX-9 Plasma Lance through VFX events and exposed Blackboard parameters.
/// Stable parameter IDs survive Blackboard renames, graph rearrangement, and asset reloads.
/// </summary>
[StableComponentId("73616e64-626f-4078-8000-000000000050")]
[ExecutionOrder(30)]
public sealed class FpsVfxShowcase : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000051")]
    [Tooltip("Input Actions asset used to fire the Sandbox VFX showcase.")]
    private InputActionAsset? _inputActions;

    // VfxEffect.keirevfx: "Energy Color".
    private static readonly AssetId EnergyColor =
        new(0xeda50d666ff044de, 0xa8e8481e82a7a6c8);

    // VfxEffect.keirevfx: "Particle Size Range". X is Random Min and Y is Random Max in the graph.
    private static readonly AssetId ParticleSizeRange =
        new(0x8047e195c5ad4479, 0xbd2824a26ed6cb45);

    private const string FireEvent = "PlasmaFire";
    private const float FireInterval = 1.0f / 30.0f;
    private const uint ParticlesPerPulse = 3;

    private static readonly Color PlasmaColor = new(0.12f, 0.82f, 1.0f, 1.0f);
    private static readonly Color HotPlasmaColor = new(0.72f, 0.96f, 1.0f, 1.0f);

    [HotReloadState]
    private float _fireAccumulator;

    [HotReloadState]
    private float _heat;

    private VfxEmitter _emitter = null!;
    private InputActionContext? _inputContext;
    private InputAction? _fireAction;

    protected override void Awake()
    {
        _emitter = GetComponent<VfxEmitter>() ??
            throw new InvalidOperationException($"{nameof(FpsVfxShowcase)} requires a VFX Emitter on the same Entity.");
        if (!_emitter.IsValid)
            throw new InvalidOperationException($"{nameof(FpsVfxShowcase)} requires a VFX Emitter on the same Entity.");
    }

    protected override void OnEnable()
    {
        DisableInput();
        if (_inputActions is not { IsValid: true })
            throw new InvalidOperationException($"{nameof(FpsVfxShowcase)} requires an Input Actions asset.");

        _inputContext = _inputActions.CreateContext();
        _fireAction = _inputContext.FindAction("Player/Fire");
        _inputContext.Enable();
    }

    protected override void OnDisable() => DisableInput();

    protected override void OnBeforeReload() => DisableInput();

    protected override void OnAfterReload() => OnEnable();

    protected override void Update()
    {
        float deltaTime = Time.DeltaTime;
        if (deltaTime <= 0.0f || !_emitter.IsValid)
            return;

        bool firing = _fireAction?.IsPressed == true;
        float targetHeat = firing ? 1.0f : 0.0f;
        float heatBlend = 1.0f - MathF.Exp(-(firing ? 10.0f : 5.0f) * deltaTime);
        _heat += (targetHeat - _heat) * heatBlend;

        Color finalColor = Lerp(PlasmaColor, HotPlasmaColor, _heat);
        float minimumSize = 0.065f + _heat * 0.025f;
        float maximumSize = 0.14f + _heat * 0.06f;

        // Plain Blackboard values use an exact range: both native endpoints contain the same value.
        _emitter.SetParameter(EnergyColor, new VfxRange<Color>(finalColor, finalColor));
        var size = new Vector2(minimumSize, maximumSize);
        _emitter.SetParameter(ParticleSizeRange, new VfxRange<Vector2>(size, size));

        if (!firing)
        {
            _fireAccumulator = 0.0f;
            return;
        }

        _fireAccumulator = MathF.Min(_fireAccumulator + deltaTime, FireInterval * 2.0f);
        if (_fireAction?.WasPressedThisFrame == true)
            _fireAccumulator = FireInterval;
        while (_fireAccumulator >= FireInterval)
        {
            if (!_emitter.SendEvent(FireEvent, ParticlesPerPulse))
                break;
            _fireAccumulator -= FireInterval;
        }
    }

    private void DisableInput()
    {
        _inputContext?.Dispose();
        _inputContext = null;
        _fireAction = null;
    }

    private static Color Lerp(Color from, Color to, float amount)
    {
        float t = Math.Clamp(amount, 0.0f, 1.0f);
        return new Color(
            from.Red + (to.Red - from.Red) * t,
            from.Green + (to.Green - from.Green) * t,
            from.Blue + (to.Blue - from.Blue) * t,
            from.Alpha + (to.Alpha - from.Alpha) * t);
    }
}
