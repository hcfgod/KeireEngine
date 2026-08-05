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
a platform/test SDL build without disabling keyboard and mouse.

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

## Rebinding And Cursor Control

`BeginInteractiveRebind` targets a stable binding ID and user context. Its bounded operation supports device filters,
excluded controls, a magnitude threshold (0.5 by default), a five-second timeout, conflict reporting, and transactional
Replace, Keep Both, or Cancel completion. Destroying the operation, timing out, or losing a candidate device cancels it.
Profile overrides are separate versioned JSON files, written atomically below the configured preference directory;
unknown stale binding IDs are ignored.

`WindowSystem::SetCursorMode` supports `Normal`, `Hidden`, `Confined`, and `RelativeLocked`. Focus loss temporarily
releases confinement or relative mode while preserving the requested mode for focus restoration. The public boundary
contains no SDL window or mouse types.

## Deliberate Limits

Text entry/IME, touch, pen, sensors, generic joysticks, XR, haptics, networking, recording/replay, and generated C++
wrappers are outside this milestone. Source assets currently accept the built-in Press, Tap, Hold, MultiTap, Deadzone,
Scale, Invert, and Normalize behaviors.
