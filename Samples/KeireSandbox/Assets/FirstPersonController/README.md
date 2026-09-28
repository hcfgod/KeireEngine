# First Person Controller

Import this package, approve its C# assembly, then drag **FirstPersonPlayer.keireprefab** into your scene.
Place the capsule above a floor with a collider, disable any other primary camera and audio listener, and press Play.
The prefab includes its camera, audio listener, capsule Character Controller, script, and input asset. No code is required.
Keep the player upright with unit scale. The capsule centre starts one metre above the floor; the camera sits 0.65 m above it.

- WASD / left stick: move. Diagonal movement is normalized.
- Mouse / right stick: look. Mouse sensitivity and stick turn rate are independent.
- Moving the mouse upward looks up by default; enable Invert Y to reverse vertical look.
- Space / gamepad south button: jump when grounded.
- Left Shift / left stick press: sprint.
- Escape / gamepad Start: release or recapture the cursor.

Select the camera child to edit walking speed, sprint multiplier, jump height, gravity, sensitivity, and invert Y.
Edit FirstPersonInput to rebind controls. UI can request cursor visibility with the engine Cursor API; this suspends player input.
Movement uses the engine collision controller and fixed simulation time. Camera pitch is limited to avoid flipping.
Only use one active player with this input context at a time. This starter does not include networking, weapons, crouching, or animations.

## Tuning and extending

The camera Inspector also exposes acceleration, braking, air control, terminal falling speed, pitch limits,
coyote time (how long a jump is allowed after leaving an edge), and jump buffering (how early you can press jump before landing).
Set the two jump timing values to zero for strict grounded jumping. All distances are metres and time values are seconds.
Acceleration and braking use metres per second squared; air control scales both while airborne.

The package separates four responsibilities:

- `FirstPersonInput`: input actions and the replaceable `IFirstPersonInputSource` interface.
- `FirstPersonMotor`: deterministic movement policy with explicit inputs, settings, and capture/restore state.
- `FirstPersonLook`: mouse delta and time-based gamepad look, inversion, and pitch limits.
- `FirstPersonController`: scene integration, native collision movement, cursor ownership, and gameplay events.

Use `MovementSettings` and `LookSettings` to change tuning at runtime. Their setters validate the complete settings
before applying them. `ControlsEnabled = false` stops horizontal input and clears buffered jumps while gravity continues.
Call `ResetMotion()` after teleporting or respawning. Supply `InputSource` before enabling the component to use replay,
AI, touch, or another input system; the caller retains ownership and must dispose its source when appropriate.
Input sources return one frame of movement, mouse delta, stick look, and edge-triggered jump/cursor requests.
The default input adapter reports which required `Player/…` action is missing if the asset is misconfigured.

Subscribe to `Jumped`, `Landed(float impactSpeed)`, `GroundedChanged(bool)`, `SprintChanged(bool)`,
`CursorCaptureChanged(bool)`, or `MotionUpdated(FirstPersonMotion)` for audio, animation, camera effects, and HUD updates.
Unsubscribe when your listener is disabled or destroyed. Landing is emitted once per contact transition; a buffered
jump on landing emits `Landed` followed by `Jumped` in the same simulation tick.
`Motion.Velocity` is the requested movement velocity; `Velocity` reads the native controller's collision-resolved velocity.
Capture/restore state supports local simulation continuity; network prediction, reconciliation, and transport remain game-specific.

For example, a behaviour with a reference to the camera's controller can adjust tuning and handle impact audio:

```csharp
private void Configure(FirstPersonController controller)
{
    controller.MovementSettings = controller.MovementSettings with { WalkSpeed = 6, AirControl = 0.5f };
    controller.Landed += OnLanded;
}

private void OnLanded(float impactSpeed)
{
    // Select a sound or camera response appropriate to the game's impact speed.
}
// Pair the subscription with controller.Landed -= OnLanded when the listener is disabled.
```

The example uses `Keire.FirstPerson`. Prefer composition through these events and interfaces over editing core movement
for every weapon, HUD, or audio feature. This is a configurable FPS foundation, not a complete competitive-shooter framework.

## Updating

Keep the .keiremeta files and stable component/field IDs when customizing the package; they preserve references and serialized settings.
Package Manager tracks your imported files. Review updates to keep local edits or replace conflicting files deliberately.
Version 0.4.5 fixes missing serialized movement defaults in the 0.4.4 prefab. Updating the package does not overwrite
settings already saved into your scene. For an existing 0.4.4 player, set Walk Speed to 5, Sprint Multiplier to 1.6,
Jump Height to 1.2, Gravity to 24, Mouse Sensitivity to 0.12, and Gamepad Look Speed to 150 on the camera child,
or replace the old player instance with the updated prefab. Keep any values you intentionally customized.
The package records the engine release it was built against; check Package Manager compatibility notices after an engine upgrade.
Existing scenes retain their original speed and look settings. A versioned migration supplies defaults for the new movement
fields when loading older controller data; newly instantiated prefabs serialize every tuning value explicitly.

### Camera and body timing

Horizontal look rotates the player body, so walking follows its heading. Vertical look rotates only the child camera,
with configurable pitch limits. Keep the character body upright and attach camera effects beneath it. Look runs each
render frame; the engine interpolates the collision-resolved body position without delaying its latest rotation.
Do not add a second position interpolator to this camera. `FixedUpdate` remains responsible for movement and collisions.
