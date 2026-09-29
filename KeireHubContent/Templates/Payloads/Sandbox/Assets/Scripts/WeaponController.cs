using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000040")]
[ExecutionOrder(20)]
public sealed class WeaponController : Behaviour
{
    [SerializeField, StableFieldId("73616e64-626f-4078-8000-000000000041")]
    [Tooltip("Input Actions asset used by the Sandbox weapons.")]
    private InputActionAsset? _inputActions;

    private readonly List<WeaponRuntime> _weapons = [];
    private readonly WeaponPresentationRig _presentation = new();
    private BallisticWorld? _ballistics;
    private int _activeWeapon;
    private float _elapsed;
    private InputActionContext? _inputContext;
    private InputAction? _aimAction;
    private InputAction? _fireAction;
    private InputAction? _fireModeAction;
    private InputAction? _moveAction;
    private InputAction? _nextWeaponAction;
    private InputAction? _previousWeaponAction;
    private InputAction? _reloadAction;
    private InputAction? _weapon1Action;
    private InputAction? _weapon2Action;
    private InputAction? _weapon3Action;

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

    protected override void OnEnable() => EnableInput();

    protected override void OnDisable() => DisableInput();

    protected override void OnBeforeReload() => DisableInput();

    protected override void OnAfterReload() => EnableInput();

    protected override void OnDestroy()
    {
        DisableInput();
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

        if (_nextWeaponAction?.WasPressedThisFrame == true)
            Switch(1);
        if (_previousWeaponAction?.WasPressedThisFrame == true)
            Switch(-1);
        if (_weapon1Action?.WasPressedThisFrame == true)
            Select(0);
        if (_weapon2Action?.WasPressedThisFrame == true)
            Select(1);
        if (_weapon3Action?.WasPressedThisFrame == true)
            Select(2);

        var command = new WeaponCommandFrame(
            _fireAction?.IsPressed == true,
            _fireAction?.WasPressedThisFrame == true,
            _reloadAction?.WasPressedThisFrame == true,
            _aimAction?.IsPressed == true,
            _fireModeAction?.WasPressedThisFrame == true);
        Vector3 origin = Entity.Transform.Position;
        Vector3 forward = Entity.Transform.Forward;
        Current.Tick(deltaTime, command, Entity, origin, forward);
        _ballistics?.Step(deltaTime);
        _presentation.Step(
            Current.Definition.Recoil, deltaTime, _moveAction?.ReadValue<Vector2>() ?? default, _elapsed);
        CameraRecoil = _presentation.Rotation;
        Hud.Apply(Current.Definition.DisplayName, Current.Snapshot);
    }

    private WeaponRuntime Current => _weapons[_activeWeapon];

    private void EnableInput()
    {
        DisableInput();
        if (_inputActions is not { IsValid: true })
            throw new InvalidOperationException($"{nameof(WeaponController)} requires an Input Actions asset.");

        _inputContext = _inputActions.CreateContext();
        _aimAction = _inputContext.FindAction("Player/Aim");
        _fireAction = _inputContext.FindAction("Player/Fire");
        _fireModeAction = _inputContext.FindAction("Player/FireMode");
        _moveAction = _inputContext.FindAction("Player/Move");
        _nextWeaponAction = _inputContext.FindAction("Player/NextWeapon");
        _previousWeaponAction = _inputContext.FindAction("Player/PreviousWeapon");
        _reloadAction = _inputContext.FindAction("Player/Reload");
        _weapon1Action = _inputContext.FindAction("Player/Weapon1");
        _weapon2Action = _inputContext.FindAction("Player/Weapon2");
        _weapon3Action = _inputContext.FindAction("Player/Weapon3");
        _inputContext.Enable();
    }

    private void DisableInput()
    {
        _inputContext?.Dispose();
        _inputContext = null;
        _aimAction = null;
        _fireAction = null;
        _fireModeAction = null;
        _moveAction = null;
        _nextWeaponAction = null;
        _previousWeaponAction = null;
        _reloadAction = null;
        _weapon1Action = null;
        _weapon2Action = null;
        _weapon3Action = null;
    }

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
