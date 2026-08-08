# Profiling and Performance Diagnostics

Kéire records bounded CPU spans, counters, subsystem statistics, and rolling frame summaries when profiling is enabled
in the application specification. Open `Window > Profiler` for the diagnostics workspace or toggle
`Window > Viewport Performance Overlay` for the compact Scene/Game HUD.

## Managed instrumentation

Use `Profiler.Sample` for scoped timings and `Profiler.Counter` for values that should appear in the current frame.
Samples are value-type scopes; after warm-up, native profiler storage reuses span, counter, and string capacity.

```csharp
using (Profiler.Sample("WeaponController.Fire"))
{
    FireWeapon();
}

Profiler.Counter("Weapon.ActiveProjectiles", activeProjectiles);
Profiler.Counter("Weapon.PoolPressure", poolPressure);
```

Names should be stable literals. Do not construct a unique name every frame. Managed samples appear under the
`Scripting` category and can be copied individually or as part of a complete profiler snapshot.

## Profiler workspace

The workspace includes:

- Current, average, P95, P99, maximum, and one-percent-low frame performance.
- Rolling stutter detection using both a 33.3 ms floor and the recent-frame average.
- Frozen captures for investigation without changing the selected data.
- CPU hotspots with category, duration, and thread identity.
- Application, scripting, physics, rendering, audio, asset-streaming, and custom counters.
- Renderer visibility, frame-graph, light-tile, CPU preparation, and latency statistics.
- Separate renderer command-recording, swapchain-wait, editor-UI recording, GPU-submission, and total-latency timings.
- Explicit frame/VFX fence-completion latency, kept distinct from true GPU timestamp duration.
- Audio voice, virtualization, rendered-frame, and underrun health.
- Asset queue, loading, residency, failure, and eviction health.
- Click-to-copy rows, complete clipboard snapshots, and rolling frame CSV export.

Profiler buffers are bounded. A truncated capture reports dropped span and counter counts instead of growing memory
without limit.

Reference captures are validated with the workflow in [Performance Gates](PerformanceGates.md). A backend that cannot
report true GPU timestamps may still expose fence-completion latency for diagnosis, but it cannot pass a profile that
requires measured GPU execution time.

The Kéire editor requests mailbox presentation to avoid falling from 60 FPS toward 30 FPS whenever a frame narrowly
misses a VSync deadline. Platforms without mailbox support automatically retain the renderer's VSync fallback.

The Profiler panel refreshes its rolling statistical analysis at a diagnostic cadence instead of cloning and sorting
the complete history every rendered frame. Captures continue recording at full frame frequency, and clipboard reports
are generated only when requested.

## Cursor release behavior

When gameplay releases relative cursor capture through Escape or opens UI through Tab, the editor centers the cursor
inside the active Game viewport. In Edit Mode, cursor release centers in the Scene viewport. Synthetic mouse movement
from the warp is suppressed before gameplay look input resumes.
