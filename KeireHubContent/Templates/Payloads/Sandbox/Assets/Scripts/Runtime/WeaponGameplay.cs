using Keire;

namespace KeireSandbox;

public enum ProjectileSimulationMode
{
    BallisticSimulation,
    PrefabProjectile
}

public enum WeaponFeedType
{
    DetachableMagazine,
    TubeMagazine
}

public enum WeaponFireMode
{
    SemiAutomatic,
    Automatic,
    Burst,
    PumpAction
}

public enum WeaponState
{
    Holstered,
    Equipping,
    Ready,
    Firing,
    Cycling,
    DryFire,
    ReloadStarting,
    RemovingMagazine,
    InsertingMagazine,
    Chambering,
    InsertingRound
}

[CreateAssetMenu("Combat/Ammunition", "Ammunition")]
[StableAssetTypeId("6b656972-652d-4077-8000-000000000101")]
public sealed class AmmunitionDefinition : ScriptableObject
{
    public string Caliber = "5.56x45mm";
    public float MassGrams = 4.0f;
    public float DiameterMillimeters = 5.7f;
    public float MuzzleVelocity = 920.0f;
    public float DragCoefficient = 0.18f;
    public float GravityScale = 1.0f;
    public float MaximumLifetime = 4.0f;
    public float BaseDamage = 32.0f;
    public float PenetrationEnergyScale = 1.0f;
    public float RicochetMinimumAngle = 72.0f;

    public float MuzzleEnergyJoules =>
        0.5f * (MassGrams / 1000.0f) * MuzzleVelocity * MuzzleVelocity;

    protected override void OnValidate() => Normalize();

    public void Normalize()
    {
        MassGrams = MathF.Max(0.01f, MassGrams);
        DiameterMillimeters = MathF.Max(0.1f, DiameterMillimeters);
        MuzzleVelocity = MathF.Max(1.0f, MuzzleVelocity);
        DragCoefficient = MathF.Max(0.0f, DragCoefficient);
        MaximumLifetime = MathF.Max(0.05f, MaximumLifetime);
        BaseDamage = MathF.Max(0.0f, BaseDamage);
    }
}

[CreateAssetMenu("Combat/Magazine", "Magazine")]
[StableAssetTypeId("6b656972-652d-4077-8000-000000000102")]
public sealed class MagazineDefinition : ScriptableObject
{
    public string Caliber = "5.56x45mm";
    public int Capacity = 30;

    protected override void OnValidate() => Normalize();

    public void Normalize() => Capacity = Math.Max(1, Capacity);
}

[CreateAssetMenu("Combat/Recoil Rig", "RecoilRig")]
[StableAssetTypeId("6b656972-652d-4077-8000-000000000103")]
public sealed class RecoilRigDefinition : ScriptableObject
{
    public Vector3 PositionImpulse = new(0.015f, 0.012f, -0.09f);
    public Vector3 RotationImpulse = new(-2.2f, 0.35f, 0.15f);
    public float PositionFrequency = 22.0f;
    public float PositionDamping = 0.82f;
    public float RotationFrequency = 18.0f;
    public float RotationDamping = 0.78f;
    public float AimMultiplier = 0.55f;
    public float Sway = 0.002f;
    public float Bob = 0.012f;

    protected override void OnValidate() => Normalize();

    public void Normalize()
    {
        PositionFrequency = MathF.Max(0.01f, PositionFrequency);
        PositionDamping = MathF.Max(0.0f, PositionDamping);
        RotationFrequency = MathF.Max(0.01f, RotationFrequency);
        RotationDamping = MathF.Max(0.0f, RotationDamping);
        AimMultiplier = Math.Clamp(AimMultiplier, 0.0f, 1.0f);
        Sway = MathF.Max(0.0f, Sway);
        Bob = MathF.Max(0.0f, Bob);
    }
}

