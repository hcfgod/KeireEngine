using System.Text;

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

public static partial class Time
{
    public static float DeltaTime => NativeRuntime.DeltaTime;
    public static float FixedDeltaTime => NativeRuntime.FixedDeltaTime;
    public static float UnscaledDeltaTime => NativeRuntime.UnscaledDeltaTime;
    public static double Elapsed => NativeRuntime.ElapsedTime;
}

public enum InputDeviceType : byte
{
    Keyboard,
    Mouse,
    Gamepad
}

[Flags]
public enum InputDeviceMask : byte
{
    None = 0,
    Keyboard = 1 << 0,
    Mouse = 1 << 1,
    Gamepad = 1 << 2,
    All = Keyboard | Mouse | Gamepad
}

public readonly record struct InputDevice(uint Id, InputDeviceType Type, string Name, bool Connected, bool Paired);

public enum InputRebindStatus : byte
{
    Listening,
    Candidate,
    Completed,
    Cancelled,
    TimedOut
}

public enum InputRebindResolution : byte
{
    Replace,
    KeepBoth,
    Cancel
}

public readonly record struct InputRebindOptions(float MagnitudeThreshold, double TimeoutSeconds,
                                                 InputDeviceMask AllowedDevices)
{
    public static InputRebindOptions Default => new(0.5f, 5.0, InputDeviceMask.All);
}

public readonly record struct InputRebindSnapshot(AssetId Binding, InputRebindStatus Status, string CandidatePath,
                                                  double RemainingSeconds, uint ConflictCount);

public readonly struct InputRebindOperation
{
    private readonly ulong _id;

    internal InputRebindOperation(ulong id) => _id = id;

    public bool IsValid => _id != 0;
    public InputRebindSnapshot Snapshot =>
        IsValid ? NativeInput.RebindSnapshot(_id) : throw new InvalidOperationException("Input rebind is invalid.");
    public bool Apply(InputRebindResolution resolution) =>
        IsValid && NativeInput.ResolveRebind(_id, resolution);
    public bool Cancel() => IsValid && NativeInput.CancelRebind(_id);
}

public static class Input
{
    public static IReadOnlyList<InputDevice> Devices => NativeInput.Devices;
    public static string ControlScheme => NativeInput.ControlScheme;
    public static Vector2 Axis2D(string action) => NativeRuntime.ReadInputAxis2D(action);
    public static bool Held(string action) => (NativeRuntime.ReadInputState(action) & 1) != 0;
    public static bool Pressed(string action) => (NativeRuntime.ReadInputState(action) & 2) != 0;
    public static bool Released(string action) => (NativeRuntime.ReadInputState(action) & 4) != 0;
    public static bool Button(string action) => Held(action);
    public static float Axis(string action) => Axis2D(action).X;

    public static bool TrySetControlScheme(string scheme, bool locked = true)
    {
        if (string.IsNullOrWhiteSpace(scheme) || Encoding.UTF8.GetByteCount(scheme) > 128)
            throw new ArgumentException("Control scheme names must contain between 1 and 128 UTF-8 bytes.", nameof(scheme));
        return NativeInput.SetControlScheme(scheme, locked);
    }

    public static bool ClearControlSchemeLock() => NativeInput.ClearControlSchemeLock();

    public static bool TrySetGamepadRumble(uint device, float lowFrequency, float highFrequency, float durationSeconds)
    {
        if (device == 0)
            throw new ArgumentOutOfRangeException(nameof(device));
        if (!float.IsFinite(lowFrequency) || lowFrequency < 0.0f || lowFrequency > 1.0f)
            throw new ArgumentOutOfRangeException(nameof(lowFrequency));
        if (!float.IsFinite(highFrequency) || highFrequency < 0.0f || highFrequency > 1.0f)
            throw new ArgumentOutOfRangeException(nameof(highFrequency));
        if (!float.IsFinite(durationSeconds) || durationSeconds < 0.0f || durationSeconds > 60.0f)
            throw new ArgumentOutOfRangeException(nameof(durationSeconds));
        return NativeInput.SetGamepadRumble(device, lowFrequency, highFrequency, durationSeconds);
    }

    public static InputRebindOperation BeginInteractiveRebind(AssetId binding) =>
        BeginInteractiveRebind(binding, InputRebindOptions.Default);

    public static InputRebindOperation BeginInteractiveRebind(AssetId binding, InputRebindOptions options)
    {
        if (!binding.IsValid)
            throw new ArgumentException("Interactive rebinding requires a valid binding ID.", nameof(binding));
        if (!float.IsFinite(options.MagnitudeThreshold) || options.MagnitudeThreshold <= 0.0f ||
            options.MagnitudeThreshold > 1.0f)
            throw new ArgumentOutOfRangeException(nameof(options));
        if (!double.IsFinite(options.TimeoutSeconds) || options.TimeoutSeconds <= 0.0 || options.TimeoutSeconds > 60.0)
            throw new ArgumentOutOfRangeException(nameof(options));
        if (options.AllowedDevices == InputDeviceMask.None ||
            (options.AllowedDevices & ~InputDeviceMask.All) != InputDeviceMask.None)
            throw new ArgumentOutOfRangeException(nameof(options));
        ulong operation = NativeInput.BeginRebind(binding, options);
        return operation != 0 ? new InputRebindOperation(operation) : default;
    }

    public static bool SaveBindingOverrides(string profile)
    {
        ValidateBindingProfile(profile);
        return NativeInput.SaveBindings(profile);
    }

    public static int LoadBindingOverrides(string profile)
    {
        ValidateBindingProfile(profile);
        int applied = NativeInput.LoadBindings(profile);
        if (applied < 0)
            throw new InvalidOperationException("The native runtime could not load the input binding profile.");
        return applied;
    }

    public static bool ClearBindingOverrides() => NativeInput.ClearBindings();

    private static void ValidateBindingProfile(string profile)
    {
        if (string.IsNullOrEmpty(profile) || Encoding.UTF8.GetByteCount(profile) > 128 ||
            profile.Any(character => !char.IsAsciiLetterOrDigit(character) && character != '-' && character != '_'))
        {
            throw new ArgumentException("Binding profile names may contain only ASCII letters, digits, '-' and '_'.",
                                        nameof(profile));
        }
    }
}

