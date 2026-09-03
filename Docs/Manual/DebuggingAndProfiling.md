# Debugging And Profiling

Start with the narrowest signal that owns the failure: managed build diagnostics for C#, the graph diagnostic strip
for visual authoring, import status for assets, Console/Diagnostics for runtime failures, and Profiler for measured
performance.

## C# Is Running Last-Good Code Until Publication Succeeds

When a saved script appears unchanged in Play Mode:

1. Open the Console and managed build diagnostics.
2. Confirm the file is below a `.keireasm` source root.
3. Fix compile errors, then type-discovery and stable-ID errors.
4. Check migration/reload diagnostics after compilation.
5. Confirm the component is enabled and the entity is active in hierarchy.
6. Look for a lifecycle exception that quarantined only that instance.

Kéire never publishes a partial generation. A failed candidate keeps the previous code active; this is why source and
runtime behavior can briefly differ.

Use `Debug.LogException(exception)` when you recover locally. Avoid noisy per-frame logging. A good message identifies
the operation, entity or asset purpose, and whether execution recovered or stopped.

## Assets, Scenes, And Graphs

- An import failure leaves the last valid derived asset available where possible; inspect the asset's importer
  diagnostic and source dependency.
- A scene open/load validates the candidate before replacing active state. Read the diagnostic instead of repeatedly
  reopening a malformed asset.
- Graph previews retain the last valid candidate. Fix the first relevant topology/type/compiler message; Save may be
  blocked by design while the draft is incomplete.
- Structured diagnostics use stable `KEIRE-*` IDs. Open the linked remediation page and retain capture/replay evidence
  requested by that guide.

## Profiler Workflow

Open **Window > Profiler** for the complete workspace or **Window > Viewport Performance Overlay** for a compact HUD.

1. Reproduce the issue with representative content and a stable camera/input path.
2. Let rolling statistics warm up.
3. Freeze a capture around the slow or incorrect frame.
4. Check frame current/average/P95/P99/max and one-percent-low data.
5. Inspect CPU spans by category and thread, then renderer, physics, scripting, audio, asset, and VFX counters.
6. Distinguish GPU timestamp duration from fence-completion latency; the latter is not a true GPU execution time.
7. Copy a focused row, complete snapshot, or export rolling frame CSV for comparison.

For dense rendered scenes, compare **Dynamic upload bytes** with **Dynamic upload buffer reallocations**. Bytes measure
the payload copied that frame; reallocations should settle to zero after the surface reaches its working-set peak. A
high particle count with a low **CPU VFX draw batches** value means compatible depth-ordered particles are sharing draw
state. Persistent reallocations or a batch count close to the particle count usually indicates changing capacity,
textures, or material surface state and is worth inspecting before increasing content budgets.

Use **Depth draw calls/triangles** and **Shadow draw calls/triangles** to account for work that is not part of the main
scene draw total. **Culled shadow submeshes**, **Culled local lights**, and **Culled CPU VFX particles** should increase
when the same camera path excludes more work. Compare these counters with identical content, camera, resolution, shadow
settings, and warm-up; a higher cull count alone is not evidence of a faster frame if frame-time percentiles regress.

## GPU Occlusion Checks

Project Settings > Rendering selects **Disabled**, **Automatic**, or **Forced** GPU occlusion. Automatic keeps direct
draws below the renderer's profitability threshold. Forced is the diagnostic mode: it bypasses that threshold but still
rejects unsafe occluders, unsupported backends, legacy shader ABIs, and effects whose depth-only geometry can differ from
their visible geometry.

The Scene toolbar's bug button toggles camera-local **Visibility Bounds** directly: green bounds are visible and red
bounds were culled for the free editor camera. Its information button toggles per-surface diagnostics metadata. The gizmo
settings popup also provides **None**, **Visibility Bounds**, and **Hierarchical Depth**; Hierarchical Depth exposes only
mips currently reported by the Scene surface, and resizing or disabling occlusion clamps the selection safely. The
Game toolbar's **GPU Bounds** control visualizes the authored game camera independently. Neither visualization proves
that every hidden pyramid resource is correct. Inspect those resources with a RenderDoc, PIX, Xcode GPU, or equivalent
platform capture and verify every mip is a conservative reduction of the previous level.

In the Profiler and Render Graph panel:

1. Confirm the occlusion depth, pyramid, classification, and indirect-argument passes execute before opaque drawing.
2. Compare candidates, visible, culled, candidate/culled triangles, safe occluders, dispatches, and indirect draws.
3. Treat visibility totals as valid only when **Readback valid** is true; pair them with **Readback age** and source frame.
4. Investigate persistent fallback events by their typed reason. Direct draws must remain visually complete.
5. Warm the scene and confirm buffer reallocations settle to zero; compare frame P95/P99 with identical content and path.