[CreateAssetMenu("Combat/Weapon", "Weapon")]
[StableAssetTypeId("6b656972-652d-4077-8000-000000000104")]
public sealed class WeaponDefinition : ScriptableObject
{
    public string DisplayName = "Service Rifle";
    public WeaponFeedType FeedType = WeaponFeedType.DetachableMagazine;
    public ProjectileSimulationMode ProjectileMode = ProjectileSimulationMode.BallisticSimulation;
    public WeaponFireMode[] FireModes = [WeaponFireMode.SemiAutomatic, WeaponFireMode.Automatic];
    public int BurstCount = 3;
    public float RoundsPerMinute = 750.0f;
    public int ProjectilesPerShot = 1;
    public float HipSpreadDegrees = 0.75f;
    public float AimSpreadDegrees = 0.18f;
    public float EquipDuration = 0.25f;
    public float ReloadStartDuration = 0.12f;
    public float RemoveMagazineDuration = 0.45f;
    public float InsertMagazineDuration = 0.65f;
    public float ChamberDuration = 0.22f;
    public float InsertRoundDuration = 0.48f;
    public int TubeCapacity = 8;
    public AmmunitionDefinition Ammunition = ScriptableObject.CreateInstance<AmmunitionDefinition>();
    public MagazineDefinition Magazine = ScriptableObject.CreateInstance<MagazineDefinition>();
    public RecoilRigDefinition Recoil = ScriptableObject.CreateInstance<RecoilRigDefinition>();

    public float SecondsPerShot => 60.0f / MathF.Max(1.0f, RoundsPerMinute);

    protected override void OnValidate() => Normalize();

    public void Normalize()
    {
        RoundsPerMinute = MathF.Max(1.0f, RoundsPerMinute);
        ProjectilesPerShot = Math.Max(1, ProjectilesPerShot);
        BurstCount = Math.Max(1, BurstCount);
        TubeCapacity = Math.Max(1, TubeCapacity);
        HipSpreadDegrees = MathF.Max(0.0f, HipSpreadDegrees);
        AimSpreadDegrees = MathF.Max(0.0f, AimSpreadDegrees);
        FireModes = FireModes.Length == 0 ? [WeaponFireMode.SemiAutomatic] : FireModes;
        Ammunition.Normalize();
        Magazine.Normalize();
        Recoil.Normalize();
    }
}

[CreateAssetMenu("Combat/Ballistic Surface", "BallisticSurface")]
[StableAssetTypeId("6b656972-652d-4077-8000-000000000105")]
public sealed class BallisticSurfaceDefinition : ScriptableObject
{
    public float ResistanceJoulesPerMeter = 12000.0f;
    public float ThicknessMeters = 0.1f;
    public float RicochetRetention = 0.55f;
}

[CreateAssetMenu("Combat/Armor", "Armor")]
[StableAssetTypeId("6b656972-652d-4077-8000-000000000106")]
public sealed class ArmorDefinition : ScriptableObject
{
    public float EnergyAbsorption = 900.0f;
    public float DamageReduction = 0.65f;
}

public sealed class MagazineInstance
{
    public MagazineInstance(MagazineDefinition definition, int rounds = -1)
    {
        Definition = definition ?? throw new ArgumentNullException(nameof(definition));
        Id = Guid.NewGuid();
        Rounds = Math.Clamp(rounds < 0 ? definition.Capacity : rounds, 0, definition.Capacity);
    }

    public Guid Id { get; }
    public MagazineDefinition Definition { get; }
    public int Rounds { get; private set; }
    public bool IsEmpty => Rounds == 0;

    public bool TryTakeRound()
    {
        if (Rounds <= 0)
            return false;
        --Rounds;
        return true;
    }
}

public sealed class WeaponInventory
{
    private readonly List<MagazineInstance> _magazines = [];
    private readonly Dictionary<string, int> _looseRounds = new(StringComparer.Ordinal);

    public IReadOnlyList<MagazineInstance> Magazines => _magazines;

    public void AddMagazine(MagazineInstance magazine) =>
        _magazines.Add(magazine ?? throw new ArgumentNullException(nameof(magazine)));

    public bool RemoveMagazine(MagazineInstance magazine) => _magazines.Remove(magazine);

    public MagazineInstance? TakeFullestMagazine(MagazineDefinition definition)
    {
        MagazineInstance? selected = null;
        foreach (MagazineInstance magazine in _magazines)
        {
            if (magazine.Definition != definition || magazine.IsEmpty)
                continue;
            if (selected is null || magazine.Rounds > selected.Rounds)
                selected = magazine;
        }
        if (selected is not null)
            _magazines.Remove(selected);
        return selected;
    }