public static class Physics
{
    public static bool TryRaycast(Entity context, Vector3 origin, Vector3 direction, out RaycastHit hit,
                                  float maximumDistance = 1000.0f, uint mask = uint.MaxValue,
                                  Entity ignoredEntity = default)
    {
        if (!IsFinite(origin))
            throw new ArgumentException("Raycast origins must be finite.", nameof(origin));
        if (!IsFinite(direction))
            throw new ArgumentException("Raycast directions must be finite.", nameof(direction));
        if (!float.IsFinite(maximumDistance) || maximumDistance <= 0.0f)
            throw new ArgumentOutOfRangeException(nameof(maximumDistance));
        Vector3 normalized = direction.Normalized;
        if (normalized.LengthSquared <= 0.0f)
            throw new ArgumentException("Raycast direction cannot be zero.", nameof(direction));
        return NativeRuntime.TryRaycast(context, origin, normalized, maximumDistance, mask, ignoredEntity, out hit);
    }

    private static bool IsFinite(Vector3 value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    public static IReadOnlyList<RaycastHit> Raycast(Entity context, Vector3 origin, Vector3 direction,
                                                    float maximumDistance = 1000.0f,
                                                    uint mask = uint.MaxValue) =>
        TryRaycast(context, origin, direction, out RaycastHit hit, maximumDistance, mask)
            ? new[] { hit }
            : Array.Empty<RaycastHit>();

    public static bool TryCapsuleCast(Entity context, Vector3 origin, Quaternion rotation, float radius, float height,
                                      Vector3 displacement, out RaycastHit hit, uint mask = uint.MaxValue,
                                      bool includeTriggers = false, Entity ignoredEntity = default)
    {
        if (!IsFinite(origin))
            throw new ArgumentException("Capsule cast origins must be finite.", nameof(origin));
        if (!IsFinite(rotation))
            throw new ArgumentException("Capsule cast rotations must be finite.", nameof(rotation));
        float rotationLengthSquared = (rotation.X * rotation.X) + (rotation.Y * rotation.Y) +
                                      (rotation.Z * rotation.Z) + (rotation.W * rotation.W);
        if (rotationLengthSquared <= 0.000001f)
            throw new ArgumentException("Capsule cast rotations cannot be zero.", nameof(rotation));
        if (!float.IsFinite(radius) || radius <= 0.0f)
            throw new ArgumentOutOfRangeException(nameof(radius));
        if (!float.IsFinite(height) || height < radius * 2.0f)
            throw new ArgumentOutOfRangeException(nameof(height));
        if (!IsFinite(displacement) || displacement.LengthSquared <= 0.000001f)
            throw new ArgumentException("Capsule cast displacement must be finite and non-zero.", nameof(displacement));
        ValidateIgnoredEntityWorld(context, ignoredEntity);
        return NativeRuntime.TryCapsuleCast(context, origin, rotation.Normalized, radius, height, displacement, mask,
                                            includeTriggers, ignoredEntity, out hit);
    }

    public static IReadOnlyList<Entity> OverlapSphere(Entity context, Vector3 center, float radius,
                                                       uint mask = uint.MaxValue, bool includeTriggers = true,
                                                       Entity ignoredEntity = default)
    {
        if (!IsFinite(center))
            throw new ArgumentException("Sphere overlap centers must be finite.", nameof(center));
        if (!float.IsFinite(radius) || radius <= 0.0f)
            throw new ArgumentOutOfRangeException(nameof(radius));
        ValidateIgnoredEntityWorld(context, ignoredEntity);
        return NativeRuntime.OverlapSphere(context, center, radius, mask, includeTriggers, ignoredEntity);
    }

    private static void ValidateIgnoredEntityWorld(Entity context, Entity ignoredEntity)
    {
        if (ignoredEntity.Id.IsValid && ignoredEntity.World != context.World)
            throw new ArgumentException("Ignored physics entities must belong to the query world.", nameof(ignoredEntity));
    }

    private static bool IsFinite(Quaternion value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z) && float.IsFinite(value.W);
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

[StableAssetTypeId("4b454952-4541-4e49-4d43-4c4950000001")]
public sealed class AnimationClip;

[StableAssetTypeId("4b454952-4541-4e49-4d47-524150480001")]
public sealed class AnimatorController;

public readonly record struct AnimatorStateInfo(string State, float NormalizedTime, bool IsPlaying, bool IsPaused,
                                                float Speed);

public enum ProceduralMotionState : byte
{
    Idle,
    Locomotion,
    TurnInPlace,
    Takeoff,
    Rising,
    Falling,
    Landing
}

public enum ProceduralMotionQuality : byte
{
    Auto,
    High,
    Medium,
    Low
}

public readonly record struct ProceduralLocomotionIntent(Vector3 DesiredWorldVelocity, Vector3 FacingWorldDirection,
                                                          Vector3 LookWorldDirection, float CrouchAmount,
                                                          float RunBlend, bool JumpRequested);

public readonly record struct ProceduralLocomotionState(ProceduralMotionState State, ProceduralMotionQuality Quality,
                                                         Vector3 ActualWorldVelocity, Vector3 GroundNormal,
                                                         float GaitPhase, float Speed, float VerticalSpeed,
                                                         float LandingIntensity, bool Grounded,
                                                         bool LeftFootPlanted, bool RightFootPlanted);

public readonly record struct CharacterControllerState(bool Grounded, Vector3 GroundNormal, Vector3 Velocity);

public enum RigidBodyMotion : byte
{
    Static,
    Dynamic,
    Kinematic
}

public enum ForceMode : byte
{
    Force,
    Acceleration,
    Impulse,
    VelocityChange
}

public readonly record struct RigidBodyProperties(RigidBodyMotion Motion, float Mass, Vector3 Velocity, bool Continuous,
                                                  bool UseGravity);

public readonly record struct RigidBodyHandle(Entity Entity)
{
    public bool IsValid => Entity.IsValid && Entity.HasComponent<RigidBodyComponent>();
    private RigidBodyProperties Properties =>
        IsValid && NativeRuntime.TryGetRigidBodyProperties(Entity, out RigidBodyProperties properties) ? properties
                                                                                                       : default;

    public RigidBodyMotion Motion
    {
        get => Properties.Motion;
        set
        {
            if (!Enum.IsDefined(value))
                throw new ArgumentOutOfRangeException(nameof(value));
            NativeRuntime.SetRigidBodyMotion(Entity, value);
        }
    }

    public float Mass
    {
        get => Properties.Mass;
        set
        {
            if (!float.IsFinite(value) || value <= 0.0f)
                throw new ArgumentOutOfRangeException(nameof(value), "Rigid Body mass must be finite and positive.");
            NativeRuntime.SetRigidBodyMass(Entity, value);
        }
    }

    public Vector3 Velocity
    {
        get => Properties.Velocity;
        set
        {
            ValidateFinite(value, nameof(value));
            NativeRuntime.SetRigidBodyVelocity(Entity, value);
        }
    }

    public bool Continuous
    {
        get => Properties.Continuous;
        set => NativeRuntime.SetRigidBodyFlag(Entity, 0, value);
    }

    public bool UseGravity
    {
        get => Properties.UseGravity;
        set => NativeRuntime.SetRigidBodyFlag(Entity, 1, value);
    }

    public void AddForce(Vector3 force, ForceMode mode = ForceMode.Force)
    {
        ValidateFinite(force, nameof(force));
        if (!Enum.IsDefined(mode))
            throw new ArgumentOutOfRangeException(nameof(mode));
        NativeRuntime.AddRigidBodyForce(Entity, force, mode);
    }

    public void AddImpulse(Vector3 impulse) => AddForce(impulse, ForceMode.Impulse);

    private static void ValidateFinite(Vector3 value, string parameter)
    {
        if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z))
            throw new ArgumentException("Rigid Body vectors must be finite.", parameter);
    }
}

