# First Person Controller

Import this package, approve its C# assembly, then drag **FirstPersonPlayer.keireprefab** into your scene.
Place the capsule above a floor with a collider, disable any other primary camera and audio listener, and press Play.
The prefab includes its camera, audio listener, capsule Character Controller, script, and input asset. No code is required.
Keep the player upright with unit scale. The capsule centre starts one metre above the floor; the camera sits 0.65 m above it.

- WASD / left stick: move. Diagonal movement is normalized.
- Mouse / right stick: look. Mouse sensitivity and stick turn rate are independent.
- Space / gamepad south button: jump when grounded.
- Left Shift / left stick press: sprint.
- Escape / gamepad Start: release or recapture the cursor.

Select the camera child to edit walking speed, sprint multiplier, jump height, gravity, sensitivity, and invert Y.
Edit FirstPersonInput to rebind controls. UI can request cursor visibility with the engine Cursor API; this suspends player input.
Movement uses the engine collision controller and fixed simulation time. Camera pitch is limited to avoid flipping.
Only use one active player with this input context at a time. This starter does not include networking, weapons, crouching, or animations.

## Updating

Keep the .keiremeta files and stable component/field IDs when customizing the package; they preserve references and serialized settings.
Package Manager tracks your imported files. Review updates to keep local edits or replace conflicting files deliberately.
The package records the engine release it was built against; check Package Manager compatibility notices after an engine upgrade.
