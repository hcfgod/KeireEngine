# Weapon Authoring

Kéire's production weapon runtime separates authored data, physical inventory state, deterministic simulation, and
presentation. Gameplay code should not keep an independently editable reserve-ammunition or magazine-count field.
Those values are derived from physical inventory contents.

## Definitions

Create one `ProductionAmmoDefinition` for each cartridge or shell. Its compatibility ID is the durable link used by
weapons, magazines, pickups, and cook validation. Projectile mass is authored in grams and converted to kilograms at
runtime. Muzzle velocity, drag, gravity, radius, damage, penetration energy, collision mask, lifetime, and pellet count
define the ballistic request.

Create one `ProductionMagazineDefinition` for each detachable magazine. Its magazine compatibility ID identifies the
weapons that accept it. Its ammunition compatibility ID identifies the cartridge it contains.

Create one `ProductionWeaponDefinition` for each weapon. Select detachable-magazine or internal-tube feed, configure
the supported fire modes, and author state durations that match the presentation animation. The runtime supports a
closed-bolt chamber, capacity-plus-one behavior, tactical retention, empty reloads, per-shell reloads, interruption,
semi-automatic, automatic, burst, and pump-action workflows.

`ProductionRecoilDefinition` configures deterministic viewmodel and camera impulses. Camera recoil is only a share of
the total impulse; the rest drives the physical viewmodel spring.

## Runtime Ownership

Each equipped weapon owns a `ProductionWeaponRuntime`. The loadout owns weapon instances and serializes switching.
`PhysicalAmmunitionInventory` owns magazine objects and loose shells. A magazine has a stable item ID and retains its
remaining rounds when removed. `ReserveRounds` and `MagazineCount` are derived from compatible inventory objects.
Detachable reloads reserve the selected replacement magazine transactionally. Interrupting a reload by unequipping
returns any replacement that has not been inserted, including after the old magazine has already been removed.

Call `Tick` once per simulation update with a `WeaponInputFrame`. Feed `ProductionShotId` and the provided spread seed
into deterministic direction generation. Never substitute frame time or a process-global random generator.

Ballistic projectiles live in a fixed-capacity `ProductionBallisticWorld`. Collision implementations must sweep a
sphere from the old position to the new position and ignore the shooter's hierarchy. The world bounds interaction
iterations, projectile lifetime, and capacity so malformed content cannot create unbounded collision loops. `Step`
accepts finite, non-negative fixed time only; a zero-time step is an explicit no-op.

## Presentation and HUD

`ProductionWeaponPresentation` evaluates recoil, ADS blending, sway, movement bob, breathing, and sprint offsets. Apply
its position and rotation offsets to the viewmodel root and its camera recoil to the gameplay camera after normal look
input. Use the field-of-view multiplier for ADS rather than changing the authored base field of view.

`WeaponHudPresenter` only pushes changed state through `IWeaponHudSink`. Implement `IWeaponRuntimeHud` with the retained
runtime UI system and connect it through `ProductionWeaponHudAdapter`. HUD rendering must not use editor ImGui.

Effects and audio are emitted through a bounded `WeaponFeedbackCommandBuffer`. Consumers acquire muzzle flashes,
tracers, impacts, casings, and one-shot voices from pools. Commands may be dropped under pressure; gameplay simulation
must never block or allocate to wait for presentation capacity. Pool activation is transactional when consumer
callbacks fail, and leases are generation-bound so stale copies cannot release a newer acquisition.

## Validation

Run:

```powershell
./Scripts/Tests/test-managed-weapons.ps1
```

On Unix:

```bash
bash Scripts/Tests/test-managed-weapons.sh
```

`ProductionWeaponValidator` verifies ammunition and magazine compatibility, fire-mode configuration, fire interval,
and muzzle energy. Cooked builds should reject error diagnostics and may publish warnings for physically unusual but
valid values.
