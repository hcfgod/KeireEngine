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

`SceneHandle` contains only an `AssetId`. Its `IsLoaded` and `IsActive` properties query the current world, so a retained
handle can become inactive after a scene transition.

## Replace A Scene

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
        SceneLoadOperation load = SceneManager.LoadSceneAsync(_destination);
        yield return load;

        if (!load.Succeeded)
        {
            Debug.Error($"Scene transition failed: {load.Error}");
            yield break;
        }

        Debug.Log($"Activated {load.Scene.Asset}");
    }
}
```

The operation exposes `Scene`, `Mode`, `State`, `Progress`, `IsDone`, `Succeeded`, `Error`, and `Cancel()`. Cancellation
succeeds only while the native asset operation is queued or loading. A terminal operation is retained for bounded
status inspection; the player reclaims old terminal operations as new transitions are requested.

Packaged-player replacement is transactional:

1. Kéire loads and validates the destination scene without stopping the current runtime session.
2. It creates the replacement physics, audio, VFX, UI, and managed-behaviour worlds and starts Play.
3. It verifies that the replacement has an active camera.
4. Only then does it publish the replacement and stop the previous session.

During the destination's `Awake`, `OnEnable`, and `Start` callbacks, `SceneManager.ActiveScene` resolves to that
destination through the callback scope. Outside those callbacks, the previous world remains the committed active scene
until the complete activation transaction succeeds.

An asset, startup, script, subsystem, or camera failure leaves the current gameplay scene running and reports the
diagnostic through `Error`. Only one managed transition may be pending at a time. Replay-driven players reject runtime
scene transitions because replacing their recorded world would invalidate deterministic replay ownership.

### Current Loading Modes

`SceneLoadMode.Single` is supported by the standalone player. `SceneLoadMode.Additive` is represented for source
compatibility with the native scene system but is rejected by the managed player today. Kéire will not claim additive
support until multiple scenes can share and unload render, physics, audio, UI, VFX, and script worlds as one explicit
transaction.

Editor Play Mode reports its active and loaded scene through the same handles. Managed scene replacement is currently
rejected in Editor Play Mode; test packaged-player transitions in a player build. This avoids silently replacing the
editor's authored scene or bypassing its Play-session ownership.

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
- Treat every `SceneHandle` as non-owning and recheck it after replacement.
- Preserve the operation until its coroutine completes if failure diagnostics matter.
- Do not busy-wait on `Progress`; yield the operation or sample it once per frame.
- A render-settings assignment is complete or rejected. There is no partially applied environment.

See [Scene System](../SceneSystem.md) for the native scene lifecycle and
[Rendering And Materials](RenderingAndMaterials.md) for cameras, lights, Mesh Renderers, and per-renderer material
overrides.
