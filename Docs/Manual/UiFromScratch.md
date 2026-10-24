# Build Your Own Game UI

Build a small HUD with a health bar and a Heal button. You will author a UI document, give it a screen presentation,
connect it to gameplay, and verify the result in Play Mode and a player. No legacy Canvas or Rect Transform is needed.

Before starting, open a project with a saved scene and an active camera. Wait for imports and managed compilation.
Use [UI Toolkit and Events](../Scripting/UiAndEvents.md) for the full API and
[Visual Style Studio](../UiStyleStudio.md) for reusable styling.

## 1. Keep Only The Starter Content You Need

If your scene contains starter UI, disable its **UI Document** entity first and enter Play to identify which overlay it
owns. Stop before deleting anything. Removing a Hierarchy entity changes the scene; it does not remove its source
assets or C# files from the Project panel.

Keep any Panel Settings asset you want to reuse. Remove unused UI entities and their controller components, save the
scene, then delete only the unneeded source assets/scripts from Project. Check other scenes and prefabs before deleting
a shared asset. Wait for compilation after deleting scripts and fix missing-component references. Do not delete
`Library` to remove template content: it is generated state, and it does not remove the authored sources.

If you want to preserve the starter example, save your work in a separate scene and leave its assets intact instead.

## 2. Author A Document

1. Create a folder such as `Assets/UI`. In its Project-panel **Create** menu choose **UI Document** and name it `GameHud`.
   Creation opens the document in UI Builder; double-click its `.keireui` asset to reopen it later.
2. In **Design**, select the root container. Use the Library's **Search controls** to find `ProgressBar` and click
   **+ ProgressBar**, or drag the control onto the canvas. Keep its default range of `0` to `100`;
   the current Builder Inspector does not expose range controls.
3. Select the root again and add a Button. Set its Name to `heal` and its text to `Heal`. A runtime query uses the
   authored Name, not the displayed text. Use unique names for controls that scripts query.
4. Move and resize the controls in the preview. Canvas manipulation writes absolute layout properties. For a responsive
   flex layout, edit the layout properties in the Inspector instead of assuming a canvas drag keeps flex positioning.
5. Save the UI document. Use **Fit** to reset pan and zoom if you lose sight of it.

The preview is an authoring canvas. Creating or selecting this asset does not add it to the scene. Screen-space UI is
shown in Game view, Play, and the player; it does not appear as an overlay in the 3D Scene viewport.

## 3. Choose Persistent Presentation Settings

Starter projects provide `.keireuipanel` assets such as `ScreenOverlay`. Reuse one if it matches your intended output.
The current Project Create menu does not provide a Panel Settings creation command. For a clean project, create
`Assets/UI/ScreenOverlay.keireuipanel` in your text editor with this source, then let the Editor import it:

```json
{
    "schemaVersion": 1,
    "target": "ScreenOverlay",
    "scaleMode": "ScaleWithViewport",
    "referenceWidth": 1920.0,
    "referenceHeight": 1080.0,
    "matchWidthOrHeight": 0.5,
    "sortingOrder": 0,
    "camera": "00000000-0000-0000-0000-000000000000",
    "renderTexture": "00000000-0000-0000-0000-000000000000",
    "respectSafeArea": true,
    "worldWidth": 1.92,
    "worldHeight": 1.08,
    "pixelsPerUnit": 1000.0,
    "depthTest": false
}
```

This is the starter screen-overlay policy: scale around a 1920×1080 reference with an equal width/height match.
World dimensions apply to world-surface output, not this screen overlay. Preview resolution and **Match Game View**
do not save a presentation policy; the `.keireuipanel` asset does.

## 4. Present It In The Scene

1. Create or select a scene entity named `GameHud`.
2. Add a **UI Document** component in the Inspector.
3. Assign `GameHud.keireui` to **Visual Tree** and `ScreenOverlay.keireuipanel` to **Panel Settings**.
4. Enable **Receives Input** for the interactive HUD. Leave **UI Material** empty for standard rendering.
5. Save the scene, enter Play, and inspect Game view. Both controls should appear. If another document covers them,
   compare component and Panel Settings sorting orders; their sum controls document stacking.