public readonly record struct CharacterControllerHandle(Entity Entity)
{
    public bool IsValid => Entity.IsValid && Entity.HasComponent<CharacterControllerComponent>();
    public CharacterControllerState State =>
        IsValid && NativeRuntime.TryGetCharacterControllerState(Entity, out CharacterControllerState state)
            ? state
            : default;
    public bool Grounded => State.Grounded;
    public Vector3 GroundNormal => State.GroundNormal;
    public Vector3 Velocity => State.Velocity;

    public bool Move(Vector3 displacement)
    {
        if (!float.IsFinite(displacement.X) || !float.IsFinite(displacement.Y) || !float.IsFinite(displacement.Z))
            throw new ArgumentException("Character movement must be finite.", nameof(displacement));
        return IsValid && NativeRuntime.MoveCharacterController(Entity, displacement);
    }
}

public readonly record struct AnimatorHandle(Entity Entity)
{
    public bool IsValid => Entity.HasComponent<AnimatorComponent>();
    public bool IsPlaying => IsValid && NativeRuntime.GetAnimatorState(Entity).IsPlaying;
    public bool IsPaused => IsValid && NativeRuntime.GetAnimatorState(Entity).IsPaused;
    public string CurrentState => IsValid ? NativeRuntime.GetAnimatorState(Entity).State : string.Empty;
    public float NormalizedTime => IsValid ? NativeRuntime.GetAnimatorState(Entity).NormalizedTime : 0.0f;
    public float Speed
    {
        get => NativeRuntime.GetAnimatorState(Entity).Speed;
        set => Animator.SetSpeed(Entity, value);
    }

    public AnimatorStateInfo StateInfo => NativeRuntime.GetAnimatorState(Entity);
    public void Play(string state, float normalizedTime = 0.0f, string? layer = null) =>
        Animator.Play(Entity, state, normalizedTime, layer);
    public void CrossFade(string state, float duration, float normalizedTime = 0.0f, string? layer = null) =>
        Animator.CrossFade(Entity, state, duration, normalizedTime, layer);
    public void Pause() => Animator.Pause(Entity);
    public void Resume() => Animator.Resume(Entity);
    public void Stop() => Animator.Stop(Entity);
}

public static class Animator
{
    public static void Play(Entity entity, string state, float normalizedTime = 0.0f, string? layer = null)
    {
        ValidatePlayback(entity, state, normalizedTime);
        NativeRuntime.PlayAnimator(entity, state, layer ?? string.Empty, normalizedTime);
    }

    public static void CrossFade(Entity entity, string state, float duration, float normalizedTime = 0.0f,
                                 string? layer = null)
    {
        ValidatePlayback(entity, state, normalizedTime);
        if (!float.IsFinite(duration) || duration < 0.0f || duration > 60.0f)
            throw new ArgumentOutOfRangeException(nameof(duration),
                "Animator cross-fade duration must be between zero and sixty seconds.");
        NativeRuntime.CrossFadeAnimator(entity, state, layer ?? string.Empty, duration, normalizedTime);
    }

