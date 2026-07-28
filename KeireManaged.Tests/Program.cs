using System.Numerics;
using Keire.Production.Weapons;

var tests = new (string Name, Action Run)[]
{
    ("Physical magazine and chamber", PhysicalMagazineAndChamber),
    ("Deterministic shot identity", DeterministicShotIdentity),
    ("Pickup transaction", PickupTransaction),
    ("Bounded ballistic lifecycle", BoundedBallisticLifecycle),
    ("Reload interruption returns reserved magazines", ReloadInterruptionReturnsReservedMagazine),
    ("Feedback pool acquisition is transactional", FeedbackPoolAcquisitionIsTransactional),
    ("Ballistic zero-time and invalid steps", BallisticZeroTimeAndInvalidSteps),
};

int failed = 0;
foreach ((string name, Action run) in tests)
{
    try
    {
        run();
        Console.WriteLine($"PASS {name}");
    }
    catch (Exception exception)
    {
        ++failed;
        Console.Error.WriteLine($"FAIL {name}: {exception.Message}");
    }
}

return failed == 0 ? 0 : 1;

static void PhysicalMagazineAndChamber()
{
    var weapon = new ProductionWeaponDefinition();
    var ammo = new ProductionAmmoDefinition();
    var magazineDefinition = new ProductionMagazineDefinition();
    var inventory = new PhysicalAmmunitionInventory();
    var sink = new RecordingSink();
    var inserted = new PhysicalMagazine(1, magazineDefinition, 30);
    var replacement = new PhysicalMagazine(2, magazineDefinition, 20);
    inventory.AddMagazine(replacement);
    var runtime = new ProductionWeaponRuntime(100, weapon, ammo, inventory, sink);
    runtime.SetInitialMagazine(inserted, chamberRound: true);
    Assert(runtime.Snapshot.ChamberedRounds == 1, "Closed-bolt setup must chamber one round.");
    Assert(runtime.Snapshot.MagazineRounds == 29, "Chambering must remove a round from the magazine.");

    runtime.Equip();
    Advance(runtime, 1.0f, 1);
    runtime.Tick(0.0f, new WeaponInputFrame(false, true, false, false, false, false, false), 10);
    Assert(sink.ShotCount == 1, "Semi-automatic press must emit exactly one shot.");
    Assert(runtime.Snapshot.ChamberedRounds == 0, "The fired chamber must be empty until cycling.");
    runtime.Tick(1.0f, default, 11);
    Assert(runtime.Snapshot.ChamberedRounds == 1, "Cycling must feed the next cartridge.");
    Assert(runtime.Snapshot.MagazineRounds == 28, "Cycling must consume one magazine cartridge.");
}

static void DeterministicShotIdentity()
{
    var weapon = new ProductionWeaponDefinition();
    var ammo = new ProductionAmmoDefinition();
    var magazineDefinition = new ProductionMagazineDefinition();
    var inventory = new PhysicalAmmunitionInventory();
    var sink = new RecordingSink();
    var runtime = new ProductionWeaponRuntime(0xabc, weapon, ammo, inventory, sink);
    runtime.SetInitialMagazine(new PhysicalMagazine(1, magazineDefinition, 3), chamberRound: true);
    runtime.Equip();
    Advance(runtime, 1.0f, 1);
    runtime.Tick(0.0f, new WeaponInputFrame(false, true, false, false, false, false, false), 7);
    ProductionShotId first = sink.LastShot.Id;
    Assert(first.WeaponInstance == 0xabc, "Shot identity must retain its weapon instance.");
    Assert(first.Sequence == 1, "The first shot sequence must be one.");
    Assert(first.Pellet == 0, "Single-projectile ammunition must use pellet zero.");
}

static void PickupTransaction()
{
    var inventory = new PhysicalAmmunitionInventory();
    var magazineDefinition = new ProductionMagazineDefinition();
    var magazine = new PhysicalMagazine(42, magazineDefinition, 12);
    var pickup = new WeaponPickupTransaction(
        new WeaponPickupContents("ammo.556", 18, magazine));
    Assert(pickup.TryCollect(inventory), "The first pickup collection must succeed.");
    Assert(!pickup.TryCollect(inventory), "A pickup transaction must be idempotent.");
    Assert(inventory.CountLooseRounds("ammo.556") == 18, "Loose ammunition must transfer.");
    Assert(inventory.CountNonEmptyMagazines("mag.stanag") == 1, "Magazine must transfer.");
}

static void BoundedBallisticLifecycle()
{
    var collision = new EmptyCollisionWorld();
    var impacts = new RecordingImpactSink();
    var world = new ProductionBallisticWorld(2, collision, impacts);
    var request = new WeaponShotRequest(
        new ProductionShotId(1, 1, 0),
        100.0f,
        0.004f,
        0.003f,
        0.0f,
        1.0f,
        0.01f,
        10.0f,
        10.0f,
        uint.MaxValue,
        1);
    Assert(world.Spawn(request, 9, Vector3.Zero, Vector3.UnitZ), "First projectile must spawn.");
    Assert(world.Spawn(request, 9, Vector3.Zero, Vector3.UnitZ), "Second projectile must spawn.");
    Assert(!world.Spawn(request, 9, Vector3.Zero, Vector3.UnitZ), "Capacity must be bounded.");
    Assert(world.DroppedProjectiles == 1, "Pool pressure must be observable.");
    world.Step(0.02f);
    Assert(world.ActiveCount == 0, "Expired projectiles must return to the pool.");
}

