using System;
using System.Collections.Generic;
using NumericsVector3 = System.Numerics.Vector3;

namespace Keire.Production.Weapons;

public enum WeaponFeedType
{
    DetachableMagazine,
    InternalTube,
}

public enum WeaponFireMode
{
    Safe,
    SemiAutomatic,
    Burst,
    Automatic,
    PumpAction,
}

public enum WeaponRuntimeState
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
    InsertingShell,
    ReloadFinishing,
    Unequipping,
    Faulted,
}

public enum WeaponReloadKind
{
    None,
    Tactical,
    Empty,
    PerShell,
}

public readonly record struct ProductionShotId(ulong WeaponInstance, ulong Sequence, uint Pellet)
{
    public override string ToString() => $"{WeaponInstance:x16}:{Sequence:x16}:{Pellet:x8}";
}

public readonly record struct WeaponInputFrame(
    bool FireHeld,
    bool FirePressed,
    bool FireReleased,
    bool AimHeld,
    bool ReloadPressed,
    bool ChangeFireModePressed,
    bool InspectPressed);

public readonly record struct WeaponRuntimeSnapshot(
    WeaponRuntimeState State,
    WeaponFireMode FireMode,
    WeaponReloadKind ReloadKind,
    int MagazineRounds,
    int ChamberedRounds,
    int ReserveRounds,
    int MagazineCount,
    bool Aiming,
    float StateProgress,
    ulong ShotSequence);

public readonly record struct WeaponShotRequest(
    ProductionShotId Id,
    float MuzzleVelocity,
    float ProjectileMass,
    float ProjectileRadius,
    float Drag,
    float GravityScale,
    float MaximumLifetime,
    float DamageScale,
    float PenetrationEnergy,
    uint CollisionMask,
    uint SpreadSeed);

public readonly record struct WeaponDamagePacket(
    ProductionShotId ShotId,
    ulong SourceEntity,
    ulong HitEntity,
    float Damage,
    float Energy,
    NumericsVector3 Point,
    NumericsVector3 Normal,
    NumericsVector3 Direction,
    string SurfaceId);

public readonly record struct WeaponDamageResponse(float AppliedDamage, bool Killed, bool Blocked);

public readonly record struct BallisticSweepRequest(
    NumericsVector3 Start,
    NumericsVector3 End,
    float Radius,
    uint CollisionMask,
    ulong IgnoredHierarchy);

public readonly record struct BallisticSweepHit(
    ulong Entity,
    NumericsVector3 Point,
    NumericsVector3 Normal,
    float Fraction,
    float SurfaceResistance,
    float DamageMultiplier,
    string SurfaceId);

[StableAssetTypeId("80836e51-06a8-43bb-91b8-f2c5ef32a001")]
public sealed class ProductionAmmoDefinition : ScriptableObject
{
    [SerializeField] private string _displayName = "5.56x45mm";
    [SerializeField] private string _compatibilityId = "ammo.556";
    [SerializeField] private float _projectileMassGrams = 4.0f;
    [SerializeField] private float _muzzleVelocity = 910.0f;
    [SerializeField] private float _projectileRadius = 0.00285f;
    [SerializeField] private float _drag = 0.0024f;
    [SerializeField] private float _gravityScale = 1.0f;
    [SerializeField] private float _maximumLifetime = 5.0f;
    [SerializeField] private float _baseDamage = 34.0f;
    [SerializeField] private float _penetrationEnergy = 950.0f;
    [SerializeField] private uint _collisionMask = uint.MaxValue;
    [SerializeField] private int _pelletCount = 1;

    public string DisplayName => _displayName;
    public string CompatibilityId => _compatibilityId;
    public float ProjectileMassKilograms => MathF.Max(0.00001f, _projectileMassGrams * 0.001f);
    public float MuzzleVelocity => MathF.Max(0.0f, _muzzleVelocity);
    public float ProjectileRadius => MathF.Max(0.0001f, _projectileRadius);
    public float Drag => MathF.Max(0.0f, _drag);
    public float GravityScale => MathF.Max(0.0f, _gravityScale);
    public float MaximumLifetime => MathF.Max(0.05f, _maximumLifetime);
    public float BaseDamage => MathF.Max(0.0f, _baseDamage);
    public float PenetrationEnergy => MathF.Max(0.0f, _penetrationEnergy);
    public uint CollisionMask => _collisionMask;
    public int PelletCount => Math.Clamp(_pelletCount, 1, 64);
}

[StableAssetTypeId("80836e51-06a8-43bb-91b8-f2c5ef32a002")]
public sealed class ProductionMagazineDefinition : ScriptableObject
{
    [SerializeField] private string _displayName = "STANAG Magazine";
    [SerializeField] private string _compatibilityId = "mag.stanag";
    [SerializeField] private string _ammoCompatibilityId = "ammo.556";
    [SerializeField] private int _capacity = 30;

    public string DisplayName => _displayName;
    public string CompatibilityId => _compatibilityId;
    public string AmmoCompatibilityId => _ammoCompatibilityId;
    public int Capacity => Math.Clamp(_capacity, 1, 512);
}

[StableAssetTypeId("80836e51-06a8-43bb-91b8-f2c5ef32a003")]
public sealed class ProductionRecoilDefinition : ScriptableObject
{
    [SerializeField] private float _verticalImpulse = 1.35f;
    [SerializeField] private float _horizontalImpulse = 0.35f;
    [SerializeField] private float _positionImpulse = 0.04f;
    [SerializeField] private float _cameraShare = 0.3f;
    [SerializeField] private float _returnSpeed = 18.0f;
    [SerializeField] private float _damping = 8.0f;
    [SerializeField] private float _adsMultiplier = 0.72f;