    public static void Pause(Entity entity) => NativeRuntime.PauseAnimator(entity, true);
    public static void Resume(Entity entity) => NativeRuntime.PauseAnimator(entity, false);
    public static void Stop(Entity entity) => NativeRuntime.StopAnimator(entity);
    public static void SetSpeed(Entity entity, float speed)
    {
        if (!float.IsFinite(speed) || speed < 0.0f || speed > 8.0f)
            throw new ArgumentOutOfRangeException(nameof(speed), "Animator speed must be between zero and eight.");
        NativeRuntime.SetAnimatorSpeed(entity, speed);
    }
    public static void SetFootGroundingWeight(Entity entity, float weight)
    {
        if (!float.IsFinite(weight) || weight < 0.0f || weight > 1.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(weight),
                "Animator foot-grounding weight must be between zero and one.");
        }
        NativeRuntime.SetAnimatorFootGroundingWeight(entity, weight);
    }
    public static AnimatorStateInfo GetStateInfo(Entity entity) => NativeRuntime.GetAnimatorState(entity);
    public static void SetProceduralLocomotion(Entity entity, ProceduralLocomotionIntent intent)
    {
        ValidateFinite(intent.DesiredWorldVelocity, nameof(intent));
        ValidateFinite(intent.FacingWorldDirection, nameof(intent));
        ValidateFinite(intent.LookWorldDirection, nameof(intent));
        if (!float.IsFinite(intent.CrouchAmount) || intent.CrouchAmount < 0.0f || intent.CrouchAmount > 1.0f ||
            !float.IsFinite(intent.RunBlend) || intent.RunBlend < 0.0f || intent.RunBlend > 1.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(intent),
                "Procedural locomotion crouch and run blends must be between zero and one.");
        }
        NativeRuntime.SetProceduralLocomotion(entity, intent);
    }
    public static ProceduralLocomotionState GetProceduralState(Entity entity) =>
        NativeRuntime.GetProceduralLocomotionState(entity);

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

    private static void ValidatePlayback(Entity entity, string state, float normalizedTime)
    {
        if (!entity.IsValid)
            throw new ArgumentException("Animator playback requires a valid entity.", nameof(entity));
        if (string.IsNullOrWhiteSpace(state) || state.Length > 256)
            throw new ArgumentException("Animator state names must contain between 1 and 256 characters.",
                                        nameof(state));
        if (!float.IsFinite(normalizedTime) || normalizedTime < 0.0f || normalizedTime > 1.0f)
            throw new ArgumentOutOfRangeException(nameof(normalizedTime),
                "Animator normalized time must be between zero and one.");
    }

    private static void ValidateFinite(Vector3 value, string parameter)
    {
        if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z))
            throw new ArgumentException("Procedural locomotion vectors must be finite.", parameter);
    }
}

[StableAssetTypeId("4b454952-4541-5544-494f-434c49500001")]
public sealed class AudioClip;

[StableAssetTypeId("4b454952-4541-5544-4d49-584552303031")]
public sealed class AudioMixer;

public enum AudioPlaybackState : byte
{
    Stopped,
    Playing,
    Paused
}

public readonly record struct AudioSourceStatus(AudioPlaybackState State, float Time, float Duration)
{
    public bool IsPlaying => State == AudioPlaybackState.Playing;
    public bool IsPaused => State == AudioPlaybackState.Paused;
}

public readonly record struct AudioPlaybackOptions
{
    public AudioPlaybackOptions()
    {
        Bus = "SFX";
        Mixer = default;
        BusId = default;
        Gain = 1.0f;
        Pitch = 1.0f;
        Priority = 128;
        Loop = false;
        Spatial = true;
        MinimumDistance = 1.0f;
        MaximumDistance = 100.0f;
    }

    public string Bus { get; init; }
    public AssetReference<AudioMixer> Mixer { get; init; }
    public AssetId BusId { get; init; }
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
    public bool IsValid => Entity.IsValid && Entity.HasComponent<AudioSourceComponent>();
    public AssetReference<AudioClip> Clip
    {
        get => new(NativeRuntime.GetAudioSourceProperties(Entity).Clip);
        set
        {
            if (!IsValid)
                throw new InvalidOperationException("The Audio Source is unavailable.");
            NativeRuntime.SetAudioSourceClip(Entity, value.Id);
        }
    }
    public float Volume
    {
        get => NativeRuntime.GetAudioSourceProperties(Entity).Gain;
        set
        {
            if (!float.IsFinite(value) || value < 0.0f || value > 16.0f)
                throw new ArgumentOutOfRangeException(nameof(value),
                    "Audio Source volume must be between zero and sixteen.");
            NativeRuntime.SetAudioSourceScalar(Entity, AudioSourceScalarProperty.Gain, value);
        }
    }
    public float VolumeDecibels
    {
        get => Audio.LinearToDecibels(Volume);
        set
        {
            if (!float.IsFinite(value) || value < -96.0f || value > 24.0824f)
                throw new ArgumentOutOfRangeException(nameof(value),
                    "Audio Source volume must be between -96 dB and +24.08 dB.");
            Volume = Audio.DecibelsToLinear(value);
        }
    }
    public AssetReference<AudioMixer> Mixer
    {
        get => new(NativeRuntime.GetAudioSourceProperties(Entity).Mixer);
        set
        {
            NativeAudioSourceProperties properties = NativeRuntime.GetAudioSourceProperties(Entity);
            NativeRuntime.SetAudioSourceRouting(Entity, value.Id, properties.BusId);
        }
    }
    public AssetId BusId
    {
        get => NativeRuntime.GetAudioSourceProperties(Entity).BusId;
        set
        {
            NativeAudioSourceProperties properties = NativeRuntime.GetAudioSourceProperties(Entity);
            NativeRuntime.SetAudioSourceRouting(Entity, properties.Mixer, value);
        }
    }
    public float Pitch
    {
        get => NativeRuntime.GetAudioSourceProperties(Entity).Pitch;
        set
        {
            if (!float.IsFinite(value) || value <= 0.01f || value > 8.0f)
                throw new ArgumentOutOfRangeException(nameof(value),
                    "Audio Source pitch must be greater than 0.01 and at most eight.");
            NativeRuntime.SetAudioSourceScalar(Entity, AudioSourceScalarProperty.Pitch, value);
        }
    }
    public bool Loop
    {
        get => NativeRuntime.GetAudioSourceProperties(Entity).Loop;
        set => NativeRuntime.SetAudioSourceFlag(Entity, AudioSourceFlagProperty.Loop, value);
    }
    public bool Spatial
    {
        get => NativeRuntime.GetAudioSourceProperties(Entity).Spatial;
        set => NativeRuntime.SetAudioSourceFlag(Entity, AudioSourceFlagProperty.Spatial, value);
    }
    public bool PlayOnAwake
    {
        get => NativeRuntime.GetAudioSourceProperties(Entity).PlayOnAwake;
        set => NativeRuntime.SetAudioSourceFlag(Entity, AudioSourceFlagProperty.PlayOnAwake, value);
    }
    public uint Priority
    {
        get => NativeRuntime.GetAudioSourceProperties(Entity).Priority;
        set
        {
            if (value > 255)
                throw new ArgumentOutOfRangeException(nameof(value), "Audio Source priority must be at most 255.");
            NativeRuntime.SetAudioSourceScalar(Entity, AudioSourceScalarProperty.Priority, value);
        }
    }
    public float MinimumDistance
    {
        get => NativeRuntime.GetAudioSourceProperties(Entity).MinimumDistance;
        set
        {
            NativeAudioSourceProperties properties = NativeRuntime.GetAudioSourceProperties(Entity);
            if (!float.IsFinite(value) || value < 0.0f || value >= properties.MaximumDistance)
                throw new ArgumentOutOfRangeException(nameof(value),
                    "Audio Source minimum distance must be non-negative and below its maximum distance.");
            NativeRuntime.SetAudioSourceScalar(Entity, AudioSourceScalarProperty.MinimumDistance, value);
        }
    }
    public float MaximumDistance
    {
        get => NativeRuntime.GetAudioSourceProperties(Entity).MaximumDistance;
        set
        {
            NativeAudioSourceProperties properties = NativeRuntime.GetAudioSourceProperties(Entity);
            if (!float.IsFinite(value) || value <= properties.MinimumDistance)
                throw new ArgumentOutOfRangeException(nameof(value),
                    "Audio Source maximum distance must exceed its minimum distance.");
            NativeRuntime.SetAudioSourceScalar(Entity, AudioSourceScalarProperty.MaximumDistance, value);
        }
    }
    public AudioSourceStatus Status => NativeRuntime.GetAudioSourceProperties(Entity).Status;
    public AudioPlaybackState State => Status.State;
    public bool IsPlaying => Status.IsPlaying;
    public bool IsPaused => Status.IsPaused;
    public float Time
    {
        get => Status.Time;
        set
        {
            if (!Seek(value))
                throw new InvalidOperationException("The Audio Source seek was rejected.");
        }
    }
    public float Duration => Status.Duration;
    public bool Play() => Audio.Play(Entity);
    public bool Play(AssetReference<AudioClip> clip) => Audio.Play(Entity, clip);
    public bool Play(AssetReference<AudioClip> clip, AudioPlaybackOptions options) => Audio.Play(Entity, clip, options);
    public bool Pause() => Audio.Pause(Entity);
    public bool Resume() => Audio.Resume(Entity);
    public bool Seek(float time) => Audio.Seek(Entity, time);
    public bool Stop() => Audio.Stop(Entity);
}