static void ReloadInterruptionReturnsReservedMagazine()
{
    var weapon = new ProductionWeaponDefinition();
    var ammo = new ProductionAmmoDefinition();
    var magazineDefinition = new ProductionMagazineDefinition();
    var inventory = new PhysicalAmmunitionInventory();
    var sink = new RecordingSink();
    var inserted = new PhysicalMagazine(1, magazineDefinition, 10);
    var replacement = new PhysicalMagazine(2, magazineDefinition, 20);
    inventory.AddMagazine(replacement);
    var runtime = new ProductionWeaponRuntime(100, weapon, ammo, inventory, sink);
    runtime.SetInitialMagazine(inserted, chamberRound: true);
    runtime.Equip();
    Advance(runtime, 1.0f, 1);

    runtime.Tick(0.0f, new WeaponInputFrame(false, false, false, false, true, false, false), 20);
    Advance(runtime, 0.8f, 21);
    Assert(runtime.Snapshot.State == WeaponRuntimeState.InsertingMagazine,
        "The reload must hold the replacement after removing the current magazine.");
    Assert(runtime.InsertedMagazine is null, "The old magazine must be removed before replacement insertion.");
    Assert(inventory.CountNonEmptyMagazines("mag.stanag") == 1,
        "Only the retained old magazine should be in inventory while the replacement is reserved.");

    runtime.Unequip();
    Assert(runtime.Snapshot.State == WeaponRuntimeState.Unequipping, "Unequip must interrupt the reload.");
    Assert(runtime.Snapshot.ReloadKind == WeaponReloadKind.None, "Interrupted reload metadata must be cleared.");
    Assert(inventory.CountNonEmptyMagazines("mag.stanag") == 2,
        "Unequip must return the reserved replacement magazine to inventory.");
    Assert(inventory.CountMagazineRounds("mag.stanag") == 29,
        "Reload interruption must preserve every round outside the chamber.");
}

static void FeedbackPoolAcquisitionIsTransactional()
{
    var item = new object();
    bool rejectAcquire = true;
    var pool = new WeaponFeedbackPool<object>(
        new[] { item },
        _ =>
        {
            if (rejectAcquire)
                throw new InvalidOperationException("activation failed");
        });

    AssertThrows<InvalidOperationException>(
        () => pool.TryAcquire(out _),
        "Activation callback failures must be observable.");
    Assert(pool.ActiveCount == 0, "A failed activation callback must release its reserved slot.");

    rejectAcquire = false;
    Assert(pool.TryAcquire(out WeaponFeedbackLease<object> first), "The rolled-back slot must remain reusable.");
    WeaponFeedbackLease<object> staleCopy = first;
    first.Dispose();
    Assert(pool.TryAcquire(out WeaponFeedbackLease<object> second), "A released slot must be reacquirable.");
    staleCopy.Dispose();
    Assert(pool.ActiveCount == 1, "A stale copied lease must not release a newer acquisition.");
    second.Dispose();
    Assert(pool.ActiveCount == 0, "The current lease must release its acquisition.");
}

static void BallisticZeroTimeAndInvalidSteps()
{
    var world = new ProductionBallisticWorld(1, new EmptyCollisionWorld(), new RecordingImpactSink());
    var request = new WeaponShotRequest(
        new ProductionShotId(1, 1, 0),
        100.0f,
        0.004f,
        0.003f,
        0.0f,
        1.0f,
        0.00005f,
        10.0f,
        10.0f,
        uint.MaxValue,
        1);
    Assert(world.Spawn(request, 9, Vector3.Zero, Vector3.UnitZ), "The test projectile must spawn.");
    world.Step(0.0f);
    Assert(world.ActiveCount == 1, "A zero-time step must not advance projectile lifetime.");
    AssertThrows<ArgumentOutOfRangeException>(
        () => world.Step(-0.01f),
        "Negative ballistic steps must be rejected.");
    AssertThrows<ArgumentOutOfRangeException>(
        () => world.Step(float.NaN),
        "Non-finite ballistic steps must be rejected.");
    world.Step(0.0001f);
    Assert(world.ActiveCount == 0, "A positive step must continue advancing projectile lifetime.");
}

static void Assert(bool condition, string message)
{
    if (!condition)
        throw new InvalidOperationException(message);
}

static void AssertThrows<TException>(Action action, string message)
    where TException : Exception
{
    try
    {
        action();
    }
    catch (TException)
    {
        return;
    }

    throw new InvalidOperationException(message);
}

static void Advance(ProductionWeaponRuntime runtime, float seconds, uint firstTick)
{
    const float step = 0.1f;
    int steps = (int)MathF.Ceiling(seconds / step);
    for (int index = 0; index < steps; ++index)
        runtime.Tick(step, default, unchecked(firstTick + (uint)index));
}

file sealed class RecordingSink : IWeaponRuntimeSink
{
    public int ShotCount { get; private set; }
    public WeaponShotRequest LastShot { get; private set; }

    public void OnStateChanged(in WeaponRuntimeSnapshot snapshot)
    {
    }

    public void OnShot(in WeaponShotRequest request)
    {
        ++ShotCount;
        LastShot = request;
    }

    public void OnDryFire()
    {
    }

    public void OnMagazineRemoved(PhysicalMagazine magazine)
    {
    }

    public void OnMagazineInserted(PhysicalMagazine magazine)
    {
    }

    public void OnShellInserted(int tubeRounds)
    {
    }
}

file sealed class EmptyCollisionWorld : IBallisticCollisionWorld
{
    public bool SweepSphere(in BallisticSweepRequest request, out BallisticSweepHit hit)
    {
        hit = default;
        return false;
    }
}

file sealed class RecordingImpactSink : IBallisticImpactSink
{
    public WeaponDamageResponse ApplyDamage(in WeaponDamagePacket damage) => default;

    public void OnImpact(in WeaponDamagePacket damage, bool penetrated, bool ricocheted)
    {
    }

    public void OnTracer(in ProductionShotId shotId, Vector3 start, Vector3 end)
    {
    }
}
