# Input Actions Editor

The dockable **Input Actions** panel opens from the Inspector, a Project-panel double-click/context action, or the
Window menu. Assets can be created from Empty, Default, 3D Gameplay, and UI Navigation templates through the Assets
menu. KeireClient uses only `UiFrame`; no Dear ImGui include or symbol crosses the client boundary.

The panel uses a master-detail workflow:

- The toolbar identifies the source asset and provides Save, Revert, bounded Undo/Redo, Validate, search, and live
  monitor controls. `Ctrl+S`, `Ctrl+Z`, `Ctrl+R`, `Ctrl+Y`, and `Ctrl+Shift+Z` mirror the toolbar.
- The left pane creates, selects, renames, duplicates, and removes action maps and control schemes. Scheme properties
  edit unique binding groups plus required or optional Keyboard, Mouse, and Gamepad families.
- The center pane shows actions with nested binding/composite rows. It creates ordinary bindings, 1D Axis composites,
  and 2D Vector composites with stable generated IDs, and supports action duplication and deletion.
- The right pane edits map capture policy; Button, Value, and Pass Through action types; Boolean, 1D Axis, and 2D Vector
  values; common control paths; scheme membership; and all built-in interactions and processors. Press, Tap, Hold,
  Multi Tap, Deadzone, Scale, Invert, and Normalize expose their validated parameters directly.

Save canonicalizes and validates the entire document, atomically replaces the `.keireinput` source, imports it through
the same `AssetDatabase` path as `KeireAssetTool`, and requests runtime hot reload. Revert discards the in-memory command
history and reloads the source. The undo stack is bounded to 128 full-document snapshots so malformed partial commands
cannot leak into saved data.

Listen uses the reusable runtime rebind API. A progress indicator shows the timeout, the candidate path is prominent,
and conflicts offer Replace, Keep Both, or Cancel. Replace removes conflicting source bindings in the selected map;
Keep Both preserves them. The live monitor shows the selected action phase/value plus connected-device and paired-user
counts.

The Project panel advertises `.keireinput` sources as typed input assets and supports asset drag payloads through the
Kéire UI facade. Switching away from a dirty input document is blocked until it is explicitly saved or reverted, so a
selection change cannot silently discard authoring work.