    public int MagazineCount(MagazineDefinition definition) =>
        _magazines.Count(magazine => magazine.Definition == definition && !magazine.IsEmpty);

    public int ReserveRounds(MagazineDefinition definition) =>
        _magazines.Where(magazine => magazine.Definition == definition).Sum(magazine => magazine.Rounds);

    public void AddLooseRounds(string caliber, int amount)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(caliber);
        if (amount < 0)
            throw new ArgumentOutOfRangeException(nameof(amount));
        _looseRounds[caliber] = LooseRounds(caliber) + amount;
    }

    public int LooseRounds(string caliber) =>
        _looseRounds.TryGetValue(caliber, out int count) ? count : 0;

    public bool TryTakeLooseRound(string caliber)
    {
        int count = LooseRounds(caliber);
        if (count <= 0)
            return false;
        _looseRounds[caliber] = count - 1;
        return true;
    }
}

public readonly record struct ShotId(EntityId Shooter, Guid Weapon, ulong Sequence, ulong Tick);

public readonly record struct WeaponCommandFrame(bool FireHeld, bool FirePressed, bool ReloadPressed,
                                                 bool AimHeld, bool SwitchFireModePressed);

public readonly record struct WeaponSnapshot(WeaponState State, WeaponFireMode FireMode, int LoadedRounds,
                                             int ReserveRounds, int MagazineCount, bool Chambered, bool Aiming,
                                             ulong ShotSequence);

public readonly record struct WeaponShot(ShotId Id, WeaponDefinition Definition, Vector3 Origin,
                                         Vector3 Direction, float EnergyJoules);

public sealed class WeaponRuntime
{
    private readonly Guid _instanceId = Guid.NewGuid();
    private float _stateTime;
    private float _shotCooldown;
    private int _fireModeIndex;
    private int _burstRemaining;
    private int _tubeRounds;
    private bool _chambered;
    private bool _aiming;
    private ulong _sequence;
    private ulong _tick;

    public WeaponRuntime(WeaponDefinition definition, WeaponInventory inventory)
    {
        Definition = definition ?? throw new ArgumentNullException(nameof(definition));
        Inventory = inventory ?? throw new ArgumentNullException(nameof(inventory));
        Definition.Normalize();
        State = WeaponState.Holstered;
    }

    public WeaponDefinition Definition { get; }
    public WeaponInventory Inventory { get; }
    public WeaponState State { get; private set; }
    public MagazineInstance? InsertedMagazine { get; private set; }
    public WeaponFireMode FireMode => Definition.FireModes[_fireModeIndex];
    public bool Aiming => _aiming;
    public event Action<WeaponShot>? Shot;
    public event Action<WeaponSnapshot>? Changed;

    public WeaponSnapshot Snapshot => new(
        State,
        FireMode,
        LoadedRounds,
        ReserveRounds,
        MagazineCount,
        _chambered,
        _aiming,
        _sequence);

    public int LoadedRounds =>
        Definition.FeedType == WeaponFeedType.TubeMagazine
            ? _tubeRounds + (_chambered ? 1 : 0)
            : (InsertedMagazine?.Rounds ?? 0) + (_chambered ? 1 : 0);

    public int ReserveRounds =>
        Definition.FeedType == WeaponFeedType.TubeMagazine
            ? Inventory.LooseRounds(Definition.Ammunition.Caliber)
            : Inventory.ReserveRounds(Definition.Magazine);

    public int MagazineCount =>
        Definition.FeedType == WeaponFeedType.TubeMagazine
            ? 0
            : Inventory.MagazineCount(Definition.Magazine);

    public void Equip()
    {
        if (State != WeaponState.Holstered)
            return;
        Transition(WeaponState.Equipping, Definition.EquipDuration);
    }

