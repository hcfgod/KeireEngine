# Scenes And Render Settings

Kéire exposes scene replacement and render-environment control through value-only managed APIs. Scene operations never
expose a native `Scene` pointer, and runtime render changes never mutate the authored project settings document.

## Inspect The Runtime World

`SceneManager.ActiveScene` identifies the scene currently driving gameplay. `SceneManager.LoadedScenes` returns a
bounded snapshot of the scenes owned by the current runtime context.

```csharp
SceneHandle active = SceneManager.ActiveScene;
if (active.IsValid)
    Debug.Log($"Active scene: {active.Asset}");
```

`SceneHandle` contains an asset ID for inspection and an opaque stable runtime identity. Handles returned by the
runtime are never reused within that Play/runtime world. Its `IsLoaded` and `IsActive` properties query the current
world, so a retained handle becomes stale after unload rather than addressing a later load of the same asset. The
one-argument `SceneHandle(AssetId)` constructor remains source-compatible for asset inspection, but only handles
returned by `SceneManager` have `HasStableIdentity` and can address a specific loaded scene.

## Load, Activate, And Unload Scenes

Scene loads integrate with Kéire coroutines through `SceneLoadOperation`:

```csharp
using System.Collections;
using Keire;

public sealed class LevelExit : Behaviour
{
    [SerializeField]
    private AssetReference<SceneAsset> _destination;

    private IEnumerator ChangeLevel()
    {
        SceneLoadOperation load = SceneManager.LoadSceneAsync(_destination, SceneLoadMode.Additive);
        yield return load;

        if (!load.Succeeded)
        {
            Debug.Error($"Scene transition failed: {load.Error}");
            yield break;
        }

        if (!SceneManager.SetActiveScene(load.Scene))
            Debug.Error("The loaded scene could not be queued for activation.");
    }
}
```

The operation exposes `Scene`, `Mode`, `State`, `Progress`, `IsDone`, `Succeeded`, `Error`, and `Cancel()`. Cancellation
succeeds only while the native asset operation is queued or loading. A terminal operation is retained for bounded
status inspection; the player reclaims old terminal operations as new transitions are requested.

`SceneLoadMode.Additive` keeps all existing loaded sessions and does not implicitly change the active scene.
`SetActiveScene` and `UnloadScene` accept only stable handles returned by the runtime, and take effect at the next safe
boundary. Unloading the only regular active scene is rejected; use a Single load to replace it transactionally:

1. Kéire loads and validates the destination scene without stopping the current runtime session.
2. It creates the replacement physics, audio, VFX, UI, and managed-behaviour worlds and starts Play.
3. It verifies that the replacement has an active camera.
4. Only then does it publish the replacement and stop the previous session.

During the destination's `Awake`, `OnEnable`, and `Start` callbacks, the previous world remains the committed active
scene until the complete activation transaction succeeds.

An asset, startup, script, subsystem, or camera failure leaves the current gameplay scene running and reports the
diagnostic through `Error`. Only one managed transition may be pending at a time. Replay-driven players reject runtime
scene transitions because replacing their recorded world would invalidate deterministic replay ownership.

Editor Play Mode and the packaged player use the same runtime-world implementation. Loads, active-handle changes,
unloads, query scopes, persistent objects, ticking, and per-scene subsystem ownership therefore follow the same
lifecycle. Stopping Play still discards the complete runtime world and never mutates an authored additive scene.

## Query Scopes And Persistent Objects

Existing query overloads remain active-scene queries. Pass an explicit `SceneQuery` when additive content is involved:

```csharp
IReadOnlyList<Entity> allEnemies = SceneManager.FindAllWithTag("Enemy", SceneQuery.Loaded);
IReadOnlyList<Entity> roomEnemies = SceneManager.FindAllWithTag("Enemy", SceneQuery.In(room.Scene));
IReadOnlyList<Entity> carried = SceneManager.FindAllWithComponent<PlayerState>(SceneQuery.Persistent);
```

`SceneQuery.Active` searches only the committed active scene, `Loaded` searches regular loaded scenes in stable load
order, `In(handle)` addresses one exact scene, and `Persistent` searches only unloaded carrier sessions.

Use `SceneManager.Preserve(entity)` to retain an entity across single transitions or unloads. Kéire promotes the
entity's hierarchy root, preserving that root, its descendants, their world/entity identities, managed instances, and
current lifecycle state. Other roots in the retiring scene receive their normal disable/destroy lifecycle. Persistent
objects finally stop when Play Mode or the player runtime world closes.

## Runtime Render Environment

`RenderSettings.Current` reads or replaces the complete render environment atomically. C# validates the request before
crossing the native boundary, and the engine repeats validation before publishing it.

```csharp
RenderEnvironmentSettings environment = RenderSettings.Current;
RenderSettings.Current = environment with
{
    AmbientColor = new Color(0.08f, 0.10f, 0.16f, 1.0f),
    AmbientIntensity = 0.35f,
    Exposure = 1.15f,
    Environment = nightSky,
    EnvironmentRotationDegrees = 35.0f,
    SkyVisible = true
};
```

Convenience properties are available for `AmbientColor`, `AmbientIntensity`, `Exposure`, `Environment`, and
`SkyVisible`. Use `Current` when several values must change together so the renderer observes one validated state.

The complete value also controls environment diffuse/specular intensity and directional-shadow distance, cascade
count, resolution, and split lambda. Invalid colors, non-finite numbers, out-of-range intensities, non-power-of-two
shadow resolutions, or invalid cascade settings throw before any field changes.

Runtime settings are transient:

- a packaged player retains them for its current process and uses them across scene replacements;
- Editor Play Mode applies them to the Game/Scene presentation for that Play session and discards them on Stop;
- neither path writes `Config/ProjectSettings/Rendering.keiresettings`.

Use the Project Settings authoring surface for persisted defaults. Use `RenderSettings` for gameplay transitions such
as interiors, weather, damage states, cinematics, and accessibility exposure presets.

## Failure And Lifetime Rules

- Call these APIs only from an active managed gameplay callback or continuation owned by that generation.
- Treat every `SceneHandle` as non-owning and recheck `IsLoaded` after a safe boundary.
- Use `SceneQuery.Loaded` or `SceneQuery.In(handle)` deliberately; compatibility query overloads search only the active
  scene.
- Preserve an entity once. Preserving a child retains its complete hierarchy root; it does not detach or clone it.
- Preserve the operation until its coroutine completes if failure diagnostics matter.
- Do not busy-wait on `Progress`; yield the operation or sample it once per frame.
- A render-settings assignment is complete or rejected. There is no partially applied environment.

See [Scene System](../SceneSystem.md) for the native scene lifecycle and
[Rendering And Materials](RenderingAndMaterials.md) for cameras, lights, Mesh Renderers, and per-renderer material
overrides.