The Scene and Game overlays identify their camera and show that surface's camera-local visibility result. During Play,
the Profiler and Render Graph select and label the authored Game camera's occlusion result; in Edit they select the free
Scene editor camera. Renderer submission and CPU-visibility totals remain explicitly labeled all-surface aggregates.
The overlays' FPS, category timing, dispatch, indirect-draw, and CPU-preparation rows are likewise completed-frame
aggregates, not separate per-camera performance measurements. The Render Graph shows the most recently finalized frame
aggregate while the next frame is being prepared, and the Profiler publishes that same completed workload after
presentation. Dispatch and indirect-draw values therefore remain nonzero and internally consistent through editor UI
recording instead of briefly displaying reset values. When the mode is Disabled,
visibility readback is explicitly unavailable; a pending readback is reserved for an active surface that has not yet
produced its asynchronous result. The FPS overlay is drawn by its owning viewport in both Edit and Play modes, so it
cannot create a detached ImGui fallback window. Its advanced view includes **Application** and **Editor/User** so an
Inspector or other editor-side hitch is not mistaken for renderer work. Profiler category totals are inclusive and can
overlap; do not add the rows together and compare that sum with the frame time. Renderer **CPU preparation** is a
frame aggregate across every submitted surface, including Scene's Main Camera Preview, rather than the last surface's
partial value. Continuous Transform drags use compact typed undo and targeted Play-change tracking; they do not
snapshot and encode the whole scene for every pointer sample.

For a deterministic Automatic-mode demonstration, open
`Assets/Scenes/GpuOcclusionStress.keirescene` in KeireSandbox and enter Play Mode so the authored primary camera is
used. Keep Project Settings > Rendering > GPU Occlusion Culling on Automatic. The authored view is validated at 16:9
resolutions from 1280x720 through 3840x2160. After the qualifying frames and readback latency, expect the Game surface
to report 161 candidates, one safe occluder, nonzero dispatch/indirect work, and approximately 144 culled hidden spheres.
The free Scene camera is independent: an oblique editor view can correctly see those targets around the wall and report
different counts. Never use the frame aggregate to infer one camera's result; it is the sum of every rendered surface.
Main Camera Preview is also independent of Game Preview and may intentionally report **NoSafeOccluders** with direct
draws at its compact size even while the full-size Game surface is Active. Verify the labeled Game-camera overlay, use
**GPU Bounds** for a red/green proof, or turn off the Scene toolbar's camera-preview icon when reading aggregate surface
counts. Sixteen cyan controls remain visible outside the blue occluder while the 144 red targets sit behind it. Disabled
mode must render every target directly; Forced mode remains useful for comparing the same scene without Automatic's
profitability gate.

For performance comparisons, warm the scene, fix the Game resolution, and test a Release build with Game isolated or
maximized. Disable Main Camera Preview, set every Scene debug view to **None**, and turn off **GPU Bounds**, FPS, and the
advanced overlay. Record that baseline before re-enabling each surface or diagnostic independently to measure its cost.
A Debug editor rendering Scene, Game, and Main Camera Preview simultaneously is a correctness setup, not a locked-60-FPS
target.

The Render Graph panel reports graphics-capture availability without loading or launching a tool. If RenderDoc was
injected before editor startup and reports Ready, **Capture Next GPU Frame** queues exactly one capture. Unavailable and
already-active states remain non-destructive and never overlap a capture.

Rendered-output acceptance compares Disabled and Forced captures, then moves a fully hidden target outside its occluder's
silhouette. The images must match while hidden and the target must appear on the next rendered frame. Repeat on D3D12 and
Vulkan; validate Metal on macOS hardware. Also exercise independent Scene/Game views, resize, minimize/restore, device
loss, empty scenes, no-safe-occluder scenes, always-visible geometry, and legacy/custom shader fallback. After a valid
readback, run Forced-Disabled-Forced around a resize and confirm no older source frame becomes valid again. GPU capture
is required evidence for pyramid contents and barriers; editor metadata alone cannot prove them.

Profiler storage is bounded. A capture reports dropped spans/counters when capacity is exceeded rather than growing
without limit.

## Add Managed Markers

```csharp
protected override void Update()
{
    using (Profiler.Sample("CrowdDirector.Update"))
    {
        UpdateCrowd();
    }

    Profiler.Counter("CrowdDirector.ActiveAgents", _activeAgentCount);
}
```

Use stable literal names. Remove empty lifecycle overrides: Kéire can skip unimplemented fixed/late callbacks, while
every Behaviour still participates in variable update to pump its coroutine scheduler.

## Validate The Fix

Retest in Edit/Play ownership as applicable, reload managed code once, repeat the profiler capture, and run a cooked
player on the target OS for player-only problems. Editor preview performance is useful evidence but is not equivalent
to player performance.

Use [Profiling](../Profiling.md) for every capture/counter surface and [Performance Gates](../PerformanceGates.md) for
reference-hardware and release acceptance rules.