    public void Tick(float deltaSeconds, WeaponCommandFrame command, Entity shooter, Vector3 origin,
                     Vector3 forward)
    {
        if (deltaSeconds < 0.0f)
            throw new ArgumentOutOfRangeException(nameof(deltaSeconds));
        ++_tick;
        _shotCooldown = MathF.Max(0.0f, _shotCooldown - deltaSeconds);
        _stateTime = MathF.Max(0.0f, _stateTime - deltaSeconds);
        _aiming = command.AimHeld;
        AdvanceState();

        if (command.SwitchFireModePressed && State == WeaponState.Ready)
        {
            _fireModeIndex = (_fireModeIndex + 1) % Definition.FireModes.Length;
            PublishChanged();
        }
        if (command.ReloadPressed)
            BeginReload();
        if (State == WeaponState.Ready && ShouldFire(command))
            Fire(shooter, origin, forward);
    }

    private bool ShouldFire(WeaponCommandFrame command) => FireMode switch
    {
        WeaponFireMode.Automatic => command.FireHeld,
        WeaponFireMode.Burst => command.FirePressed || _burstRemaining > 0,
        _ => command.FirePressed
    };

    private void Fire(Entity shooter, Vector3 origin, Vector3 forward)
    {
        if (_shotCooldown > 0.0f)
            return;
        if (!_chambered && !TryChamber())
        {
            Transition(WeaponState.DryFire, 0.12f);
            return;
        }

        _chambered = false;
        _shotCooldown = Definition.SecondsPerShot;
        if (FireMode == WeaponFireMode.Burst && _burstRemaining == 0)
            _burstRemaining = Definition.BurstCount;
        if (_burstRemaining > 0)
            --_burstRemaining;

        for (int projectile = 0; projectile < Definition.ProjectilesPerShot; ++projectile)
        {
            Vector3 direction = ApplySpread(forward.Normalized, projectile);
            Shot?.Invoke(new WeaponShot(
                new ShotId(shooter.Id, _instanceId, ++_sequence, _tick),
                Definition,
                origin,
                direction,
                Definition.Ammunition.MuzzleEnergyJoules));
        }
        TryChamber();
        Transition(FireMode == WeaponFireMode.PumpAction ? WeaponState.Cycling : WeaponState.Firing,
                   Definition.SecondsPerShot);
    }

    private Vector3 ApplySpread(Vector3 forward, int projectile)
    {
        uint seed = unchecked((uint)(_sequence * 747796405UL) + (uint)projectile * 2891336453U + 277803737U);
        float x = NextSigned(ref seed);
        float y = NextSigned(ref seed);
        float spread = (_aiming ? Definition.AimSpreadDegrees : Definition.HipSpreadDegrees) *
                       (MathF.PI / 180.0f);
        Vector3 right = Vector3.Cross(MathF.Abs(forward.Y) > 0.98f ? Vector3.Right : Vector3.Up, forward).Normalized;
        Vector3 up = Vector3.Cross(forward, right).Normalized;
        return (forward + (right * x * spread) + (up * y * spread)).Normalized;
    }

    private static float NextSigned(ref uint state)
    {
        state = (state * 1664525U) + 1013904223U;
        return ((state >> 8) / 8388607.5f) - 1.0f;
    }

    private bool TryChamber()
    {
        if (_chambered)
            return true;
        if (Definition.FeedType == WeaponFeedType.TubeMagazine)
        {
            if (_tubeRounds <= 0)
                return false;
            --_tubeRounds;
            _chambered = true;
            return true;
        }
        if (InsertedMagazine?.TryTakeRound() != true)
            return false;
        _chambered = true;
        return true;
    }

    private void BeginReload()
    {
        if (State is WeaponState.ReloadStarting or WeaponState.RemovingMagazine or
            WeaponState.InsertingMagazine or WeaponState.Chambering or WeaponState.InsertingRound)
            return;
        if (Definition.FeedType == WeaponFeedType.TubeMagazine)
        {
            if (_tubeRounds >= Definition.TubeCapacity ||
                Inventory.LooseRounds(Definition.Ammunition.Caliber) == 0)
                return;
        }
        else if (Inventory.MagazineCount(Definition.Magazine) == 0)
        {
            return;
        }
        Transition(WeaponState.ReloadStarting, Definition.ReloadStartDuration);
    }

