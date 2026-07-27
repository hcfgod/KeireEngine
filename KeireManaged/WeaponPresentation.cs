using System;
using NumericsQuaternion = System.Numerics.Quaternion;
using NumericsVector2 = System.Numerics.Vector2;
using NumericsVector3 = System.Numerics.Vector3;

namespace Keire.Production.Weapons;

public readonly record struct WeaponPresentationInput(
    NumericsVector2 LookDelta,
    NumericsVector2 MoveInput,
    float HorizontalSpeed,
    bool Grounded,
    bool Aiming,
    bool Sprinting);

public readonly record struct WeaponPresentationPose(
    NumericsVector3 PositionOffset,
    NumericsQuaternion RotationOffset,
    NumericsVector2 CameraRecoil,
    float AimWeight,
    float FieldOfViewMultiplier);

public readonly record struct WeaponRecoilImpulse(
    float Vertical,
    float Horizontal,
    float Position,
    float CameraShare);

public sealed class ProductionWeaponPresentation
{
    private NumericsVector3 _position;
    private NumericsVector3 _positionVelocity;
    private NumericsVector3 _rotation;
    private NumericsVector3 _rotationVelocity;
    private NumericsVector2 _cameraRecoil;
    private NumericsVector2 _cameraVelocity;
    private float _aimWeight;
    private float _bobPhase;
    private uint _recoilSequence;

    public float PositionFrequency { get; set; } = 12.0f;
    public float PositionDamping { get; set; } = 0.82f;
    public float RotationFrequency { get; set; } = 15.0f;
    public float RotationDamping { get; set; } = 0.78f;
    public float CameraFrequency { get; set; } = 18.0f;
    public float CameraDamping { get; set; } = 0.86f;
    public float AimSpeed { get; set; } = 12.0f;
    public float AimFieldOfViewMultiplier { get; set; } = 0.78f;
    public float SwayScale { get; set; } = 0.0018f;
    public float MovementSwayScale { get; set; } = 0.016f;
    public float BobAmplitude { get; set; } = 0.018f;
    public float BobFrequency { get; set; } = 9.0f;
    public float BreathingAmplitude { get; set; } = 0.0025f;

    public WeaponPresentationPose Pose { get; private set; } =
        new(NumericsVector3.Zero, NumericsQuaternion.Identity, NumericsVector2.Zero, 0.0f, 1.0f);

    public void AddRecoil(ProductionRecoilDefinition definition, uint deterministicSeed, bool aiming)
    {
        ArgumentNullException.ThrowIfNull(definition);
        ++_recoilSequence;
        uint seed = MixSeed(deterministicSeed, _recoilSequence);
        float horizontalNoise = ToSignedUnit(seed);
        float aimMultiplier = aiming ? definition.AdsMultiplier : 1.0f;
        var impulse = new WeaponRecoilImpulse(
            definition.VerticalImpulse * aimMultiplier,
            definition.HorizontalImpulse * horizontalNoise * aimMultiplier,
            definition.PositionImpulse * aimMultiplier,
            definition.CameraShare);
        AddRecoil(impulse);
    }

    public void AddRecoil(in WeaponRecoilImpulse impulse)
    {
        float cameraShare = Math.Clamp(impulse.CameraShare, 0.0f, 1.0f);
        float viewmodelShare = 1.0f - cameraShare;
        _rotationVelocity.X -= impulse.Vertical * viewmodelShare;
        _rotationVelocity.Y += impulse.Horizontal * viewmodelShare;
        _positionVelocity.Z -= MathF.Abs(impulse.Position) * viewmodelShare;
        _cameraVelocity.X -= impulse.Vertical * cameraShare;
        _cameraVelocity.Y += impulse.Horizontal * cameraShare;
    }

