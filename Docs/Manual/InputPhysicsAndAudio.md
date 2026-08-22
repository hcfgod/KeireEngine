# Input, Physics, And Audio

Input is polled during variable updates, physics advances on fixed simulation ticks, and audio is controlled through
scene components plus asset references. Author the data first, then keep frame and fixed-step responsibilities clear.

## Input Actions

Create an Input Action asset in the Project panel and open it in the Input Actions editor. Organize actions into maps,
add bindings and interactions, validate the asset, then assign it in Project Settings. Use the Input Debugger and Listen
mode to confirm devices and bindings before debugging gameplay code.

```csharp
protected override void Update()
{
    Vector2 move = Input.Axis2D("Move");

    if (Input.Pressed("Jump"))
        QueueJump();
    if (Input.Released("Fire"))
        StopCharging();
    if (Input.Held("Sprint"))
        EnableSprint(move);
}
```

`Pressed` and `Released` are one-frame edges; `Held` is the current button state. `Axis` reads the X channel of an
action and `Axis2D` reads both channels. Read input in `Update`, retain intent, then consume that intent in
`FixedUpdate` when it drives physics.

Runtime device inspection, control-scheme locking, bounded gamepad rumble, interactive rebinding, and named binding-
override profiles are supported by `Input`. The Input Actions editor is the safer place to build and validate the
initial binding graph.

## Physics Queries

Physics queries require a valid context entity so Kéire can select the correct runtime world:

```csharp
private bool HasGround(Vector3 origin)
{
    return Physics.TryRaycast(
        Entity,
        origin,
        new Vector3(0.0f, -1.0f, 0.0f),
        out RaycastHit hit,
        maximumDistance: 1.2f,
        ignoredEntity: Entity) && hit.Entity.IsValid;
}
```

The direction must be finite and non-zero. Masks filter layers. `TryCapsuleCast` supports swept character queries, and
`OverlapSphere` returns bounded matching entities. Collision and trigger callbacks deliver `CollisionContact` records
to the involved Behaviours in fixed-step order.

Use Collider, Rigid Body, Character Controller, and Joint components in the Inspector for authored physics state.
Changing transforms every frame is not a substitute for fixed-step rigid-body control. Test collision layers and
trigger flags with the real Play Mode scene.

## Audio Sources And One-Shots

Import an audio clip, add an Audio Source component for persistent playback, and ensure the scene has an Audio Listener
or a camera that supplies the documented listener fallback. The managed API accepts the asset object directly:

```csharp
[SerializeField, StableFieldId("2fbf2b3f-8c43-458c-9eaf-c23099cadf0a")]
private AudioClip? _footstep = null;

private void PlayFootstep()
{
    if (_footstep is not { IsValid: true })
        return;

    Audio.Play(Entity, _footstep, new AudioPlaybackOptions
    {
        Bus = "SFX",
        Gain = 0.8f,
        Spatial = true
    });
}
```

`AudioSource` exposes clip, play-on-awake, looping, volume, pitch, spatial blend, routing, priority, distance, playback
state, seek, play, pause, resume, and stop controls. Use the Audio Mixer and Mix Console for buses, parameters,
snapshots, and typed live editing. Reverb Zone components blend eligible snapshots by priority and distance.

## Timing And Diagnostics

- `Time.DeltaTime` is scaled variable-frame time.
- `Time.UnscaledDeltaTime` continues independently of the simulation scale.
- `Time.FixedDeltaTime` is the fixed simulation interval.
- Audio and input polling belong in `Update`; simulation impulses and motors belong in `FixedUpdate`.

The Profiler exposes physics step/query counts and audio voice, virtualization, rendered-frame, and underrun health.
Use the Console and [KEIRE-AUDIO-0001](../Diagnostics/KEIRE-AUDIO-0001.md) when a streaming underrun emits that structured
diagnostic.

See [Gameplay Services](../Scripting/GameplayServices.md), [Audio](../Scripting/Audio.md), and
[Input Actions Editor](../InputActionsEditor.md) for the full contracts.
