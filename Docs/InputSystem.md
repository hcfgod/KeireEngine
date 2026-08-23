# Input System

Kéire Input is an opt-in, application-owned action runtime. Enable `ApplicationSpecification::Assets` and
`ApplicationSpecification::Input`; enabling Input while Assets are disabled is rejected before window or UI work
begins. Obtain the service through `Application::Input()`. SDL devices and events remain private.

## Frame Contract

Window polling forwards every native event to tokenized UI and Input sinks in registration order. After queued events
and asset completions, Input publishes one immutable snapshot for the outer frame. All fixed ticks and the variable
update observe that snapshot. UI capture from the last completed UI frame suppresses keyboard or pointer bindings on
`RespectUiCapture` maps; `AlwaysReceive` maps remain available for editor and global shortcuts.

Input mutation, pairing, map enablement, subscriptions, rebinding, and override persistence are owner-thread operations.
Keyboard and mouse are stable logical devices 1 and 2. Gamepad handles use RAII, reconnect by a stable hardware key,
cancel actions on disconnect, and restore surviving user ownership deterministically. Gamepad support may be absent in
an unsupported platform or custom SDL build without disabling keyboard and mouse. Kéire's production Windows, Linux,
and macOS dependency profiles enable native joystick backends, HIDAPI gamepads, and normalized rumble. Their virtual
joystick backend keeps gamepad discovery and rumble tests independent of attached hardware.

## Users And Actions

Create an `InputUser`, pair its devices, then create a context for an `InputActionAsset` ID. Devices are exclusive by
default. Keyboard and mouse are normally paired together; gamepads are exclusive unless both the application and the
pairing request opt into sharing. Automatic join is enabled by default and bounded by `MaximumUsers` (four by default).
When an unlocked user owns devices from multiple binding groups, meaningful input selects `KeyboardMouse` or `Gamepad`
before the frame's actions are evaluated. `SetControlScheme(..., true)` keeps an explicitly locked scheme unchanged.

```cpp
const auto input = application.Input();
const auto player = input->CreateUser("Player 1");
(void)input->PairDevice(player, Keire::InputDeviceId(1));
(void)input->PairDevice(player, Keire::InputDeviceId(2));

const auto actions = input->CreateActionContext(defaultInputAssetId, player);
(void)actions->EnableMap("Player");
const auto move = actions->FindAction("Player", "Move");
const Keire::InputVector2 value = move.Value().AsAxis2D();
```

Action phases are `Started`, `Performed`, and `Canceled`, with `Waiting` and `Disabled` polling states. RAII
subscriptions disconnect safely, including from inside a callback. Successful asset revisions cancel active actions
before rebuilding and preserve enabled map IDs, users, schemes, subscriptions, and valid binding overrides. A malformed
reload stays on the last-good asset revision. A press and release received within one outer frame still publishes its
phase transitions even though the final polled value is released.

Fixed-tick capture is also the bridge to the application-owned Replay service. `CaptureFixedTick()` produces the
stable, sorted action snapshot and input-map fingerprint recorded in `.keirereplay`; verified playback applies that
snapshot back to gameplay contexts while leaving editor-control contexts live. Replay file ownership, checkpoints,
seeking, and verification remain Replay responsibilities rather than action-map APIs.

## Rebinding And Cursor Control

`BeginInteractiveRebind` targets a stable binding ID and user context. Its bounded operation supports device filters,
excluded controls, a magnitude threshold (0.5 by default), a five-second timeout, conflict reporting, and transactional
Replace, Keep Both, or Cancel completion. Destroying the operation, timing out, or losing a candidate device cancels it.
Profile overrides are separate versioned JSON files, written atomically below the configured preference directory;
unknown stale binding IDs are ignored.

`SetGamepadRumble` targets a connected logical gamepad with normalized low- and high-frequency motor strengths and a
duration from zero through 60 seconds. A zero-strength request stops the effect. Keyboard, mouse, disconnected, and
unsupported gamepad requests return `false`; invalid strengths or durations are rejected before SDL dispatch. The
managed runtime additionally restricts rumble to devices paired to the active player.

Linux uses native evdev plus HIDAPI's hidraw backend through dynamically loaded libudev. Distribution rules normally
grant local desktop users access to supported controllers; an unusually configured controller may require an
administrator-provided udev rule for its `/dev/input` or `/dev/hidraw` node. Kéire does not install broad device-access
rules or elevate the player, and inaccessible hardware is ignored without affecting keyboard or mouse input.

`WindowSystem::SetCursorMode` supports `Normal`, `Hidden`, `Confined`, and `RelativeLocked`. Focus loss temporarily
releases confinement or relative mode while preserving the requested mode for focus restoration. The public boundary
contains no SDL window or mouse types.

## Deliberate Limits

Text entry/IME, touch, pen, sensors, generic joysticks, XR, advanced trigger/adaptive haptics, networking, and generated
C++ wrappers are outside this milestone. Source assets currently accept the built-in
Press, Tap, Hold, MultiTap, Deadzone, Scale, Invert, and Normalize behaviors.