public enum AudioReverbZoneShape : byte
{
    Box,
    Sphere
}

/// <summary>Runtime control surface for an Audio Listener component.</summary>
public readonly record struct AudioListenerHandle(Entity Entity)
{
    public bool IsValid => Entity.IsValid && Entity.HasComponent<AudioListenerComponent>();

    public bool Primary
    {
        get => NativeRuntime.GetAudioListenerProperties(Entity).Primary;
        set
        {
            NativeAudioListenerProperties properties = NativeRuntime.GetAudioListenerProperties(Entity);
            properties.PrimaryValue = value ? (byte)1 : (byte)0;
            NativeRuntime.SetAudioListenerProperties(Entity, properties);
        }
    }

    public float Gain
    {
        get => NativeRuntime.GetAudioListenerProperties(Entity).Gain;
        set
        {
            if (!float.IsFinite(value) || value < 0.0f || value > 16.0f)
                throw new ArgumentOutOfRangeException(nameof(value), "Audio Listener gain must be between zero and sixteen.");
            NativeAudioListenerProperties properties = NativeRuntime.GetAudioListenerProperties(Entity);
            properties.Gain = value;
            NativeRuntime.SetAudioListenerProperties(Entity, properties);
        }
    }

    public float VolumeDecibels
    {
        get => Audio.LinearToDecibels(Gain);
        set => Gain = Audio.DecibelsToLinear(value);
    }
}

/// <summary>Runtime control surface for a spatial Audio Reverb Zone component.</summary>
public readonly record struct AudioReverbZoneHandle(Entity Entity)
{
    public bool IsValid => Entity.IsValid && Entity.HasComponent<AudioReverbZoneComponent>();

    public AssetReference<AudioMixer> Mixer
    {
        get => new(NativeRuntime.GetAudioReverbZoneProperties(Entity).Mixer);
        set
        {
            NativeAudioReverbZoneProperties properties = NativeRuntime.GetAudioReverbZoneProperties(Entity);
            properties.MixerHigh = value.Id.High;
            properties.MixerLow = value.Id.Low;
            NativeRuntime.SetAudioReverbZoneProperties(Entity, properties);
        }
    }

    public AssetId SnapshotId
    {
        get => NativeRuntime.GetAudioReverbZoneProperties(Entity).Snapshot;
        set
        {
            NativeAudioReverbZoneProperties properties = NativeRuntime.GetAudioReverbZoneProperties(Entity);
            properties.SnapshotHigh = value.High;
            properties.SnapshotLow = value.Low;
            NativeRuntime.SetAudioReverbZoneProperties(Entity, properties);
        }
    }

    public AudioReverbZoneShape Shape
    {
        get => (AudioReverbZoneShape)NativeRuntime.GetAudioReverbZoneProperties(Entity).ShapeValue;
        set
        {
            if (value is not AudioReverbZoneShape.Box and not AudioReverbZoneShape.Sphere)
                throw new ArgumentOutOfRangeException(nameof(value));
            NativeAudioReverbZoneProperties properties = NativeRuntime.GetAudioReverbZoneProperties(Entity);
            properties.ShapeValue = (byte)value;
            NativeRuntime.SetAudioReverbZoneProperties(Entity, properties);
        }
    }

    public Vector3 BoxHalfExtent
    {
        get => NativeRuntime.GetAudioReverbZoneProperties(Entity).BoxHalfExtent;
        set
        {
            if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z) ||
                value.X <= 0.0f || value.Y <= 0.0f || value.Z <= 0.0f ||
                value.X > 100000.0f || value.Y > 100000.0f || value.Z > 100000.0f)
                throw new ArgumentOutOfRangeException(nameof(value), "Reverb box half extents must be positive and finite.");
            NativeAudioReverbZoneProperties properties = NativeRuntime.GetAudioReverbZoneProperties(Entity);
            properties.BoxHalfExtent = value;
            NativeRuntime.SetAudioReverbZoneProperties(Entity, properties);
        }
    }

    public float SphereRadius
    {
        get => NativeRuntime.GetAudioReverbZoneProperties(Entity).SphereRadius;
        set
        {
            if (!float.IsFinite(value) || value <= 0.0f || value > 100000.0f)
                throw new ArgumentOutOfRangeException(nameof(value));
            NativeAudioReverbZoneProperties properties = NativeRuntime.GetAudioReverbZoneProperties(Entity);
            properties.SphereRadius = value;
            NativeRuntime.SetAudioReverbZoneProperties(Entity, properties);
        }
    }

    public int Priority
    {
        get => NativeRuntime.GetAudioReverbZoneProperties(Entity).Priority;
        set
        {
            if (value < -32768 || value > 32767)
                throw new ArgumentOutOfRangeException(nameof(value));
            NativeAudioReverbZoneProperties properties = NativeRuntime.GetAudioReverbZoneProperties(Entity);
            properties.Priority = value;
            NativeRuntime.SetAudioReverbZoneProperties(Entity, properties);
        }
    }

    public float BlendDistance
    {
        get => NativeRuntime.GetAudioReverbZoneProperties(Entity).BlendDistance;
        set
        {
            if (!float.IsFinite(value) || value < 0.0f || value > 100000.0f)
                throw new ArgumentOutOfRangeException(nameof(value));
            NativeAudioReverbZoneProperties properties = NativeRuntime.GetAudioReverbZoneProperties(Entity);
            properties.BlendDistance = value;
            NativeRuntime.SetAudioReverbZoneProperties(Entity, properties);
        }
    }

    public float ReverbSend
    {
        get => NativeRuntime.GetAudioReverbZoneProperties(Entity).ReverbSend;
        set
        {
            if (!float.IsFinite(value) || value < 0.0f || value > 1.0f)
                throw new ArgumentOutOfRangeException(nameof(value));
            NativeAudioReverbZoneProperties properties = NativeRuntime.GetAudioReverbZoneProperties(Entity);
            properties.ReverbSend = value;
            NativeRuntime.SetAudioReverbZoneProperties(Entity, properties);
        }
    }
}