    private void AdvanceState()
    {
        if (_stateTime > 0.0f)
            return;
        switch (State)
        {
        case WeaponState.Equipping:
        case WeaponState.Firing:
        case WeaponState.Cycling:
        case WeaponState.DryFire:
            Transition(WeaponState.Ready, 0.0f);
            break;
        case WeaponState.ReloadStarting:
            Transition(Definition.FeedType == WeaponFeedType.TubeMagazine
                           ? WeaponState.InsertingRound
                           : WeaponState.RemovingMagazine,
                       Definition.FeedType == WeaponFeedType.TubeMagazine
                           ? Definition.InsertRoundDuration
                           : Definition.RemoveMagazineDuration);
            break;
        case WeaponState.RemovingMagazine:
            if (InsertedMagazine is not null && !InsertedMagazine.IsEmpty)
                Inventory.AddMagazine(InsertedMagazine);
            InsertedMagazine = null;
            Transition(WeaponState.InsertingMagazine, Definition.InsertMagazineDuration);
            break;
        case WeaponState.InsertingMagazine:
            InsertedMagazine = Inventory.TakeFullestMagazine(Definition.Magazine);
            Transition(_chambered ? WeaponState.Ready : WeaponState.Chambering,
                       _chambered ? 0.0f : Definition.ChamberDuration);
            break;
        case WeaponState.Chambering:
            TryChamber();
            Transition(WeaponState.Ready, 0.0f);
            break;
        case WeaponState.InsertingRound:
            if (_tubeRounds < Definition.TubeCapacity &&
                Inventory.TryTakeLooseRound(Definition.Ammunition.Caliber))
            {
                ++_tubeRounds;
            }
            Transition(_tubeRounds < Definition.TubeCapacity &&
                               Inventory.LooseRounds(Definition.Ammunition.Caliber) > 0
                           ? WeaponState.InsertingRound
                           : WeaponState.Chambering,
                       _tubeRounds < Definition.TubeCapacity &&
                               Inventory.LooseRounds(Definition.Ammunition.Caliber) > 0
                           ? Definition.InsertRoundDuration
                           : Definition.ChamberDuration);
            break;
        }
    }

    private void Transition(WeaponState state, float duration)
    {
        State = state;
        _stateTime = MathF.Max(0.0f, duration);
        PublishChanged();
    }

    private void PublishChanged() => Changed?.Invoke(Snapshot);
}

public readonly record struct BallisticQuery(Entity Context, Entity IgnoredEntity, Vector3 Start,
                                              Vector3 End, float Radius, uint Mask);

public readonly record struct BallisticCollision(RaycastHit Hit, BallisticSurfaceDefinition Surface);

public interface IBallisticCollisionWorld
{
    bool Sweep(in BallisticQuery query, out BallisticCollision collision);
}

public sealed class EngineBallisticCollisionWorld : IBallisticCollisionWorld
{
    private readonly BallisticSurfaceDefinition _defaultSurface =
        ScriptableObject.CreateInstance<BallisticSurfaceDefinition>();

    public bool Sweep(in BallisticQuery query, out BallisticCollision collision)
    {
        Vector3 delta = query.End - query.Start;
        if (Physics.TryRaycast(query.Context, query.Start, delta, out RaycastHit hit, delta.Length,
                               query.Mask, query.IgnoredEntity))
        {
            collision = new BallisticCollision(hit, _defaultSurface);
            return true;
        }
        collision = default;
        return false;
    }
}

public readonly record struct DamageInfo(ShotId Shot, Entity Source, Entity Target, Vector3 Point,
                                         Vector3 Normal, float Damage, float EnergyJoules,
                                         float PenetrationMeters);

public readonly record struct DamageResult(float AppliedDamage, float RemainingHealth, bool Killed,
                                           bool ArmorStopped);

public interface IDamageReceiver
{
    DamageResult ApplyDamage(in DamageInfo damage);
}

public static class Damage
{
    private static readonly Dictionary<Entity, IDamageReceiver> Receivers = [];

    public static void Register(Entity entity, IDamageReceiver receiver)
    {
        if (!entity.IsValid)
            throw new ArgumentException("Damage receiver entity must be valid.", nameof(entity));
        Receivers[entity] = receiver ?? throw new ArgumentNullException(nameof(receiver));
    }