    public float VerticalImpulse => MathF.Max(0.0f, _verticalImpulse);
    public float HorizontalImpulse => MathF.Max(0.0f, _horizontalImpulse);
    public float PositionImpulse => MathF.Max(0.0f, _positionImpulse);
    public float CameraShare => Math.Clamp(_cameraShare, 0.0f, 1.0f);
    public float ReturnSpeed => MathF.Max(0.01f, _returnSpeed);
    public float Damping => MathF.Max(0.01f, _damping);
    public float AdsMultiplier => MathF.Max(0.0f, _adsMultiplier);
}

[StableAssetTypeId("80836e51-06a8-43bb-91b8-f2c5ef32a004")]
public sealed class ProductionWeaponDefinition : ScriptableObject
{
    [SerializeField] private string _displayName = "Service Rifle";
    [SerializeField] private string _weaponId = "weapon.rifle";
    [SerializeField] private string _ammoCompatibilityId = "ammo.556";
    [SerializeField] private string _magazineCompatibilityId = "mag.stanag";
    [SerializeField] private WeaponFeedType _feedType = WeaponFeedType.DetachableMagazine;
    [SerializeField] private WeaponFireMode _defaultFireMode = WeaponFireMode.SemiAutomatic;
    [SerializeField] private WeaponFireMode[] _availableFireModes =
    {
        WeaponFireMode.Safe,
        WeaponFireMode.SemiAutomatic,
        WeaponFireMode.Automatic,
    };
    [SerializeField] private int _internalCapacity = 8;
    [SerializeField] private int _burstCount = 3;
    [SerializeField] private float _roundsPerMinute = 750.0f;
    [SerializeField] private float _equipDuration = 0.35f;
    [SerializeField] private float _unequipDuration = 0.25f;
    [SerializeField] private float _dryFireDuration = 0.15f;
    [SerializeField] private float _cycleDuration = 0.08f;
    [SerializeField] private float _reloadStartDuration = 0.18f;
    [SerializeField] private float _removeMagazineDuration = 0.42f;
    [SerializeField] private float _insertMagazineDuration = 0.62f;
    [SerializeField] private float _chamberDuration = 0.36f;
    [SerializeField] private float _shellInsertDuration = 0.55f;
    [SerializeField] private float _reloadFinishDuration = 0.2f;
    [SerializeField] private bool _closedBolt = true;
    [SerializeField] private bool _retainPartialMagazines = true;
    [SerializeField] private float _hipSpreadDegrees = 1.35f;
    [SerializeField] private float _adsSpreadDegrees = 0.18f;

    public string DisplayName => _displayName;
    public string WeaponId => _weaponId;
    public string AmmoCompatibilityId => _ammoCompatibilityId;
    public string MagazineCompatibilityId => _magazineCompatibilityId;
    public WeaponFeedType FeedType => _feedType;
    public WeaponFireMode DefaultFireMode => _defaultFireMode;
    public IReadOnlyList<WeaponFireMode> AvailableFireModes => _availableFireModes;
    public int InternalCapacity => Math.Clamp(_internalCapacity, 1, 128);
    public int BurstCount => Math.Clamp(_burstCount, 2, 16);
    public float SecondsPerShot => 60.0f / MathF.Max(1.0f, _roundsPerMinute);
    public float EquipDuration => MathF.Max(0.0f, _equipDuration);
    public float UnequipDuration => MathF.Max(0.0f, _unequipDuration);
    public float DryFireDuration => MathF.Max(0.0f, _dryFireDuration);
    public float CycleDuration => MathF.Max(0.0f, _cycleDuration);
    public float ReloadStartDuration => MathF.Max(0.0f, _reloadStartDuration);
    public float RemoveMagazineDuration => MathF.Max(0.0f, _removeMagazineDuration);
    public float InsertMagazineDuration => MathF.Max(0.0f, _insertMagazineDuration);
    public float ChamberDuration => MathF.Max(0.0f, _chamberDuration);
    public float ShellInsertDuration => MathF.Max(0.0f, _shellInsertDuration);
    public float ReloadFinishDuration => MathF.Max(0.0f, _reloadFinishDuration);
    public bool ClosedBolt => _closedBolt;
    public bool RetainPartialMagazines => _retainPartialMagazines;
    public float HipSpreadDegrees => MathF.Max(0.0f, _hipSpreadDegrees);
    public float AdsSpreadDegrees => MathF.Max(0.0f, _adsSpreadDegrees);
}

public sealed class PhysicalMagazine
{
    public PhysicalMagazine(ulong itemId, ProductionMagazineDefinition definition, int rounds)
    {
        ItemId = itemId;
        Definition = definition ?? throw new ArgumentNullException(nameof(definition));
        Rounds = Math.Clamp(rounds, 0, definition.Capacity);
    }

    public ulong ItemId { get; }
    public ProductionMagazineDefinition Definition { get; }
    public int Rounds { get; private set; }
    public bool IsEmpty => Rounds == 0;

    public bool TryTakeRound()
    {
        if (Rounds == 0)
            return false;

        --Rounds;
        return true;
    }

    public int AddRounds(int count)
    {
        int accepted = Math.Clamp(count, 0, Definition.Capacity - Rounds);
        Rounds += accepted;
        return accepted;
    }
}

public sealed class PhysicalAmmunitionInventory
{
    private readonly List<PhysicalMagazine> _magazines;
    private readonly Dictionary<string, int> _looseRounds = new(StringComparer.Ordinal);

    public PhysicalAmmunitionInventory(int magazineCapacity = 12)
    {
        _magazines = new List<PhysicalMagazine>(Math.Max(1, magazineCapacity));
    }

    public IReadOnlyList<PhysicalMagazine> Magazines => _magazines;