    public WeaponPresentationPose Tick(float deltaTime, in WeaponPresentationInput input)
    {
        float step = Math.Clamp(deltaTime, 0.0f, 0.05f);
        float targetAim = input.Aiming ? 1.0f : 0.0f;
        _aimWeight = MoveTowards(_aimWeight, targetAim, AimSpeed * step);

        float motion = Math.Clamp(input.HorizontalSpeed / 6.0f, 0.0f, 1.0f);
        if (input.Grounded && motion > 0.01f)
            _bobPhase += step * BobFrequency * (0.65f + motion * 0.75f);

        float sprintMultiplier = input.Sprinting ? 1.6f : 1.0f;
        float bob = MathF.Sin(_bobPhase) * BobAmplitude * motion * sprintMultiplier;
        float bobVertical = MathF.Abs(MathF.Cos(_bobPhase)) * BobAmplitude * 0.65f * motion;
        float breathing = MathF.Sin(_bobPhase * 0.23f) * BreathingAmplitude * (1.0f - _aimWeight * 0.5f);
        NumericsVector3 proceduralPosition = new(
            bob + input.MoveInput.X * -MovementSwayScale,
            -bobVertical + breathing,
            input.Sprinting ? 0.035f : 0.0f);
        NumericsVector3 proceduralRotation = new(
            -input.LookDelta.Y * SwayScale,
            -input.LookDelta.X * SwayScale,
            -input.MoveInput.X * MovementSwayScale * 0.5f);

        IntegrateSpring(
            ref _position,
            ref _positionVelocity,
            proceduralPosition,
            PositionFrequency,
            PositionDamping,
            step);
        IntegrateSpring(
            ref _rotation,
            ref _rotationVelocity,
            proceduralRotation,
            RotationFrequency,
            RotationDamping,
            step);
        IntegrateSpring(
            ref _cameraRecoil,
            ref _cameraVelocity,
            NumericsVector2.Zero,
            CameraFrequency,
            CameraDamping,
            step);

        NumericsQuaternion rotation = NumericsQuaternion.CreateFromYawPitchRoll(
            _rotation.Y,
            _rotation.X,
            _rotation.Z);
        float fieldOfViewMultiplier = Lerp(1.0f, AimFieldOfViewMultiplier, SmoothStep(_aimWeight));
        Pose = new WeaponPresentationPose(
            _position,
            rotation,
            _cameraRecoil,
            _aimWeight,
            fieldOfViewMultiplier);
        return Pose;
    }

    public void Reset()
    {
        _position = NumericsVector3.Zero;
        _positionVelocity = NumericsVector3.Zero;
        _rotation = NumericsVector3.Zero;
        _rotationVelocity = NumericsVector3.Zero;
        _cameraRecoil = NumericsVector2.Zero;
        _cameraVelocity = NumericsVector2.Zero;
        _aimWeight = 0.0f;
        _bobPhase = 0.0f;
        Pose = new WeaponPresentationPose(
            NumericsVector3.Zero,
            NumericsQuaternion.Identity,
            NumericsVector2.Zero,
            0.0f,
            1.0f);
    }

    private static void IntegrateSpring(
        ref NumericsVector3 value,
        ref NumericsVector3 velocity,
        NumericsVector3 target,
        float frequency,
        float damping,
        float deltaTime)
    {
        float angular = MathF.Max(0.01f, frequency) * 2.0f * MathF.PI;
        NumericsVector3 acceleration =
            (target - value) * (angular * angular) -
            velocity * (2.0f * Math.Clamp(damping, 0.0f, 2.0f) * angular);
        velocity += acceleration * deltaTime;
        value += velocity * deltaTime;
    }

    private static void IntegrateSpring(
        ref NumericsVector2 value,
        ref NumericsVector2 velocity,
        NumericsVector2 target,
        float frequency,
        float damping,
        float deltaTime)
    {
        float angular = MathF.Max(0.01f, frequency) * 2.0f * MathF.PI;
        NumericsVector2 acceleration =
            (target - value) * (angular * angular) -
            velocity * (2.0f * Math.Clamp(damping, 0.0f, 2.0f) * angular);
        velocity += acceleration * deltaTime;
        value += velocity * deltaTime;
    }

    private static float MoveTowards(float value, float target, float maximumDelta)
    {
        float delta = target - value;
        if (MathF.Abs(delta) <= maximumDelta)
            return target;
        return value + MathF.CopySign(maximumDelta, delta);
    }

    private static float SmoothStep(float value)
    {
        float clamped = Math.Clamp(value, 0.0f, 1.0f);
        return clamped * clamped * (3.0f - 2.0f * clamped);
    }

    private static float Lerp(float start, float end, float amount) =>
        start + (end - start) * amount;