public static class Audio
{
    public static float DecibelsToLinear(float decibels)
    {
        if (!float.IsFinite(decibels) || decibels <= -96.0f)
            return 0.0f;
        return MathF.Pow(10.0f, decibels / 20.0f);
    }

    public static float LinearToDecibels(float gain)
    {
        if (!float.IsFinite(gain) || gain <= 0.0f)
            return -96.0f;
        return MathF.Max(-96.0f, 20.0f * MathF.Log10(gain));
    }

    public static bool Play(Entity entity)
    {
        ValidateEntity(entity);
        return NativeRuntime.PlayAudioSource(entity);
    }

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
        if (string.IsNullOrWhiteSpace(options.Bus) || System.Text.Encoding.UTF8.GetByteCount(options.Bus) > 128)
            throw new ArgumentException("Audio bus names must contain between 1 and 128 UTF-8 bytes.", nameof(options));
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
    public static bool Pause(Entity entity) => NativeRuntime.PauseAudio(entity, true);
    public static bool Resume(Entity entity) => NativeRuntime.PauseAudio(entity, false);
    public static bool Seek(Entity entity, float time)
    {
        ValidateEntity(entity);
        if (!float.IsFinite(time) || time < 0.0f)
            throw new ArgumentOutOfRangeException(nameof(time), "Audio playback time must be finite and non-negative.");
        return NativeRuntime.SeekAudio(entity, time);
    }
    public static AudioSourceStatus GetStatus(Entity entity)
    {
        ValidateEntity(entity);
        return NativeRuntime.GetAudioSourceProperties(entity).Status;
    }

    private static void ValidateEntity(Entity entity)
    {
        if (!entity.IsValid)
            throw new ArgumentException("Audio playback requires a valid entity.", nameof(entity));
    }
}

/// <summary>Typed reference target for a cooked <c>.keirevfx</c> asset.</summary>
[StableAssetTypeId("4b454952-4556-4658-4546-464543540001")]
public sealed class VfxEffect;

[StableAssetTypeId("4b454952-4556-4658-564f-4c554d450001")]
public sealed class VfxVolume;