    public void AddMagazine(PhysicalMagazine magazine)
    {
        ArgumentNullException.ThrowIfNull(magazine);
        if (_magazines.Exists(candidate => candidate.ItemId == magazine.ItemId))
            throw new InvalidOperationException($"Magazine item {magazine.ItemId} is already in this inventory.");

        _magazines.Add(magazine);
    }

    public bool RemoveMagazine(ulong itemId, out PhysicalMagazine? magazine)
    {
        int index = _magazines.FindIndex(candidate => candidate.ItemId == itemId);
        if (index < 0)
        {
            magazine = null;
            return false;
        }

        magazine = _magazines[index];
        _magazines.RemoveAt(index);
        return true;
    }

    public PhysicalMagazine? TakeBestMagazine(string compatibilityId)
    {
        int bestIndex = -1;
        int bestRounds = 0;
        for (int index = 0; index < _magazines.Count; ++index)
        {
            PhysicalMagazine candidate = _magazines[index];
            if (!StringComparer.Ordinal.Equals(candidate.Definition.CompatibilityId, compatibilityId) ||
                candidate.Rounds <= bestRounds)
            {
                continue;
            }

            bestIndex = index;
            bestRounds = candidate.Rounds;
        }

        if (bestIndex < 0)
            return null;

        PhysicalMagazine result = _magazines[bestIndex];
        _magazines.RemoveAt(bestIndex);
        return result;
    }

    public void AddLooseRounds(string compatibilityId, int count)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(compatibilityId);
        if (count <= 0)
            return;

        _looseRounds.TryGetValue(compatibilityId, out int existing);
        _looseRounds[compatibilityId] = checked(existing + count);
    }

    public bool TryTakeLooseRound(string compatibilityId)
    {
        if (!_looseRounds.TryGetValue(compatibilityId, out int count) || count <= 0)
            return false;

        if (count == 1)
            _looseRounds.Remove(compatibilityId);
        else
            _looseRounds[compatibilityId] = count - 1;
        return true;
    }

    public int CountLooseRounds(string compatibilityId) =>
        _looseRounds.TryGetValue(compatibilityId, out int count) ? count : 0;

    public int CountMagazineRounds(string magazineCompatibilityId)
    {
        int rounds = 0;
        foreach (PhysicalMagazine magazine in _magazines)
        {
            if (StringComparer.Ordinal.Equals(magazine.Definition.CompatibilityId, magazineCompatibilityId))
                rounds += magazine.Rounds;
        }

        return rounds;
    }

    public int CountNonEmptyMagazines(string magazineCompatibilityId)
    {
        int count = 0;
        foreach (PhysicalMagazine magazine in _magazines)
        {
            if (!magazine.IsEmpty &&
                StringComparer.Ordinal.Equals(magazine.Definition.CompatibilityId, magazineCompatibilityId))
            {
                ++count;
            }
        }

        return count;
    }
}

public interface IWeaponRuntimeSink
{
    void OnStateChanged(in WeaponRuntimeSnapshot snapshot);
    void OnShot(in WeaponShotRequest request);
    void OnDryFire();
    void OnMagazineRemoved(PhysicalMagazine magazine);
    void OnMagazineInserted(PhysicalMagazine magazine);
    void OnShellInserted(int tubeRounds);
}

public sealed class ProductionWeaponRuntime
{
    private readonly ulong _instanceId;
    private readonly ProductionWeaponDefinition _weapon;
    private readonly ProductionAmmoDefinition _ammo;
    private readonly PhysicalAmmunitionInventory _inventory;
    private readonly IWeaponRuntimeSink _sink;
    private PhysicalMagazine? _insertedMagazine;
    private PhysicalMagazine? _pendingMagazine;
    private WeaponRuntimeState _state = WeaponRuntimeState.Holstered;
    private WeaponReloadKind _reloadKind;
    private WeaponFireMode _fireMode;
    private float _stateElapsed;
    private float _nextShotDelay;
    private int _chamberedRounds;
    private int _tubeRounds;
    private int _burstRemaining;
    private ulong _shotSequence;
    private bool _aiming;
    private bool _reloadInterrupted;

    public ProductionWeaponRuntime(
        ulong instanceId,
        ProductionWeaponDefinition weapon,
        ProductionAmmoDefinition ammo,
        PhysicalAmmunitionInventory inventory,
        IWeaponRuntimeSink sink)
    {
        _instanceId = instanceId;
        _weapon = weapon ?? throw new ArgumentNullException(nameof(weapon));
        _ammo = ammo ?? throw new ArgumentNullException(nameof(ammo));
        _inventory = inventory ?? throw new ArgumentNullException(nameof(inventory));
        _sink = sink ?? throw new ArgumentNullException(nameof(sink));
        _fireMode = weapon.DefaultFireMode;
    }

    public WeaponRuntimeSnapshot Snapshot => CreateSnapshot();
    public PhysicalMagazine? InsertedMagazine => _insertedMagazine;

    public void SetInitialMagazine(PhysicalMagazine? magazine, bool chamberRound)
    {
        if (_state != WeaponRuntimeState.Holstered)
            throw new InvalidOperationException("Initial weapon state can only be configured while holstered.");
        if (magazine is not null &&
            !StringComparer.Ordinal.Equals(
                magazine.Definition.CompatibilityId,
                _weapon.MagazineCompatibilityId))
        {
            throw new InvalidOperationException("The magazine is not compatible with this weapon.");
        }

        _insertedMagazine = magazine;
        if (chamberRound && _weapon.ClosedBolt && _insertedMagazine?.TryTakeRound() == true)
            _chamberedRounds = 1;
    }