    private static uint MixSeed(uint seed, uint sequence)
    {
        uint value = seed ^ (sequence * 0x9e3779b9u);
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }

    private static float ToSignedUnit(uint value) =>
        (value / (float)uint.MaxValue) * 2.0f - 1.0f;
}

public enum WeaponFeedbackKind
{
    MuzzleFlash,
    Tracer,
    Impact,
    Casing,
    DryFire,
    FireAudio,
    ReloadAudio,
}

public readonly record struct WeaponFeedbackCommand(
    WeaponFeedbackKind Kind,
    ProductionShotId ShotId,
    NumericsVector3 Position,
    NumericsVector3 Direction,
    float Intensity,
    string Variant);

public sealed class WeaponFeedbackCommandBuffer
{
    private readonly WeaponFeedbackCommand[] _commands;
    private int _head;
    private int _count;

    public WeaponFeedbackCommandBuffer(int capacity)
    {
        if (capacity <= 0)
            throw new ArgumentOutOfRangeException(nameof(capacity));
        _commands = new WeaponFeedbackCommand[capacity];
    }

    public int Capacity => _commands.Length;
    public int Count => _count;
    public int DroppedCommands { get; private set; }

    public bool TryEnqueue(in WeaponFeedbackCommand command)
    {
        if (_count == _commands.Length)
        {
            ++DroppedCommands;
            return false;
        }

        int tail = (_head + _count) % _commands.Length;
        _commands[tail] = command;
        ++_count;
        return true;
    }

    public bool TryDequeue(out WeaponFeedbackCommand command)
    {
        if (_count == 0)
        {
            command = default;
            return false;
        }

        command = _commands[_head];
        _commands[_head] = default;
        _head = (_head + 1) % _commands.Length;
        --_count;
        return true;
    }

    public void Clear()
    {
        Array.Clear(_commands);
        _head = 0;
        _count = 0;
    }
}

public readonly record struct WeaponPickupContents(
    string AmmoCompatibilityId,
    int LooseRounds,
    PhysicalMagazine? Magazine);

public sealed class WeaponPickupTransaction
{
    private WeaponPickupContents _contents;
    private bool _consumed;

    public WeaponPickupTransaction(in WeaponPickupContents contents)
    {
        if (contents.LooseRounds < 0)
            throw new ArgumentOutOfRangeException(nameof(contents));
        _contents = contents;
    }

    public bool IsConsumed => _consumed;

    public bool TryCollect(PhysicalAmmunitionInventory inventory)
    {
        ArgumentNullException.ThrowIfNull(inventory);
        if (_consumed)
            return false;

        if (_contents.Magazine is not null)
            inventory.AddMagazine(_contents.Magazine);
        if (_contents.LooseRounds > 0)
            inventory.AddLooseRounds(_contents.AmmoCompatibilityId, _contents.LooseRounds);

        _contents = default;
        _consumed = true;
        return true;
    }
}

public interface IWeaponRuntimeHud
{
    void SetVisible(bool visible);
    void SetWeaponName(string value);
    void SetAmmunition(int magazine, int chambered, int reserve, int magazines);
    void SetFireMode(string value);
    void SetReloadState(string value, float progress);
    void SetCrosshair(float spread, bool aiming);
    void SetInteractionPrompt(string value, bool visible);
}

public sealed class ProductionWeaponHudAdapter : IWeaponHudSink
{
    private readonly IWeaponRuntimeHud _hud;

    public ProductionWeaponHudAdapter(IWeaponRuntimeHud hud)
    {
        _hud = hud ?? throw new ArgumentNullException(nameof(hud));
    }

    public void Apply(in WeaponHudState state)
    {
        _hud.SetVisible(true);
        _hud.SetWeaponName(state.WeaponName);
        _hud.SetAmmunition(
            state.MagazineRounds,
            state.ChamberedRounds,
            state.ReserveRounds,
            state.MagazineCount);
        _hud.SetFireMode(state.FireMode);
        _hud.SetReloadState(state.ReloadState, string.IsNullOrEmpty(state.ReloadState) ? 0.0f : 1.0f);
        _hud.SetCrosshair(state.CrosshairSpread, state.Aiming);
    }
}