    public static void Unregister(Entity entity, IDamageReceiver receiver)
    {
        if (Receivers.TryGetValue(entity, out IDamageReceiver? current) && ReferenceEquals(current, receiver))
            Receivers.Remove(entity);
    }

    public static bool TryApply(in DamageInfo info, out DamageResult result)
    {
        if (Receivers.TryGetValue(info.Target, out IDamageReceiver? receiver))
        {
            result = receiver.ApplyDamage(info);
            return true;
        }
        result = default;
        return false;
    }
}

public sealed class BallisticWorld
{
    private struct Projectile
    {
        public bool Active;
        public WeaponShot Shot;
        public Entity Shooter;
        public Vector3 Position;
        public Vector3 Velocity;
        public float Age;
        public float Energy;
        public int Penetrations;
    }

    private readonly Projectile[] _projectiles;
    private readonly IBallisticCollisionWorld _collisionWorld;

    public BallisticWorld(IBallisticCollisionWorld collisionWorld, int capacity = 2048)
    {
        _collisionWorld = collisionWorld ?? throw new ArgumentNullException(nameof(collisionWorld));
        _projectiles = new Projectile[Math.Max(1, capacity)];
    }

    public int ActiveCount { get; private set; }

    public bool Spawn(Entity shooter, in WeaponShot shot)
    {
        for (int index = 0; index < _projectiles.Length; ++index)
        {
            if (_projectiles[index].Active)
                continue;
            _projectiles[index] = new Projectile
            {
                Active = true,
                Shot = shot,
                Shooter = shooter,
                Position = shot.Origin,
                Velocity = shot.Direction * shot.Definition.Ammunition.MuzzleVelocity,
                Energy = shot.EnergyJoules
            };
            ++ActiveCount;
            return true;
        }
        return false;
    }

    public void Step(float deltaSeconds)
    {
        if (deltaSeconds <= 0.0f)
            return;
        for (int index = 0; index < _projectiles.Length; ++index)
        {
            ref Projectile projectile = ref _projectiles[index];
            if (!projectile.Active)
                continue;
            AmmunitionDefinition ammunition = projectile.Shot.Definition.Ammunition;
            projectile.Age += deltaSeconds;
            if (projectile.Age >= ammunition.MaximumLifetime || projectile.Energy <= 1.0f)
            {
                Retire(ref projectile);
                continue;
            }

            projectile.Velocity += new Vector3(0.0f, -9.80665f * ammunition.GravityScale, 0.0f) * deltaSeconds;
            projectile.Velocity *= MathF.Exp(-ammunition.DragCoefficient * deltaSeconds);
            Vector3 next = projectile.Position + (projectile.Velocity * deltaSeconds);
            var query = new BallisticQuery(projectile.Shooter, projectile.Shooter, projectile.Position,
                                           next, ammunition.DiameterMillimeters * 0.0005f, uint.MaxValue);
            if (!_collisionWorld.Sweep(query, out BallisticCollision collision))
            {
                projectile.Position = next;
                continue;
            }

            float speed = projectile.Velocity.Length;
            projectile.Energy = 0.5f * (ammunition.MassGrams / 1000.0f) * speed * speed;
            float normalizedEnergy = Math.Clamp(projectile.Energy / MathF.Max(1.0f, ammunition.MuzzleEnergyJoules),
                                                0.0f, 1.0f);
            var damageInfo = new DamageInfo(
                projectile.Shot.Id,
                projectile.Shooter,
                collision.Hit.Entity,
                collision.Hit.Point,
                collision.Hit.Normal,
                ammunition.BaseDamage * normalizedEnergy,
                projectile.Energy,
                collision.Surface.ThicknessMeters);
            Damage.TryApply(damageInfo, out _);

            float incidence = MathF.Acos(Math.Clamp(
                -Vector3.Dot(projectile.Velocity.Normalized, collision.Hit.Normal), -1.0f, 1.0f)) *
                (180.0f / MathF.PI);
            float resistance = collision.Surface.ResistanceJoulesPerMeter *
                               collision.Surface.ThicknessMeters /
                               MathF.Max(0.01f, ammunition.PenetrationEnergyScale);
            if (incidence >= ammunition.RicochetMinimumAngle)
            {
                projectile.Velocity =
                    Vector3.Reflect(projectile.Velocity, collision.Hit.Normal) *
                    Math.Clamp(collision.Surface.RicochetRetention, 0.0f, 1.0f);
                projectile.Position = collision.Hit.Point + (collision.Hit.Normal * 0.002f);
                projectile.Energy *= collision.Surface.RicochetRetention;
            }
            else if (projectile.Energy > resistance && projectile.Penetrations < 4)
            {
                projectile.Energy -= resistance;
                float retained = MathF.Sqrt(projectile.Energy /
                                            MathF.Max(1.0f, 0.5f * (ammunition.MassGrams / 1000.0f) *
                                                                  speed * speed));
                projectile.Velocity *= Math.Clamp(retained, 0.0f, 1.0f);
                projectile.Position = collision.Hit.Point +
                                      (projectile.Velocity.Normalized *
                                       (collision.Surface.ThicknessMeters + 0.002f));
                ++projectile.Penetrations;
            }
            else
            {
                Retire(ref projectile);
            }
        }
    }