    public void SetInitialTubeRounds(int rounds, bool chamberRound)
    {
        if (_state != WeaponRuntimeState.Holstered)
            throw new InvalidOperationException("Initial weapon state can only be configured while holstered.");

        _tubeRounds = Math.Clamp(rounds, 0, _weapon.InternalCapacity);
        if (chamberRound && _weapon.ClosedBolt && _tubeRounds > 0)
        {
            --_tubeRounds;
            _chamberedRounds = 1;
        }
    }

    public void Equip()
    {
        if (_state is WeaponRuntimeState.Holstered or WeaponRuntimeState.Unequipping)
            Transition(WeaponRuntimeState.Equipping);
    }

    public void Unequip()
    {
        if (_state == WeaponRuntimeState.Holstered)
            return;

        CancelReload();
        Transition(WeaponRuntimeState.Unequipping);
    }

    public void Tick(float deltaTime, in WeaponInputFrame input, uint deterministicSeed)
    {
        float step = Math.Clamp(deltaTime, 0.0f, 0.25f);
        _stateElapsed += step;
        _nextShotDelay = MathF.Max(0.0f, _nextShotDelay - step);
        _aiming = input.AimHeld;

        if (input.ChangeFireModePressed)
            SelectNextFireMode();

        if (input.ReloadPressed)
        {
            if (IsReloading)
                _reloadInterrupted = true;
            else
                BeginReload();
        }

        AdvanceTimedState();

        if (_state != WeaponRuntimeState.Ready)
            return;

        bool requestsShot = _fireMode switch
        {
            WeaponFireMode.SemiAutomatic => input.FirePressed,
            WeaponFireMode.Automatic => input.FireHeld,
            WeaponFireMode.Burst => input.FirePressed || _burstRemaining > 0,
            WeaponFireMode.PumpAction => input.FirePressed,
            _ => false,
        };

        if (requestsShot && _nextShotDelay <= 0.0f)
            Fire(deterministicSeed);
    }

    private bool IsReloading => _state is
        WeaponRuntimeState.ReloadStarting or
        WeaponRuntimeState.RemovingMagazine or
        WeaponRuntimeState.InsertingMagazine or
        WeaponRuntimeState.InsertingShell or
        WeaponRuntimeState.Chambering or
        WeaponRuntimeState.ReloadFinishing;

    private void Fire(uint deterministicSeed)
    {
        if (!TryConsumeChamberedRound())
        {
            _sink.OnDryFire();
            Transition(WeaponRuntimeState.DryFire);
            return;
        }

        ++_shotSequence;
        if (_fireMode == WeaponFireMode.Burst && _burstRemaining == 0)
            _burstRemaining = _weapon.BurstCount;
        if (_burstRemaining > 0)
            --_burstRemaining;

        for (int pellet = 0; pellet < _ammo.PelletCount; ++pellet)
        {
            var id = new ProductionShotId(_instanceId, _shotSequence, (uint)pellet);
            var request = new WeaponShotRequest(
                id,
                _ammo.MuzzleVelocity,
                _ammo.ProjectileMassKilograms,
                _ammo.ProjectileRadius,
                _ammo.Drag,
                _ammo.GravityScale,
                _ammo.MaximumLifetime,
                _ammo.BaseDamage,
                _ammo.PenetrationEnergy,
                _ammo.CollisionMask,
                MixSeed(deterministicSeed, id));
            _sink.OnShot(request);
        }

        _nextShotDelay = _weapon.SecondsPerShot;
        Transition(WeaponRuntimeState.Firing);
    }

    private bool TryConsumeChamberedRound()
    {
        if (_chamberedRounds > 0)
        {
            --_chamberedRounds;
            return true;
        }

        if (!_weapon.ClosedBolt)
        {
            if (_weapon.FeedType == WeaponFeedType.DetachableMagazine)
                return _insertedMagazine?.TryTakeRound() == true;
            if (_tubeRounds > 0)
            {
                --_tubeRounds;
                return true;
            }
        }

        return false;
    }

    private void BeginReload()
    {
        if (_state != WeaponRuntimeState.Ready)
            return;

        _reloadInterrupted = false;
        if (_weapon.FeedType == WeaponFeedType.InternalTube)
        {
            if (_tubeRounds >= _weapon.InternalCapacity ||
                _inventory.CountLooseRounds(_weapon.AmmoCompatibilityId) == 0)
            {
                return;
            }

            _reloadKind = WeaponReloadKind.PerShell;
        }
        else
        {
            PhysicalMagazine? replacement = _inventory.TakeBestMagazine(_weapon.MagazineCompatibilityId);
            if (replacement is null)
                return;

            _pendingMagazine = replacement;
            _reloadKind = _chamberedRounds > 0 ? WeaponReloadKind.Tactical : WeaponReloadKind.Empty;
        }

        Transition(WeaponRuntimeState.ReloadStarting);
    }

