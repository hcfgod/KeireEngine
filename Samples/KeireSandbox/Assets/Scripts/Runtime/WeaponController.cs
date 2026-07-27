using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000040")]
[ExecutionOrder(20)]
public sealed class WeaponController : Behaviour
{
    private readonly List<WeaponRuntime> _weapons = [];
    private readonly WeaponPresentationRig _presentation = new();
    private BallisticWorld? _ballistics;
    private int _activeWeapon;
    private float _elapsed;

    public static Vector3 CameraRecoil { get; private set; }
    public static WeaponHudModel Hud { get; } = new();

    protected override void Awake()
    {
        _ballistics = new BallisticWorld(new EngineBallisticCollisionWorld());
        _weapons.Add(CreateRifle());
        _weapons.Add(CreatePistol());
        _weapons.Add(CreateShotgun());
        foreach (WeaponRuntime weapon in _weapons)
        {
            weapon.Shot += OnShot;
            weapon.Changed += snapshot => Hud.Apply(weapon.Definition.DisplayName, snapshot);
        }
        Current.Equip();
        Hud.Apply(Current.Definition.DisplayName, Current.Snapshot);
    }

    protected override void OnDestroy()
    {
        foreach (WeaponRuntime weapon in _weapons)
            weapon.Shot -= OnShot;
        CameraRecoil = default;
    }

    protected override void Update()
    {
        float deltaTime = Time.DeltaTime;
        if (deltaTime <= 0.0f)
            return;
        _elapsed += deltaTime;

        if (Input.Pressed("NextWeapon"))
            Switch(1);
        if (Input.Pressed("PreviousWeapon"))
            Switch(-1);
        if (Input.Pressed("Weapon1"))
            Select(0);
        if (Input.Pressed("Weapon2"))
            Select(1);
        if (Input.Pressed("Weapon3"))
            Select(2);

        var command = new WeaponCommandFrame(
            Input.Held("Fire"),
            Input.Pressed("Fire"),
            Input.Pressed("Reload"),
            Input.Held("Aim"),
            Input.Pressed("FireMode"));
        Vector3 origin = Entity.Transform.Position;
        Vector3 forward = Entity.Transform.Forward;
        Current.Tick(deltaTime, command, Entity, origin, forward);
        _ballistics?.Step(deltaTime);
        _presentation.Step(Current.Definition.Recoil, deltaTime, Input.Axis2D("Move"), _elapsed);
        CameraRecoil = _presentation.Rotation;
        Hud.Apply(Current.Definition.DisplayName, Current.Snapshot);
    }

    private WeaponRuntime Current => _weapons[_activeWeapon];

    private void OnShot(WeaponShot shot)
    {
        if (shot.Definition.ProjectileMode == ProjectileSimulationMode.BallisticSimulation)
            _ballistics?.Spawn(Entity, shot);
        _presentation.AddShotImpulse(shot.Definition.Recoil, Current.Aiming);
    }

    private void Switch(int direction)
    {
        int next = (_activeWeapon + direction + _weapons.Count) % _weapons.Count;
        Select(next);
    }

    private void Select(int index)
    {
        if (index < 0 || index >= _weapons.Count || index == _activeWeapon)
            return;
        _activeWeapon = index;
        Current.Equip();
        Hud.Apply(Current.Definition.DisplayName, Current.Snapshot);
    }

    private static WeaponRuntime CreateRifle()
    {
        AmmunitionDefinition ammunition = ScriptableObject.CreateInstance<AmmunitionDefinition>();
        ammunition.Caliber = "5.56x45mm";
        ammunition.MassGrams = 4.0f;
        ammunition.MuzzleVelocity = 920.0f;
        ammunition.BaseDamage = 34.0f;
        MagazineDefinition magazine = ScriptableObject.CreateInstance<MagazineDefinition>();
        magazine.Caliber = ammunition.Caliber;
        magazine.Capacity = 30;
        WeaponDefinition definition = ScriptableObject.CreateInstance<WeaponDefinition>();
        definition.DisplayName = "KAR-5 Service Rifle";
        definition.Ammunition = ammunition;
        definition.Magazine = magazine;
        definition.FireModes = [WeaponFireMode.SemiAutomatic, WeaponFireMode.Automatic];
        definition.RoundsPerMinute = 750.0f;
        WeaponInventory inventory = new();
        for (int index = 0; index < 4; ++index)
            inventory.AddMagazine(new MagazineInstance(magazine));
        return new WeaponRuntime(definition, inventory);
    }

    private static WeaponRuntime CreatePistol()
    {
        AmmunitionDefinition ammunition = ScriptableObject.CreateInstance<AmmunitionDefinition>();
        ammunition.Caliber = "9x19mm";
        ammunition.MassGrams = 8.0f;
        ammunition.MuzzleVelocity = 360.0f;
        ammunition.BaseDamage = 26.0f;
        MagazineDefinition magazine = ScriptableObject.CreateInstance<MagazineDefinition>();
        magazine.Caliber = ammunition.Caliber;
        magazine.Capacity = 17;
        WeaponDefinition definition = ScriptableObject.CreateInstance<WeaponDefinition>();
        definition.DisplayName = "K9 Duty Pistol";
        definition.Ammunition = ammunition;
        definition.Magazine = magazine;
        definition.FireModes = [WeaponFireMode.SemiAutomatic];
        definition.RoundsPerMinute = 420.0f;
        WeaponInventory inventory = new();
        for (int index = 0; index < 3; ++index)
            inventory.AddMagazine(new MagazineInstance(magazine));
        return new WeaponRuntime(definition, inventory);
    }

    private static WeaponRuntime CreateShotgun()
    {
        AmmunitionDefinition ammunition = ScriptableObject.CreateInstance<AmmunitionDefinition>();
        ammunition.Caliber = "12 Gauge";
        ammunition.MassGrams = 3.5f;
        ammunition.MuzzleVelocity = 410.0f;
        ammunition.BaseDamage = 14.0f;
        WeaponDefinition definition = ScriptableObject.CreateInstance<WeaponDefinition>();
        definition.DisplayName = "K12 Pump Shotgun";
        definition.FeedType = WeaponFeedType.TubeMagazine;
        definition.Ammunition = ammunition;
        definition.FireModes = [WeaponFireMode.PumpAction];
        definition.ProjectilesPerShot = 8;
        definition.HipSpreadDegrees = 3.2f;
        definition.AimSpreadDegrees = 2.1f;
        definition.RoundsPerMinute = 85.0f;
        definition.TubeCapacity = 8;
        WeaponInventory inventory = new();
        inventory.AddLooseRounds(ammunition.Caliber, 32);
        return new WeaponRuntime(definition, inventory);
    }
}