    private void Retire(ref Projectile projectile)
    {
        projectile = default;
        --ActiveCount;
    }
}

public sealed class WeaponPresentationRig
{
    private Vector3 _position;
    private Vector3 _positionVelocity;
    private Vector3 _rotation;
    private Vector3 _rotationVelocity;

    public Vector3 Position => _position;
    public Vector3 Rotation => _rotation;

    public void AddShotImpulse(RecoilRigDefinition definition, bool aiming)
    {
        float multiplier = aiming ? definition.AimMultiplier : 1.0f;
        _positionVelocity += definition.PositionImpulse * multiplier;
        _rotationVelocity += definition.RotationImpulse * multiplier;
    }

    public void Step(RecoilRigDefinition definition, float deltaSeconds, Vector2 movement, float elapsed)
    {
        if (deltaSeconds <= 0.0f)
            return;
        IntegrateSpring(ref _position, ref _positionVelocity, Vector3.Zero,
                        definition.PositionFrequency, definition.PositionDamping, deltaSeconds);
        IntegrateSpring(ref _rotation, ref _rotationVelocity, Vector3.Zero,
                        definition.RotationFrequency, definition.RotationDamping, deltaSeconds);
        float movementAmount = Math.Clamp(movement.Length, 0.0f, 1.0f);
        _position += new Vector3(MathF.Sin(elapsed * 8.0f), MathF.Abs(MathF.Cos(elapsed * 8.0f)), 0.0f) *
                     definition.Bob * movementAmount;
    }

    private static void IntegrateSpring(ref Vector3 value, ref Vector3 velocity, Vector3 target,
                                        float frequency, float damping, float deltaSeconds)
    {
        float angular = MathF.Max(0.01f, frequency) * 2.0f * MathF.PI;
        Vector3 acceleration = ((target - value) * (angular * angular)) -
                               (velocity * (2.0f * Math.Clamp(damping, 0.0f, 2.0f) * angular));
        velocity += acceleration * deltaSeconds;
        value += velocity * deltaSeconds;
    }
}

public sealed class WeaponHudModel
{
    public string WeaponName { get; private set; } = string.Empty;
    public string FireMode { get; private set; } = string.Empty;
    public int LoadedRounds { get; private set; }
    public int ReserveRounds { get; private set; }
    public int MagazineCount { get; private set; }
    public bool Reloading { get; private set; }

    public void Apply(string weaponName, in WeaponSnapshot snapshot)
    {
        WeaponName = weaponName;
        FireMode = snapshot.FireMode.ToString();
        LoadedRounds = snapshot.LoadedRounds;
        ReserveRounds = snapshot.ReserveRounds;
        MagazineCount = snapshot.MagazineCount;
        Reloading = snapshot.State is WeaponState.ReloadStarting or WeaponState.RemovingMagazine or
                    WeaponState.InsertingMagazine or WeaponState.Chambering or WeaponState.InsertingRound;
    }
}