    private void AdvanceTimedState()
    {
        switch (_state)
        {
            case WeaponRuntimeState.Equipping when Elapsed(_weapon.EquipDuration):
                Transition(WeaponRuntimeState.Ready);
                break;
            case WeaponRuntimeState.Unequipping when Elapsed(_weapon.UnequipDuration):
                Transition(WeaponRuntimeState.Holstered);
                break;
            case WeaponRuntimeState.Firing when Elapsed(_weapon.SecondsPerShot):
                if (_fireMode == WeaponFireMode.PumpAction)
                    Transition(WeaponRuntimeState.Cycling);
                else
                {
                    FeedChamber();
                    Transition(WeaponRuntimeState.Ready);
                }
                break;
            case WeaponRuntimeState.Cycling when Elapsed(_weapon.CycleDuration):
                FeedChamber();
                Transition(WeaponRuntimeState.Ready);
                break;
            case WeaponRuntimeState.DryFire when Elapsed(_weapon.DryFireDuration):
                Transition(WeaponRuntimeState.Ready);
                break;
            case WeaponRuntimeState.ReloadStarting when Elapsed(_weapon.ReloadStartDuration):
                Transition(_weapon.FeedType == WeaponFeedType.DetachableMagazine
                    ? WeaponRuntimeState.RemovingMagazine
                    : WeaponRuntimeState.InsertingShell);
                break;
            case WeaponRuntimeState.RemovingMagazine when Elapsed(_weapon.RemoveMagazineDuration):
                RemoveCurrentMagazine();
                Transition(WeaponRuntimeState.InsertingMagazine);
                break;
            case WeaponRuntimeState.InsertingMagazine when Elapsed(_weapon.InsertMagazineDuration):
                InsertReplacementMagazine();
                Transition(_chamberedRounds == 0
                    ? WeaponRuntimeState.Chambering
                    : WeaponRuntimeState.ReloadFinishing);
                break;
            case WeaponRuntimeState.InsertingShell when Elapsed(_weapon.ShellInsertDuration):
                InsertShell();
                if (_reloadInterrupted ||
                    _tubeRounds >= _weapon.InternalCapacity ||
                    _inventory.CountLooseRounds(_weapon.AmmoCompatibilityId) == 0)
                {
                    Transition(_chamberedRounds == 0
                        ? WeaponRuntimeState.Chambering
                        : WeaponRuntimeState.ReloadFinishing);
                }
                else
                {
                    Transition(WeaponRuntimeState.InsertingShell);
                }
                break;
            case WeaponRuntimeState.Chambering when Elapsed(_weapon.ChamberDuration):
                FeedChamber();
                Transition(WeaponRuntimeState.ReloadFinishing);
                break;
            case WeaponRuntimeState.ReloadFinishing when Elapsed(_weapon.ReloadFinishDuration):
                _reloadKind = WeaponReloadKind.None;
                Transition(WeaponRuntimeState.Ready);
                break;
        }
    }

    private void RemoveCurrentMagazine()
    {
        if (_insertedMagazine is null)
            return;

        PhysicalMagazine removed = _insertedMagazine;
        _insertedMagazine = null;
        if (_weapon.RetainPartialMagazines && !removed.IsEmpty)
            _inventory.AddMagazine(removed);
        _sink.OnMagazineRemoved(removed);
    }

    private void InsertReplacementMagazine()
    {
        if (_pendingMagazine is null)
            return;

        _insertedMagazine = _pendingMagazine;
        _pendingMagazine = null;
        _sink.OnMagazineInserted(_insertedMagazine);
    }

    private void CancelReload()
    {
        if (_pendingMagazine is not null)
        {
            _inventory.AddMagazine(_pendingMagazine);
            _pendingMagazine = null;
        }

        _reloadKind = WeaponReloadKind.None;
        _reloadInterrupted = false;
    }

    private void InsertShell()
    {
        if (_tubeRounds >= _weapon.InternalCapacity ||
            !_inventory.TryTakeLooseRound(_weapon.AmmoCompatibilityId))
        {
            return;
        }

        ++_tubeRounds;
        _sink.OnShellInserted(_tubeRounds);
    }

    private void FeedChamber()
    {
        if (_chamberedRounds > 0 || !_weapon.ClosedBolt)
            return;

        if (_weapon.FeedType == WeaponFeedType.DetachableMagazine)
        {
            if (_insertedMagazine?.TryTakeRound() == true)
                _chamberedRounds = 1;
        }
        else if (_tubeRounds > 0)
        {
            --_tubeRounds;
            _chamberedRounds = 1;
        }
    }

    private void SelectNextFireMode()
    {
        IReadOnlyList<WeaponFireMode> modes = _weapon.AvailableFireModes;
        if (modes.Count == 0)
            return;

        int current = -1;
        for (int index = 0; index < modes.Count; ++index)
        {
            if (modes[index] == _fireMode)
            {
                current = index;
                break;
            }
        }

        _fireMode = modes[(current + 1) % modes.Count];
        _burstRemaining = 0;
        PublishSnapshot();
    }

    private bool Elapsed(float duration) => _stateElapsed >= duration;

    private void Transition(WeaponRuntimeState next)
    {
        _state = next;
        _stateElapsed = 0.0f;
        PublishSnapshot();
    }

    private WeaponRuntimeSnapshot CreateSnapshot()
    {
        int magazineRounds = _weapon.FeedType == WeaponFeedType.DetachableMagazine
            ? _insertedMagazine?.Rounds ?? 0
            : _tubeRounds;
        int reserveRounds = _weapon.FeedType == WeaponFeedType.DetachableMagazine
            ? _inventory.CountMagazineRounds(_weapon.MagazineCompatibilityId)
            : _inventory.CountLooseRounds(_weapon.AmmoCompatibilityId);
        int magazineCount = _weapon.FeedType == WeaponFeedType.DetachableMagazine
            ? _inventory.CountNonEmptyMagazines(_weapon.MagazineCompatibilityId)
            : 0;
        float duration = StateDuration(_state);
        float progress = duration <= 0.0f ? 1.0f : Math.Clamp(_stateElapsed / duration, 0.0f, 1.0f);
        return new WeaponRuntimeSnapshot(
            _state,
            _fireMode,
            _reloadKind,
            magazineRounds,
            _chamberedRounds,
            reserveRounds,
            magazineCount,
            _aiming,
            progress,
            _shotSequence);
    }

