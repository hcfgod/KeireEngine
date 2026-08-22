# Animation From C#

Managed animation controls Animator Controller states, parameters, layers, playback state, events, procedural
locomotion intent/state, and named IK goals. Scripts refer to engine-owned assets and scene components; they do not own
skeletons, clips, motion profiles, or native animator instances.

## Animation Graph Prerequisites

Before scripting playback:

1. Import or generate a rigged model and animation clips.
2. Create an Animator Controller.
3. Add states, parameters, layers, transitions, masks, blend trees, and events.
4. Assign the controller and compatible skinned mesh to an Animator component.
5. Reference state, parameter, layer, event, goal, and bone names exactly in C#.

See [Animation And Rigging](../AnimationRigging.md) for import, retargeting, controller authoring, preview, and cooking.
For a complete pose generated without clips or a controller, assign the Animator's `ProceduralHumanoid` pose source,
Rig Definition, skeleton, skinned mesh, and `.keiremotionprofile`; see
[Procedural Humanoid Motion](../ProceduralMotion.md).

## Animation Asset References

These typed fields are serializable:

```csharp
[SerializeField, StableFieldId("5eb02211-195c-45ee-b651-082a9bd3830a")]
private AnimationClip? _reloadClip;

[SerializeField, StableFieldId("d4fb6451-fbee-4483-8314-f787c8c693f0")]
private AnimatorController? _controller;
```

They support authoring and dependency references. Runtime `Play` selects a named state in the controller; it does not
take an `AnimationClip` reference directly. This keeps layers, transitions, events, blend trees, and root motion
coherent.

## Animator Component

```csharp
Animator? animator = GetComponent<Animator>();
if (!animator.IsValid)
{
    Debug.Warn($"{Entity.Name} has no Animator component.");
    return;
}
```

Stateful playback:

```csharp
animator.Speed = 1.25f;
animator.Play("Locomotion", normalizedTime: 0.0f);
animator.CrossFade("Jump", duration: 0.15f);
animator.Pause();
animator.Resume();
animator.Stop();
```

Inspect state:

```csharp
AnimatorStateInfo state = animator.StateInfo;
Debug.Log(
    $"{state.State} at {state.NormalizedTime:P0}; " +
    $"playing={state.IsPlaying}, paused={state.IsPaused}, speed={state.Speed:0.00}");
```

The handle also exposes `CurrentState`, `NormalizedTime`, `IsPlaying`, `IsPaused`, and `Speed`.

`Stop` evaluates the skeleton bind pose until another state is played.

## Playback Arguments

```csharp
Animator.Play(Entity, "UpperBody.Reload", normalizedTime: 0.25f, layer: "UpperBody");
Animator.CrossFade(Entity, "Locomotion.Run", duration: 0.2f, normalizedTime: 0.0f, layer: "Base");
```

Contracts:

- the entity must be valid and have a usable Animator;
- state names must contain 1–256 non-whitespace characters;
- normalized start time must be finite and between `0.0` and `1.0`;
- cross-fade duration must be finite and between `0.0` and `60.0` seconds;
- speed must be finite and between `0.0` and `8.0`;
- an omitted or `null` layer selects the controller's default resolution.

Invalid values throw before native playback. Unknown states and layers are rejected by the animation boundary.

## Parameters

Write controller parameters:

```csharp
Animator.SetFloat(Entity, "Speed", velocity.Length);
Animator.SetInteger(Entity, "WeaponIndex", weaponIndex);
Animator.SetBool(Entity, "Grounded", grounded);
Animator.SetTrigger(Entity, "Jump");
Animator.ResetTrigger(Entity, "Jump");
```

Read parameters:

```csharp
float speed = Animator.GetFloat(Entity, "Speed");
int weaponIndex = Animator.GetInteger(Entity, "WeaponIndex");
bool grounded = Animator.GetBool(Entity, "Grounded");
```

`Get*` throws when a parameter is unavailable. Use `TryGet*` when absence is expected:

```csharp
if (Animator.TryGetFloat(Entity, "Speed", out float speed))
    UpdateSpeedDisplay(speed);
```

Parameter names and types must match the controller. Prefer constants in gameplay code that accesses the same names
from several places:

```csharp
private const string SpeedParameter = "Speed";
```

## Layers

```csharp
Animator.SetLayerWeight(Entity, "UpperBody", 0.75f);

if (Animator.TryGetLayerWeight(Entity, "UpperBody", out float weight))
    Debug.Log($"Upper-body weight: {weight:0.00}");
```

`GetLayerWeight` throws for an unavailable layer; `TryGetLayerWeight` reports absence without throwing.

## Animation Events

Override `OnAnimationEvent`:

```csharp
protected override void OnAnimationEvent(AnimationEvent animationEvent)
{
    switch (animationEvent.Name)
    {
        case "Footstep":
            PlayFootstep(animationEvent.Scalar);
            break;
        case "HitWindowOpen":
            _canDealDamage = true;
            break;
        case "HitWindowClose":
            _canDealDamage = false;
            break;
    }
}
```

The payload contains:

| Property | Meaning |
| --- | --- |
| `Name` | Authored event name |
| `NormalizedTime` | Event location within playback |
| `Integer` | Authored integer payload |
| `Scalar` | Authored floating-point payload |
| `Text` | Authored text payload |

Use the payload that fits the event instead of encoding several values into its name.

## Procedural Humanoid Locomotion

Submit procedural intent from `FixedUpdate` after gameplay has resolved the desired direction and jump request. The
engine combines that intent with the Character Controller's actual post-physics velocity, grounding, surface normal,
and support motion:

