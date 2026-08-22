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