/// <summary>A canonical inclusive range used by VFX Blackboard range parameters.</summary>
/// <remarks>
/// Endpoints may be supplied in either order. The constructor stores component-wise minima and maxima so the value
/// always matches the native persisted range contract. Floating-point endpoints must be finite. Supported element
/// types are <see cref="float"/>, <see cref="long"/>, <see cref="ulong"/>, <see cref="Vector2"/>,
/// <see cref="Vector3"/>, <see cref="Vector4"/>, and <see cref="Color"/>.
/// </remarks>
public readonly record struct VfxRange<T>
    where T : unmanaged
{
    public VfxRange(T first, T second) { (Minimum, Maximum) = Normalize(first, second); }

    /// <summary>Component-wise minimum endpoint.</summary>
    public T Minimum { get; }
    /// <summary>Component-wise maximum endpoint.</summary>
    public T Maximum { get; }

    public void Deconstruct(out T minimum, out T maximum)
    {
        minimum = Minimum;
        maximum = Maximum;
    }

    private static (T Minimum, T Maximum) Normalize(T first, T second)
    {
        if (typeof(T) == typeof(float))
        {
            (float minimum, float maximum) = NormalizeScalar((float)(object)first, (float)(object)second);
            return ((T)(object)minimum, (T)(object)maximum);
        }
        if (typeof(T) == typeof(long))
        {
            long left = (long)(object)first;
            long right = (long)(object)second;
            return ((T)(object)Math.Min(left, right), (T)(object)Math.Max(left, right));
        }
        if (typeof(T) == typeof(ulong))
        {
            ulong left = (ulong)(object)first;
            ulong right = (ulong)(object)second;
            return ((T)(object)Math.Min(left, right), (T)(object)Math.Max(left, right));
        }
        if (typeof(T) == typeof(Vector2))
        {
            Vector2 left = (Vector2)(object)first;
            Vector2 right = (Vector2)(object)second;
            (float minimumX, float maximumX) = NormalizeScalar(left.X, right.X);
            (float minimumY, float maximumY) = NormalizeScalar(left.Y, right.Y);
            return ((T)(object)new Vector2(minimumX, minimumY), (T)(object)new Vector2(maximumX, maximumY));
        }
        if (typeof(T) == typeof(Vector3))
        {
            Vector3 left = (Vector3)(object)first;
            Vector3 right = (Vector3)(object)second;
            (float minimumX, float maximumX) = NormalizeScalar(left.X, right.X);
            (float minimumY, float maximumY) = NormalizeScalar(left.Y, right.Y);
            (float minimumZ, float maximumZ) = NormalizeScalar(left.Z, right.Z);
            return ((T)(object)new Vector3(minimumX, minimumY, minimumZ),
                    (T)(object)new Vector3(maximumX, maximumY, maximumZ));
        }
        if (typeof(T) == typeof(Vector4))
        {
            Vector4 left = (Vector4)(object)first;
            Vector4 right = (Vector4)(object)second;
            (float minimumX, float maximumX) = NormalizeScalar(left.X, right.X);
            (float minimumY, float maximumY) = NormalizeScalar(left.Y, right.Y);
            (float minimumZ, float maximumZ) = NormalizeScalar(left.Z, right.Z);
            (float minimumW, float maximumW) = NormalizeScalar(left.W, right.W);
            return ((T)(object)new Vector4(minimumX, minimumY, minimumZ, minimumW),
                    (T)(object)new Vector4(maximumX, maximumY, maximumZ, maximumW));
        }
        if (typeof(T) == typeof(Color))
        {
            Color left = (Color)(object)first;
            Color right = (Color)(object)second;
            (float minimumRed, float maximumRed) = NormalizeScalar(left.Red, right.Red);
            (float minimumGreen, float maximumGreen) = NormalizeScalar(left.Green, right.Green);
            (float minimumBlue, float maximumBlue) = NormalizeScalar(left.Blue, right.Blue);
            (float minimumAlpha, float maximumAlpha) = NormalizeScalar(left.Alpha, right.Alpha);
            return ((T)(object)new Color(minimumRed, minimumGreen, minimumBlue, minimumAlpha),
                    (T)(object)new Color(maximumRed, maximumGreen, maximumBlue, maximumAlpha));
        }
        throw new NotSupportedException($"{typeof(T).FullName} is not a supported VFX range element type.");
    }

    private static (float Minimum, float Maximum) NormalizeScalar(float first, float second)
    {
        if (!float.IsFinite(first) || !float.IsFinite(second))
            throw new ArgumentOutOfRangeException(nameof(first), "VFX range endpoints must be finite.");
        return (MathF.Min(first, second), MathF.Max(first, second));
    }
}

/// <summary>
/// Entity-scoped control surface for a scene VFX Emitter.
/// </summary>
/// <remarks>
/// The managed handle identifies an entity, not a native VFX generation. <see cref="IsAlive"/> can remain false while
/// the assigned effect loads asynchronously. Native generation safety is provided internally by
/// <c>Keire::VfxHandle</c>.
/// </remarks>
public readonly record struct VfxEmitterHandle(Entity Entity)
{
    /// <summary>Whether the entity still exists and has a VFX Emitter component.</summary>
    public bool IsValid => Entity.IsValid && Entity.HasComponent<VfxEmitterComponent>();
    /// <summary>Whether the runtime entity currently owns a live native effect instance.</summary>
    public bool IsAlive => IsValid && NativeRuntime.IsVfxAlive(Entity);
    /// <summary>Queues a named event for every matching system in this entity's live effect.</summary>
    public bool SendEvent(string eventName, uint spawnCount = 1)
    {
        Vfx.ValidateEvent(eventName, spawnCount);
        return IsValid && NativeRuntime.SendVfxEvent(Entity, eventName, spawnCount);
    }
    /// <summary>Pauses this emitter by setting its runtime simulation speed to zero.</summary>
    public bool Pause() => IsValid && NativeRuntime.PauseVfx(Entity, true);
    /// <summary>Resumes this emitter at simulation speed 1.0.</summary>
    public bool Resume() => IsValid && NativeRuntime.PauseVfx(Entity, false);
    /// <summary>
    /// Stops this entity's playback without removing its VFX Emitter component or disturbing other emitters.
    /// </summary>
    public bool Stop() => IsValid && NativeRuntime.StopVfx(Entity);
    /// <summary>Assigns <paramref name="effect"/> and replaces only this entity's live instance.</summary>
    public bool Restart(AssetId effect) => IsValid && NativeRuntime.PlayVfx(Entity, effect, true);
    /// <summary>Assigns <paramref name="effect"/> and replaces only this entity's live instance.</summary>
    public bool Restart(AssetReference<VfxEffect> effect) => Restart(effect.Id);
    /// <summary>Sets an exposed scalar-range Blackboard parameter on the component and live effect.</summary>
    public bool SetParameter(AssetId parameter,
                             VfxRange<float> value) => IsValid && parameter.IsValid
                                                       && NativeRuntime.SetVfxParameter(Entity, parameter, value);
    /// <summary>Sets an exposed signed-integer-range Blackboard parameter on the component and live effect.</summary>
    public bool SetParameter(AssetId parameter,
                             VfxRange<long> value) => IsValid && parameter.IsValid
                                                      && NativeRuntime.SetVfxParameter(Entity, parameter, value);
    /// <summary>Sets an exposed unsigned-integer-range Blackboard parameter on the component and live effect.</summary>
    public bool SetParameter(AssetId parameter,
                             VfxRange<ulong> value) => IsValid && parameter.IsValid
                                                       && NativeRuntime.SetVfxParameter(Entity, parameter, value);
    /// <summary>Sets an exposed Vector2-range Blackboard parameter on the component and live effect.</summary>
    public bool SetParameter(AssetId parameter,
                             VfxRange<Vector2> value) => IsValid && parameter.IsValid
                                                         && NativeRuntime.SetVfxParameter(Entity, parameter, value);
    /// <summary>Sets an exposed Vector3-range Blackboard parameter on the component and live effect.</summary>
    public bool SetParameter(AssetId parameter,
                             VfxRange<Vector3> value) => IsValid && parameter.IsValid
                                                         && NativeRuntime.SetVfxParameter(Entity, parameter, value);
    /// <summary>Sets an exposed Vector4-range Blackboard parameter on the component and live effect.</summary>
    public bool SetParameter(AssetId parameter,
                             VfxRange<Vector4> value) => IsValid && parameter.IsValid
                                                         && NativeRuntime.SetVfxParameter(Entity, parameter, value);
    /// <summary>Sets an exposed color-range Blackboard parameter on the component and live effect.</summary>
    public bool SetParameter(AssetId parameter,
                             VfxRange<Color> value) => IsValid && parameter.IsValid
                                                       && NativeRuntime.SetVfxParameter(Entity, parameter, value);
}