    private float StateDuration(WeaponRuntimeState state) => state switch
    {
        WeaponRuntimeState.Equipping => _weapon.EquipDuration,
        WeaponRuntimeState.Unequipping => _weapon.UnequipDuration,
        WeaponRuntimeState.Firing => _weapon.SecondsPerShot,
        WeaponRuntimeState.Cycling => _weapon.CycleDuration,
        WeaponRuntimeState.DryFire => _weapon.DryFireDuration,
        WeaponRuntimeState.ReloadStarting => _weapon.ReloadStartDuration,
        WeaponRuntimeState.RemovingMagazine => _weapon.RemoveMagazineDuration,
        WeaponRuntimeState.InsertingMagazine => _weapon.InsertMagazineDuration,
        WeaponRuntimeState.Chambering => _weapon.ChamberDuration,
        WeaponRuntimeState.InsertingShell => _weapon.ShellInsertDuration,
        WeaponRuntimeState.ReloadFinishing => _weapon.ReloadFinishDuration,
        _ => 0.0f,
    };

    private void PublishSnapshot()
    {
        WeaponRuntimeSnapshot snapshot = CreateSnapshot();
        _sink.OnStateChanged(snapshot);
    }

    private static uint MixSeed(uint seed, ProductionShotId shotId)
    {
        ulong value = shotId.WeaponInstance ^ (shotId.Sequence * 0x9e3779b97f4a7c15UL) ^ shotId.Pellet ^ seed;
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9UL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebUL;
        value ^= value >> 31;
        return (uint)(value ^ (value >> 32));
    }
}

public interface IBallisticCollisionWorld
{
    bool SweepSphere(in BallisticSweepRequest request, out BallisticSweepHit hit);
}

public interface IBallisticImpactSink
{
    WeaponDamageResponse ApplyDamage(in WeaponDamagePacket damage);
    void OnImpact(in WeaponDamagePacket damage, bool penetrated, bool ricocheted);
    void OnTracer(in ProductionShotId shotId, NumericsVector3 start, NumericsVector3 end);
}

public sealed class ProductionBallisticWorld
{
    private struct Projectile
    {
        public bool Active;
        public ProductionShotId Id;
        public ulong Shooter;
        public NumericsVector3 Position;
        public NumericsVector3 Velocity;
        public float Radius;
        public float Mass;
        public float Drag;
        public float GravityScale;
        public float Lifetime;
        public float MaximumLifetime;
        public float DamageScale;
        public float Energy;
        public uint CollisionMask;
        public int RemainingInteractions;
    }

    private readonly Projectile[] _projectiles;
    private readonly IBallisticCollisionWorld _collision;
    private readonly IBallisticImpactSink _impactSink;
    private int _activeCount;
    private int _cursor;

    public ProductionBallisticWorld(
        int capacity,
        IBallisticCollisionWorld collision,
        IBallisticImpactSink impactSink)
    {
        if (capacity <= 0)
            throw new ArgumentOutOfRangeException(nameof(capacity));

        _projectiles = new Projectile[capacity];
        _collision = collision ?? throw new ArgumentNullException(nameof(collision));
        _impactSink = impactSink ?? throw new ArgumentNullException(nameof(impactSink));
    }

    public int Capacity => _projectiles.Length;
    public int ActiveCount => _activeCount;
    public int DroppedProjectiles { get; private set; }

    public bool Spawn(
        in WeaponShotRequest request,
        ulong shooter,
        NumericsVector3 origin,
        NumericsVector3 direction)
    {
        int slot = FindFreeSlot();
        if (slot < 0)
        {
            ++DroppedProjectiles;
            return false;
        }

        NumericsVector3 normalized = direction.LengthSquared() > 0.000001f
            ? NumericsVector3.Normalize(direction)
            : NumericsVector3.UnitZ;
        _projectiles[slot] = new Projectile
        {
            Active = true,
            Id = request.Id,
            Shooter = shooter,
            Position = origin,
            Velocity = normalized * request.MuzzleVelocity,
            Radius = request.ProjectileRadius,
            Mass = request.ProjectileMass,
            Drag = request.Drag,
            GravityScale = request.GravityScale,
            MaximumLifetime = request.MaximumLifetime,
            DamageScale = request.DamageScale,
            Energy = MathF.Max(
                request.PenetrationEnergy,
                0.5f * request.ProjectileMass * request.MuzzleVelocity * request.MuzzleVelocity),
            CollisionMask = request.CollisionMask,
            RemainingInteractions = 8,
        };
        ++_activeCount;
        return true;
    }

    public void Step(float fixedDeltaTime)
    {
        if (!float.IsFinite(fixedDeltaTime) || fixedDeltaTime < 0.0f)
            throw new ArgumentOutOfRangeException(nameof(fixedDeltaTime));
        if (fixedDeltaTime == 0.0f)
            return;

        float step = Math.Min(fixedDeltaTime, 0.05f);
        for (int index = 0; index < _projectiles.Length; ++index)
        {
            if (_projectiles[index].Active)
                StepProjectile(index, step);
        }
    }

    public void Clear()
    {
        Array.Clear(_projectiles);
        _activeCount = 0;
        _cursor = 0;
    }

