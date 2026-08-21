# Game-Owned Weapon Example

Weapons are gameplay, not an engine subsystem. `Keire.Managed` intentionally does not define weapon, ammunition,
magazine, damage, ballistics, recoil, loadout, or weapon-HUD abstractions. A game owns those concepts in its C#
assembly so it can choose its simulation, networking, inventory, and presentation rules without inheriting an engine
opinion.

## Sandbox Example

The Sandbox project includes an editable example in
`Samples/KeireSandbox/Assets/Scripts/Runtime/WeaponGameplay.cs`. It demonstrates ordinary game-owned definitions and
runtime state. `WeaponController.cs` composes that code from a `Behaviour` and uses generic Kéire services for input,
scene entities, transforms, physics queries, audio, VFX, animation, and runtime UI.

The Hub Sandbox template contains the same scripts as starter project content. They are copied into a new project and
can be changed or deleted like any other gameplay script; they are not part of `Keire.Managed` and do not expand the
engine API.

## Recommended Ownership

Keep authored tuning in game-defined `ScriptableObject` types and runtime state in game-defined C# objects or
components. Treat inventory, firing, damage, hit response, camera recoil, effects, and HUD presentation as separate
gameplay policies. Use stable asset references for content, fixed simulation time when determinism matters, bounded
pools for presentation-heavy objects, and explicit identities when shots must be reproduced across replay or a future
network layer.

Kéire supplies the reusable capabilities beneath those policies:

- Input Actions and device APIs for intent;
- scene queries, prefabs, transforms, and animation for composition;
- ray, capsule, and overlap queries for collision decisions;
- audio, VFX, materials, and runtime UI for presentation;
- profiling, logging, time, and asset APIs for production diagnostics and data.

## Validation

The managed API regression suite confirms that the generic engine surface remains usable without any built-in weapon
types:

```powershell
./Scripts/Tests/test-managed-api.ps1
```

On Unix:

```bash
bash Scripts/Tests/test-managed-api.sh
```

Game projects should add focused tests for their own weapon rules alongside their gameplay assembly.
