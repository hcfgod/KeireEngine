# Reference-Hardware Performance Gates

Kéire's release performance gate consumes three exported artifacts from one uninterrupted run on named hardware:

- a Profiler **Copy Full Snapshot** text file;
- the matching rolling frame-history CSV;
- a small JSON metadata file identifying the hardware, driver, configuration, resolution, and workload.

The gate recomputes average, P95, and P99 frame time from history instead of trusting copied summary values. It also
cross-checks the snapshot, requires a minimum sample count, rejects dropped or malformed data, and applies the selected
renderer/VFX counter budgets from `Config/PerformanceGates.json`.

## Timing semantics

Three measurements answer different questions:

| Measurement | Meaning | Valid as GPU execution time |
| --- | --- | --- |
| GPU frame timestamp | Device timestamp interval around GPU work | Yes |
| GPU completion latency | CPU submit until its fence is observed complete | No; includes queueing and polling cadence |
| GPU fence wait | CPU time blocked when the frames-in-flight limit is reached | No; measures back-pressure on that frame |

The maintained SDL patch adds optional native GPU timestamp queries for D3D12, Vulkan, and capable Metal devices.
The renderer publishes a device timestamp interval only after its submission fence completes. `GPU timing supported`
remains false until a valid sample exists, and remains false on unsupported devices. `GPU timing frame` identifies the
accepted frame that produced the asynchronous sample; it is not necessarily the currently captured CPU frame.
Completion latency, command recording, and CPU fence waits remain separate counters. The reference profiles still
require true timestamps; no gate threshold or timing requirement has been relaxed.

### 2026-08-16 acceptance audit

The available reference host matches `keire-win-rtx3060-i7-12700f`: NVIDIA GeForce RTX 3060, driver
`32.0.15.9597`, and 12th Gen Intel Core i7-12700F on Windows 11 x86-64. The repository is locked to SDL revision
`8e37db5e797b6167f3a00d697d816a684bd259c7`; its public GPU API exposes fence completion queries but no timestamp-query
or timestamp-frequency contract. SDL's upstream [GPU timestamp-query request](https://github.com/libsdl-org/SDL/issues/11696)
also remains open. Consequently, this host cannot produce the required `GPU timing supported = 1` counter through the
maintained backend boundary. No snapshot/history triplet was retained or labelled as passing reference evidence,
because completion latency would not satisfy the profile.

Functional rendered-output acceptance on the same host passed all 22 Debug cases on both D3D12 (2,987 assertions)
and Vulkan (3,149 assertions), including CPU/GPU VFX, shader/material readback, and the Solid/Inverted Kill Shape
differential. The local ignored evidence bundle records the raw logs, SHA-256 hashes, hardware identity, and this
timestamp limitation under `Build/TestLogs/RenderRepeat`; it is renderer correctness evidence, not performance-gate
evidence.

This Windows audit is not native macOS, Metal, or ARM64 acceptance. Those lanes require their own native hosts and raw
build, test, rendered-output, package-consumer, and player-smoke artifacts. Platform claims remain unchanged until that
evidence exists.

### 2026-09-29 implementation: maintained timestamp backend