```csharp
protected override void FixedUpdate()
{
    Vector2 move = Input.Axis2D("Move");
    Vector3 desiredVelocity =
        (Entity.Transform.Right * move.X + Entity.Transform.Forward * move.Y) * _moveSpeed;

    Animator.SetProceduralLocomotion(
        Entity,
        new ProceduralLocomotionIntent(
            desiredVelocity,
            Entity.Transform.Forward,
            Vector3.Zero,
            _crouching ? 1.0f : 0.0f,
            _sprinting ? 1.0f : 0.0f,
            Input.Pressed("Jump")));
}
```

`CrouchAmount` and `RunBlend` must be finite values in `0..1`; all direction and velocity values must be finite.
`JumpRequested` is a one-tick request. Zero facing or look vectors ask the runtime to use the resolved character root.
Read the actual resolved state without taking ownership of native pose data:

```csharp
ProceduralLocomotionState state = Animator.GetProceduralState(Entity);
if (state.State == ProceduralMotionState.Landing)
    Debug.Log($"Landing intensity: {state.LandingIntensity:0.00}");
```

Override the typed callback for `FootLift`, `FootPlant`, `Takeoff`, `Apex`, `Land`, and `StateChanged` events:

```csharp
protected override void OnProceduralMotionEvent(ProceduralMotionEvent motionEvent)
{
    if (motionEvent.Type == ProceduralMotionEventType.FootPlant)
        PlayFootstep(motionEvent.Intensity);
}
```

The event also reports foot side, phase, state, contact position/normal, support entity, and physics material. A
procedural foot plant continues to emit the legacy `Footstep` animation event for compatibility.

## Two-Bone IK

Submit a named two-bone goal:

```csharp
Animator.SetTwoBoneIK(
    Entity,
    goal: "LeftHand",
    rootBone: "LeftUpperArm",
    middleBone: "LeftLowerArm",
    endBone: "LeftHand",
    target: handTarget,
    pole: elbowPole,
    weight: 1.0f,
    space: AnimatorIkSpace.World);
```

## FABRIK IK

```csharp
Animator.SetFabrikIK(
    Entity,
    goal: "SpineAim",
    bones: new[] { "Pelvis", "Spine", "Chest", "Neck", "Head" },
    target: lookTarget,
    weight: 0.75f,
    maximumIterations: 12,
    tolerance: 0.001f,
    space: AnimatorIkSpace.World);
```

Goals persist by name until replaced or cleared:

```csharp
Animator.ClearIK(Entity, "LeftHand");
```

`AnimatorIkSpace.Model` interprets positions in model space. `World` converts world-space targets at the animation
boundary. Bone names must exist in the active skeleton, and solver limits must be valid.

`OnAnimatorIk(AnimationIkContext context)` runs after pose sampling and immediately before named IK goals are solved.
Submit frame-specific goals there:

```csharp
protected override void OnAnimatorIk(AnimationIkContext context)
{
    Animator.SetTwoBoneIK(
        Entity,
        "LookHand",
        "UpperArm.R",
        "LowerArm.R",
        "Hand.R",
        _handTarget,
        _elbowPole,
        context.LayerWeight);
}
```

Clear goals on disable when they should not remain active:

```csharp
protected override void OnDisable()
{
    if (Entity.IsValid)
        Animator.ClearIK(Entity, "LookHand");
}
```

## Runtime Foot Grounding Weight

Automatic Ground Adaptation belongs to the `AnimationGraph` pose source. Gameplay can blend its influence without
changing or serializing those authored settings:

```csharp
float groundingWeight = grounded && !jumping ? 1.0f : 0.0f;
Animator.SetFootGroundingWeight(Entity, groundingWeight);
```

The runtime value is a `0..1` multiplier over the Animator's authored foot-position, foot-rotation, and pelvis
grounding weights. Zero clears transient foot locks and restores the sampled animation, which prevents terrain IK from
pulling an airborne character back toward the surface. The multiplier returns to one when runtime pose state is
cleared and is never written into the scene or prefab. Procedural mode owns its leg contacts internally and ignores the
legacy automatic grounding pass rather than applying both solvers.

## Complete Animation-Graph Controller Pattern

```csharp
using Keire;

namespace MyGame;

[StableComponentId("883121b1-9281-4271-bc11-a43c54336baa")]
public sealed class CharacterAnimation : Behaviour
{
    private const string SpeedParameter = "Speed";
    private const string GroundedParameter = "Grounded";
    private const string JumpTrigger = "Jump";

    [SerializeField, StableFieldId("79ed4997-9e73-4154-ad29-1a0ca35163da")]
    private Entity _movementSource;

    protected override void Update()
    {
        if (!Entity.Animator.IsValid)
            return;

        Entity source = _movementSource.IsValid ? _movementSource : Entity;
        float speed = Input.Axis2D("Move").Length;

        Animator.SetFloat(Entity, SpeedParameter, speed);
        Animator.SetBool(Entity, GroundedParameter, IsGrounded(source));

        if (Input.Pressed("Jump"))
            Animator.SetTrigger(Entity, JumpTrigger);
    }

    private static bool IsGrounded(Entity source)
    {
        Vector3 origin = source.Transform.Position;
        return Physics.TryRaycast(
            source,
            origin,
            -Vector3.Up,
            out _,
            maximumDistance: 0.2f,
            ignoredEntity: source);
    }
}
```

Playback and parameter commands are ordered together at the scene animation boundary. Keep controller names stable and
use `TryGet` reads when optional controller variants may omit a parameter or layer.
