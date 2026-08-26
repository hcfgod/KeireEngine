# Input Actions Editor

The dockable **Input Actions** panel opens from the Inspector, a Project-panel double-click/context action, or the
Window menu. Assets can be created from Empty, Default, 3D Gameplay, and UI Navigation templates through the Assets
menu. KeireClient uses only `UiFrame`; no Dear ImGui include or symbol crosses the client boundary.

The panel uses a master-detail workflow:

- The compact icon toolbar identifies unsaved state and provides Save, Revert, bounded Undo/Redo, Validate, search,
  and live monitor controls. Delayed tooltips explain every command; `Ctrl+S`, `Ctrl+Z`, `Ctrl+R`, `Ctrl+Y`, and
  `Ctrl+Shift+Z` mirror the toolbar.
- The left pane creates, selects, renames, duplicates, and removes action maps and control schemes. Scheme properties
  edit unique binding groups plus required or optional Keyboard, Mouse, and Gamepad families.
- The center pane shows actions with nested binding/composite rows. It creates ordinary bindings, 1D Axis composites,
  and 2D Vector composites with stable generated IDs, and supports action duplication and deletion.
- The right pane edits map capture policy; Button, Value, and Pass Through action types; Boolean, 1D Axis, and 2D Vector
  values; common control paths; scheme membership; and all built-in interactions and processors. Press, Tap, Hold,
  Multi Tap, Deadzone, Scale, Invert, and Normalize expose their validated parameters directly.

The panes resize with the available dock width. Search is case-insensitive across map, scheme, action, binding, and
control-path text, keeps a parent visible when one of its children matches, and presents control paths as readable
device/control pairs while retaining the exact source path in Properties.

Save canonicalizes and validates the entire document, atomically replaces the `.keireinput` source, imports it through
the same `AssetDatabase` path as `KeireAssetTool`, and requests runtime hot reload. Revert discards the in-memory command
history and reloads the source. The undo stack is bounded to 128 full-document snapshots so malformed partial commands
cannot leak into saved data.

Listen creates a transient runtime context from the current in-memory document, so existing, new, and unsaved bindings
can capture any keyboard key, mouse control, or gamepad control without modifying gameplay input. A progress indicator
shows the timeout, the candidate path is prominent, and conflicts offer Replace, Keep Both, or Cancel. Replace removes
conflicting source bindings in the selected map; Keep Both preserves them. The live monitor shows the selected action
phase/value plus connected-device and paired-user counts.

The Project panel advertises `.keireinput` sources as typed input assets and supports asset drag payloads through the
Kéire UI facade. Switching away from a dirty input document is blocked until it is explicitly saved or reverted, so a
selection change cannot silently discard authoring work.

## Project Default And Managed Lifecycle

Project Settings has an **Input** section with a typed Input Action Asset picker and an action-map dropdown. The map is
stored by stable ID, so renaming it does not break startup. A change applies the next time Play starts and is also
written into packaged runtime manifests.

An asset offers a Unity-style shared context for simple ownership:

```csharp
[SerializeField] private InputActionAsset _input = null!;
private InputAction? _interact;

protected override void OnEnable()
{
    _interact = _input.FindAction("Player/Interact")
        ?? throw new InvalidOperationException("Player/Interact is missing.");
    _interact.performed += OnInteract;
    _input.Enable();
}

protected override void OnDisable()
{
    if (_interact is not null)
        _interact.performed -= OnInteract;
    _input.Disable();
}

private void OnInteract(InputAction.CallbackContext context) => Debug.Log("Interact");
```

Use `using InputActionContext context = _input.CreateContext()` when a component needs independent enable state or a
second local player. Contexts expose `Enable`, `Disable`, `FindActionMap`, `FindAction`, and stable-ID access; maps and
actions each expose their own `Enable` and `Disable`. `ReadValue<bool>()`, `ReadValue<float>()`, and
`ReadValue<Vector2>()` read the current immutable frame snapshot. Disposing a context makes all of its maps and actions
inert and invalid to call. `InputAction.BeginInteractiveRebind` starts rebinding against that same explicit context, so
local-player contexts do not accidentally modify or listen through the project's shared context.

The **C# Code Generation** section creates a deterministic wrapper in `Assets/Scripts/Generated`. The editor resolves
the owning runtime `.keireasm` with the same folder-independent placement rules used by ordinary C# creation and adds
that folder to its source roots when needed. Scripts remain valid anywhere under `Assets`; the generated folder is a
convention, not a scripting boundary. Generated map and action properties use stable IDs instead of names and therefore
survive authoring renames. Wrapper construction creates an independent disabled context: call the wrapper's `Enable()`
for every map or a generated map property's `Enable()` for one map, and pair it with `Disable()`/`Dispose()`.

## Direct Polling

Action-independent polling reads the same paired-player snapshot:

```csharp
if (Input.Keyboard.Current?.wKey.WasPressed == true)
    Debug.Log("W pressed");

Vector2 lookDelta = Input.Mouse.Current?.delta.ReadValue() ?? Vector2.Zero;
float throttle = Input.Gamepad.Current?.RightTrigger.ReadValue() ?? 0.0f;
```

`ButtonControl` exposes `IsPressed`, `WasPressedThisFrame`/`WasPressed`, and
`WasReleasedThisFrame`/`WasReleased`. Mouse relative delta and scroll reset at the start of each input frame. Direct
polling also exposes `WheelUp`/`WheelDown` one-frame pulses and gamepad `LeftStickButton`/`RightStickButton` controls.
It respects the editor's active Play session and Game-view routing and is captured in fixed-tick replays.

The older string-based `Input.Button`, `Input.Axis`, and `Input.Axis2D` helpers remain source-compatible but are
obsolete. New gameplay should use an action asset/context or a direct control.