To edit the assigned document, select the entity and click **Open Visual Tree in UI Builder** in its Inspector.
Selecting the entity alone does not open Builder. Edit the visual tree in Builder and assign scene references on the
component Inspector; these are distinct tasks.

## 5. Bind Health And Handle Activation

1. Select the ProgressBar in Builder. Set **Target Property** to `value`, **Source Path** to `Player.Health`, and the
   mode to **OneWay**, then click **Add or Replace Binding**. Save the document.
2. Copy [HealthPanel.cs](../Scripting/Examples/HealthPanel.cs) to `Assets/HealthPanel.cs`. In your copied file, change
   the `_health` initializer from `100` to `75` for this smoke test. Wait for a successful build, then attach
   **HealthPanel** to the scene entity holding your UI Document.
3. Its `Update` publishes `Player.Health` with `SetBindingValue`. This is an explicit key; it does not discover an
   object called Player through reflection.
4. Enter Play. The bar should show 75; click Heal and confirm it reaches 100. Stop Play before editing the scene.
   After this smoke test, restore the initializer to `100` and connect `Damage(25)` to your own damage/interaction
   logic. Clicking Heal at full health intentionally produces no visible change.

The supplied example demonstrates pointer activation. For keyboard/gamepad menus, focus a live control with
`RuntimeVisualElement.Focus()` and route both `ClickedThisFrame` and `SubmittedThisFrame` to the same action, reading
both before combining their results. See the [Resume button example](UiJobsAndDiagnostics.md#connect-a-scene-button-to-gameplay).
Event getters consume pending events, so a second script should not independently poll the same control.

If gameplay captures the pointer, hold a `Cursor.RequestVisible()` token while the menu owns interaction and dispose
it when the menu closes, disables, or reloads. See [cursor ownership](../Scripting/GameplayServices.md#cursor-ownership).

## 6. Style And Test The HUD

Create a **UI Style Sheet** in Project, link it with Builder's typed asset picker, and open **Styles** for Style Studio.
Use classes for reusable appearance. Document and stylesheet drafts have independent histories and Save/Revert
boundaries; save each source you intend to keep. Avoid a fully opaque full-screen background if the HUD should let the
game world remain visible.

Check landscape, portrait, and a smaller window. Use **Match Game View** to compare logical dimensions. The Builder's
**Preview Material** affects only its preview; assign **UI Material** on the scene component for the actual game.

During Play, an unsaved Builder document draft can update matching live documents. Saving writes source to disk.
Stopping Play, switching documents, reloading, or closing the workspace restores the imported baseline for an unsaved
draft. **Play Mode Changes** controls scene changes separately; discarding scene changes does not undo saved UI files.

Build a player using [Player Builds](../PlayerBuilds.md). Launch it from its output folder and verify HUD scaling,
pointer interaction, intended keyboard/gamepad focus, and scene transitions. A correct Builder preview alone does not
verify scene assignment, cooked dependencies, or input routing.

## Troubleshooting

| What you see | Check next |
| --- | --- |
| Builder looks right; Game is empty | Scene UI Document's Visual Tree and Panel Settings, active entity, active camera, Game view |
| Scene view has no HUD | ScreenOverlay is intentionally presented in Game/Play/player, not over the 3D Scene viewport |
| Clicks do nothing | Receives Input, cursor ownership, covering documents, exact `heal` name, script build and enabled state |
| ProgressBar stays at its default | `Player.Health` spelling, target `value`, OneWay binding, script on the same entity, range 0–100 |
| UI breaks after saving a document | Re-query runtime handles after a successful document generation change; check Builder diagnostics |
| Two UI layers appear | Another starter/scene/persistent UI Document is still active |
| A style change disappears | Save the stylesheet draft as well as the visual tree; preview settings are not source settings |
| UI differs in the player | Verify scene/dependency cooking, actual window dimensions, font/texture availability, and runtime diagnostics |

Next: [Gameplay Testing](GameplayTesting.md) for a repeatable Play/reload/player validation pass.
