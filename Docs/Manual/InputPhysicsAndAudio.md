# Input, Physics, And Audio

Input is polled during variable updates, physics advances on fixed simulation ticks, and audio is controlled through
scene components plus asset references. Author the data first, then keep frame and fixed-step responsibilities clear.

## Input Actions

Create an Input Action asset in the Project panel and open it in the Input Actions editor. Organize actions into maps,
add bindings and interactions, then validate the asset. For script-owned controls, expose an `InputActionAsset` field,
assign the asset in the Inspector, and create/dispose a private context with the controller. Project Settings assignment
does not assign a script's serialized asset field. Use the Input Debugger and Listen mode to confirm bindings first.

```csharp
Vector2 move = moveAction?.ReadValue<Vector2>() ?? Vector2.Zero;
bool jumpPressed = jumpAction?.WasPressedThisFrame == true;
bool fireReleased = fireAction?.WasReleasedThisFrame == true;
bool sprintHeld = sprintAction?.IsPressed == true;
```

Here `moveAction`, `jumpAction`, `fireAction`, and `sprintAction` are enabled `InputAction` references resolved from the
owning context. `WasPressedThisFrame` and `WasReleasedThisFrame` are frame edges; `IsPressed` reads the current button
state. `ReadValue<float>()` and `ReadValue<Vector2>()` read scalar and two-dimensional actions. Read one-shot input in
`Update`, retain intent, then consume it once in `FixedUpdate` when it drives physics. Several fixed ticks can share
the same frame snapshot, so polling a press on each tick can repeat a jump or shot.

Follow the complete [ActionMotor recipe](../Scripting/Cookbook.md#fixed-step-action-movement), including its
`Player/Move` action, Inspector assignment, context disposal, and reload handling. String helpers such as
`Input.Axis2D("Move")`, `Input.Pressed`, and `Input.Held` are obsolete compatibility APIs.

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

### Build A Mixer Without Changing Scene State

1. Create or open a `.keiremixer` asset from the Project panel. In **Routing**, select Master and use **+ Bus** to add
   a child channel. Give channels purpose-based names such as `Music`, `SFX`, and `Dialogue`.
2. Select a channel, then use **Channel Inspector** to set its output and effect rack. Effects process in the listed
   order; **Move Up** and **Move Down** change that order. Use the named parameter controls rather than guessing a
   numeric parameter index.
3. In **Mix Console**, adjust volume in dB and check peak meters. `0 dB` preserves the original level; positive values
   boost it. A clipping warning means lower this channel or its upstream sources. Mute is separate from the fader.
4. For a shared reverb return, use **Create Reverb Return**. It adds a wet reverb effect and sends from existing child
   buses. If you create source buses afterward, add their sends explicitly in Channel Inspector. Do not route a return
   back into its own upstream path: cyclic routing is rejected.
5. Choose **Validate**, then **Save**. Live authoring preview is transient; it is not a substitute for saving the mixer
   asset, assigning it to the source's routing, or configuring the project's Default Mixer.

### Author Snapshots And Ducking

In **Snapshots**, use **+ Snapshot**, give it a descriptive name, and add a parameter with **+ Bus Gain**. Each row's
**Type** selects bus gain/mute/solo, send gain, effect bypass, or an effect parameter; **Target** identifies the bus,
send, or effect. Create the required send or effect first. Effect parameter indices are zero-based and depend on the
effect type. A snapshot is a set of overrides, not an independent mixer or a new bus hierarchy.

To compare the result, set **Preview Weight** and choose **Audition Snapshot** while a source routed through this mixer
is playing. Zero uses the authored draft; one uses the full snapshot. Change the weight and audition again to compare;
this is not an automatic timed transition. **Restore Authored Mix** restores the draft, including unsaved edits.
Audition never changes the saved asset or adds an undo command. Saving writes your authored values, not the audition
blend. Successful content edits and Undo/Redo also restore authored preview state.

Closing the mixer restores its saved source, not an unsaved preview. If that file becomes unreadable, a retained saved
mix and its decoded impulse responses provide the fallback. If restoration reports a failure, reopen and close the
mixer to retry. Missing impulse responses are reported before a new preview is published; if saving cannot prepare the
saved baseline, resolve that resource error and save again. Hardware/device failures still require separate diagnosis.

In **Ducking**, use **+ Rule** after creating at least two buses. Choose the signal-detecting **Sidechain** and the
channel to attenuate as **Target**. Set threshold and ratio, then attack, hold, and release to control the response.
These time controls are in seconds. Keep the sidechain and target distinct, and check the result with actual audio
rather than judging the rule from its values alone.

Mixer edits participate in the document's **Undo**/**Redo** history. **Discard** restores the saved baseline;
**Reload** refuses to replace unsaved local changes. Back up source assets before a migration. Device hot-plug,
platform latency, and long-running underrun behavior still need hardware acceptance; a successful asset save does not
prove those runtime properties. See [Audio Production](../AudioProduction.md) for the remaining acceptance work.

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
