# UI, Jobs, And Diagnostics

Kéire's runtime UI is scene-backed. Canvas, text, image, button, slider, toggle, input-field, scroll-view, layout, and
accessibility components are authored in the Editor and driven through typed managed wrappers in Play Mode and players.

## Bind A Scene Button

Assign a `UiButton` in the Inspector and make registration symmetric across disable and reload:

```csharp
using Keire;

namespace MyGame;

[StableComponentId("ee3548b8-662a-4898-8db0-27b034d9f08a")]
public sealed class ResumeButton : Behaviour
{
    [SerializeField, StableFieldId("3b98ad0f-347e-44a9-8c95-e773c60767cb")]
    private UiButton? _button = null;

    protected override void OnEnable() => Bind();
    protected override void OnDisable() => Unbind();
    protected override void OnBeforeReload() => Unbind();
    protected override void OnAfterReload() => Bind();

    private void Bind()
    {
        if (_button is not null)
            _button.Clicked += Resume;
    }

    private void Unbind()
    {
        if (_button is not null)
            _button.Clicked -= Resume;
    }

    private void Resume() => Time.TimeScale = 1.0f;
}
```

Buttons can also be polled with `RuntimeUi.WasClicked`; use events or polling for one button, not both. `UiSlider`,
`UiToggle`, `UiInputField`, and `UiScrollView` expose typed live values and per-frame events. `RuntimeUi.SetText` updates
compatible scene text and returns `false` when the entity is invalid or incompatible.

Menus should hold `Cursor.RequestVisible()` while open. Gameplay should hold `Cursor.RequestCapture()` while it owns
look input. Dispose requests in `OnDisable` and `OnBeforeReload`; visible requests take precedence without destroying a
still-owned capture request.

## Managed Events

`KeireEvent` and its generic variants store Inspector listeners and accept runtime listeners. Persistent listeners run
before runtime listeners. Renaming a component is safe when its stable component ID is retained; renaming a callback
requires reselecting it in persisted listener data.

## Background Jobs

Jobs execute on scheduler workers. They may calculate or perform suitable blocking work, but they must not mutate
entities, components, windows, or other owner-thread services directly.

```csharp
private async Task<int[]> BuildTableAsync(CancellationToken cancellation)
{
    int[] table = new int[1024];
    Job job = Jobs.Submit(
        context =>
        {
            for (int index = 0; index < table.Length; ++index)
            {
                context.CancellationToken.ThrowIfCancellationRequested();
                table[index] = index * index;
            }
        },
        new JobDescription
        {
            Name = "Build lookup table",
            Priority = JobPriority.Low,
            Class = JobClass.Compute
        });

    using CancellationTokenRegistration registration = cancellation.Register(job.Cancel);
    await job.Completion;
    return table;
}
```

Call this from a lifecycle callback with `LifetimeToken`. The captured Behaviour synchronization context resumes the
continuation on the simulation thread. `Job.Completion` preserves failure and cancellation; dependencies can be listed
in `JobDescription.Dependencies`.

## Logging And Profiling

```csharp
using (Profiler.Sample("Inventory.Rebuild"))
{
    RebuildInventory();
}

Profiler.Counter("Inventory.VisibleSlots", visibleSlots);
Debug.Assert(visibleSlots >= 0, "Visible slot count must be non-negative.");
```

Use `Debug.Log`, `Warn`, `Error`, `LogException`, and `Assert` for gameplay-facing diagnostics. Use `Log` when explicit
trace/debug/info/warning/error/critical severity matters. Stable profiler names keep captures comparable; do not build
unique strings every frame.

## Failure Model

- A lifecycle exception quarantines the failing Behaviour and reports the callback, type, entity, and generation.
- A failed script build or reload candidate leaves the last-good generation active.
- Disable, destroy, reload, and Play Mode teardown cancel `LifetimeToken`.
- A job callback must cooperate with cancellation and must not retain objects from an old managed generation.
- Structured `KEIRE-*` diagnostics link to packaged remediation when a matching guide exists.

Open **Window > Profiler** for full captures, **Window > Viewport Performance Overlay** for the compact HUD, and the
Console/Diagnostics panels for actionable failure text. Continue with [Debugging and Profiling](DebuggingAndProfiling.md).

## Collect A Diagnostic Bundle

In the Editor or Kéire Hub, choose **Help > Collect Diagnostics...**. Both actions show the exact frozen ZIP inventory
before anything is written. The preview lists each archive filename, byte size, SHA-256 hash, redaction count, and
omission reason. Deselect logs, project
metadata, package versions, or crash information to rebuild the preview before saving.

Bundles stay on the local computer until they are manually shared. They contain allowlisted build and hardware
identity, cached renderer capabilities and bounded statistics, aggregate project metadata, package IDs and versions,
stable failure counts, recognized Kéire last-failure availability, and sanitized tails from the two configured log
files. Log tails are limited to 2 MiB per file and 8 MiB total.

The collector does not scan project, home, package-cache, or operating-system crash directories. Project names and
paths, assets, documents, private package contents, source URLs, credentials, entitlements, environment values,
emails, signed URLs, private keys, and native dumps are excluded. Source and destination links are rejected, and the
ZIP is published atomically; cancellation or failure does not leave a partial archive.