The lock pins SDL `release-3.4.10`, commit `11a9d3212ef3063a2755982ce71a26b365cef32a`. Upstream does not supply this
query API; its [query API request](https://github.com/libsdl-org/SDL/issues/11696) was still open when checked on this
date. The first-party patch [`0001-gpu-timestamp-queries.patch`](../Patches/SDL/0001-gpu-timestamp-queries.patch) adds
an explicitly marked extension in the isolated dependency source. The tracked vendor checkout and submodule pin are
unchanged. Build/package provenance records the patch digest.

- D3D12 records timestamp query-heap writes and resolves into readback memory, converting device ticks with that
  submission queue's native frequency.
- Vulkan records query-pool reset/start/end operations, reads completed results without a wait flag, and converts
  ticks using the device period and queue-family valid-bit mask. Unsupported timestamp queues report unavailable.
- Metal checks for timestamp counters and blit-boundary sampling, samples counter buffers with barriers, and resolves
  only after completion. It calibrates GPU units against paired device CPU/GPU clock samples using
  [Apple's documented conversion](https://developer.apple.com/documentation/metal/converting-gpu-timestamps-into-cpu-time).
  Unsupported devices continue reporting unavailable; completion latency is never used as a substitute.

The private renderer owns one reusable two-slot query per accepted-frame slot (at most three). A start-marker
submission precedes uploads and offscreen surfaces; the final marker follows swapchain commands on the same device.
Swapchain acquisition and CPU recording finish before the measured submissions begin. The measurement is elapsed GPU
queue time across that frame's submissions, not a sum of individual pass busy times; queue scheduling and submission
gaps within the markers may contribute. There is no per-frame device wait for measurement.

The final frame fence owns completion. Only retirement reads its query, publishes `GpuFrameMilliseconds` and
`GpuTimingFrame`, and permits slot reuse. Invalid/unavailable results clear the supported flag, duration, and frame ID;
a new recording does not replace a valid older sample with zero. Normal teardown releases the query ring after idle.
Lost generations abandon their queries with the other unusable native handles; test-injected loss safely releases them
after its explicit idle barrier. Device recreation starts with unavailable timing until a fresh valid result arrives.

`KeireRenderTests` adds `GPU timestamp queries*` and `GPU timing publishes*` cases for native query recording rejection,
reuse, cancellation, completed results, and renderer frame attribution. Headless core coverage verifies unavailable
zeros; existing loss/recovery and post-submit failure tests exercise the extended teardown paths. Native D3D12/Vulkan,
Metal, ASan, SDK/package checks, and controlled Release captures are delegated to the coordinated validation chat.
Implementation is not evidence that the reference performance budgets pass. No new passing capture is claimed here.

## Capture metadata

Store metadata beside the snapshot and history:

```json
{
  "hardwareId": "keire-win-rtx3060-i7-12700f",
  "gpuBackend": "direct3d12",
  "gpuName": "NVIDIA GeForce RTX 3060",
  "gpuDriver": "32.0.15.9597",
  "cpuName": "12th Gen Intel(R) Core(TM) i7-12700F",
  "buildConfiguration": "Release",
  "resolution": "3440x1377",
  "workload": "sandbox-vfx-reference",
  "engineCommit": "full-commit-id"
}
```

Hardware and workload names are stable laboratory identifiers. Update a profile baseline intentionally when hardware,
driver, resolution, content, or quality settings change; do not silently compare unlike captures.

## Validate a capture

Run the validator directly:

```powershell
python Scripts/Performance/validate_capture.py `
    --profile sandbox-vfx-reference `
    --snapshot Artifacts/perf/vfx-snapshot.txt `
    --history Artifacts/perf/vfx-history.csv `
    --metadata Artifacts/perf/vfx-metadata.json
```

Or attach the capture to the normal production matrix:

```powershell
./Scripts/Windows/validate-production.ps1 `
    -PerformanceProfile sandbox-vfx-reference `
    -PerformanceSnapshot Artifacts/perf/vfx-snapshot.txt `
    -PerformanceHistory Artifacts/perf/vfx-history.csv `
    -PerformanceMetadata Artifacts/perf/vfx-metadata.json
```

```sh
bash Scripts/Unix/validate-production.sh \
    --performance-profile sandbox-vfx-reference \
    --performance-snapshot Artifacts/perf/vfx-snapshot.txt \
    --performance-history Artifacts/perf/vfx-history.csv \
    --performance-metadata Artifacts/perf/vfx-metadata.json
```

Exit code `0` passes, `1` identifies a budget failure, and `2` identifies invalid or incomparable capture input.
Production scripts always run the validator's deterministic regression suite even when no hardware capture is supplied.

## Shipped profiles

`sandbox-renderer-reference` gates the representative renderer scene. `sandbox-vfx-reference` adds live GPU-world,
particle-drop, VFX preparation, pipeline-warmup completion/cost, and VFX completion-latency requirements. Both require
300 frames, a Release or Dist build, a supported production graphics driver, no stutters, and true GPU timestamp
availability. Thresholds remain machine-readable in `Config/PerformanceGates.json`; this document intentionally does
not duplicate mutable budget numbers.

Before accepting a baseline, run the workload cold and warm, retain the raw artifacts, and record shader-cache state.
The warmup duration counter is persistent for the session so an excessive cold initialization cannot disappear from a
later steady-state capture.
D3D12, Vulkan, and Metal reference workers should each use their own reviewed profile once portable device timestamps
are implemented for that backend.