/// <summary>High-level Play Mode controls for entity-scoped VFX playback.</summary>
public static class Vfx
{
    internal static bool ValidateEvent(string eventName, uint spawnCount)
    {
        if (string.IsNullOrWhiteSpace(eventName))
            throw new ArgumentException("A VFX event name cannot be empty or whitespace.", nameof(eventName));
        if (Encoding.UTF8.GetByteCount(eventName) > 256)
            throw new ArgumentOutOfRangeException(nameof(eventName), "A VFX event name cannot exceed 256 UTF-8 bytes.");
        if (spawnCount is 0 or > 1_000_000)
            throw new ArgumentOutOfRangeException(nameof(spawnCount), "A VFX event spawn count must be in 1..1,000,000.");
        return true;
    }

    /// <summary>
    /// Assigns and requests playback of <paramref name="effect"/> on <paramref name="entity"/>.
    /// </summary>
    /// <remarks>
    /// The request can succeed before asynchronous asset loading creates a live native instance. Poll
    /// <see cref="VfxEmitterHandle.IsAlive"/> when activation timing matters.
    /// </remarks>
    public static VfxEmitterHandle Play(Entity entity, AssetReference<VfxEffect> effect, bool restart = false) =>
        Play(entity, effect.Id, restart);

    /// <summary>
    /// Assigns and requests playback of <paramref name="effect"/> on <paramref name="entity"/>.
    /// </summary>
    /// <exception cref="ArgumentException">The entity or effect ID is invalid.</exception>
    public static VfxEmitterHandle Play(Entity entity, AssetId effect, bool restart = false)
    {
        if (!entity.IsValid)
            throw new ArgumentException("VFX playback requires a valid entity.", nameof(entity));
        if (!effect.IsValid)
            throw new ArgumentException("VFX playback requires a valid effect.", nameof(effect));
        return NativeRuntime.PlayVfx(entity, effect, restart) ? new VfxEmitterHandle(entity) : default;
    }

    /// <summary>Stops only the entity's runtime effect without removing its VFX Emitter component.</summary>
    public static bool Stop(Entity entity) => entity.IsValid && NativeRuntime.StopVfx(entity);
    /// <summary>Pauses the entity's runtime effect.</summary>
    public static bool Pause(Entity entity) => entity.IsValid && NativeRuntime.PauseVfx(entity, true);
    /// <summary>Resumes the entity's runtime effect at simulation speed 1.0.</summary>
    public static bool Resume(Entity entity) => entity.IsValid && NativeRuntime.PauseVfx(entity, false);
    /// <summary>Reports whether the entity currently owns a live native effect instance.</summary>
    public static bool IsAlive(Entity entity) => entity.IsValid && NativeRuntime.IsVfxAlive(entity);
    /// <summary>Queues a named event for every matching system in an entity's live effect.</summary>
    public static bool SendEvent(Entity entity, string eventName, uint spawnCount = 1)
    {
        if (!entity.IsValid)
            throw new ArgumentException("VFX events require a valid entity.", nameof(entity));
        ValidateEvent(eventName, spawnCount);
        return NativeRuntime.SendVfxEvent(entity, eventName, spawnCount);
    }
    /// <summary>Sets an exposed scalar-range Blackboard parameter on an entity's component and live effect.</summary>
    public static bool SetParameter(Entity entity, AssetId parameter,
                                    VfxRange<float> value) => new VfxEmitterHandle(entity).SetParameter(parameter,
                                                                                                        value);
    /// <summary>Sets an exposed signed-integer-range Blackboard parameter on an entity's component and live
    /// effect.</summary>
    public static bool SetParameter(Entity entity, AssetId parameter,
                                    VfxRange<long> value) => new VfxEmitterHandle(entity).SetParameter(parameter,
                                                                                                       value);
    /// <summary>Sets an exposed unsigned-integer-range Blackboard parameter on an entity's component and live
    /// effect.</summary>
    public static bool SetParameter(Entity entity, AssetId parameter,
                                    VfxRange<ulong> value) => new VfxEmitterHandle(entity).SetParameter(parameter,
                                                                                                        value);
    /// <summary>Sets an exposed Vector2-range Blackboard parameter on an entity's component and live effect.</summary>
    public static bool SetParameter(Entity entity, AssetId parameter,
                                    VfxRange<Vector2> value) => new VfxEmitterHandle(entity).SetParameter(parameter,
                                                                                                          value);
    /// <summary>Sets an exposed Vector3-range Blackboard parameter on an entity's component and live effect.</summary>
    public static bool SetParameter(Entity entity, AssetId parameter,
                                    VfxRange<Vector3> value) => new VfxEmitterHandle(entity).SetParameter(parameter,
                                                                                                          value);
    /// <summary>Sets an exposed Vector4-range Blackboard parameter on an entity's component and live effect.</summary>
    public static bool SetParameter(Entity entity, AssetId parameter,
                                    VfxRange<Vector4> value) => new VfxEmitterHandle(entity).SetParameter(parameter,
                                                                                                          value);
    /// <summary>Sets an exposed color-range Blackboard parameter on an entity's component and live effect.</summary>
    public static bool SetParameter(Entity entity, AssetId parameter,
                                    VfxRange<Color> value) => new VfxEmitterHandle(entity).SetParameter(parameter,
                                                                                                        value);
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