    private void StepProjectile(int index, float fixedDeltaTime)
    {
        ref Projectile projectile = ref _projectiles[index];
        projectile.Lifetime += fixedDeltaTime;
        if (projectile.Lifetime >= projectile.MaximumLifetime ||
            projectile.RemainingInteractions <= 0 ||
            projectile.Energy <= 0.01f)
        {
            Retire(ref projectile);
            return;
        }

        projectile.Velocity += new NumericsVector3(0.0f, -9.80665f * projectile.GravityScale, 0.0f) * fixedDeltaTime;
        float dragFactor = MathF.Max(0.0f, 1.0f - projectile.Drag * projectile.Velocity.Length() * fixedDeltaTime);
        projectile.Velocity *= dragFactor;
        NumericsVector3 end = projectile.Position + projectile.Velocity * fixedDeltaTime;
        var sweep = new BallisticSweepRequest(
            projectile.Position,
            end,
            projectile.Radius,
            projectile.CollisionMask,
            projectile.Shooter);

        if (!_collision.SweepSphere(sweep, out BallisticSweepHit hit))
        {
            _impactSink.OnTracer(projectile.Id, projectile.Position, end);
            projectile.Position = end;
            return;
        }

        NumericsVector3 direction = projectile.Velocity.LengthSquared() > 0.000001f
            ? NumericsVector3.Normalize(projectile.Velocity)
            : NumericsVector3.UnitZ;
        float resistance = MathF.Max(0.01f, hit.SurfaceResistance);
        float incidence = Math.Clamp(-NumericsVector3.Dot(direction, hit.Normal), 0.0f, 1.0f);
        float damage = projectile.DamageScale * hit.DamageMultiplier *
                       Math.Clamp(projectile.Energy / MathF.Max(1.0f, resistance), 0.0f, 1.5f);
        var packet = new WeaponDamagePacket(
            projectile.Id,
            projectile.Shooter,
            hit.Entity,
            damage,
            projectile.Energy,
            hit.Point,
            hit.Normal,
            direction,
            hit.SurfaceId);
        _impactSink.ApplyDamage(packet);

        bool ricochet = incidence < 0.25f && projectile.Energy > resistance * 1.5f;
        bool penetrate = !ricochet && projectile.Energy > resistance;
        _impactSink.OnImpact(packet, penetrate, ricochet);
        --projectile.RemainingInteractions;

        if (ricochet)
        {
            projectile.Energy *= Math.Clamp(0.55f - incidence, 0.1f, 0.5f);
            projectile.Velocity = NumericsVector3.Reflect(projectile.Velocity, hit.Normal) * 0.55f;
            projectile.Position = hit.Point + hit.Normal * MathF.Max(projectile.Radius, 0.001f);
            return;
        }

        if (penetrate)
        {
            float retained = Math.Clamp(1.0f - resistance / projectile.Energy, 0.05f, 0.9f);
            projectile.Energy *= retained;
            projectile.Velocity *= MathF.Sqrt(retained);
            projectile.Position = hit.Point + direction * MathF.Max(projectile.Radius * 2.0f, 0.002f);
            return;
        }

        Retire(ref projectile);
    }

    private int FindFreeSlot()
    {
        for (int attempt = 0; attempt < _projectiles.Length; ++attempt)
        {
            int index = (_cursor + attempt) % _projectiles.Length;
            if (_projectiles[index].Active)
                continue;

            _cursor = (index + 1) % _projectiles.Length;
            return index;
        }

        return -1;
    }

    private void Retire(ref Projectile projectile)
    {
        projectile = default;
        --_activeCount;
    }
}

public sealed class WeaponFeedbackPool<T> where T : class
{
    private readonly T[] _items;
    private readonly bool[] _active;
    private readonly uint[] _generations;
    private readonly Action<T>? _onAcquire;
    private readonly Action<T>? _onRelease;

    public WeaponFeedbackPool(
        IReadOnlyList<T> items,
        Action<T>? onAcquire = null,
        Action<T>? onRelease = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items.Count == 0)
            throw new ArgumentException("A feedback pool requires at least one instance.", nameof(items));

        _items = new T[items.Count];
        _active = new bool[items.Count];
        _generations = new uint[items.Count];
        for (int index = 0; index < items.Count; ++index)
            _items[index] = items[index];
        _onAcquire = onAcquire;
        _onRelease = onRelease;
    }

    public int Capacity => _items.Length;
    public int ActiveCount { get; private set; }

    public bool TryAcquire(out WeaponFeedbackLease<T> lease)
    {
        for (int index = 0; index < _items.Length; ++index)
        {
            if (_active[index])
                continue;

            uint generation = unchecked(++_generations[index]);
            if (generation == 0)
                generation = ++_generations[index];
            _active[index] = true;
            ++ActiveCount;
            try
            {
                _onAcquire?.Invoke(_items[index]);
            }
            catch
            {
                _active[index] = false;
                --ActiveCount;
                throw;
            }

            lease = new WeaponFeedbackLease<T>(this, index, generation, _items[index]);
            return true;
        }

        lease = default;
        return false;
    }

    internal void Release(int index, uint generation)
    {
        if ((uint)index >= (uint)_items.Length || !_active[index] || _generations[index] != generation)
            return;

        _active[index] = false;
        --ActiveCount;
        _onRelease?.Invoke(_items[index]);
    }
}

public readonly struct WeaponFeedbackLease<T> : IDisposable where T : class
{
    private readonly WeaponFeedbackPool<T>? _owner;
    private readonly int _index;
    private readonly uint _generation;

    internal WeaponFeedbackLease(WeaponFeedbackPool<T> owner, int index, uint generation, T value)
    {
        _owner = owner;
        _index = index;
        _generation = generation;
        Value = value;
    }

    public T? Value { get; }
    public void Dispose() => _owner?.Release(_index, _generation);
}

public sealed class WeaponLoadout
{
    private readonly ProductionWeaponRuntime[] _weapons;
    private int _activeIndex = -1;
    private int _pendingIndex = -1;

    public WeaponLoadout(IReadOnlyList<ProductionWeaponRuntime> weapons)
    {
        ArgumentNullException.ThrowIfNull(weapons);
        if (weapons.Count == 0)
            throw new ArgumentException("A loadout requires at least one weapon.", nameof(weapons));

        _weapons = new ProductionWeaponRuntime[weapons.Count];
        for (int index = 0; index < weapons.Count; ++index)
            _weapons[index] = weapons[index];
    }

    public int Count => _weapons.Length;
    public int ActiveIndex => _activeIndex;
    public ProductionWeaponRuntime? ActiveWeapon =>
        _activeIndex >= 0 ? _weapons[_activeIndex] : null;

    public void EquipInitial(int index)
    {
        ValidateIndex(index);
        if (_activeIndex >= 0)
            throw new InvalidOperationException("The initial weapon has already been equipped.");

        _activeIndex = index;
        _weapons[index].Equip();
    }

    public void RequestSwitch(int index)
    {
        ValidateIndex(index);
        if (index == _activeIndex)
            return;

        _pendingIndex = index;
        if (_activeIndex >= 0)
            _weapons[_activeIndex].Unequip();
        else
            CompleteSwitch();
    }

    public void Tick(float deltaTime, in WeaponInputFrame input, uint deterministicSeed)
    {
        if (_activeIndex < 0)
            return;

        ProductionWeaponRuntime active = _weapons[_activeIndex];
        active.Tick(deltaTime, input, deterministicSeed);
        if (_pendingIndex >= 0 && active.Snapshot.State == WeaponRuntimeState.Holstered)
            CompleteSwitch();
    }

    private void CompleteSwitch()
    {
        _activeIndex = _pendingIndex;
        _pendingIndex = -1;
        _weapons[_activeIndex].Equip();
    }

    private void ValidateIndex(int index)
    {
        if ((uint)index >= (uint)_weapons.Length)
            throw new ArgumentOutOfRangeException(nameof(index));
    }
}

public readonly record struct WeaponHudState(
    string WeaponName,
    string FireMode,
    string ReloadState,
    int MagazineRounds,
    int ChamberedRounds,
    int ReserveRounds,
    int MagazineCount,
    bool Aiming,
    float CrosshairSpread);

public interface IWeaponHudSink
{
    void Apply(in WeaponHudState state);
}

public sealed class WeaponHudPresenter
{
    private readonly IWeaponHudSink _sink;
    private WeaponHudState _last;
    private bool _hasLast;

    public WeaponHudPresenter(IWeaponHudSink sink)
    {
        _sink = sink ?? throw new ArgumentNullException(nameof(sink));
    }

    public void Present(
        ProductionWeaponDefinition definition,
        in WeaponRuntimeSnapshot snapshot,
        float movementSpread)
    {
        float baseSpread = snapshot.Aiming ? definition.AdsSpreadDegrees : definition.HipSpreadDegrees;
        var state = new WeaponHudState(
            definition.DisplayName,
            snapshot.FireMode.ToString(),
            snapshot.ReloadKind == WeaponReloadKind.None ? string.Empty : snapshot.ReloadKind.ToString(),
            snapshot.MagazineRounds,
            snapshot.ChamberedRounds,
            snapshot.ReserveRounds,
            snapshot.MagazineCount,
            snapshot.Aiming,
            MathF.Max(0.0f, baseSpread + movementSpread));
        if (_hasLast && state == _last)
            return;

        _last = state;
        _hasLast = true;
        _sink.Apply(state);
    }
}

public readonly record struct WeaponAuthoringIssue(string Code, string Message, bool IsError);

public static class ProductionWeaponValidator
{
    public static IReadOnlyList<WeaponAuthoringIssue> Validate(
        ProductionWeaponDefinition weapon,
        ProductionAmmoDefinition ammo,
        ProductionMagazineDefinition? magazine)
    {
        ArgumentNullException.ThrowIfNull(weapon);
        ArgumentNullException.ThrowIfNull(ammo);
        var issues = new List<WeaponAuthoringIssue>(8);

        if (!StringComparer.Ordinal.Equals(weapon.AmmoCompatibilityId, ammo.CompatibilityId))
        {
            issues.Add(new WeaponAuthoringIssue(
                "KEIREWEAPON001",
                $"Weapon '{weapon.DisplayName}' expects '{weapon.AmmoCompatibilityId}' but ammo is " +
                $"'{ammo.CompatibilityId}'.",
                true));
        }

        if (weapon.FeedType == WeaponFeedType.DetachableMagazine)
        {
            if (magazine is null)
            {
                issues.Add(new WeaponAuthoringIssue(
                    "KEIREWEAPON002",
                    "A detachable-magazine weapon requires a magazine definition.",
                    true));
            }
            else
            {
                if (!StringComparer.Ordinal.Equals(
                        weapon.MagazineCompatibilityId,
                        magazine.CompatibilityId))
                {
                    issues.Add(new WeaponAuthoringIssue(
                        "KEIREWEAPON003",
                        $"Weapon expects magazine '{weapon.MagazineCompatibilityId}' but received " +
                        $"'{magazine.CompatibilityId}'.",
                        true));
                }

                if (!StringComparer.Ordinal.Equals(
                        magazine.AmmoCompatibilityId,
                        ammo.CompatibilityId))
                {
                    issues.Add(new WeaponAuthoringIssue(
                        "KEIREWEAPON004",
                        $"Magazine expects ammo '{magazine.AmmoCompatibilityId}' but received " +
                        $"'{ammo.CompatibilityId}'.",
                        true));
                }
            }
        }

        if (weapon.AvailableFireModes.Count == 0)
        {
            issues.Add(new WeaponAuthoringIssue(
                "KEIREWEAPON005",
                "At least one fire mode must be configured.",
                true));
        }

        float muzzleEnergy = 0.5f * ammo.ProjectileMassKilograms *
                             ammo.MuzzleVelocity * ammo.MuzzleVelocity;
        if (muzzleEnergy <= 0.0f)
        {
            issues.Add(new WeaponAuthoringIssue(
                "KEIREWEAPON006",
                "Muzzle energy must be greater than zero.",
                true));
        }

        if (weapon.SecondsPerShot <= 0.0f)
        {
            issues.Add(new WeaponAuthoringIssue(
                "KEIREWEAPON007",
                "Fire interval must be greater than zero.",
                true));
        }

        return issues;
    }
}
