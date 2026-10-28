# Cloud and weather implementation checkpoint

October 27, 2026. Layered clouds have natural-lit and legacy appearance modes, settings, editor controls and
clock/capture propagation. A separate experimental local cloud volume now has a depth-aware background pass,
material integration and built-in particle bindings. Early volume checks exposed soft gray bodies and dark dusk
silhouettes. After HDR and regional-weather changes, the user positively reviewed the maximized native cloud view
on October 27. Preserve that approved appearance while completing control and performance validation.
Particle overlap still awaits desktop validation; density and independent shape-detail edits have passed the fixed-view control check. Historical numerical
results below apply to the earlier layered model unless explicitly stated otherwise. This work does not close LWE-5
or LWE-6. Independent regional influence, scale and base-height variation were exercised in the native editor;
wind, final Undo/reopen, motion and player checks remain incomplete after the editor hang.

## Startup and responsiveness investigation

The October 27 generator-34/importer-47 QA session completed 33 imports with 60 cache hits,
then became unresponsive. Windows recorded AppHangB1 and event 1002; the user confirmed closing
it because it froze. This is not a spontaneous-crash diagnosis. Final Undo, reopen and wind checks
are incomplete. The current QA settings retain independent weather, full regional influence,
6 km regions and 500 m base-height variation; they are test settings, not new production defaults.

DevDist uses the Release runtime and speed optimization, with symbols enabled and link-time
optimization disabled (`Scripts/Premake/Common.lua`). The delay cannot be dismissed as an
unoptimized Debug build. Startup detects importer-version changes and defers the initial scene
while queuing project import (`EditorWorkspaceLayer.cpp`). The frozen QA build's `ResolveShader` synchronously created
all runtime material passes for a new revision (`RenderResourceCaches.cpp`). Frame admission waits
for available render slots, so a long render-side operation can block UI progress. Pipeline-start
log entries were separated by about 5–6 seconds, but those intervals do not isolate driver cost.
New shader/pipeline timing diagnostics compile successfully; runtime measurement is pending.

The working tree now schedules one material-pipeline batch at a time through the renderer job scope.
The worker owns an immutable shader snapshot and captured device formats; frame-boundary publication
checks asset revision and device generation. Resident variants rebuild together, old material bindings
are invalidated before their pipelines retire, and shutdown/recovery cancel and join the worker before
device teardown. Six standalone batch lifecycle tests pass 26 assertions, including cancellation,
partial failure, ownership transfer and abandoned-device cleanup. These tests do not yet establish
end-to-end job scheduling, stale-result rejection or native-editor responsiveness. The five initial
integration objects compiled; the final helper extraction and expanded tests await the next DevDist build.
Asset diagnostics now distinguish scan/selection, slow preparation source (validated/cache/importer),
catalog publication and total import time. No startup speedup or freeze resolution is claimed yet.

Native follow-up: the updated DevDist staged successfully in 525 seconds (49 build actions). The cached
QA project restored SandboxShowcase with 24 entities and rendered clouds; maximizing the window,
selecting Clouds and expanding Volumetric Clouds responded. A UI-triggered refresh completed with
zero imports and 93 cache hits in 3,928.687 ms: scan/selection 297.132 ms, catalog publication
2,337.869 ms. The one-asset startup warmup took 229.218 ms. Individual forward-transparent GPU
pipeline creations took 8,276.578 ms and approximately 5,066 ms on background worker threads.
These timings identify catalog publication and driver pipeline creation as separate costs; they do not
establish a cold-start speedup. The refresh completed before the next panel-expansion capture, so that
capture proves post-refresh interaction rather than sustained input during the whole import. The prior
33-import shader-upgrade workload, stale-result/cancellation integration, and long-session hang remain
unqualified. The editor was left maximized and desktop control released after this check.

Subsequent stability failure: the same QA process crashed after approximately five minutes. Windows recorded
APPCRASH/0xc0000005 in `nvwgf2umx.dll` at 09:26 on October 27. The minidump stack passes through
SDL's D3D12 graphics-resource binding and ImGui indexed drawing in `RecordSwapchain`, rather than the
background material-compilation stack. Disassembly places the failure in descriptor copying. This is not proof
of a driver defect or proof that the new worker path is unrelated. Some referenced draw/texture memory is absent
from the minidump. UI texture/descriptor ownership and cross-frame retirement require further investigation;
the successful short cached-refresh check does not close editor stability. Movement profiling has not started.

Performance work must cover cold and warm startup, version upgrades, asset dependency/cache work,
UI/toolkit layout and redraw, GPU pipeline preparation, cloud/fog frame cost and large-world streaming.
The user also reports poor FPS while moving the camera. Treat navigation as a separate acceptance gate:
capture a repeatable moving-view sequence at the same maximized viewport size, compare clouds enabled/disabled,
and distinguish application frame time, render admission/queue, CPU preparation and supported GPU timing.
Use sustained captures and frame-time distribution rather than a stationary status-bar sample; the existing
capture reports application summaries and asynchronous renderer observations, not displayed-frame cadence.
Do not close this gate based on the cached-refresh responsiveness check.
Record first usable frame, interaction stalls, import completion, CPU/GPU time and memory separately.
Background pipeline publication must preserve last-good materials, reject stale device/asset revisions,
and cancel/join owned work before device teardown. Do not replace correct rendering with dropped
passes merely to improve timing. Fog and large-world implementation remain in scope.

## Current Acceptance Summary

Use this table for current status. The dated implementation narrative below records intermediate revisions;
references there to pending batches or earlier filtering constants are historical unless repeated here.

| Requirement | Latest evidence | Remaining gate |
| --- | --- | --- |
| Cloud temporal reconstruction | The HDR-updated DevDist built and staged in 201.4 seconds, and the fixed QA scene rendered in the editor. Shared transport GPU regression passes 8,192 cases per backend. Native RGBA16F storage preserves encoded radiance over 65,536 pixels each on D3D12/Vulkan; two encoding unit tests pass 60,012 assertions. | Moving-view quality, silhouettes, memory/time cost, failure/recovery, MSAA/transparent parity and player checks remain unverified. Fixed-view rendering does not establish motion-history correctness. |
| Density authoring | Rechecked in HDR-updated DevDist: density 1 to 2 increases opacity without visibly shrinking cloud bodies; Undo and Save Environment restore the baseline. An isolated attenuation-only GPU diagnostic also passes 384,000 per-ray monotonic-opacity checks on each of D3D12 and Vulkan. The latest saved QA baseline retains the version-9 migration. | Repeat in Play Mode and the matching player. |
| Natural default appearance | The user positively reviewed the maximized native regional-weather cloud view on October 27. Earlier soft-body and repetitive-horizon findings describe intermediate builds. | Preserve the approved appearance while qualifying controls and motion. The viewed QA settings are not the production defaults; default-preset validation remains separate. Defaults remain unchanged. |
| Independent regional weather | Source generator 34/importer 47 permits clear and dense regions. Five weather unit tests pass 59,923 assertions; shader compilation and finite D3D12 image diagnostics pass. Baseline/candidate settings are saved for repeatable QA. | DevDist staged successfully in 2,056.3 seconds. Maximized native editor checks confirmed live updates and autosave for independent weather, region scale 24 km to 6 km, and base-height variation 0 to 500 m. Wind, camera travel, reopen, final Undo/save restoration and player acceptance remain open; the session ended in an application hang. |
| Ambient sky occlusion | Generator 31/importer 44 adds optional strength, legacy-zero migration and matching cached/direct evaluation. D3D12/Vulkan checks preserve opacity and monotonically darken ambient light at strengths 0, 0.5 and 1. | DevDist staging and native 0 → 0.8 → Undo/save passed; interiors darken without visible outline changes. Default appearance still fails; moving views, player and performance acceptance pending. |
| Native light-cache reuse | Submitted-key publication is integrated. Native paused/playback/paused profiler passes changed 20 → 21 → 20 with one surface and 11 draw calls; the preview changed illumination and reset correctly. Eight ASan cases cover helper lifecycle and abandonment. | Wind motion, camera-boundary continuity, multiple views, device recovery and measured performance remain open. Pass counts alone do not prove those contracts. |
| Shape starting points | Native Defined billows application, Undo and save restoration passed. Matching preset/Custom labels and full-width selection are implemented and covered by three focused tests. | Label refinement is staged but has not yet received a native selection-label check. Realistic defaults still fail; shortening variation period from 64 to 8 produced stretched shapes and was rejected. |
| Vertical body profile | Generator 25/importer 38 applies the height profile before coverage thresholding. Eight focused density cases and nine fragment shader compilations pass. DevDist and the fixed-view desktop comparison completed. | Default appearance still fails: broad soft bodies and repeated horizon bands remain. |
| View-sample comparison | In the same fixed view, changing only View Samples from 48 to 128 retained the softness and bands. Displayed frame time rose from roughly 23–25 ms to 50 ms; this is an observation, not an isolated GPU benchmark. Undo/save restored 48. | Increasing the default budget alone is not an accepted remedy. Test density structure and lighting independently before another integrated build. |
| Displacement filtering | Generator 24/importer 37 source uses a 1.4 default stretch bound; exhaustive atlas bound is 1.37352. Focused tests and cloud fragment compilation pass. | DevDist and desktop comparison completed; softness and repeated horizon bands persist. |
| Narrow-panel controls | Frequency, variation, seed and quality controls now use stacked labels and full-width fields in source. | Layout and frequency edit/Undo/save passed at about 420 px width; broader workspace coverage remains open. |
| GPU atlas sampling | Local D3D12 and Vulkan sampled readbacks match the CPU reference within the stated tolerance. | Integrated backend rendering, performance measurements and long-session testing. |
| Planetary travel and weather | Local Cartesian volume only; numerical weather foundation. | Planetary shells/clipping, large-world integration and weather consumers remain unfinished. |
| Regional coverage | Generator 26/importer 39 DevDist passes. In the narrow Environment panel, strength 0 to 1 and scale 96 km to 24 km change the rendered grouping; Undo/save restores all original values, with only the expected version-4 migration and disabled regional defaults added. Focused settings/density, metadata and layout tests and D3D12/Vulkan probes pass. | The combined shape trial improves outlines but retains repeated rows and horizon banding. Natural-default, motion/wind, player and performance acceptance remain open. |
| Atmosphere integration | ABI 5 DevDist builds and stages successfully. The regenerated QA scene renders cloud/sky transport in the editor. Finite-segment and view-transport probes pass on D3D12 and Vulkan. | Native atmosphere-only surfaces, transparent overlap, MSAA, recovery, player parity and performance remain unverified. The fixed sky view does not establish these contracts. |
| Authored particle materials | A new opaque/masked composition test failed six assertions against the old helper. Generator 28/importer 41 source now applies foreground transport for active media, preserving disabled-media identity for ordinary opaque scene draws. Six focused composition cases pass with 65 assertions. The actual emitted composition function compiles in a fragment wrapper for DXIL, SPIR-V and Metal. | Validate opaque/masked instanced particles and alpha overrides in the editor/player. Dedicated VFX graphs require transparent output; this fix also covers other instanced materials drawn as VFX. Shader compilation does not prove runtime binding or rendered overlap. |

Successful source, codec or sampling checks do not close the visual, runtime, player or performance gates.

Generator 29/importer 42 DevDist builds and stages successfully (279 native actions, 892.5 seconds including staging).
The generator-29 QA editor regenerated its materials and rendered the fixed 09:00 scene. This confirms the
non-MSAA sky path renders, but its soft bodies and horizon repetition still fail natural-default acceptance.
The new source terminates a cloud march when remaining transmission reaches 0.00001, snapping the residual to zero
in both blend passes. Three focused lighting/termination cases pass with 312 assertions, including retained thin
rays, monotonic opacity and the omitted transport bound with bright sources behind dark foreground layers.
Cloud, unlit and VFX fragment shaders plus the appearance compute probe compile to DXIL, SPIR-V and MSL.
D3D12 and Vulkan each pass 384,000 transport-parity and 384,000 density-monotonicity checks with the change.
The fixed structured view differs from the previous source by at most 0.000010014 in opacity and 0.000013292 in
linear RGB (float rounding included). Rendered integration and representative performance qualification are still
pending; the sharper density-quadrature trial below has not been adopted.

The same staged batch adds a single-march background composition path for non-MSAA views. It copies the
HDR scene into the workset-owned fullscreen input, computes radiance and RGB transmission together, and writes
the final color without blending. Multisample views still use separate attenuation/radiance draws to preserve
per-sample background colors. Seven focused composition cases pass with 110 assertions, including colored
transmission independent of cloud opacity and transparent-background alpha. Both changed renderer translation
units pass an MSVC DevDist syntax check, and the built-in fragment compiles to DXIL, SPIR-V and MSL with three
samplers. The staged runtime completes four 12-frame D3D12 runs using the existing QA cook and its atmosphere-only,
2x MSAA and 4x MSAA variants: all exit zero without error/critical/device-lost log entries. These reuse generation-28
cooked materials and exercise the updated built-in pipeline; they do not establish regenerated-material parity or
visual quality. Native images, HDR camera effects sharing the scratch texture, resize, recovery, instrumented
lifecycle checks and representative performance remain pending.

A focused MSVC AddressSanitizer runtime check also completes the 12-frame D3D12 QA scene and exits zero,
with no sanitizer stderr or error/critical log entries. Only `RenderCloudVolume.cpp`,
`RenderRuntimeUiSurface.cpp` and `RenderDeviceFrame.cpp` are instrumented; the remaining development libraries
are unchanged. Vector/string container annotations are disabled to match those libraries. This checks accesses
in the changed recording/allocation code and shared cleanup path during startup, rendering and shutdown; it
is not full-engine ASan coverage, a GPU memory checker, or evidence for resize/recovery and HDR-effect reuse.
Independent erosion is implemented; generator 28/importer 41 DevDist builds and stages successfully. Nested volume record
version 9 contains 30 fields and adds `baseHeightVariationMeters`; version 8 introduced 29 fields, adding `independentWeather`, `cloudType`, and `typeVariation` to the version-7 record. Versions 1–7 preserve the legacy weather field; independent weather is opt-in. The record includes `heightProfile` (ordered normalized base start/end and crown start/end). Versions 1–6 retain the original `[0, 0.15, 0.7, 1]` profile. Versions 1–5 migrate ambient strength to zero; versions 1–4 migrate `erosionAmount` from `detailAmount` to preserve authored appearance.
Shape Detail changes the secondary shape octave, while Cloud Erosion independently removes thin regions.
Settings, migration, density and uniform fallback tests pass: 21 cases and 91,559 assertions. The appearance probe
accepts an optional erosion value after its mode argument; omission preserves its earlier coupled behavior.
Canonical shader generation and the emitted material-composition fragment compile successfully. The appearance probe
compiles for DXIL, SPIR-V and Metal. D3D12 and Vulkan produce mean opacity 0.427089 with detail 0.7 and erosion 0.2;
their encoded images differ by at most one channel level. Both pass 384,000 zero-erosion transport checks. Omitting
the override reproduces the earlier baseline pixels exactly on D3D12. In the paused 09:00 Editor view, changing
Shape Detail from 0.2 to 0.7 leaves Cloud Erosion at 0.2 without the previous large coverage collapse.
Undo and Save Environment restore every original rendering setting, allowing only the version-5 migration
and new erosionAmount 0.2. A subsequent generator-29 desktop check changes Cloud Erosion from 0.2 to 0.7:
thin regions disappear while Cloud Density remains 1 and Shape Detail remains 0.2. Undo and Save Environment
restore the exact pretest settings file byte-for-byte. This validates the independent control, not natural appearance.

The matching DevDist tools cooked the QA project into 77 assets in one pack. Catalog validation passed, and every
cloud and atmosphere field in the cooked runtime manifest matched the authored settings exactly. The runtime
created a D3D12 device, loaded the managed assemblies and completed a hidden 12-frame run with exit code 0 and
no logged error/critical messages. This establishes cooked startup and settings propagation only; it does not
establish image parity, particle overlap, MSAA edge quality, long-session behavior or shipping-player performance.
Three isolated cooked-content variants also completed 12-frame D3D12 runs with exit code 0: atmosphere enabled
with volume clouds disabled, and Forward+ with volume clouds enabled at 2x and 4x MSAA. Their logs contain no
error/critical messages and show forward pipelines using the expected sample counts. These are native startup
and pipeline-creation smoke checks; no image readback or edge-quality acceptance is inferred from successful exit.

## Fast appearance diagnostic

`Scripts/Tests/Source/CloudAppearanceGpuProbe.cpp` renders the production `CloudVolume.hlsli` integration and the fixed
64-cubed noise atlas into a 960-by-400 offscreen image. It uses a synthetic background and fixed camera/light setup;
it does not reproduce the Editor sky, depth buffer, atmosphere, post effects, transparency or temporal behavior.
The output BMP uses the fitted ACES curve and sRGB encoding. Treat this as a way to reject poor shape/lighting
changes before a full DevDist build, never as a substitute for the Editor and player acceptance gates.

From the repository root in PowerShell, with the cached shader compiler already built:

```powershell
. ./Scripts/Windows/common.ps1
Enter-WindowsToolEnvironment ninja msc x86_64 | Out-Null
New-Item -ItemType Directory -Force Build/Validation | Out-Null
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /DNDEBUG `
  /I Vendor/SDL/include /I KeireCore/Include `
  Scripts/Tests/Source/CloudAppearanceGpuProbe.cpp `
  KeireCore/Source/Rendering/CloudNoiseUpload.cpp KeireCore/Source/Rendering/CloudNoiseVolume.cpp `
  /Fe:Build/Validation/cloud-appearance-gpu.exe /Fo:Build/Validation/ /link `
  Build/Dependencies/windows-x86_64-msc/Release/SDL/SDL3-static.lib `
  user32.lib gdi32.lib winmm.lib imm32.lib ole32.lib oleaut32.lib version.lib uuid.lib `
  advapi32.lib setupapi.lib shell32.lib
./Build/Tools/ShaderCompiler/KeireShaderCompiler.exe Scripts/Tests/CloudAppearanceProbe.hlsl `
  -s HLSL -d DXIL -t compute -e CSMain -I KeireCore/Shaders `
  -o Build/Validation/cloud-appearance-DXIL.bin
./Build/Validation/cloud-appearance-gpu.exe direct3d12 `
  Build/Validation/cloud-appearance-DXIL.bin Build/Validation/cloud-appearance-baseline.bmp
```

After a shader-only edit, repeat the last two commands. The C++ probe only needs rebuilding when its host or atlas
implementation changes. To compare Vulkan, compile with `-d SPIRV` to a different output and pass `vulkan` with that
binary. Optional positional arguments after the BMP path are density multiplier, shape detail, integer detail
frequency, coverage, ambient radiance, regional strength, regional period, core concentration, view samples and
shadow samples; defaults are `1 0.2 4 0.6 0.416 0 8 1 48 6`. Regional period must be a power of two from 1 through 64.
View samples must be an integer from 1 through 128; shadow samples must be an integer from 1 through 16.
An optional mode is `cloud` (default), `air`, `air-check` or `density-check`, followed by an optional independent
erosion amount from 0 to 1. `air` uses a fixed Earth atmosphere and the
shared cloud/air kernel; it still has no opaque or transparent scene geometry. `air-check` requires finite,
nonnegative output and agreement within 0.000001 opacity between the full and transmission-only paths; the host
returns failure for any failed sample. For example, append `2 0.7 4 0.7 0.06 1 2 0.65 96 8 air-check` after the
output BMP path. Compile the current shader when using these modes; older shaders do not implement the checks.
`density-check` compares transmission-only cloud opacity before and after tripling extinction at each pixel.
It rejects non-finite/out-of-range results or an opacity decrease greater than 0.000001. Append
`1 0.2 4 0.6 0.416 0 8 1 48 6 density-check 0.2` to reproduce the density-control regression check.
Both D3D12 and Vulkan pass all 384,000 sampled rays with the current shader. This checks opacity monotonicity
at fixed geometry and sampling, not lighting, silhouette quality, Editor bindings or player parity.
The tool rejects non-finite/out-of-range
controls and validates every returned pixel for finite radiance and bounded opacity. All output belongs under
ignored `Build/Validation`, not source control.

The same compiled host runs the atmosphere helper regression shader. After building the host above, run:

```powershell
python Scripts/Tests/test-atmosphere-medium-gpu.py --backend direct3d12 --backend vulkan
```

This compiles the helper probe for DXIL, SPIR-V and MSL, then executes the selected backends. It fails on a missing
result or any reported invariant failure. The shader encodes failures as opacity, so `mean_opacity=0` means all
384,000 sampled cases passed, not that a cloud scene is transparent. Checks cover finite/bounded density and
transmission, ground shadow, vacuum, doubled extinction and invariance when adding vacuum before the same
atmospheric chord. Finite intervals additionally check disabled/zero-length identity, planet occlusion, stopping
before ground or atmosphere entry, solar-intensity linearity and Beer-Lambert absorption against an analytic
homogeneous medium, including two adjacent intervals. The chord check failed in 96% of local probe cases before clipping solar integration to atmosphere
entry and passes after the fix on D3D12 and Vulkan. This is numerical transport evidence, not space-travel visual
acceptance. The runner uses repository-relative native arguments because absolute Unicode paths currently fail
in the shader compiler's Windows command-line input; arbitrary external Unicode paths remain unqualified.

Local evidence: D3D12 and Vulkan baseline images differ by at most one encoded channel value out of 255, and the
D3D12 ASan run passes. Zero optical density yields zero mean opacity. Density 3 increases mean opacity from about
0.425 to 0.545. Detail 1 reduces it to about 0.064 while breaking up the outline. Lower ambient radiance reveals
more shading without fixing the broad soft structure. These are controlled diagnostic observations, not approved
defaults or a performance benchmark. The production fragment bindings retain their default register space; only
the compute diagnostic overrides the cloud resource space.

The regional-coverage path preserves the disabled baseline image exactly in the local D3D12 comparison. Its enabled
trial (`1 0.7 4 0.7 0.416 1 8 0.35`) renders on D3D12 and Vulkan with a maximum encoded-channel difference of 2/255;
this trial is not an approved preset. Regional modulation is bounded by the smaller distance to empty or full
coverage, preserving both endpoints and monotonic coverage adjustment. It samples a horizontal noise slice and
does not stretch the body coordinates. The record keeps regional variation off by default until integrated visual
acceptance. Trials that increased vertical frequency or shortened displacement scale did not produce acceptable
defaults; the latter produced elongated shapes. Ambient-shadowing trials added shading but remain outside the
production path pending a quality/cost design.

An independent-erosion appearance trial (`3 0.7 4 0.7 0.06 1 2 0.65 48 6 air 0.65`) produces more distinct
cloud bodies than the soft default, but exposes striped edges and repeated distant rows. Raising only shadow
samples from 6 to 16 leaves opacity unchanged at 0.465742 and does not remove the stripes. Raising only view
samples from 48 to 128 reduces the visible stripes, with mean opacity 0.484650, while distant repetition remains.
An isolated quadratic view-spacing prototype at 48 samples did not remove the artifacts and was not adopted.
These comparisons prioritize view-march sampling/reconstruction for the edge artifacts; they do not justify
raising default sample counts or establish a production preset. The prototype and images remain ignored diagnostics.

A follow-up diagnostic averages 16 evenly spaced ray-sample phases at the same 48-view/6-shadow budget per phase.
Mean opacity stays near the single-phase result (0.465768), and visible artifacts remain. Disabling the shape/detail
mip selection and detail-band fading in that accumulated diagnostic reduces more of the striping but changes mean
opacity to 0.487223. This implicates sampling-dependent filtering as well as quadrature; neither disabling filtering
nor adding jitter alone is an accepted fix. Existing scene TAA consumes opaque motion vectors and does not provide
separate cloud-depth/wind reprojection, so cloud history needs an explicit validity and motion contract before reuse.

A bounded density-quadrature diagnostic now provides a more accurate comparison without disabling mip filtering.
Each view and shadow interval samples four subinterval midpoints, evaluates the nonlinear density at each point,
and averages those densities before transport. Lighting and atmospheric integration still run once per view
interval. At the fixed independent-erosion trial above, the stripes are visibly reduced. A denser comparison uses
128 view intervals and 16 density subintervals per view/shadow interval; it is a convergence reference for this
field, not a physical ground truth or a production configuration.

| Diagnostic at 48 view / 6 shadow intervals | Mean opacity | Mean absolute opacity difference from dense reference | Median dispatch-to-readback-fence time |
| --- | --- | --- | --- |
| Current production kernel | 0.465742 | 0.025038 | 3.99 ms |
| Two density subintervals for view and shadow | 0.481887 | 0.006606 | 6.41 ms |
| Four density subintervals for view and shadow | 0.486524 | 0.001284 | 9.43 ms |
| Four density subintervals for view only | 0.486524 | 0.001284 | 6.18 ms |

The reference mean opacity is 0.487436. Giving only the view ray extra density samples retains the opacity
improvement, but its mean cloud-pixel linear RGB difference from the reference is 0.019990, versus 0.003633 when
both view and shadow intervals use four. Two-point Gauss nodes did not improve the two-midpoint result. Timings
are three local D3D12 runs at 960 by 400, measured on the CPU from command recording through dispatch and readback
fence completion after waiting for prior work. They include submission/readback overhead and are not isolated GPU
timings, Editor frame times or shipping performance claims.

The four-subinterval trial compiles to DXIL, SPIR-V and MSL. D3D12 and Vulkan produce the same reported mean opacity;
their maximum/mean linear RGBA differences are 0.001899/0.000000362. Its D3D12 density monotonicity diagnostic also
passes all 384,000 rays. These results qualify only the isolated trial. It remains under ignored
`Build/Validation/CloudDensityQuadratureTrial`; the extra cost is not acceptable as an unqualified default change.
The next rendering step is to retain this coverage accuracy with less repeated density/shadow work, then test
camera motion, wind and native scene integration. Distant repetition and natural-default acceptance remain open.

A follow-up reused the center weather and warp samples within each four-subinterval group. It increased opacity
error against the dense reference to 0.001744 without a demonstrated speedup, so it was rejected. Warmed-pipeline
measurements now reuse one pipeline and resources for 12 dispatches and discard the first two. Under the same
CPU dispatch-to-readback-fence timing method, median times were 4.77 ms for the original kernel, 3.86 ms with the
opaque-ray exit, 9.88 ms for the full four-subinterval trial, 9.05 ms for that trial with the exit, and 10.88 ms for
shared weather/warp samples. Ranges overlapped and other desktop GPU work was not controlled. These observations
support testing the conservative early exit, not a general frame-rate claim or acceptance of fourfold sampling.

An additional conservative empty-density test skips the detail sample only when its maximum possible contribution
cannot cross the coverage threshold. Both the current kernel and four-subinterval trial produce bit-identical
structured-view readbacks with this test, but warmed timing differences were too small to establish a benefit;
it remains an ignored prototype. A larger-period procedural-gradient body costs more and retains soft structure.
A 128-cubed atlas with doubled cell counts and a doubled physical repeat period also retains soft bodies and
striped distant edges in the inspected views. Neither repetition trial is adopted. Their source and images remain
under ignored `Build/Validation/CloudProceduralBody` and `Build/Validation/CloudLargerAtlas`; the production atlas
still uses its original 64-cubed contract and coordinate wrapping.

The generator 26 desktop comparison completed after DevDist and QA material reimport. Regional strength and scale
update the rendered scene, and the scale menu remains readable in the approximately 420-pixel Environment panel.
The combined trial uses coverage 0.7, shape detail 0.7, core concentration 0.35 and regional strength 1 at a 96 km
repeat distance. It gives more defined outlines than the baseline but retains repeated cloud rows and horizontal
banding near the horizon. Do not adopt it as a natural default. Undo and Save Environment restored every original
rendering setting; the saved volume record migrated from version 3 to 4 with regional strength 0 and period 8.
This fixed-view control check does not establish camera-motion, wind, player, GPU-cost or long-session acceptance.

The probe's quality comparison holds the combined trial fixed while changing view/shadow samples from 48/6 to
128/6 and 128/16. Mean opacity rises from 0.382282 to 0.400560 when the view budget increases; changing only the
shadow budget leaves opacity unchanged, as expected. More distant structure appears at the higher view budget,
but the repeated distribution remains. D3D12 and Vulkan both execute the 128/16 case successfully. These images
do not justify raising default budgets or claim that the Editor's visible banding is solved. Invalid zero,
out-of-range, fractional and non-finite sample counts reject before creating a GPU device.

## Files and contracts

| File | Contract |
| --- | --- |
| `KeireCore/Include/Keire/Rendering/Cloud.h` | Two optional artistic sheets plus separate volume settings; disabled defaults and value comparison. |
| `KeireCore/Include/KeireInternal/Environment/CloudEvaluation.h` | Strict validation, signed-time advection, periodic CPU density reference. |
| `KeireCore/Include/KeireInternal/Environment/CloudRecord.h` | Strict authoring JSON: default volume settings retain sheet-only version 2; customized volumes use version 3. Versions 1/2 migrate with volume disabled, and version 1 preserves legacy sheet appearance. Excludes runtime/preview phase. |
| `KeireCore/Include/KeireInternal/Rendering/CloudUniforms.h` | Validated 128-byte block, two 64-byte sheets including appearance/seed; elapsed and wrapped-phase overloads. |
| `KeireCore/Shaders/CloudLayered.hlsl` | Reusable premultiplied linear artistic-sheet evaluation. |
| `KeireCore/Include/Keire/Rendering/Weather.h` | Unit-documented weather and gust values. |
| `KeireCore/Include/KeireInternal/Environment/WeatherEvaluation.h` | Checked weather blending and stateless seeded periodic gust. |
| `KeireTests/Source/CloudTests.cpp` | Numerical, packet, phase and strict codec regressions. |
| `KeireTests/Source/CloudRenderSettingsTests.cpp` | Real settings save/load, legacy migration, malformed input and failed-save preservation. |
| `KeireTests/Source/WeatherTests.cpp` | Validation, endpoint blends and deterministic wind regressions. |

These numerical components are header-only. Only the private record needs nlohmann/json. Cloud values are exported
through `Keire/Core.h`; there is no managed weather/cloud query service yet.

Existing-file integration completed by this workstream:

- `RenderSystem.h` owns `CloudSettings Clouds` alongside Atmosphere and Bloom. The subsequent bloom integration
  introduced rendering schema 7, followed by stars in current schema 8. `RenderSettings.cpp` validates and serializes `clouds`; schema 6 and later
  require a valid cloud record, while schema 1–5 migrate to disabled defaults. Malformed records are not discarded.
- `RenderShaderDataInternal.h` appends CloudUniforms, evaluated ambient/sun lighting and star settings to SkyUniforms;
  the current total is 528 bytes (132 float-sized words, including integer fields and the artistic sky gradient).
  Volume views use a separate
  416-byte block shared by background and current surface evaluation. Legacy material ABI 1/2 retains the
  original 272-byte prefix; ABI 3 retains 288 bytes and ABI 4 retains 304 bytes. ABI 5 appends atmosphere transport.
- `RenderSceneRecording.cpp` sky branch draws clouds even without an environment texture or atmosphere, uses a
  white bound fallback texture with a negative-layout sentinel selecting a black background, and builds cloud
  uniforms from the view observer and `packet.CloudAdvectionPhase`. Disabled clouds skip cloud coordinate conversion.
  Observers outside the bounded local cloud domain suppress clouds and retain the existing sky; a clouds-only view
  submits no sky draw in that fallback. This avoids a render-thread domain exception, but no live quality-fallback
  diagnostics UI exists yet.
- `BuiltinSky.hlsl` keeps the existing atmosphere calculation and environment rotation. It composites independent
  sheets far to near before exposure, keeps clouds stationary when only the environment texture rotates, clips
  sheets behind the enabled atmosphere's planet intersection, and preserves finite RGBA16F storage for the
  atmosphere/cloud path. The disabled legacy texture-only path retains its prior output range.
- Windows/Unix built-in rendering and VFX shader scripts fingerprint their shared cloud includes; Windows stages
  includes beside temporary sources. Generated headers are build outputs and are not edited directly.

## Coordinate, time and history boundary

The integrated renderer is explicitly a bounded Cartesian mode: identity east/up/north basis, world-zero pattern
anchor, Y-up horizontal sheets. Camera/view positions are the existing local renderer positions, not qualified
universe coordinates. This does not implement planetary cloud geometry or a large-world render-origin conversion.
Compute high-precision observer-minus-pattern-anchor differences before upload when those systems are integrated;
never narrow absolute universe coordinates or move the procedural anchor with each camera.

The wrapped-phase overload accepts two X/Z positive wind-displacement phases, each in [0,1). It wraps observer X/Z,
subtracts the provided advection phase, and narrows only bounded values. Different views sample the same immutable
clock/phase source independently. Layer wind affects phase only through the owner/capture evaluation contract.

The main coordinator owns `EnvironmentLightingSnapshot` clock sampling, `EnvironmentCloudPhase.h`, capture and
`SceneRenderPacket::CloudAdvectionPhase`. Its supplied contract evaluates constant authored wind against integer day
plus bounded day fraction with modular arithmetic; no absolute `day * secondsPerDay` flattening. No snapshot means
zero phase. Capture and integration regressions passed for that field; visible cloud-motion acceptance remains open.
Profile edits and seeks participate in the
existing accepted-frame history transaction through source lifetime/revision and cloud/frame settings identity.

The older elapsed overload remains for focused numerical callers: explicit simulation seconds within +/-1e9 from
a stable epoch, constant wind, supports reverse scrubbing. Do not multiply changing instantaneous gust wind by elapsed
time: a changing wind requires integrated displacement and a save/replay contract. No mutable global cloud clock exists.

## Units and protective bounds

| Cloud input | Range |
| --- | --- |
| Anchor-datum altitude | -10,000 to 100,000 m |
| Pattern repeat size | 100 to 1,000,000 m |
| Coverage / opacity | 0 to 1 |
| Pattern edge softness | 0.001 to 1 |
| Horizon fade cosine magnitude | 0.01 to 1 |
| X/Z wind component | -1,000 to 1,000 m/s |
| Linear RGB component | 0 to 16 |
| Anchor-relative observer component | +/-1e9 m |

Validation rejects nonfinite inputs even in disabled authored layers. These limits protect a local model; they are
not measured guarantees of visual precision over the complete domain. At a coplanar observer the sheet is invisible.
The shader bounds intersections to 100 km with an 80-100 km fade and rejects near-tangent directions.

Weather rain and liquid-water-equivalent snow rates are mm/hour [0,1000], temperature Celsius [-273.15,100], and
coverage/wetness/snow hooks [0,1]. Base wind is east/up/north m/s, each component [-200,200]. Gust amplitude [0,200]
adds a rotating horizontal vector, so effective components can reach +/-400. Period [0.1,86400] seconds and explicit
evaluation time +/-3,155,760,000 seconds are checked. Seeds select phase without random state. Standard-library trig
is repeatable on one implementation, not guaranteed bit-identical across platforms.

Weather blending rejects malformed endpoints even at weights zero/one and preserves exact valid endpoints. It blends
wind vectors rather than angles. Surface values are authored query hooks, not accumulated rain/snow or mutation of
material assets. The optional failure results must not be silently replaced with valid defaults.

## Historical Layered-Cloud Validation

Outputs remain confined to ignored `Build/Validation/CloudWeather/`.

- Isolated MSVC C++20 Release `/DNDEBUG /O2 /W4 /WX`: the final real settings/numerical suite passed
  **14 cases / 986 assertions**, including wrapped phase and observer-domain checks.
- Actual `RenderSceneRecording.cpp` compiled independently with the coordinator's packet field and the updated
  shader layout; a final MSVC C++20 `/Zs` check passed after adding the domain fallback. Outputs remain in the
  CloudWeather validation directory; no shared build object was replaced.
- Actual integrated `BuiltinSky.hlsl` vertex and fragment entrypoints compiled to DXIL, SPIR-V and MSL through the
  repository cached shadercross. This checks the real shared sky shader, not only a wrapper. It is compilation
  evidence, not GPU execution or backend acceptance.
- C++ clang-format and dry-run checks cover the touched C++ sources; `git diff --check` is part of handoff.
- The Windows shader script parser check passed. Unix script execution is not established on this Windows host.

The real settings suite links RenderSettings.cpp, Asset.cpp and FileSystem.cpp with local doctest main, the three
Cloud/Weather test files, `KEIRE_STATIC` and `NOMINMAX`; no shared engine build or packaging is necessary.
No new resource ownership or concurrent service was introduced, so an ASan ownership run is not required for this
stateless subset. Main coordinator retains full-build, capture, settings compatibility, editor and desktop testing.

## Integration evidence and remaining acceptance

1. Clock snapshot, modular phase, capture and accepted-history invalidation are integrated. Release/ASan capture
   tests passed 1 case / 114 assertions before the natural-lit extension; rerun when its ABI changes land.
2. AssetTool/runtime manifest 7/player preserve authored atmosphere and cloud records. The 77-asset QA cook matched
   authored fields and a bounded headless player run passed. This does not establish rendered standalone quality.
3. Controls use document edit transactions. Existing settings regressions passed 80 cases / 3128 assertions in both
   Release and ASan before the appearance extension. New appearance/seed migration tests require a fresh run.
4. Build DevDist and inspect Scene, Edit Game, camera preview, Play and rendered player. Test disabled legacy output,
   clouds-only background, atmosphere overlap, observers below/above/between sheets, camera motion, time freeze/scrub,
   reversed wind, source replacement and multiple views. Verify no sky output leaks through foreground scene depth.
5. Resolve regional priority before weather blending; future consumers must share one frame-bound evaluated result.
   An explicit policy can map weather coverage/base X/Z wind to selected cloud layers; no automatic overwrite exists.

## Remaining production limits

Legacy unlit retains its cheap trigonometric pattern and fixed color. Natural lit introduces seeded periodic detail
and evaluated ambient/selected-sun shading; it is still an artistic sheet, not a physical cloud volume. The new mode
requires visual qualification at dawn/noon/dusk/night and under camera motion before any quality claim.
Layered sheets have no editable texture, physical sun/moon scattering, self-shadow, ground cloud shadow, thickness
or interior representation. The separate local volume provides thickness and sampled self-shadowing; it does not
make these sheets volumetric. Atmosphere planet clipping does not make flat sheets spherical.
The layered sky is background composition. `CreateSkyPipeline` binds a depth attachment but
does not enable depth testing, and `BuiltinSky.hlsl` does not sample scene depth. Merely binding that attachment
does not establish cloud/geometry occlusion. Forward rendering draws the sky before opaque geometry; deferred
recording draws it before deferred lighting, whose background discard preserves the sky. This ordering is distinct
from fogging geometry or placing a cloud volume in front of it. Depth-aware cloud/transparent-object interaction needs integrated
acceptance; the separate volume implementation is described below. These sheets must remain labeled layered.

### Volumetric integration requirements

The current frame graph and command recorder draw a cloud-volume pass after opaque indirect lighting and before
transparency. It samples opaque depth, uses sample-count-matched HDR targets and preserves multisample color for
the later transparency resolve. The shared kernel integrates a bounded horizontal slab, with view and light samples,
extinction and scattering. Cloud radiance follows the renderer's pre-exposed HDR convention; opacity is not exposed.
Generated material shaders and built-in particle shaders evaluate the same kernel to their own fragment depths.
See [the volume shader interface](Rendering.md#experimental-cloud-volume-shader-interface) for layouts and blend modes.
These source integrations do not prove correct GPU behavior across all cases. Remaining qualification includes:

- Reconstruct view rays and opaque termination distances from the active camera and sampled depth, including
  background pixels, projection variants, render scale and multisampling. Clip marches against planet geometry.
- Verify accumulated HDR radiance and transmittance composition before tone mapping, including matching exposure
  between the background and transparent surfaces; do not replace transport with a sky tint or opacity slider.
- Define transparent-surface integration explicitly. Applying opaque-depth fog after all transparency incorrectly
  uses the opaque distance for nearer glass and particles; applying it only before transparency leaves those
  surfaces unfogged. Transparent shaders need an appropriate depth-indexed volume evaluation or equivalent method.
- Keep view/light sample counts and volume resolution bounded. Temporal reconstruction needs per-view owned history
  and rejection for camera cuts, settings changes, time jumps, resolution changes and reference-frame changes.
- Verify empty-volume identity, opaque occlusion, near/far transparent objects, cameras inside clouds, horizon and
  planet intersections, HDR/bloom interaction, and Edit/Play/player parity. Measure GPU time and memory on each
  supported backend before assigning production quality labels.

The latest Windows DevDist Edit-mode check confirmed that multiple-scattering contribution changes cloud brightness
and numeric edits commit successfully. It did not meet the visual-quality target: the default clouds appeared soft
and banded. The density kernel used for that comparison combined one smooth value-noise shape with one four-times-frequency erosion
sample and a shared vertical envelope. The integrator uses equally spaced midpoint samples over the clipped ray span.
At a 96 km span and the default 48 view samples, each step is 2 km, while the default 3 km pattern's erosion cell is
750 m. This arithmetic identifies a potential undersampling case, not proof of the cause of every visible band.
Before increasing noise detail, compare fixed-camera renders across sample budgets and ray angles; added high-frequency
detail without adequate sampling or reconstruction can make aliasing worse. Preserve a fixed seed, paused environment
clock, exposure and render scale for those comparisons, and record GPU cost separately from the Editor footer.

The isolated Windows DevDist comparison now covers both a shallow horizon view and a camera pitched 30 degrees upward.
Layered clouds and the showcase VFX were disabled; the clock was paused at 09:00, wind was zero, exposure and render
scale were one, and dynamic resolution was disabled. At both 48 and 128 view samples, the upward view retained broad,
soft rounded patches with little fine structure. This is qualitative evidence that increasing the view budget alone
does not resolve the current appearance; it does not establish convergence, eliminate horizon aliasing, or measure GPU
cost. The pass currently draws directly into the active viewport-sized HDR target, without a separate reduced-resolution
cloud buffer. The QA fixture was restored to 48 view samples and six shadow samples after comparison.

The preceding source revision added a centered secondary shape octave and two erosion bands. Added bands faded toward
their mean as view or shadow march spacing becomes too coarse; fully unresolved bands skip their noise evaluations.
The base shape remains unfiltered, and this approximation is not spatial/temporal reconstruction or a guarantee of
sampling convergence. It changes experimental cloud appearance without changing serialized settings. Shader generation
and importer versions invalidate older generated materials. The subsequent Windows DevDist check used the same
30-degree upward camera and 48-sample paused fixture after material reimport completed. Some cloud contours changed,
but broad soft patches remained: the revision did not meet the visual-quality target. GPU cost was not measured;
the Editor's frame-time footer is not a cloud-pass timing measurement. The earlier 48/128 comparisons describe the
preceding implementation. More substantial shape/erosion work and performance qualification remain necessary.

An internal CPU noise-volume generator now provides a bounded foundation for sampled density. It packs combined
gradient/cellular shape and three cellular erosion frequencies into linear RGBA8, with repeating coordinates and a
complete mip chain. Dimensions are powers of two from 4 through 64. The 64-cubed chain occupies 1,198,372 bytes;
one local optimized CPU measurement took approximately 580 ms. This is initialization work, not a per-frame budget
or a GPU performance result. Release and AddressSanitizer tests cover repeatability, periodic seams, seed variation,
every generated mip texel's averaging, and invalid dimensions/coordinates. A GPU upload helper and lazy renderer cache
now exist. The opt-in `Scripts/Tests/Source/CloudNoiseGpuProbe.cpp` validates malformed-chain rejection and uploads/downloads
every mip of 4-cubed and 64-cubed volumes. Local Direct3D 12 and Vulkan runs matched every byte; the Direct3D 12 probe
also passed with AddressSanitizer. These checks cover texture transfer, not shader sampling, renderer recovery or visual
quality. The current source connects the shared density kernel to this atlas, with explicit mip selection from view
and shadow march spacing and seed-dependent coordinate translation. Background, built-in surfaces, particles and
generated material ABI 2 use the same atlas. Nine built-in fragment targets compile across DXIL, SPIR-V and MSL;
the integrated Windows DevDist build subsequently passed. Desktop testing exposed and repaired a missing fallback
mesh-particle sampler declaration/binding and a generated lit-material texture/Forward+ register collision. After
the repairs, the QA import reported 24 imported assets and 66 cache hits without failed states, and the default-lit
warmup succeeded. The same paused 09:00 upward camera rendered broad, soft overcast forms with little convincing
fine structure. This still does not meet the visual target. GPU cost, transparent overlap, motion, interior views,
recovery, player parity and other backend rendering remain acceptance gates.
An additional CPU distribution check found that the initial atlas's positive shape combination gave nonzero interior
density to 99.9% of its base texels at default coverage 0.6 and detail 0.2. The current generator removes that upward
bias before quantization and mip averaging; the same seed-1, 64-cubed check now reports 57.6%. A regression checks
that multiple seeds retain substantial empty and occupied regions. These are voxel statistics, not projected sky
coverage or visual acceptance. The matched paused 09:00 editor view now shows clear blue-sky gaps between cloud
forms, confirming an improvement over the near-solid overcast. Edges and internal structure remain too soft for
visual acceptance. The Profiler reported an available GPU timestamp of approximately 12.8 ms for one full frame
in this view; this is neither an isolated cloud cost nor a sustained performance result.
The approach is informed by the
[Guerrilla cloud rendering presentation](https://advances.realtimerendering.com/s2015/The%20Real-time%20Volumetric%20Cloudscapes%20of%20Horizon%20-%20Zero%20Dawn%20-%20ARTR.pdf);
its published performance is not an acceptance result for this implementation.

The next density revision samples erosion at four times the body frequency, increases its explicit mip level with
that frequency, and fades back to coarse erosion when march spacing cannot resolve the detail. Zero detail skips
the extra sample. Focused scalar tests and builtin fragment compilation cover its numerical and shader contracts;
the matched paused 09:00 editor comparison after successful material reimport still showed broad, overly soft
forms without a convincing fine-detail improvement. This revision has not passed visual acceptance. No sustained
GPU comparison was collected for it; its extra sampling cost remains unqualified. Diagnose the sampled density field
and filtering before adding more lighting or noise complexity.
A controlled UI comparison reduced pattern size from 3,000 m to 1,000 m while retaining thickness, time, camera,
coverage and extinction. Forms became smaller but remained soft, and conspicuous repeated rows appeared near the
horizon. Restoring 3,000 m restored the baseline, with the settings file hash unchanged. Scale alone is not a quality
fix; short-period atlas repetition is an additional acceptance defect, requiring spatial variation without seams or
wind discontinuities. This check did not measure GPU cost.
`Scripts/Tests/Source/CloudNoiseSamplingProbe.cpp` provides a CPU reference for repeat-addressed trilinear sampling of
the generated mip chain, including negative coordinates and seam continuity. Its seed-1 run measured base-level
one-period repeat error below 1e-12 and a mean quarter-period difference of about 0.1135, falling to about 0.0033
at the two-texel mip. Compile it with `CloudNoiseVolume.cpp` and the `KeireCore/Include` and `Scripts/Tests/Include`
include directories; it needs
no GPU or full Editor build. This diagnoses the sampled data and addressing model, not actual GPU sampling or
rendered quality. Larger-scale variation must preserve continuity across the host's wrapped wind/origin domain.
The initial variation candidate added bounded smooth coordinate displacement sampled over sixteen atlas periods. This divides
the host's sixty-four-atlas-period wrapping domain, avoiding a discontinuity when its origin/wind phase wraps.
The CPU probe measures an adjacent-tile mean difference of about 0.1451 and host-wrap error below 1e-11 for seed 1;
shared scalar tests and builtin shader compilation pass. Integrated appearance, distortion, aliasing under motion,
generated-material execution and the additional texture-sample cost remain unverified. The staged Editor still
contains the preceding revision until the batch is rebuilt.
A subsequent finite-difference audit over 4,096 positions measured axis stretch from about 0.292 to 1.751 for this
candidate. The current mip calculation still assumes unwarped spacing, so the candidate needs a displacement-aware
filtering bound or reduced distortion before integrated acceptance. These sampled extrema are diagnostics, not
global derivative bounds, and passing periodicity checks does not establish alias-free sampling.
The revised source batch lengthens the variation period to sixty-four atlas periods and uses twice the original
ray spacing for body/detail mip selection and detail-band fading. An exhaustive fixed-atlas test bounds every
GBA edge derivative at every mip; convex trilinear/mip interpolation and a Frobenius-norm bound establish stretch
below two in any ray direction for that atlas. A changed atlas must continue passing this regression. The diagnostic
now observes axis stretch about 0.825–1.184, adjacent-tile mean difference about 0.0952 and host-wrap error below
1e-11. Release tests and nine builtin fragment compilations pass. This addresses the identified displacement/LOD
mismatch; it does not prove pixel-footprint filtering, temporal stability, rendered quality or GPU cost. No new
DevDist stage has been made for this batch yet.
The same pending batch remaps the surviving density interval after erosion, preserving a fully dense core while
removing thinner regions. Full erosion still produces vacuum, and erosion never increases density above the
uneroded body. The shared density suite passes six cases with 10,086 assertions; nine builtin fragment compilations
pass. Rendered contrast remains unverified. Independent authoring controls and natural-looking presets are required
before accepting these fixed internal variation/detail choices as the requested customizable cloud system.
The pending authoring contract adds detail frequency, variation strength/period and density contrast in nested
volumetric record version 3. Versions 1/2 migrate with explicit current defaults, and malformed/future records reject.
Integer detail frequencies and power-of-two variation periods preserve the current wrapped coordinate domain.
Focused codec/validation tests pass. Shared authoring controls, appended uniform packing and shader consumption are
connected in source. Material volume ABI 3 uses a 288-byte view block while preserving the 272-byte prefix for older
materials. The DevDist build and QA material reimport passed. Desktop testing confirmed density contrast and
variation scale affect the rendered scene, and Undo plus Save restored their defaults. However, the fixed 09:00
view still showed broad blurred shapes and repeated horizon bands. Contrast 3 thinned the clouds; reducing variation
scale from 768 km to 192 km changed their placement without resolving the soft structure. This is a failed visual
acceptance result, not a completed repetition fix. Camera-motion, cooked/player and backend acceptance remain open.

The follow-up sampling probe compares 416 RGBA samples at integer and fractional mip levels against the CPU
trilinear repeat-addressing reference, including negative coordinates. Local Direct3D 12 and Vulkan both passed
with maximum channel error 0.002065 (tolerance 0.004); the Direct3D 12 ASan run also passed. This establishes sampled
atlas agreement for those cases, not integrated cloud quality. The pending shader change reuses the detail sample's
red channel for secondary body structure; the prior kernel passed zero body-detail weight. The UI now distinguishes
optical Cloud Density from Core Concentration, which intentionally removes thin edges. The generator 23/importer 36
DevDist build and material reimport passed. In the isolated desktop comparison, increasing Cloud Density from 1 to 3
made the same bodies more opaque and darkened their undersides without shrinking them; coverage and shape controls
were held fixed. Undo and Save Environment restored the baseline settings exactly. Overall appearance still fails
acceptance: the default remains overly soft with repetitive horizon bands, despite the secondary body detail.

The next source revision tightens the displacement filtering bound from 2.0 to 1.4 at default settings.
An exhaustive scan of wrapped texel edges across every mip bounds the fixed atlas's displacement stretch at
1.37352; the shader retains margin and scales the bound with variation strength and inverse period. The combined
atlas and density tests pass 12 cases with 12,784 assertions, and the cloud fragment compiles for DXIL, SPIR-V and
MSL. This is filtering-contract evidence only: the generator 24/importer 37 change still needs a staged Editor
comparison and does not establish that softness or horizon repetition is resolved.

Further density work needs controlled shape and erosion detail with sampling-aware filtering, rather than simply
adding high-frequency noise to every view and shadow sample. Keep coverage, thickness and extinction independently
meaningful; test empty/full coverage, volume boundaries and continuity under wind. Compare the same fixed views before
and after the revision, then test camera motion and cloud interiors. A sampled noise-volume implementation must include
resource lifetime, cache invalidation, backend texture support and transparent-surface consistency, with GPU measurements
before changing default quality budgets. These are implementation and acceptance requirements, not completed features.

Volumetrics still need planet-aware shells, large-world-stable noise coordinates, weather maps,
ground shadows, temporal reconstruction and history, capability/budget fallback and qualified near/far transitions.
The earlier cloud-only renderer supplied unattenuated ambient/sun colors and omitted clear-air transport between
the observer and cloud samples. The ABI 5 source revision below addresses that path; native and visual acceptance
remain pending. Validation must cover below/inside/above-cloud views, surface depth ordering and the disabled
atmosphere path. Planet clipping is implemented in the shared kernel, but is not a substitute for curved cloud shells.
Multiplicative material compensation and legacy custom shaders remain integration gaps. The current volume control
must retain its experimental label until visual, backend and performance acceptance is established.

The sky's sphere intersection, medium density, extinction, solar transmission and finite view integration now live in
`KeireCore/Shaders/AtmosphereMedium.hlsli`. The helper has no bound resources or pixel derivatives, allowing later
cloud and compute callers to share the same kilometre-based transport. `IntegrateAtmosphereMedium` returns RGB
in-scattered radiance and RGB transmission up to a caller-provided distance, clipped to atmosphere and planet.
The sky calls it for the full visible atmosphere and adds the filtered solar disc separately. The finite helper
does not add a disc, background or exposure. The cloud/air kernel and ABI 5 view bindings described below consume
it. Sky fragments compile for DXIL, SPIR-V and MSL; a diagnostic GPU
probe passes 384,000 finite/bounded, vacuum, ground-shadow and extinction-composition checks on both D3D12 and
Vulkan. These checks do not establish sky image parity, cloud aerial perspective or visual acceptance.

Cloud composition must account for the clear-air radiance already in the sky, rather than simply adding a second
air integral over it. The current source uses depth-limited RGB transport for background, opaque geometry and
transparent surfaces, including the atmosphere-disabled path. Runtime validation of those paths is still required;
the offscreen sky-only prototype cannot establish it.

A local offscreen prototype now evaluates clear-air radiance to each cloud sample and attenuates cloud lighting
by both view and solar transmission. For the fixed diagnostic view, this softens the conspicuous distant dark
bands without changing cloud opacity (mean 0.422319 before and after). D3D12 and Vulkan images differ by at most
1/255 per encoded RGB channel; zero cloud density returns the clear sky with zero cloud opacity. This prototype
uses the shared production atmosphere helper but remains under ignored `Build/Validation/AirCloudTrial20261027`.
It handles only a clear-sky background, uses fixed atmosphere parameters and repeatedly integrates overlapping
prefixes. It is not an accepted engine implementation, performance result or solution to repeated cloud shapes.

An alternative four-offset noise-blending prototype remains rejected: default-view opacity rose from 0.424573
to 0.584935, broad softness remained, and the horizon bands persisted despite extra texture samples. Its images
are under ignored `Build/Validation/RepetitionTrial20261027`; no production noise or default settings changed.

The ABI 5 source integration avoids a scene-color copy by multiplying the existing HDR target by RGB transmission,
then adding in-scattered radiance in a second draw within the same render pass.
For sky pixels, whose clear-air scattering is already rendered, only the cloud correction should be added.
For opaque geometry, it includes the clear-air integral to the actual surface, even beyond the cloud distance cap.
Current built-in and generated transparent shaders evaluate the same transport to their own depth. The air prefix
advances through the cloud march instead of restarting at the camera. Native build, runtime composition, recovery,
MSAA and cost qualification remain required before acceptance.

The shared `IntegrateCloudVolumeWithAtmosphere` kernel now implements the cloud correction with an incremental
air prefix, solar attenuation and planet clipping. It advances through empty cloud intervals too. Its
transmission-only path skips lighting and shadow work. The legacy `IntegrateCloudVolume` entry point still disables
atmosphere explicitly; ABI 5 view evaluation selects the combined path. Generator 27/importer 40 regenerates
materials with the new layout. Existing ABI 0–4 binaries retain their layouts and cannot supply the new transparent
atmosphere behavior until rebuilt.

The incremental kernel's D3D12 and Vulkan diagnostic passes 384,000 full/transmission-path comparisons. D3D12
initially disagreed beyond the 0.000001 tolerance; precise density-march calculations fixed that regression.
The atmosphere-disabled default image differs from its earlier reference by at most 1/255 per encoded RGB
channel. Built-in shader generation, nine VFX fragment compilations and the embedded material source compile
successfully across DXIL, SPIR-V and MSL. These are shader and offscreen checks; no new Editor, player or GPU-cost
acceptance is claimed for this kernel.

`Scripts/Tests/CloudViewTransportProbe.hlsl` exercises the actual ABI 5 view entry point with clouds disabled.
Its 384,000 cases pass on D3D12 and Vulkan: opaque depth attenuates even with a zero cloud-distance cap, sky is
not attenuated twice, disabled atmosphere is identity, and full/transmission-only results agree. Compile it with
the same compute command as the appearance probe, then run the host in `air-check` mode. Layout tests preserve
all four older view prefixes and verify the 416-byte current view; metadata tests accept ABI 5 and reject ABI 6.
These checks do not execute native scene draw recording or qualify transparent overlap and MSAA.

Regional volumes, seasons/altitude rules, procedural schedules, weather simulation persistence/replay, rain/snow collision
and exclusion, wetness/drying/melting, fog, lightning/thunder and accessibility remain unimplemented. No weather
consumers, production/backend qualification, GPU/memory budget measurements or soak results are claimed here.


### Adaptive density quadrature diagnostic — October 27

An ignored diagnostic trial selects one to four midpoint density samples from the local detail footprint,
for both view and shadow intervals. With `ceil(footprint * 4)` capped at four, the structured view has mean
opacity 0.474428 and opacity MAE 0.015278 against the dense reference above (99th percentile 0.221227).
This is substantially worse than fixed four-sample quadrature (MAE 0.001284). The warm dispatch-to-readback
median is 4.9245 ms after discarding two dispatches; it includes CPU/readback overhead and uncontrolled desktop
GPU activity. Its faster measurement does not compensate for the lost density fidelity. Discrete sample-count
transitions also need motion testing before any production use.

A conservative `ceil(footprint * 16)` trial recovers mean opacity 0.486524 and reference MAE 0.001468, but its
warm median is 9.06835 ms, with a wide 7.81–18.47 ms range. It does not establish a cost improvement over the
fixed four-sample early-exit trial. Neither variant is adopted. Both were compiled and run on D3D12 only;
no Vulkan, Metal, editor, motion or performance acceptance is implied. The first trial's image was inspected:
cloud outlines are more varied than the current defaults, but distant rows remain. Further work must preserve
nonlinear density integration without relying on abrupt budget changes or accepting the higher-cost trial as a
production solution.

### Native viewport resize check — October 27

The generation-29 D3D12 QA editor continues to render its paused 09:00 cloud view when restored from a
3440-by-1392 maximized window to a 1280-class window, when the Scene/Inspector divider narrows the Scene
viewport, and after restoring the divider and maximizing again. The observed frames contain the sky and clouds
without a blank output or stale background copy. Rendering settings remain byte-for-byte unchanged. The test
editor closes normally. This is a short visual resize check, not an instrumented resize test, exhaustive resize
stress, device-loss recovery, or verification of an HDR camera effect reusing the same scratch texture.

### HDR camera-effect coexistence check — October 27

A QA-only Vignette graph created with the public template API and its schema-4 Material both round-trip through
the supported source codecs. The generated fragment compiles to DXIL, SPIR-V and MSL. A cloned showcase scene
assigns that material Before Tonemapping; its cooked catalog contains the scene, material and shader dependencies.
The staged D3D12 runtime completes 12 frames with exit zero and no logged errors. A subsequent visible-player
comparison against the original scene shows darker cloud-image edges with the effect assigned, while the scene
remains present. This supplies native evidence for an HDR camera effect following the cloud pass and reusing its
scratch texture. An unrelated window obscured part of both captures, so full-frame pixel acceptance, resizing with
the effect active, other backends and device recovery remain open. No original scene or rendering settings changed.

### Shadow-march cutoff diagnostic — October 27

A diagnostic-only cutoff stops shadow sampling after optical depth times the least-attenuated active scattering
order reaches 11.512926 (transmission about 0.00001). Zero multiple-scattering occlusion needs separate treatment:
its added orders are independent of depth, so only the direct order can disappear. The ordinary structured view is
float-identical to the current shader. At density 30, coverage 0.9 and 128/16 view/shadow samples, opacity remains
identical and maximum linear RGB difference is 0.000000768. Warm dispatch/readback timings overlap the baseline
in both cases; the dense case medians are about 7.07 versus 7.13 ms. This includes CPU/readback overhead and desktop
GPU contention, and does not establish a useful improvement. The cutoff is not adopted. This D3D12-only trial does
not resolve the density-filtering fidelity/cost tradeoff or the default appearance failure.

### Half-resolution sky diagnostic — October 27

A diagnostic renders the four-point density quadrature at 480 by 200 and bilinearly reconstructs it to
960 by 400. Against the dense reference, mean absolute opacity error is 0.005849, the 99th-percentile
opacity error is 0.123726, and mean absolute linear RGB error is 0.006693. Large cloud shapes remain,
but fine edges soften and distant repeated bands remain visible. This sky-only experiment does not
cover foreground depth discontinuities, motion or native frame timing. It is not adopted as a default;
a production reconstruction would need depth-aware edges and separate transport handling.

The existing density regression also passes again on D3D12: all 384,000 tested rays produce a zero
failure mask when extinction increases threefold. This checks nondecreasing opacity and finite bounded
results, not realistic appearance or every camera position.

The repeatable offscreen validation command now also accepts `--suite cloud-density`:

```text
python Scripts/Tests/test-atmosphere-medium-gpu.py --suite cloud-density --backend direct3d12 --backend vulkan --output Build/Validation/CloudDensitySuite
```

This compiles the current probe to DXIL, SPIR-V and MSL, then checks density multipliers 0, 1 and 30
against threefold extinction on each requested backend. All six D3D12/Vulkan cases pass, each covering
384,000 rays. Nonzero failure masks fail the command. A separate deliberately broken shader was also
verified to exit with code 1. These are optical monotonicity checks; visual realism remains unfinished.

### Three-point density and default-lighting diagnostic — October 27

Three midpoint density samples per ray interval give structured-view opacity MAE 0.002608 and
99th-percentile error 0.062429 against the dense reference; linear RGB MAE is 0.003249. The warm
CPU-dispatch-to-readback median is about 7.08 ms, with desktop contention and readback included.
This is a candidate, not a production performance result. The default-view comparison remains soft
and washed out with both the current and three-point kernels (mean opacity 0.424573 and 0.427649).

Inspection identifies a separate lighting limitation: the cloud kernel adds `ambientRadiance` unchanged
at every occupied sample, while only sunlight receives cloud optical-depth attenuation. Increasing
sampling alone cannot correct that unoccluded ambient floor. A sky-light attenuation model needs its
own image, cost and parameter validation before changing defaults. No diagnostic kernel is adopted.

### Ambient sky occlusion diagnostic — October 27

A diagnostic adds a four-sample vertical optical-depth estimate from each cloud sample to the slab top
and attenuates ambient radiance by its transmission. Default-view opacity is unchanged, as required for
a lighting-only change. Shading improves modestly, but this single upward direction is only an approximation
to hemispheric sky lighting and omits indirect-light replenishment.

A separate 1 km body-scale stress case with density 3 exposes pronounced bands at 48 view, 6 sun-shadow
and 4 sky samples. Raising these to 128, 16 and 16 reduces the visible bands substantially; this reference
is not a performance-qualified default. Mean opacity changes from 0.721982 to 0.731870 when the view budget
also changes. Consequently neither the four-sample lighting trial nor the smaller body settings are adopted.
The evidence points to coupled lighting and density integration quality, not just an ambient brightness setting.

### Development integration candidate — generation 30

The development source now uses three density subintervals for both view and sun-shadow intervals,
applying coverage and erosion before averaging. The existing transport cutoff is preserved. Generator 30
and importer 43 invalidate old generated shaders. All six current-source density cases pass on D3D12/Vulkan;
cloud, unlit and VFX fragment shaders compile to DXIL, SPIR-V and MSL. The UI explains the extra density
samples. This adopts the accuracy improvement for development validation, not release acceptance: DevDist
rebuild, native imagery, motion and performance remain pending. Ambient-occlusion experiments remain excluded.


### Generation-30 native editor check — October 27

DevDist built and staged successfully in 836.7 seconds. A 12-frame native D3D12 run using the earlier QA
cook exits zero with empty stderr; its cooked materials remain generation 28, so it does not prove regenerated
material parity. The editor then imports the QA project with generation 30 and loads the 24-entity showcase.
The fixed sky view still fails natural-appearance acceptance: broad soft bodies and horizon rows remain.

The displayed frame time is about 54–60 ms in the maximized 3440-by-1392 editor and about 15 ms in its restored
1280-class window. These are UI observations at different viewport sizes, not isolated GPU benchmarks or a
controlled old/new comparison. Resize preserves the sky image. The original maximized layout is restored,
the owned editor closes, and rendering settings retain the exact baseline hash. Motion, wind, Play Mode,
regenerated-player parity and performance acceptance remain open. Three-point integration is a development
candidate with numerical accuracy benefits; it is not an accepted default-quality or performance solution.

### Bounded GPU lighting-cache prototype — October 27

An offscreen prototype builds a 129-by-33-by-129 world-space buffer containing three scattering-order
transmissions, then interpolates those values in the cloud view pass. Phase functions remain view-dependent
and are evaluated after lookup. Samples outside the bounded volume use the uncached shadow integration.
The float4 buffer occupies 8,786,448 bytes; allocation, building and consumption occur in one diagnostic run.

Against the generation-30 uncached structured view, opacity is identical, linear RGB mean absolute error is
0.001479, the 99th percentile is 0.016223, and maximum error is 0.050859. The inspected image avoids the obvious
ray-aligned stripes from the rejected ray-local reuse experiment. Builder and consumer compile to DXIL,
SPIR-V and MSL. D3D12 and Vulkan produce finite output with mean RGBA difference 0.000000385 and maximum
0.005211. This is prototype evidence only: no engine integration, speedup, wind-update correctness, cache-boundary
motion, origin-rebase behavior, resize/recovery safety or complete sky-light occlusion is established.

### Cache timing and boundary diagnostic — October 27

Twelve D3D12 dispatch/readback runs, discarding the first two, measured median elapsed times of 7.365 ms
uncached, 6.098 ms rebuilding the bounded cache each run, and 5.469 ms reusing it. These include CPU submission,
fence and readback overhead on a shared desktop; they do not establish native GPU frame cost. The reused test
keeps all cloud/light inputs constant, so it does not validate changing-weather invalidation.

A boundary sweep found a hard-switch discontinuity: the maximum transmission jump beyond the underlying
uncached field's change was 0.241795. Blending from cached transmission to the uncached calculation over two
outer grid cells reduced that excess to 0.000000060 in 384,000 samples across the positive-X boundary. The fade
remains a prototype; other boundaries, camera motion, wind updates and its additional boundary cost need checks.

The boundary test now covers all six faces (64,000 points per face). Maximum excess transmission jump is
at most 0.000000060. At two nearby wind offsets, rebuilt caches retain exact opacity parity with the uncached
reference; RGB mean error is about 0.00181. The difference between their wind-step image changes has mean
absolute RGB error 0.0000295 and maximum 0.00450. This checks two rebuilt states, not automatic invalidation,
long-session motion or origin rebasing. With the boundary fade enabled, one wind-state timing run measured
6.277 ms including cache rebuild versus 6.836 ms uncached (same CPU/fence/readback limitations as above).

### Renderer cache-key foundation

`CloudLightCacheKey` now explicitly records geometry, density/detail, wrapped wind phase and scattering
occlusion, shape controls, regional coverage/erosion, sun direction, grid origin/cell size/dimensions, enabled state,
seed and shadow budget. Three focused cases (34 assertions) verify invalidation and reuse for changes applied
after lookup: view/projection, exposure, radiance, scattering albedo/contribution, anisotropy and view budget.
This is an integration foundation only; the renderer does not yet allocate or consume the prototype cache.
Matching inputs are not a validity flag: the eventual resource owner must also prove a successful build and
reset validity after allocation, device recovery and failed recording/submission.

### Shared builder source and matched-input parity

The source now factors cloud parameter decoding and direct shadow optical-depth integration into shared helpers.
`BuiltinCloudLightCache.hlsl` uses them with a checked 464-byte C++/HLSL uniform layout. The builder and background
fragment compile to DXIL, SPIR-V and MSL, and the six D3D12/Vulkan density cases still pass. An offscreen host runs
the new builder successfully. Its maximum image difference from the earlier prototype is 0.0000595 (mean
0.0000000399), exceeding the diagnostic's strict 0.00001 parity limit. Repeating with explicitly packed reference
sun/wind values did not remove the difference. The renderer is not wired to the cache.

A subsequent fixed-input comparison produced bit-identical cache buffers and rendered float images when the
canonical helper used the prototype's constant inputs. A second comparison supplied the same runtime uniforms to
the canonical helper and the original prototype shadow loop. On both D3D12 and Vulkan, all 549,153 float4 cache
entries and all 384,000 rendered float4 pixels matched bit for bit, with finite values throughout. This isolates
the original mismatch to comparing differently supplied inputs rather than the shadow-helper refactor. No tolerance
was relaxed. These are fixed-scene parity checks; they do not prove renderer ownership, cache invalidation,
performance, motion stability or visual acceptance of the cloud defaults.

The Windows and Unix built-in shader generators now select compute-only variants for the cache builder. The
Windows generator successfully emitted DXIL, SPIR-V and MSL byte arrays; both Unix scripts passed Bash syntax
checks. The full Windows script harness initially stopped at the source-file budget gate. Extracting the
cloud-pass enumeration and checked pipeline mapping into `CloudVolumePass.h` resolved that gate without
changing its limit. All source budgets now pass; two focused cases with 42 assertions verify unique supported
slots and rejection of invalid inputs, and the renderer translation unit passes the DevDist syntax check.
The full Windows script harness subsequently passed in 676.52 seconds, including the generator checks and
installer integration fixtures. Native Unix generation and renderer resource integration remain unverified.

`CloudLightCacheGrid` defines a bounded, omnidirectional horizontal grid and spans the cloud slab vertically.
Horizontal cells align to the advected cloud coordinates rather than camera orientation. Two focused CPU cases
(11 assertions) verify slab coverage, observer displacement, periodic phase wrapping and rejection of invalid
inputs. This helper is not yet connected to GPU allocation or lookup; visual motion stability is still unproven.

`CloudLightCacheSampling.hlsli` now contains the shared trilinear transmission lookup and two-cell boundary
weight. Out-of-grid or invalid grid inputs request direct lighting without reading the buffer. In the offscreen
prototype, all six faces passed a 0.000001 excess-discontinuity limit on D3D12 and Vulkan (64,000 sample pairs
per face and backend); the largest measured value was 0.0000000596. DXIL, SPIR-V and MSL compilation passed.
This lookup remains outside the native renderer until its resource binding and direct-light fallback are wired.

### Native cache integration awaiting runtime acceptance

The source now connects the builder and lookup to the background cloud pass. Each frame workset owns an
8,786,448-byte transmission buffer and compute pipeline, with transactional allocation and explicit cleanup.
Enabled clouds rebuild the buffer before the fragment pass on the same command buffer. No previous-frame validity
claim or cache-key reuse is made. The fragment shader receives a separate 48-byte grid uniform and blends to
direct lighting at cache boundaries. Other material and VFX cloud paths continue direct lighting.

The cached background fragment compiles to DXIL, SPIR-V and MSL. All three changed renderer translation units
pass MSVC DevDist syntax checks. These checks do not cover allocation failure, retirement, device recovery,
AddressSanitizer, native rendering or performance; those gates remain required before this integration is accepted.
The earlier prototype-only notes above describe prior checkpoints and are superseded by this source wiring.

The isolated `CloudLightCacheLifecycleTests.cpp` executable compiles the production resource implementation
against SDL doubles, separate from the engine and normal test runner. Four cases with 101 assertions passed
under MSVC AddressSanitizer: failed pipeline/buffer creation, cleanup before publication, successful retry,
per-use rebuilding, idempotent release, compute-pass failure and disabled/invalid recording inputs. This covers
CPU allocation transactions and ownership bookkeeping, not GPU execution, retirement or real device recovery.

### Native editor density check and remaining appearance work

The cache-enabled DevDist editor staged successfully in 344.9 seconds and a 12-frame native player smoke exited
cleanly. In the QA editor at paused 09:00, dragging density from 1 to 3.5 visibly increased cloud opacity without
the reported shrinking in that view. Undo and Save Environment restored the original appearance and exact settings
file hash. The QA editor and control session were closed afterward. Camera motion, wind and recovery remain untested.

The default view still fails appearance acceptance: broad soft bodies and repetitive horizon shapes remain.
An offscreen two-scale atlas trial blends a second, rotated/scaled periodic sample into the base field and accounts
for its coordinate stretch in filtering. The default remains washed out. A structured preset with stronger density,
detail and erosion and reduced ambient radiance shows more distinct bodies in both the trial and the unchanged
baseline. This points to default authoring and ambient lighting as separate work from atlas repetition; the trial
has not been adopted, and no default-quality or performance claim follows from these fixed images.

An additional offscreen trial stores upward skylight transmission in the cache's fourth channel and applies it
to ambient cloud lighting. The structured case retains bit-identical cloud opacity, with darker undersides;
the default still has soft bodies. Four vertical shadow intervals versus a 16-interval reference give RGB mean
absolute error 0.001759, 99th-percentile error 0.015525 and maximum error 0.093831. Warm CPU dispatch-to-readback
time including cache rebuilding has a 7.330 ms median after discarding two of twelve iterations; this is not a
GPU timestamp or native-editor performance result. A single upward direction is not hemispherical skylight,
and indirect illumination, controls, boundary motion and backend parity still need work. The trial is not adopted.

A subsequent upper-hemisphere trial uses equal-solid-angle midpoint directions, rather than only the zenith.
For the same structured cloud case, a 32-direction, 16-interval reference gives the following RGB comparison:

| Ambient transmission approximation | Mean absolute error | 99th percentile | Maximum |
| --- | --- | --- | --- |
| Unoccluded ambient | 0.112977 | 0.366795 | 0.378337 |
| One vertical direction, four intervals | 0.016479 | 0.098051 | 0.193666 |
| Four directions, four intervals | 0.009151 | 0.064607 | 0.126369 |
| Eight directions, eight intervals | 0.004922 | 0.036919 | 0.065985 |

All compared outputs are finite and retain bit-identical opacity. This reference is numerical quadrature of
the same simplified cloud model, not a physical ground truth. The images improve interior depth but do not
resolve repeated horizon shapes. Twelve-iteration CPU dispatch-to-readback runs including cache rebuilding
show substantial cost and variance: the eight-direction run ranges from 23.8 to 29.0 ms, and the four-direction
run from 10.9 to 24.2 ms. These are probe timings, not GPU timestamps or native-editor budgets. Neither variant
is adopted. Integration requires a cloud-specific strength/quality control, matching direct fallback, bounded
cache update/reuse policy and native camera/wind tests; raising per-frame work silently is not accepted.

The native resource helper now separates a recorded key from a successfully submitted key. Matching submitted
inputs can skip a rebuild; changing inputs invalidates old contents before recording, and abandoned writes cannot
restore an older key. Six standalone SDL-double cases with 176 assertions pass under MSVC AddressSanitizer,
including reuse, wind/light changes, canceled writes and failed-pass retry. The renderer does not yet call the
submission publication hook, so native frames still rebuild. Frame-level submission/cancellation wiring and real
GPU motion tests remain required; these helper tests do not establish a performance improvement in the editor.

The next source checkpoint wires a frame publication transaction into all cloud-recording paths, including nested
views. The transaction retains only shared validity state, so moving or retiring GPU resources cannot leave dangling
resource pointers. Release invalidates that state. A successful frame submission publishes the recorded keys;
exceptions and destruction invalidate pending writes. Exact matching submitted inputs now skip the native compute
pass, and pass statistics count actual rebuilds. Focused ASan tests cover retirement and multi-surface abandonment;
DevDist staging and native paused/moving-camera, wind, and failure/recovery acceptance are still pending.

The submission-reuse/preset DevDist batch subsequently staged in 220.6 seconds. In the native QA editor at paused
09:00, choosing Defined billows immediately produced more distinct bodies while the displayed base height,
thickness and pattern size remained 1500/1200/3000 metres. Undo restored the original image; Save Environment
restored the exact baseline settings hash. The owned editor and control session were closed. This verifies the
preset application/Undo/save workflow and basic rendering after submission integration, not actual cache hit rate,
performance, camera/wind motion, recovery or final cloud realism. Softness and horizon repetition remain visible.

The preset QA also exposed missing selection feedback: the combo retained its action prompt after application.
The next authoring change derives Defined billows/Soft bodies/Custom from shape values, independently of placement,
wind, lighting and quality, and places the label above a full-width selector. Three focused cases with 41 assertions
pass; this UI refinement is not yet staged. Native cache reuse verification can use the existing Profiler rendering
Passes counter: compare warmed paused views against time-preview playback with other passes held constant, then
pause again. A pass-count change alone is not sufficient for correctness: inspect lighting and wind motion, and
exercise changed cloud parameters, multiple views and device recovery separately. These checks remain open.

A subsequent native time-preview check retained profiler snapshots at paused 09:00, during playback and after
pausing at 09:12. Rendering Passes changed 20 → 21 → 20 while Surfaces stayed 1 and Draw calls stayed 11.
The sky illumination changed with preview time and returned to the original view after Reset Time Preview.
This supports the expected rebuild/reuse transition for changing solar direction in this scene. It does not
establish frame-time savings, wind advection correctness or camera-boundary continuity. The saved settings hash
remained unchanged; both QA panels, the owned editor and the control session were closed afterward.

### Ambient sky control and variation-scale checkpoint — October 27

The four-direction/four-interval ambient approximation is now integrated as optional **Ambient Sky Occlusion**.
Zero retains prior illumination; old volume records migrate to zero. Positive strengths blend cached alpha/direct
fallback transmission without altering cloud density or opacity. Generator 31/importer 44 invalidate shaders.
Focused optimized settings/cache tests pass 16 cases with 275 assertions, and preset tests pass three cases with
43 assertions. ASan cache lifecycle tests pass eight cases with 241 assertions. Direct3D 12 and Vulkan each pass
three density and three ambient-invariance cases with 384,000 samples per case; opacity is invariant under ambient
strength and ambient radiance decreases monotonically. Native appearance/performance acceptance remains pending
in the DevDist build. The shared uniform layout stays 416 bytes; no extra cache storage is required.

An isolated structured-cloud render compares variation periods 64 and 8 with otherwise matching controls and
ambient strength 0.8. Period 8 stretches visible bodies into streaks; period 64 keeps more recognizable billows but
still repeats near the horizon. Mean opacity changes from 0.485380 to 0.530821 because displacement changes the
field. This is a different shape, not an opacity regression. Reducing the variation period alone is rejected as a
realistic default. Multi-scale cloud grouping and height-profile variation remain candidates requiring isolated
visual comparisons, sampling bounds, continuity tests and native acceptance rather than a blind default change.

The generator-31/importer-44 DevDist batch built and staged in 1,009.9 seconds (311 native actions).
After material regeneration, the native fixed 09:00 QA scene rendered successfully. Ambient Sky Occlusion 0 → 0.8
visibly darkened interiors while retaining cloud outlines; Undo restored zero and the baseline image. Save Environment
reported all settings saved. An order-independent JSON comparison confirmed every previous rendering value was
preserved, with only volume version 6 and ambient strength zero added. The QA editor and control helper were closed.
This closes the basic authoring/Undo/save check only. Broad soft bodies and horizon repetition still fail default
appearance acceptance; cache-boundary motion, wind, player parity and performance remain unverified.

### Repetition diagnosis — October 27

An isolated smooth hash-displacement trial replaces atlas-driven macro displacement with four interpolated
horizontal cell hashes. It wraps at the existing coordinate boundary and uses a conservative displacement sampling
bound. Its rendered structured view retains rounded bodies, but horizon rows remain visually similar.
A 384,000-point horizontal density slice compares each sample with positions 12 km east and north (one atlas repeat)
over a 96 km square, at 45% slab height, with regional coverage disabled. The current method gives Pearson
correlations 0.294194 and 0.345992; the trial gives 0.401730 and 0.401542. Mean densities are 0.126051 and 0.128546.
All values are finite. This single-slice diagnostic does not establish motion or image-space perception, but it
contradicts adopting the trial as an anti-repetition improvement. The trial remains outside production source.
The next visual investigation should separate repeated morphology and distant sampling/filtering from literal
atlas-period correlation; changing the macro warp blindly is not supported by this evidence.

### View integration reference and denser shape authoring — October 27

A matched 240×100 view compares 48, 128 and 512 view intervals with unchanged default shape values and ambient
occlusion 0.8. Against 512, 48 intervals have RGB mean absolute error 0.000388 and opacity mean/max errors
0.000428/0.102103; 128 intervals give 0.0000543 RGB and 0.0000510/0.015225 opacity errors. All outputs are finite.
The broad softness persists in the reference. This isolates view integration only: the atlas, six sun-shadow
intervals and four-direction/four-interval ambient approximation remain unchanged. It does not qualify the full
lighting model, but increasing the default view budget alone is not supported as the appearance fix.

Isolated comparisons of existing density/detail/erosion controls show firmer bodies with extinction 0.04/m,
detail 0.7, erosion 0.65, coverage 0.7 and core concentration 0.65. A new editable Dense billows starting point
uses those controls plus the existing Defined billows regional grouping. It preserves placement, seed, wind,
lighting and budgets; no default or renderer algorithm changes. Four optimized preset cases pass 63 assertions,
including shape-field equivalence to Defined billows when only extinction is restored. UI header compilation passes.
The comparison images used regional coverage disabled; the final grouped preset still requires native acceptance.

### Dense billows native check — October 27

The preset-only DevDist batch stages successfully in 55.9 seconds. In the fixed 09:00 QA view, the selector displays
Soft bodies initially, lists all three starting points and displays Dense billows after application. Cloud bodies
become visibly firmer. Undo restores both the Soft bodies label and the original image; Save Environment restores
the exact settings hash. The owned QA editor and desktop helper are closed.

Dense billows also exposes fine horizontal bands on middle/distant cloud bodies. Its default-quality appearance is
not accepted as a new realistic default. Investigate view integration, cache/direct lighting and filtering separately
using the dense case; the earlier soft-default high-sample comparison does not qualify this denser case.
Two native sessions also needed a second click to switch the Environment panel from Lighting to Clouds: the first
click highlighted Clouds while Lighting content remained visible after a subsequent observation. This is a reproducible
UI/input symptom, but the cause is not yet isolated from the automation input path. No speculative input fix is applied.

### Dense direct-integration reference — October 27

The isolated direct-lighting probe now includes the final Dense billows regional grouping (strength 1,
period 2), with view budgets 48, 128 and a diagnostic-only 512. All readbacks are finite. Against 512,
48 has RGB mean absolute error 0.0067908 and opacity error 0.0027169; 128 reduces these to 0.0013655
and 0.0002451. Maximum component errors are 0.98098 and 0.25509 respectively. Measurements cover
240 by 100 active pixels, excluding the unused host-buffer area. Ambient sky occlusion is 0.8.

Increasing view samples improves integration but does not remove the visible repeated horizon structure.
This is a direct-lighting diagnostic, not a native cached-lighting comparison or a performance acceptance
result. Keep the dense native banding investigation open; do not increase the shipping budget to 512.

### Environment tab input acceptance — October 27

The first-click symptom was reproduced in a headless application using the real UI frame/backend: moving
the pointer and pressing/releasing in one event batch failed to select the tab, while separate-frame input
worked. Tab overlap hit-testing requires hover from a preceding frame. The backend now preserves the
ordered press/release tail until that hover frame has been processed when input trickling is enabled.
Focus-loss cancellation and explicitly disabled trickling retain their behavior. This adds one frame before
press processing for a combined pointer movement/press batch.

Seventeen optimized input cases pass 464 assertions, covering tabs, sections, floating panel collapse/drag/close,
wheel routing, queue ordering, cancellation and reveal scrolling. The DevDist stage completed in 83.9 seconds.
Native QA then switched Lighting to Clouds and back with exactly one click each; the correct content appeared
after each click. The QA settings hash stayed unchanged, and the owned editor and desktop helper were closed.
This closes the reproduced tab issue; it does not close the separate cloud appearance or performance gates.

### Dense full-resolution cache isolation — October 27

The isolated D3D12 probe now compares current direct lighting against the canonical light-cache builder and
sampling shaders with native grid placement (129 by 33 by 129, origin -6000/1490/-6009 for this view).
At 960 by 400 active pixels, both outputs are finite and opacity is bit-identical. Linear RGB mean absolute
difference is 0.00369963, maximum 0.187747. The thin horizontal bands are visible in both images; cache
interpolation is therefore not their sole cause. This is a synthetic view with Dense billows controls,
ambient sky occlusion 0.8 and no editor post-processing, not a native performance result.

A direct-lighting 512-view-interval diagnostic at the same full resolution substantially reduces the fine
horizontal bands, while the broad repeated horizon cloud structure remains. The earlier 240 by 100 active
pixel comparison was insufficient to judge these fine bands. Next investigate integration/filtering at a
practical budget separately from cloud morphology. The diagnostic 512 limit is confined to ignored build
artifacts; shipping budgets and defaults have not changed.

### Dense interval sampling trials — October 27

Two isolated full-resolution D3D12 trials retain 48 view intervals and the same shadow/ambient budgets.
Increasing only view-density quadrature from three to nine points, while retaining the original filtering
footprint, changes RGB error against the 512-interval reference from 0.00655635 to 0.00629300 and opacity
error from 0.00273945 to 0.00247252. Fine horizontal bands remain visually apparent; the extra density work
is not justified as the fix.

A second trial retains three density samples and evaluates direct/ambient lighting at their density-weighted
centroid rather than the interval midpoint. Opacity stays bit-identical to the baseline, but RGB error rises
to 0.00659343 and the fine bands remain. Neither trial is adopted. The next investigation should examine
radiance/transmission integration and distance-dependent filtering, rather than adding density samples or
moving a single lighting sample alone. Trials are confined to ignored `Build/Validation/CloudQuadrature40`.

### Lighting isolation — October 27

At 960 by 400 pixels, disabling atmosphere still leaves the fine stripes. Replacing cloud illumination
with a constant removes the interior stripes while preserving the same density/opacity integration.
Disabling ambient sky occlusion alone does not remove them; raising direct shadow steps from 6 to 16
with ambient occlusion disabled also leaves them visible. Thus atmosphere accumulation, ambient occlusion
alone and insufficient direct shadow steps alone do not explain the artifact. The next trial should
integrate varying illumination within view intervals rather than sampling it once per interval. Constant
illumination is diagnostic only and is not an acceptable visual fix. All trials remain under ignored
`Build/Validation/CloudLighting41`; no production shading or defaults changed.

### Within-interval radiance trial — October 27

An isolated 48-interval trial evaluates illumination at three density sample positions per interval,
weighted by front-to-back local absorption, while retaining baseline opacity. All outputs are finite;
opacity is bit-identical to baseline. Against the 512-interval reference, RGB mean absolute error drops
from 0.00655635 to 0.00481653 (about 27%), but bands remain. This expands lighting evaluations up to
threefold and is not adopted without a clear quality/cost advantage.

The existing 128-view-step setting instead produces RGB error 0.00132957 and opacity error 0.000247906
at the same 960 by 400 resolution, with visibly reduced bands. Its maximum RGB error remains 0.293978,
and repeated horizon morphology remains. This does not establish native frame-time acceptability or
justify silently increasing defaults. The renderer has temporal antialiasing infrastructure; evaluating
whether cloud sampling can use it safely requires cloud motion/history treatment and non-temporal fallback
validation, not simply injecting noise. Trial files are under ignored `Build/Validation/CloudRadiance42`.

### Temporal reconstruction source audit — October 27

`RenderCloudVolume.cpp` binds an HDR color target and sampled depth, not a cloud velocity or rejection target.
`BuiltinToneMap.hlsl::ApplyTaa` reprojects using the scene velocity texture, with foreground velocity dilation
and color-neighborhood clipping. `CameraMotionHistoryEntry::HasContinuousEnvironment` compares authored cloud
settings, source/revision, atmosphere, stars, bloom and day duration; it does not compare elapsed advection.
These mechanisms do not prove correct wind/parallax reprojection for participating media. No native ghosting
failure is claimed from this source audit alone.

Dedicated cloud temporal reconstruction remains an implementation gate before using frame-varying ray samples
as a banding solution: track representative cloud depth/motion or an appropriate history rejection signal;
handle camera motion, wind, disocclusion, lighting/time changes and empty cloud pixels; validate settings edits,
resize/device recovery, independently rendered views, and non-temporal/MSAA paths. Preserve foreground geometry
motion and the existing publication lifetime rules. Rendering and Project Settings guides now expose this gap
and distinguish view sampling, shadow sampling, ambient occlusion and repeated morphology.

### Broader base-noise morphology trial — October 27

An isolated CPU atlas-generation variant shifts the gradient component from octave weights
4:0.625 / 8:0.25 / 16:0.125 to 2:0.5 / 4:0.3 / 8:0.15 / 16:0.05, leaving cellular channels,
shader integration and lighting unchanged. The fixed 960 by 400 D3D12 view uses 128 view steps.
At coverage 0.7, mean opacity falls from the baseline 0.551515 to 0.472896, so the first visual
comparison is confounded by less cloud cover. Raising trial coverage to 0.75 produces mean opacity
0.545352, close to baseline; the grouping remains visibly different, with broader irregular gaps.
Repeated distant morphology still remains, and this single view does not establish better defaults.

The variant remains isolated in `Build/Validation/CloudMorphology44`. Changing the shared atlas would
change existing authored appearances and regional/warp fields, not just a new preset. Before adoption,
compare multiple seeds, camera positions, default and dense settings, seam continuity and wind motion;
decide how existing authored scenes retain their appearance. No production atlas or defaults changed.

### Broader-noise validation across seeds — October 27

The isolated atlas variant passes all five existing optimized noise cases (575 assertions), including
periodic seams, determinism, mip averaging, supported dimensions and displacement bounds. Those tests
establish numerical contracts, not appearance quality.

Two additional full-resolution D3D12 views compare baseline coverage 0.7 with the trial's proposed 0.75
compensation. With shader seed 2, mean opacity changes from 0.514480 to 0.589841; with shader seed 17
and a 90-degree camera rotation, it changes from 0.550750 to 0.500630. Thus the single-view compensation
is not stable across seeds/views. Both image pairs retain repeated distant structure, and there is no
consistent visual improvement sufficient to replace the shared atlas. Reject this variant as a default
replacement; do not repeat the same octave-weight adjustment as an established fix. Any future optional
shape model needs its own authoring/compatibility contract rather than silently changing existing scenes.

### Research decision and completion gates — October 27

Stop isolated noise-weight and sample-count trials. The following primary sources guide the next implementation;
this is an implementation decision, not a claim that the remaining features already work.

- [Guerrilla, Nubis (2017)](https://d3d3g8mu99pzk9.cloudfront.net/AndrewSchneider/Nubis-Authoring-Realtime-Volumetric-Cloudscapes-with-the-Decima-Engine-Final.pdf),
  PDF pages 27–29 and 51–58: separate weather coverage and cloud type, with type-dependent height profiles.
  Pages 45–50 discuss noise advection, height-dependent skew and anvil shaping. These provide a more deliberate
  authoring model than changing the weights of one shared noise atlas.
- [Hillaire, Frostbite sky/cloud technical notes (2016)](https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016-pbs-frostbite-sky-clouds-new.pdf),
  sections 5.3–5.4: weather coverage/type and height-dependent density/erosion profiles, with separate base shape
  and erosion detail. Section 5.5.3 describes jittered interval sampling with temporal reprojection; section 5.6.3
  derives analytical segment integration. Kéire already uses the latter through `VolumeSegmentWeight` and
  `opticalDepth * VolumeSegmentWeight(opticalDepth)`; replacing that formula is not the next priority.
- [Epic, Volumetric Cloud](https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-in-unreal-engine):
  reduced-resolution tracing and reconstruction have explicit opaque-intersection and quality tradeoffs.
  Adopt a reconstruction path only after depth-overlap tests and measurements, not as an assumed free speedup.
- [Nubis³, SIGGRAPH 2023 abstract](https://advances.realtimerendering.com/s2023/index.html):
  the move from 2.5D clouds to fly-through voxel clouds required a different representation and acceleration.
  This supports keeping planet/space and fly-through acceptance explicit; it does not establish that Kéire's
  current horizontal slab can provide seamless planetary travel.

#### Selected implementation order

1. Separate low-frequency weather coverage/type from the repeated base/detail atlas. Add height-dependent cloud
   profiles and erosion, with coherent defaults for layered and cumulus shapes. Keep density an extinction control;
   coverage, shape and erosion must remain independently understandable. Introduce an explicit compatibility model
   so existing authored scenes retain their appearance. Update serialization, runtime/cache inputs, editor controls,
   presets and focused tests together. Do not silently replace the shared atlas.
2. Add cloud-specific temporal reconstruction before enabling frame-varying ray jitter. Retain radiance,
   transmittance and representative cloud depth, reproject camera/wind motion, and reject invalid history after
   disocclusion, lighting/settings changes, view changes or resource resets. Preserve non-temporal/MSAA rendering
   and per-view resource ownership. Evaluate reduced-resolution rendering only with depth-aware reconstruction and
   measured benefit. Existing scene TAA is not sufficient evidence of cloud history correctness.
3. Run one batched DevDist validation after coherent changes: fixed full-resolution captures, moving camera/wind,
   native editor interaction and player parity. Keep the current direct high-step reference for numerical comparisons;
   do not mistake a lower image error for acceptable appearance or acceptable frame time.

#### Cloud completion criteria

- Default clouds have believable broad formations without obvious repeating horizon bands in multiple seeds,
  positions and viewing directions. Compare the same camera, exposure, weather and time, not differently covered skies.
- Increasing density thickens the existing formations without changing their authored coverage/shape field.
  Verify low, default and high density, plus coverage/type/detail extremes and restored legacy scenes.
- Dawn, noon and dusk lighting remain finite and plausible; camera movement, wind, paused time and settings edits
  do not introduce visible trails, popping or persistent noise. Verify empty skies and transitions into/out of clouds.
- Foreground geometry and transparent surfaces compose correctly. Resize, independently rendered views and device
  recovery do not reuse invalid history. Exercise supported backend paths and non-temporal/MSAA fallback.
- Record GPU/frame-time cost on the actual QA machine at stated resolution/settings; report quality/cost tradeoffs.
  Published timings from other engines are not a performance target or a measurement of this implementation.
- Editor controls, Undo/Redo, save/reload and standalone player agree. Close the owned QA window after desktop checks.

Planetary shell geometry, precision, reference frames and uninterrupted ground-to-space travel remain subsequent
world-travel gates. Do not mark the broader environment/world system complete when only local clouds pass.

### Independent height-profile kernel — October 27

The shared density kernel now accepts an explicit height envelope, separate from coverage, erosion and extinction.
A four-boundary height-profile helper supports thin layers and sharp boundaries without degenerate smoothstep
intervals. The existing entry point retains the original 0 / 0.15 / 0.7 / 1 envelope; this is compatible groundwork,
not the completed weather model or an appearance fix. Weather/type fields, serialized authoring, UI and temporal
reconstruction still need implementation. The optimized density suite passes 12 cases / 91,845 assertions,
including original-envelope equivalence, thin layers, sharp boundaries and bounded envelopes. The production
cloud fragment shader compiles to DXIL and SPIR-V. No full build or native visual acceptance is claimed here.

### Authored height-profile integration — October 27

Height profiles now round trip in volume record version 7, with strict finite/order/nonzero-layer validation and
legacy migration. The editor exposes normalized base and crown ramps; presets restore their matching default
profile. The profile reaches background/surface shading through appended volume ABI 6 (432-byte view), and the
lighting-cache key includes all four boundaries. Older material uniform prefixes retain their original sizes.
Generator 32/importer 45 invalidate generated shaders for the new layout. The diagnostic producer explicitly
initializes its profile. Focused settings/cache/layout tests pass 18 cases / 302 assertions; editor controls pass
MSVC syntax checking, and the production cloud fragment compiles to DXIL and SPIR-V. Source budgets pass.
Full DevDist integration, shader asset contract runtime checks, native authoring/Undo and visual acceptance are
pending. Independent weather/type distribution and temporal reconstruction remain unfinished; no claim of
realistic cloud defaults follows from these data-path checks.

### DevDist integration and symbol database recovery — October 27

The profile batch exposed two stale VFX total-size assertions. Their verified shader layouts are six/two float4
prefixes followed by the shared cloud view; assertions now express those prefixes plus the shared view size.
After compilation, editor linking hit LNK1140. Its PDB header used 4096-byte pages and 1,040,329 pages. The Windows
MSVC DevDist editor now uses 16384-byte PDB pages and non-incremental linking, retaining symbols. Regeneration and
retry completed with three native build actions; DevDist staging succeeded (127.1 seconds for the recovery run).
The rebuilt-library volume shader contract suite passes three cases / 35 assertions, accepting ABI 6 and rejecting 7.
Native visual acceptance is still pending. A separately identified float-collapse edge in tiny authored height
profiles remains queued; staging success does not prove that edge or cloud appearance resolved.

### Native profile authoring check — October 27

The staged editor opened SandboxShowcase and the Clouds tab correctly. Dragging Crown End from 1.0 to 0.8
visibly shortened the cloud layer; the setting persisted. Ctrl+Z restored 1.0 and showed the expected unsaved
Undo/Redo notice; Save Environment persisted profile [0, 0.15, 0.7, 1] in record version 7. Density remained 0.004
and coverage 0.6. The QA editor was closed and its process exit confirmed; the desktop helper was reset.
This verifies edit/Undo/save, not reopen/player parity. Asset reimport was still progressing during this check,
so no claim is made about all regenerated material paths. Repeated horizon formations remain visually evident.
The shader legacy-profile fallback is now restricted to the all-zero producer sentinel, so a nonzero profile
whose boundaries collapse to one float value cannot expand into the full legacy layer. This follow-up shader
change still needs targeted compilation/regression validation and staging with the next cloud batch.

### Float-collapse regression closed in source — October 27

Profile fallback now lives in the shared density helper and recognizes only an all-zero producer sentinel.
A collapsed nonzero profile stays empty; valid profiles and the legacy sentinel retain their expected envelopes.
Host validation also rejects profiles whose base/crown extent vanishes when converted to GPU float precision,
including an all-zero underflow case. Focused density tests pass 13 cases / 92,149 assertions; settings/cache/layout
checks pass 19 cases / 305 assertions. The production cloud fragment compiles to DXIL and SPIR-V. These changes
are not yet in the staged editor; include them with the independent-weather batch rather than starting another
full rebuild for this edge case alone.

### Independent weather field kernel — October 27

`CloudWeather.hlsli` defines a separate seeded horizontal field instead of sampling the shape atlas for weather.
Its lattice wraps to the host's 256-pattern-unit domain, including negative coordinates. Three smoothly
interpolated octaves fade toward their mean when unresolved. Coverage and cloud type can use independent seeds.
Two focused shared-kernel cases pass 1,242 assertions for periodicity, seam continuity, finite bounded output,
seed separation and unresolved sampling. The standalone compute probe compiles to DXIL and SPIR-V.
This kernel is not yet connected to production shading or authoring; it does not prove improved cloud appearance.
Next batch work must connect explicit legacy/new weather selection, type/variation controls, serialization,
cache invalidation and all shader-source packaging paths before native visual acceptance.

## Independent weather integration — October 27

The independent periodic 2D weather kernel now supplies regional coverage and cloud-type variation separately
from the 3D shape atlas. Cloud type changes vertical development within the authored height profile; density
continues to control extinction. Editor controls expose the opt-in mode, type, and variation. Existing records
preserve their prior appearance. Volume record 8 has 29 fields; appended volume ABI 7 uses a 448-byte view.
Generator 33/importer 46 invalidate generated shaders. Weather controls also participate in light-cache keys,
and both Windows and Unix shader embedding lists include the weather helper.

Focused settings/cache/layout validation passes 20 cases / 327 assertions; weather continuity, filtering and
seed checks pass 2 cases / 1,242 assertions. Production fragment compilation passes DXIL and SPIR-V.
These are source-level checks: this integration has not yet passed native visual or performance acceptance.
It is not a claim of realistic defaults, complete cloud-type authoring, or temporal reconstruction.
### Independent weather fixed-view result

The D3D12 production-kernel probe at 960 x 400 compares legacy weather with independent weather type 0.8,
variation 0.5. Both use density multiplier 4, detail/erosion 0.35, frequency 4, coverage 0.6, regional strength
0.5/period 64, core 1, 48 view steps, 8 shadow steps, and zero ambient radiance. Mean opacity changes from
0.39606 to 0.43827. The comparison is diagnostic, not a default-lighting evaluation: both images still show
soft bodies and repeated horizon bands. Independent weather alone does not satisfy appearance acceptance.
Ignored artifacts are in Build/Validation/CloudWeather47: legacy.bmp, weather.bmp, and comparison.png.

A separate attenuation-only check starts at density multiplier 1 and compares extinction multiplied by 3,
with ambient input 0.35. All 384,000 output rays are finite and report zero violations of bounded,
monotonically increasing opacity. This establishes numerical opacity behavior for this view on D3D12;
it does not establish perceived size under lighting, native editor behavior, or Vulkan parity.

Next implementation gate remains cloud-specific sampling/reconstruction and a measured lighting reference;
independent weather must not be presented as a completed fix for the repetition or banding.
### Sampling and shadow reference audit — October 27

CloudSampling48 compares the independent-weather fixed view above with a 512-view-step / 64-shadow-step
numerical reference. This is a high-sample reference of the current model, not physical ground truth:
changing step spacing also changes noise filtering. The regular 48/8 trial has RGB MAE 0.00239523;
eight shifted interval phases averaged offline have MAE 0.00201892 (about 16% lower). The 512/8 result
has MAE 0.000258390; 48/64 has MAE 0.00228370. Quadratic shadow intervals at 48/8 have MAE 0.00248507,
slightly worse than regular intervals. All still show the broad repeated horizon forms. Neither trial
is integrated into production. Offline phase averaging does not verify temporal reprojection or moving views.
The zero-phase prototype differs from the original by at most 0.00173461 because its interval arithmetic
changes floating-point evaluation; it is not a bit-exact replacement.

The authoring-scale audit found the independent weather UI reported four times the actual largest cell size.
Its label now uses Pattern Size times region period; legacy weather retains four times that value. Help text
distinguishes cell size from the full 256-pattern-unit weather repetition domain. The C++ control syntax check
passes. A native UI check remains pending.

At density multiplier 1, detail/erosion 0.2, coverage 0.6, ambient radiance 0.35, core 1, and 48/6 samples,
independent weather (type 0.8, variation 0.5, regional strength 1) was checked at periods 2, 8, and 32.
Mean opacity is 0.383641, 0.400483, and 0.348778 respectively. These correspond to 6, 24, and 96 km cells.
The legacy uniform-coverage comparison gives 0.427649. Grouping changes, but soft bodies and repeated horizon
forms remain. Artifacts: Build/Validation/CloudSampling48/regions.png and shadow-comparison.png.

The repository diagnostic now accepts five additional optional arguments after erosion:
weather-enabled (0 or 1), cloud-type, type-variation, ambient-occlusion (these three in [0,1]), and base-height-variation in metres ([0,100000]).
Recompile both host and shader together: the diagnostic constant buffer is now 96 bytes, not 64.
This makes independent weather and ambient occlusion reproducible without editing shader constants.

Further primary-source comparison: [Hillaire's noise generator](https://github.com/sebh/TileableVolumeNoise/blob/master/main.cpp)
uses a 128-cubed base atlas and a different base/detail frequency distribution. Kéire's 64-cubed atlas and
additional shape-floor remap are not appearance-equivalent. The basic Perlin/Worley combination is algebraically
consistent; this audit does not establish that its formula is a bug. The next structural comparison should
isolate atlas detail and density distribution, rather than credit temporal sampling with fixing broad repetition.
### Regional cloud-base variation — October 27

User feedback requested uneven coverage and different cloud altitudes across the sky. Independent weather now
has Base Height Variation (m), a separately seeded horizontal field that shifts the local base by a bounded
amount while retaining local thickness. It shares Region Scale and horizontal wind with the other weather
channels, but does not use their seed or extinction. Height lookup deliberately does not fade with ray sample
spacing: changing the view/shadow sampling budget must not move the cloud layer. View and shadow slab bounds
and the lighting grid cover nominal base minus variation through nominal top plus variation. Cache keys include
the amplitude. Record version 9 stores 30 fields; versions 1–8 migrate the new amplitude to zero. The existing
ABI-7 weather vector carries amplitude in its previously reserved W lane; no binary layout size changes.

Focused settings/cache/layout/grid tests pass 24 cases / 354 assertions. Weather tests pass 3 cases / 2,530
assertions, including bounded regional displacement, positive/negative variation and host-wrap continuity.
The editor controls compile in the focused syntax check. Production fragment, cache compute and diagnostic
compute shaders compile to DXIL and SPIR-V. The diagnostic now saves raw linear RGBA32F next to each BMP and
returns a nonzero exit on failed transport-check rays, instead of requiring inspection of a nonblack image.

At region period 2 (6 km), type 0.8, type variation 0.5, coverage 0.6, density multiplier 4, detail/erosion 0.35,
ambient radiance 0.1, zero ambient occlusion and 48/8 samples, changing base variation 0 to 1000 m changes mean
opacity from 0.438351 to 0.416920. The fixed-view image shows displaced cloud bases and a different distribution.
It also exposes stronger fine bands at the same sampling budget over the expanded altitude interval. This is
not visual acceptance. Native UI/Undo/save, cached rendering, motion, player and performance checks remain open.
A separate density multiplier 1-to-3 attenuation check passes all 384,000 rays on each of D3D12 and Vulkan with
1000 m base variation. Artifacts: Build/Validation/CloudSampling48/height-comparison.png and height-density-*.bmp.
### Shifted view samples for regional-height clouds — October 27

CloudSampling49 tests the 1000 m regional-height case against a 512-view-step reference (8 shadow steps).
The near-camera quadratic interval distribution was rejected: RGB MAE increases from 0.00835492 to 0.01202337,
and opacity MAE from 0.00959827 to 0.01681680. Without height variation, RGB error is almost unchanged
(0.00187288 versus 0.00186117), while opacity error grows from 0.00117886 to 0.00201350. This is not the
occupied-space adaptive traversal described in Nubis, and it is not retained in production.

Eight shifted interval phases averaged offline reduce RGB MAE in the height-varying case to 0.00642949,
about 23% below the regular 48-step result; opacity MAE is 0.00951131. Visual inspection shows fewer fine
interior bands, while broad repeated forms remain. This supports temporal integration for sampling artifacts,
not a claim that temporal integration fixes cloud morphology. Reference noise filtering changes with step size,
so this is a numerical comparison with the current model, not a physical ground truth.

The shared kernel now supports a positive RaySamplePhase and preserves both clipped end intervals.
Zero retains the original midpoint path. All normal view/material producers explicitly set zero: jitter must
not be enabled before history reconstruction is connected. The diagnostic phase-average mode averages eight
phases in one dispatch so both backends can reproduce the experiment; it is not native temporal history.
Five geometry cases / 11,184 assertions pass, including gap-free interval partitioning and homogeneous optical
thickness across phases, sample counts and distant entry positions. Production fragment and diagnostic compute
shaders compile to DXIL and SPIR-V. Native history/reprojection integration remains the next rendering gate.

Required integration remains: dedicated unexposed HDR cloud radiance/opacity and depth history; per-surface
published/writer ownership; successful-submission-only publication; current/previous camera and horizontal-wind
reprojection; geometry/depth rejection; environment and lighting discontinuity invalidation; resize/device/cut
reset; and explicit MSAA/direct-material fallback behavior. Existing tone-mapped scene TAA is not sufficient.
The lighting-cache vertical precision also needs review: expanding a fixed 33-slice grid for regional base
variation reduces detail. A grid following the local cloud base is a candidate, not yet implemented or accepted.
The rebuilt diagnostic's disabled-phase D3D12 output is bit-identical to the prior 1000 m-height image.
For eight-phase output, D3D12/Vulkan RGB mean absolute difference is 3.004282e-7 (maximum 0.0010766983);
opacity mean difference is 4.2487025e-7 (maximum 0.001947403). All outputs are finite. These bounded fixed-view
comparisons do not establish moving-view or whole-renderer backend acceptance.

### Cloud depth for temporal reprojection — October 27

The shared transport kernel now returns an opacity-weighted mean cloud distance along the view ray. Each
piecewise-constant interval uses the analytical extinction-event mean rather than its midpoint, so very dense
foreground intervals do not place history behind their visible front. Small optical depths use a cancellation-safe
series; thick intervals use the asymptotic limit. The distance remains in metres, is bounded by the clipped cloud
interval, and is zero for rays without measurable cloud opacity. Depth accumulation uses actual interval optical
depth even when radiance transport clamps its exponent for numerical safety. Early opaque termination normalizes
by the accumulated event weight rather than inventing a contribution from the omitted tail.

CloudVolumeViewTransport exposes this distance; existing consumers retain the prior radiance/transmission path.
No temporal buffers or motion reprojection are connected yet. The diagnostic depth-check mode checks shifted
sample phases, finite/bounded depth, empty rays, density monotonicity and forward movement of representative depth
when extinction triples. All 384,000 rays pass on each of D3D12 and Vulkan for the 1000 m regional-height case.
These are fixed-view data checks, not a moving-camera history test.

Five lighting cases / 373 assertions pass, including independent Simpson quadrature of the event distribution
and homogeneous interval-subdivision invariance. Background fragment compilation passes DXIL, SPIR-V and MSL;
diagnostic compute compilation passes DXIL and SPIR-V. The ordinary D3D12 image remains bit-identical to the
preceding 1000 m-height baseline. No Metal runtime acceptance is claimed. Evidence: Build/Validation/CloudHistory50.
### Cloud history resource lifetime — October 27

Dedicated half-resolution cloud history storage is implemented but not yet allocated by the rendering passes.
It uses RGBA16F for unexposed radiance/opacity and RG32F for cloud/scene distances, avoiding float16 distance
overflow beyond 65 km. Each surface has one published pair and independent current/history pairs per frame slot.
Allocation failure rolls back the candidate and retains the previous resources. Frame recording discards stale
pending metadata; only successful submission publishes recorded history. Frames without a cloud recording
invalidate reuse. Resize retirement follows the existing SurfaceResources exchange/deferred-release convention.

The standalone AddressSanitizer lifecycle suite passes 6 cases / 945 assertions, including each allocation
failure, failed replacement, repeated release, abandoned recording, writer isolation and deferred retirement
while replacement resources remain live. The existing light-cache lifecycle suite passes 8 cases / 241 assertions;
its uniform-size check now follows the actual ABI type instead of a stale literal. Evidence: CloudHistory51.
Trace, reprojection, rejection, resolve and composition passes are still pending. No temporal appearance or
performance improvement is claimed from resource allocation tests.
Focused renderer syntax checks, clang-format checks, diff whitespace checks and source budgets also pass. No native cloud-history rendering has been enabled.

### Dedicated cloud trace pass — October 27

CloudVolumeView now separates ray construction and unexposed cloud-event transport from current-frame air
composition/exposure. The ordinary background and transparent-surface entry point retains phase zero and its
prior composition. BuiltinCloudTrace writes unexposed radiance/opacity and cloud/scene distances to two targets;
scene distance -1 marks sky. Shifted sampling is explicitly controlled and its frame ordinal is bounded before
float conversion. Both Windows and Unix built-in shader generators include the new trace source.

The trace vertex/fragment and existing background fragment compile to DXIL, SPIR-V and MSL. A dedicated GPU
regression probe retains the former inline view calculation and compares it against the shared split path:
8,192 rays vary perspective/orthographic projection, air enabled/disabled, sky/opaque surface depth and exposure.
All pass on Direct3D 12 and Vulkan. Remaining probe pixels are deliberately zero; the 384,000-pixel output size
is not the number of evaluated cases. Evidence: Build/Validation/CloudTrace52.

The native renderer does not invoke this trace pass yet. History reprojection, rejection, resolve, depth-aware
composition and pipeline recording remain to be connected before shifted sampling can be enabled in the editor.
No native appearance/performance or Metal runtime acceptance is claimed.

### Cloud history resolve and reset policy — October 27

BuiltinCloudResolve now reprojects cloud events separately from opaque endpoints: cloud motion includes wind,
while geometry uses camera translation only. It rejects invalid/out-of-view projections, incompatible cloud
depths and sky/foreground disocclusion, clamps reused radiance/opacity to the current 3x3 neighborhood, and keeps
current depth metadata unblended. Sky cloud projection is allowed beyond the opaque geometry far plane.
The shader compiles as vertex/fragment for DXIL, SPIR-V and MSL; reflected Metal bindings contain only the four
intended textures/samplers and three constant buffers. Runtime GPU resolve validation is still pending.

The CPU reprojection policy uses successful consecutive frames, unjittered projection identity, stable shape
and atmosphere settings, bounded camera/wind displacement, lighting continuity and camera-cut detection.
Exposure and projection jitter do not reset unexposed cloud history. Horizontal phase wraps remain continuous.
Vertical noise advection currently resets history because it does not rigidly move the authored height envelope;
phase quantization tolerance scales with pattern size, including the supported 100 km pattern limit.
The 112-byte resolve constants carry previous rotation/view projection, cloud offset, geometry offset and history
controls. The initial weight/depth tolerances are implementation candidates and still need motion QA.

Focused tests pass: shared HLSL depth/scene rejection 2 cases / 24 assertions; host reprojection 4 cases /
25 assertions. Evidence: CloudResolve53. Trace/resolve/composition pipelines are still not invoked by the native
renderer. Next connect depth-aware composition, pipeline ownership and recording, then validate with actual
moving cameras, wind, foreground silhouettes and environment edits. No visual acceptance is claimed.

The preceding full fast script checks completed successfully: Windows 156.88 seconds and Unix 444 seconds.
Unix POSIX publication-lock checks were explicitly skipped because Windows Python lacks fcntl; Linux/macOS
coverage for that unrelated gate is not claimed. Latest shader-generator scripts pass PowerShell/Bash parsing.

### Cloud composition and native recording integration — October 27

BuiltinCloudComposite reconstructs half-resolution cloud radiance/opacity with four depth-compatible taps.
If no tap represents the full-resolution foreground surface, it traces that exact ray instead of borrowing sky
clouds or erasing the cloud contribution. Current-frame atmosphere and exposure are applied through the shared
ComposeCloudVolumeTrace function. All composite vertex/fragment targets compile (DXIL/SPIR-V/MSL), and the
8,192-case legacy-view regression still passes on D3D12 and Vulkan after extracting the composition helper.
This proves equivalence of the shared path, not runtime acceptance of the new upsampling shader.

Native single-sample cloud recording now invokes trace, resolve and composition. History data is published only
after successful submission; rejected history uses unshifted trace samples. MSAA and direct material paths keep
the existing transport. Pipeline cleanup covers shutdown and device recovery. The cloud recording and pipeline
helpers live in a dedicated internal module rather than growing the renderer header beyond its source budget.
Built-in shader generation succeeds and source budgets pass for 1,871 first-party files. Evidence: CloudComposite54.

The new native path is not yet staged or visually tested. Pending checks include full C++ build, resource and
pipeline lifetime under failure, runtime shader binding validation, full/half-resolution silhouette comparisons,
wind/camera motion, temporal rejection quality, HDR range, memory/time cost and editor/player behavior.

### HDR history range audit — October 27

A source-level edge case remains in the new history format: DirectionalLightComponent accepts intensity up to
100,000 and nonnegative HDR RGB colors, while cloud radiance is stored unexposed in RGBA16F (finite maximum
65,504). Forward scattering can raise radiance further. The existing final composition applies exposure before
clamping, so a low-exposure scene can have valid output that the intermediate history cannot represent.
The compositor rejects nonfinite history taps and falls back to direct tracing, but that is not sufficient proof
of correct filtering or acceptable cost. Reproduce with an allowed bright directional light and low exposure,
then select and test a range-preserving history representation before acceptance. Source changes are deferred
until the current DevDist build completes. This is a source-proven range mismatch, not a measured visual failure.

The CloudHdr55 isolated D3D12 probe confirms this mismatch with permitted sun intensity 100,000, anisotropy 0.9,
and the sun aligned with the viewing direction. All 384,000 raw outputs are finite; maximum linear RGB is
178,287.453125. Of those pixels, 5,470 exceed 65,504 and 5,468 become nonfinite on actual float16 conversion.
At exposure 0.01 the peak would be 1,782.875, within the final target's range. The probe's RGB includes a bounded
synthetic sky background, which does not account for the large overflow. This is an isolated transport probe,
not a rendered native history-buffer capture. Evidence: Build/Validation/CloudHdr55/range.json and raw output.

A diagnostic log2(1+L) float16 round-trip avoids the range failure in these samples, with maximum relative error
0.00542987 and mean 0.000839798. It is only a candidate representation: interpolation and temporal blending must
remain in linear radiance, and low-light precision, extremes, cross-backend execution and image quality need
verification before adoption. The production path still uses unencoded half-float history while its current
DevDist compilation finishes.

The low-light sweep rejects log2(1+L) as the preferred representation: among 200,001 logarithmically spaced
samples spanning 1e-8..1e8, 3,939 tiny values quantize to zero. A direct signed log2(L) candidate preserves every
positive sample with maximum relative error 0.00542988. The bright GPU image round-trip has maximum relative
error 0.00542979 and mean 0.000797975. It needs an explicit zero sentinel, finite-range decode handling, and
linear-domain filtering/blending. These are CPU float16 round-trip experiments, not GPU encoding validation.
Evidence: CloudHdr55/log_range_sweep.json and signed_log_sweep.json. No production encoding has changed.

The first explicit GPU half-conversion diagnostic does not meet the CPU 0.6% error bound: D3D12 reports
68,128 failures out of 384,000 samples. Decoded-value readback is finite; for the ordinary 1e-8..1e8 sweep,
maximum relative error is 0.010889301, versus approximately 0.00542990 for NumPy's nearest-rounded float16.
The explicit f32tof16 route therefore needs a separate rounding analysis; it does not prove the native color
attachment uses the same rounding. The maximum-float test also exposed an unsuitable relative-error expression:
GPU reciprocal-based division can flush at that scale. Use an absolute comparison scaled by the reference for
extreme-value tests. Candidate implementation is deferred; do not relax a bound and call visual quality proved.
Evidence: CloudHdr55/encoding-values.bmp.rgba32f. GPU probe sessions 27352 failed as designed and 70523 exited 0
with value readback. The engine DevDist build remains active, with source unchanged.

Microsoft's Direct3D functional specification (§3.2.2) requires truncation when converting to a lower-range
float format; finite out-of-range render-target values saturate rather than necessarily becoming infinity.
Accordingly, NumPy's nonfinite count above is not a native render-target overflow claim. The underlying HDR
range loss remains: saturation before exposure also destroys valid brightness information.
Source: https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm

A revised ignored GPU experiment rounds signed-log values to exactly representable half values with
nearest-even before conversion, preserving the original 0.6% error bound. All 384,000 cases pass on D3D12 and
Vulkan, including zero, maximum finite float and the sampled 1e-8..1e8 range. Extreme comparisons use scaled
absolute error instead of reciprocal-based relative error. This removes the measured conversion discrepancy
without widening the bound. Artifacts: CloudHdr55/RoundedEncodingProbe.hlsl and rounded-encoding-*.bmp.
The production shaders have not adopted this representation; interpolation/temporal accumulation must still
be tested in linear radiance, along with native storage and image quality after integration.

## Regional variety acceptance criteria (October 27)

The requested default is a partly cloudy, realistic sky with genuinely clear regions, sparse patches and
larger cloud banks. Coverage must vary over a broader spatial scale than individual cloud shapes. Cloud type,
shape displacement and base height must vary smoothly across regions rather than repeating the same silhouettes
at one elevation. Weather advection should move the regional field continuously; it must not randomly reshuffle
when the camera moves. All these controls remain independently customizable, including a uniform overcast option.

Increasing optical density must increase extinction within the existing cloud footprint, not increase the
shape threshold and shrink clouds. Validate this with fixed camera, seed, coverage and weather controls before
changing the artistic defaults. Check horizon and overhead views, regional transitions, camera travel and wind;
a numerical pass alone does not establish realistic appearance.

The new DevDist build completed successfully (session 90352, 1,948 seconds). Native editor startup and the
Independent Weather toggle were exercised. The authoring panel exposes cloud type, type variation and base
height variation when enabled. Broad softness and repetitive horizon bands are still visible in the QA scene;
visual acceptance remains open. Asset processing was still active during this initial inspection.

## HDR history encoding integrated (October 27)

CloudRadianceEncoding.hlsli now implements signed-log RGB storage, an exact zero sentinel and explicit
nearest-even half quantization. Trace encodes its output; resolve decodes current/history/neighborhood values
before clamping and blending, then encodes the accepted result. Early rejection preserves the encoded current
sample. Composite decodes every tap before depth-aware interpolation. Opacity remains linear. Both platform
shader dependency lists include the helper.

The production helper passes 384,000 encoding cases on each of D3D12 and Vulkan using
Scripts/Tests/CloudRadianceEncodingProbe.hlsl, including zero and maximum finite float. All three modified
fragment shaders compile to DXIL, SPIR-V and MSL. The wrapper command ended nonzero only because its final
source lookup used the obsolete CloudAppearanceProbe.cpp filename; the preceding shader compiles succeeded.
Native render-target storage, visual quality and temporal motion still require staged-editor validation.
No new full build has been started for this change.

The owned QA editor PID 29936 remained nonresponsive after Alt+F4. This is an unresolved shutdown finding;
its presence must be checked before launching another editor. Desktop-control helper has been closed.

Cloud HDR regression follow-up: KeireTests/Source/Rendering/CloudRadianceEncodingTests.cpp directly includes
the production shader helper with C++ intrinsic adapters. Two doctest cases / 60,012 assertions pass under
Clang C++20, covering dark-to-bright RGB, zero, maximum finite float, opacity preservation and signed tie rounding.
clang-format applied. This supplements, rather than replaces, the two GPU conversion probes. Actual color
attachment storage remains unverified. PID 29936 is now absent from both process and CIM queries; shutdown
completed eventually without forced termination, but its observed delay still needs investigation.

Native attachment validation now passes: Scripts/Tests/CloudRadianceAttachmentProbe.hlsl renders the production
encoding to an RGBA16F color attachment. Scripts/Tests/Source/CloudRadianceAttachmentGpuProbe.cpp downloads the
stored half values and checks 65,536 pixels, three logarithmically distributed RGB channels plus exact opacity,
including black and maximum finite float. D3D12 and Vulkan each report zero failed channels with the same 0.6%
error bound. This validates attachment conversion, not temporal motion, final composition or cloud appearance.
Both vertex and fragment probe shaders compile to DXIL/SPIR-V. Standalone Clang C++20 host build passed.
An initial attempt to reuse the MSVC response file with clang-cl failed argument parsing; the explicit standalone
Clang command succeeded. Resource cleanup is scoped; ASan validation of this new probe remains pending.

ASan probe validation could not link with this installed Clang/MSVC combination: stl_asan.lib is missing,
including when container annotations were disabled. No ASan pass is claimed. The normal standalone native
probe passes on both backends; its new host ownership still needs sanitizer coverage on a compatible toolchain.

Native density workflow validated in the updated DevDist editor: fixed SandboxShowcase camera, paused 09:00,
coverage 0.6 and independent weather disabled. Editing Cloud Density from 1 to 2 visibly increased optical
thickness and apparent cloud extent rather than shrinking the footprint. Enter committed the edit, Ctrl+Z
returned density to 1 and restored the prior view, and Save Environment cleared the unsaved indicator.
This is a single fixed-view comparison, not general visual acceptance. Broad softness and repeated horizon
shapes remain. The editor/helper were asked to close after the test; verify process termination before relaunch.

## Regional coverage range limitation (verified source audit)

VolumeCloudRegionalCoverage currently adds a bounded perturbation using min(coverage, 1-coverage).
At global coverage 0.6 and regional strength 1, its mathematical output range is [0.2, 1.0], even if the weather
field spans all of [0, 1]. The three-octave weather field generally samples a narrower interval, so actual
regional contrast is smaller. This does not prove every ray is cloudy (shape erosion can remove bodies), but
it proves the regional field cannot explicitly designate a clear region at that coverage. The current guide's
clear-region workflow therefore exceeds what this mapping alone guarantees.

Next change: introduce an independent-weather coverage mapping with genuinely empty and dense regions,
continuous transitions, monotonic coverage authoring, preserved empty/full endpoints and strength-zero identity.
Keep legacy scenes on the existing mapping. Validate spatial clear-region area and transition continuity,
not just bounded scalar output, then compare native regional views before selecting defaults. This is a
confirmed authoring limitation, not a completed fix; avoid claiming random noise or increased density solves it.

Independent weather now uses VolumeCloudWeatherCoverage: a smooth threshold moves across the independent
weather field as coverage increases. Full regional strength can reach zero and one; zero strength retains
uniform authored coverage. The legacy atlas-based branch still uses VolumeCloudRegionalCoverage unchanged.
The transition width is currently an internal 0.2 weather-value interval, not an artist control; native visual
validation is required before settling this default. Strength below one intentionally mixes regional and uniform
coverage, so fully clear region guarantees apply only at full regional strength.

CloudWeatherTests: five cases / 59,923 assertions pass. Includes a 128x128 spatial grid at coverage 0.6, seed 1,
region size 8 verifying more than 100 clear, dense and transition samples each, nearby-point continuity,
monotonic coverage, endpoints, disabled-strength identity and existing wrapping/height tests. Updated trace
fragment compiles DXIL, SPIR-V and MSL. Shader/importer cache version updates are still required before the
next staged build so cached material programs adopt this semantic change. No new build started yet.

## Corrected regional image inspection

The first regional-appearance probe command passed density 0.004, but the host expects the UI multiplier and
the shader multiplies it by physical extinction 0.004. That image was 250 times too thin and cannot support
appearance acceptance. Re-ran with multiplier 1 and ambient 0.416: regional-corrected.bmp has finite output,
mean opacity 0.779672 (coverage 0.6, region 8, height variation 1000). Visually this camera lies under a broad
soft bank with strong horizon bands; no realistic-default acceptance.

A second diagnostic with coverage 0.45, region 2 and height variation 0 has mean opacity 0.250526 and visible
clear gaps/separate banks. It demonstrates regional separation in that view but changes multiple artistic
parameters, so it is not an isolated old/new mapping comparison. Bodies remain excessively flat/soft and
horizon repetition remains. PNG copies inspected from the diagnostic outputs; these are not editor screenshots.
Next isolate morphology/profile/type and sampling contributions before another expensive version-changing build.

Regional morphology isolation: repeated the exact regional-small setup at 128 view / 6 shadow samples,
and at 48 / 6 with eight shifted phases averaged. Both images retain the broad flat bodies and repeated
horizon shapes seen at 48 / 6. Mean opacities 0.250567 and 0.250527 respectively (baseline 0.250526).
This is evidence against solving this broad shape defect by simply increasing sampling. It does not rule out
sampling artifacts in other scenes. Raw-difference metrics saved in CloudHdr55/regional-sampling-comparison.json.
Next audit: at local coverage one, the body threshold is zero and the height envelope primarily thins density
rather than excluding it; high optical depth can expose the same upper/lower profile on every bank. Separate
regional occupancy from body coverage or otherwise preserve shape modulation in dense weather. Validate this
hypothesis with an isolated comparison before changing production defaults.

Isolated coverage/shape variant (ignored diagnostic only): cap independent regional coverage at the authored
global coverage before evaluating body density. All other regional-small inputs unchanged. D3D12 output is
finite, mean opacity 0.0598177 versus baseline 0.250526. Visual comparison shows broken-up smaller bodies instead
of broad flat banks, but loses too much cloud mass. This supports coverage saturation affecting shape, while
rejecting this simple clamp as the production solution. Source kernel remains unchanged by this experiment.
Artifacts: CloudHdr55/CoverageShapeVariant.hlsli, CoverageShapeProbe.hlsl, coverage-shape.png. Compile required
both core shader and diagnostic include paths; initial missing-include attempt failed, corrected attempt passed.
Next design should preserve independently authored body structure and regional occupancy without turning a
cloudy weather cell into an optically solid slab or silently reducing intended cloud mass.

Body-coverage limit follow-up: isolated 0.8 cap, same camera/weather/lighting/sampling as regional-small,
produces mean opacity 0.222353 versus 0.250526 baseline. It preserves most opacity but the broad flat silhouettes
remain visually similar. Therefore coverage saturation is not a sufficient explanation or fix. Do not introduce
a new authoring control on this evidence alone. Both cap variants remain ignored experiments; no production
change. BodyCoverage08Variant.hlsli preserves the second source, CoverageShapeVariant.hlsli restored to the first
0.45/global-coverage experiment. Next isolate authored vertical envelope and the vertical-to-horizontal noise
scale; retain a mass comparison so apparent improvement is not merely cloud removal.

Vertical morphology diagnostics: one shader with three modes keeps camera, weather, lighting and density
fixed. Mode 1 reproduces regional-small mean opacity 0.250526. Mode 2 scales vertical noise frequency by 2.5
(with conservative LOD footprint adjustment), mean opacity 0.256795; edges change but broad flat banks remain.
Mode 3 starts the crown taper at height 0.25 instead of 0.7, mean opacity 0.192782; primarily removes cloud mass
without a convincing morphology improvement. Neither adopted. Artifacts: VerticalShapeVariant.hlsli,
VerticalShapeProbe.hlsl and vertical-{1,2,3}.png in CloudHdr55. All GPU images finite; session 15535 exited zero.
These controls alone do not solve the defect. Next compare layer thickness/body-size aspect and inspect the
underlying noise distribution before adding further authoring parameters. No production defaults changed.

Raw noise axis audit: sampled the actual SampleCloudNoise implementation on a 16^3 grid, seed 1, then
measured absolute changes at a 1/64-unit offset independently along each axis. Shape mean 0.488554,
variance 0.0209318; mean absolute X/Y/Z changes 0.0314672 / 0.0316921 / 0.0304974. This limited sample does
not indicate a flattened raw noise field. It does not validate mip sampling, all seeds or rendered isotropy.
Ignored standalone host CloudHdr55/noise-axis.cpp compiled against production CloudNoiseVolume.cpp and ran
successfully. Next isolate layer thickness versus Pattern Size (current 1200 m vs 3000 m), preserving other
inputs. No noise-generation change is justified by this audit.

Layer aspect comparison: changed only diagnostic Thickness from 1200 m to 3000 m, with original Pattern Size
3000 m and all other regional-small inputs fixed. D3D12 384,000 pixels finite, mean opacity 0.460177 versus
0.250526. Clouds become visibly taller and less pancake-like, but remain soft and horizon sampling bands grow
more apparent. Thus layer aspect contributes to flatness; it is not a complete appearance fix. Do not adopt
3000 m as a universal default or claim improved lighting. Artifact DeepLayerProbe.hlsl / deep-layer.png;
session 91338 exited zero. Next inspect existing shape presets and ambient-occlusion settings for a coherent
cumulus starting point, keeping it separate from shallow/overcast styles and preserving user customization.

Cumulus-style diagnostic combination: ambient occlusion 0.5 on the deeper layer retains identical opacity
0.460177 while visibly darkening interiors. Existing Defined billows shape settings (density 3, detail 0.7,
erosion 0.65, core 0.65, coverage 0.7) with independent weather produce near-overcast opacity 0.86135 in this
view. Lowering coverage to 0.45 and averaging eight phases yields opacity 0.443286, clearer gaps and more
billowing detail (deep-billows-partial.png). This last comparison changes coverage and sample reconstruction;
it is a candidate recipe, not an isolated causal test or accepted default. Forms remain stylized and repetitive
near the horizon. Preserve shape-only preset scope: do not silently change height or lighting when applying it.
Next validate this recipe in the updated editor and compare multiple viewpoints before deciding preset changes.

Authoring UI finding from native QA: at the tested 1282x754 window, Environment's fixed preview/header consumes
roughly y150..500, leaving about 200 pixels for the scrollable cloud controls. EnvironmentPreviewControls.h
always draws time shortcuts, day-fraction slider, advanced-section header and Apply Time even on the Clouds tab.
This causes repeated scrolling to reach closely related shape/weather controls. Next UI batch should keep
clock state and Play/Pause accessible while allowing the remaining preview controls to collapse, with no changes
to preview/save semantics. Validate narrow panels and keyboard access. Source remains frozen during build 2971.

Build iteration finding: Windows builtin-vfx.ps1 fingerprints all cloud includes together with the full VFX
source and regenerates every compute/vertex/fragment entry when any changes. Current regional build is
observably recompiling CSSimulate, CSSimulateOutput, CSSpawn and CSSpawnInitialize despite the cloud change
being consumed by fragment composition. BuiltinVfx.hlsl includes CloudVolumeView at file scope. A future
stage-specific cache must preserve dependencies for graphics uniforms/composition and compiler changes,
atomic output publication and cross-platform equivalence; simply dropping cloud dependencies is unsafe.
Audit matching Unix generator and cache regression tests before implementing. Do not interrupt build 2971.

## Next validation sequence agreed with user

After the current cloud UI pass, retain unresolved cloud appearance/performance findings in this backlog and
move to fog, then large-world travel. Do not imply those systems are already qualified. The current source audit
finds shared VolumeTransport numerical support but no authored GPU volumetric-fog implementation; the existing
large-world plan explicitly lists global height falloff, local-volume blending and depth-aware integration as
remaining work. Start fog with a documented overlapping-medium policy and settings/renderer integration, then
validate opaque and transparent depths, sky/space exclusion, edit/Play/player parity and quality costs.
Large-world qualification must subsequently exercise ordinary editor/player workflows for long-distance travel,
ground-to-space transitions, reference-frame changes, streaming and physics precision. Cloud polish remains open.

## Navigation frame-time investigation (2026-10-27)

The user reported approximately 74 ms frames during camera movement and requested a 10 ms maximum target.
This is an unverified target, not an achieved performance claim. Qualification must record sustained movement,
frame-time distributions and worst observed frames at the user's maximized viewport, with CPU, GPU and queue
measurements. Keep cold imports and debugger overhead separate from steady-state navigation.

Source audit identifies a motion-dependent cloud-cache rebuild candidate: MakeVolumetricCloudUniforms includes
observer position in PatternOrigin and camera-relative base height in Geometry. MakeCloudLightCacheKey compares
both arrays exactly, and RecordCloudLightCache rebuilds the entire 129 x 33 x 129 grid (549,153 cells) whenever
that key changes. Therefore translation that changes those float values invalidates the submitted cache even
with paused weather. This establishes repeated work, not its measured share of the 74 ms frame. Rotation alone
does not change those observer-position inputs. Profile translation and rotation separately. A fix must preserve
cloud-space sample positions and lighting; removing camera coordinates from the key without changing the grid's
coordinate mapping would reuse incorrect data. Consider stable cloud-space addressing and incremental grid
updates, with wrap, height, wind, sun, weather, recovery and submission-failure tests.

The UI packet also copies referenced RGBA texture pixels every frame, including unchanged textures. Those
snapshots currently allow recovery when the render-device generation changes or an earlier packet is dropped.
An optimization needs immutable shared pixel versions with bounded retention; skipping unchanged pixels alone
would break that recovery contract. Measure packet-copy cost before attributing navigation stalls to it.

The debugger reproduction process ended without the configured full access-violation dump. This does not
establish the earlier crash was fixed or reproduced. Desktop control was stopped with Escape; no further
UI input was issued. Keep crash diagnosis independent from frame-time acceptance.

First implementation: cloud-cache compute builds now use a horizontally cell-aligned frame, with the original
camera-relative grid retained for lookup. The key describes the build frame rather than subcell camera motion.
This preserves nominal cloud-space sample positions and avoids a dispatch for horizontal subcell movement.
Vertical movement still invalidates the cache; crossing a horizontal cell boundary still rebuilds the entire
grid. Incremental updates and vertical reuse remain performance work, not completed claims.
Optimized standalone tests passed: cache lifecycle 9 cases / 272 assertions; grid mapping 4 cases / 68 assertions.
These cover submission/abandonment, subcell reuse, boundary/vertical invalidation and sample mapping through
near-wrap phases. The editor has not been rebuilt with this change, and GPU equivalence, arbitrary pattern sizes,
native motion, visual quality and frame-time improvements still require validation.

Expanded coordinate regression: optimized grid tests now pass 5 cases / 212 assertions, including pattern sizes
1, 1.3, 1234.567, 3000, 99999.9 and 100000 metres and six phases near zero, cell boundaries and the 256-pattern
wrap. Tested sample-coordinate error remains below 0.001% of a cache cell. This is CPU coordinate evidence,
not GPU lighting equivalence or an exhaustive test of the continuous settings domain. Initial test compilation
failed because this doctest CAPTURE macro accepts one argument; split the captures and reran successfully.

Additional cost audit: each cache cell evaluates ShadowSteps light intervals, with three density samples per
interval; enabled ambient occlusion adds four directions of four intervals, also with three density samples.
At the QA 6-shadow-step setting, a full 549,153-cell rebuild therefore invokes up to 9,884,754 direct-shadow
density-point evaluations before density early-outs. Ambient occlusion, when enabled, adds up to 26,359,344.
These counts explain why cache invalidation deserves profiling; they are not GPU timing measurements. The QA
baseline has ambient occlusion disabled. Preserve it and the user's approved cloud appearance during comparison.

GPU coordinate comparison (2026-10-27): added the opt-in CloudCacheMotionGpuProbe.cpp diagnostic. It uses the
production cache builder and noise upload, reads every RGBA transmission entry, and compares original versus
aligned build coordinates for phase 0.012345 and 255.99998. Each case compares 2,196,612 scalar values with
independent weather, 500 m height variation, 3000 m patterns, vertical sun and ambient occlusion disabled.
D3D12 maximum/mean absolute error: 4.05312e-6 / 1.43668e-8 for the first phase, exactly zero near wrap.
Vulkan maximum/mean absolute error: 4.05312e-6 / 1.4695e-8, exactly zero near wrap. All outputs were finite.
These are two bounded GPU comparisons, not a universal accuracy guarantee, Metal validation, rendered motion
acceptance or a timing benchmark. The diagnostic reports differences rather than imposing an acceptance
threshold. Build response is in ignored Build/Validation/CloudHdr55/cache-motion-gpu.rsp; executable arguments
are direct3d12 or vulkan. Next extend coverage to oblique sunlight and fractional patterns, then batch the
remaining performance work before the DevDist editor validation.

Vertical reuse extension: cache build coordinates now put the cloud base at zero and incorporate its prior
height into the wrapped vertical noise phase. Lookup remains at the original camera-relative height. This
allows matching normalized fields to reuse on ascent/descent; actual vertical wind/field changes still rebuild.
Exact float-key equality remains conservative: rounding may cause an unnecessary rebuild, never tolerance-based
reuse of different fields. Cell-boundary updates remain full rebuilds. Optimized lifecycle tests passed 10 cases /
294 assertions, including a one-metre observer ascent and changed vertical field. Grid tests passed 5 cases /
217 assertions. This is not exhaustive vertical-domain coverage.

The GPU diagnostic now uses oblique sunlight (0.6, 0.8, 0). D3D12 maximum/mean differences were 5.78165e-6 /
2.45499e-8 at phase 0.012345 and 0.000764012 / 1.39031e-6 near phase 255.99998. Vulkan differences were
5.72205e-6 / 2.46887e-8 and 0.000581682 / 8.35409e-7 respectively. All compared values were finite.
These observed differences are larger near wrap and need visual motion validation; no universal tolerance,
Metal support, frame-time improvement or native acceptance is inferred from the diagnostic exiting zero.

UI crash hardening: packet capture previously sanitized reset callbacks into ordinary zero-element draw
commands; SDL's ImGui backend still binds a texture before issuing such a draw. Capture now skips bindings for
empty commands and resolve removes them, including reset callbacks. Non-empty commands without a texture are
rejected explicitly. Unknown callbacks remain rejected. Focused optimized ImGui tests passed 10 cases / 1287
assertions, including reset-without-texture and missing-texture rejection, existing recovery, texture ownership
and draw-list preservation cases. No Vendor code changed. This is a verified defensive correction, not proof
of the earlier non-null invalid-descriptor crash's root cause. Native regression and crash reproduction remain.


### Empty-region density sampling optimization (2026-10-27)

Weather coverage and the authored height envelope are now evaluated before warped body and erosion atlas reads.
Zero coverage or a zero envelope returns the same zero density without those reads. This preserves sample counts,
noise settings, and the occupied-region density calculation. Focused density tests cover independence from the
skipped samples. Before/after GPU readbacks use the same shader host and settings; native editor movement timing
is still required before claiming a frame-time improvement.

### Movement capture and vertical cache precision (2026-10-27)

A 30-second user camera-movement capture recorded 1,845 application intervals: P95 37.15 ms,
P99 41.79 ms, and maximum 47.03 ms. Asynchronous GPU observations reached 46.20 ms; command recording
reached 1.02 ms. These observations are not joined by application frame ID. The earlier stationary capture
used a larger viewport, so it is not a controlled before/after comparison. The 10 ms target remains unmet.

Vertical observer motion below the cloud anchor exposed a cache-key precision defect: reconstructing the
cloud-base phase from separately rounded camera-relative floats changed the key at every 10 cm step in a
100 m sweep. Packet capture now evaluates that phase at the authored base before float conversion and
passes it separately to cache construction. Focused tests cover 2,001 vertical positions, wind, and base-height
changes; the old reconstruction fails the regression. Native performance acceptance is still pending.

The near-black QA floor remains under investigation. D3D12 tests cover authored back-face culling,
self-shadowing, and its dark material factors with unassigned texture slots in both render paths. They pass
in a controlled fixture, but do not reproduce the QA scene's daily-lighting and camera state. Earlier cold
runs also exposed variable graph-cookie results during asynchronous pipeline creation; warmed passes do
not establish startup correctness.

### Authored geometry below atmosphere sea level (2026-10-27)

The atmosphere reference sphere previously forced zero view transmission for scene surfaces below sea level. The QA plinth top is below the configured sphere, making this a concrete explanation for its black appearance independent of material and shadow reception. Surface aerial perspective now uses finite, sea-level-clamped medium density without synthetic ground occlusion; sky transport and solar planet occlusion retain their existing behavior. The shared GPU probe verifies below-ground and crossing-ground surface absorption, zero-length identity, and existing sky occlusion on Direct3D 12 and Vulkan (384,000 samples each); DXIL, SPIR-V and MSL compilation passed. Native QA appearance and performance remain unverified until the next DevDist stage.

Native QA follow-up: the staged DevDist editor now renders the original Showcase Plinth as blue, with visible object shadows, where it previously appeared black. No material or lighting values were changed for this comparison. The maximized 3440-by-1392 window retains an approximately 1097-by-880 scene viewport. A warmed 10-second stationary capture (`surface-idle-20261027.csv`) recorded 897 intervals: 89.61 average application FPS, P95 12.398 ms, P99 13.007 ms, maximum 13.991 ms. This is stationary evidence only; the 10 ms maximum target is not met and a matching movement capture remains pending. Saving the CSV produced a separate visible frame-time spike after the capture; export-path responsiveness remains an open item.

The matching maximized-window 30-second human camera-movement capture (`surface-motion-20261027.csv`) recorded 2,118 intervals: 70.59 average application FPS, P95 18.476 ms, P99 50.945 ms, maximum 80.494 ms. Asynchronous GPU duration reached 79.965 ms; CPU submission peaked at 1.347 ms and command recording at 1.569 ms. The movement spike remains reproducible and GPU-bound; canonical vertical cache phase is not sufficient to meet the performance target. The native cache diagnostic confirms one build followed by 20 successfully submitted reuses across sea level on D3D12 and Vulkan. These checks validate reuse, not editor performance. Next isolate cache rebuild cost from view-dependent cloud tracing/compositing with per-pass evidence.

The native cache diagnostic now brackets production cache recording with GPU timestamps and reports the adapter. A Direct3D 12 RTX 3060 run measured 360.057/364.344 ms for two full 129-by-33-by-129 cache builds and approximately zero time for reuse. Earlier concurrent runs varied widely (D3D12 580–709 ms; Vulkan 300–363 ms), so these are diagnostic observations, not isolated benchmark claims or an explanation of the editor’s exact 80 ms spike. The editor remained open during these probes. The probe also uses its own authored view, not a recorded editor packet. Next add per-frame cache rebuild evidence and evaluate bounded/incremental updates without silently reducing cloud quality.

### Time-preview cache diagnosis and fallback night lighting (2026-10-28)

Sustained captures now append cloud_cache_builds, cloud_cache_reuses and cloud_cache_cells. These describe
recording for observed_renderer_frame, whereas GPU duration belongs to observed_gpu_timing_frame; correlate
by frame identifier before attributing a hitch. Changing sun direction currently invalidates the entire light
cache. This instrumentation does not itself reduce that cost or establish the time-preview root cause.

When an evaluated ambient track is active and no HDR environment asset is assigned, the immutable render
packet now scales fallback diffuse/specular environment energy by ambient intensity times linear luminance.
Previously the static fallback environment retained daytime energy at night. Explicit environment assets and
projects without evaluated ambient tracks retain authored behavior. This is energy modulation of the fallback,
not a dynamic sky capture or a sky-color-matched reflection map. Native day/night acceptance remains pending.

Native follow-up (2026-10-28): DevDist build and staging passed. In the maximized QA editor, seeking midnight
darkened the plinth and lit material samples; noon restored illumination and shadows. Several emissive or unlit
showcase samples remain bright and need material-specific assessment. These observations do not qualify every
material or custom HDR environment. A noon stationary capture recorded 822 intervals, P95 13.354 ms,
P99 13.903 ms and maximum 20.004 ms. All observed frames reused the cloud cache.

A 30-second time-preview capture recorded 2,014 intervals, P95 16.061 ms, P99 69.597 ms and maximum 81.967 ms.
All 2,015 renderer observations recorded one cache build of 549,153 cells and no reuse. Joining GPU timings to
renderer frame IDs shows the largest GPU duration, 82.556 ms, also belongs to a rebuilding frame. This confirms
continuous rebuilding during preview, but does not isolate its duration from the other GPU passes. Camera framing
changed between screenshots, so this is not a strictly stationary comparison. The 10 ms target is still unmet.
Artifacts: Build/Validation/CloudHdr55/night-cache-noon-idle.csv and night-cache-preview.csv.

### Batched weather and night-cache optimizations (2026-10-28, native validation pending)

Weather hashing now uses unsigned masking for the validated power-of-two periods. Independent CPU checks cover
negative coordinates, integer extremes, all supported octave periods, and seed extremes against Euclidean modulo.
Sampling stops when finer weather octaves are unresolved; disabled regional and type-variation channels no longer
sample their fields. Zero-sun frames skip solar shadow marching while retaining ambient illumination and occlusion.
The cache key ignores direction only while all solar radiance channels are zero and invalidates on sunrise.
Zero coverage or extinction skips cache dispatch without publishing an unbuilt cache as valid.

Focused combined weather/cache/display checks passed 14 cases and 166,598 assertions. Cache lifecycle checks under
AddressSanitizer passed 11 cases and 320 assertions, including empty-cloud transitions and restoration. Shared cloud
ambient GPU checks passed on D3D12 and Vulkan; cache shaders compiled to DXIL, SPIR-V, and MSL. These results do not
establish native editor speed or Metal runtime correctness. The full DevDist build is still in progress.

Dark-gradient quantization is being addressed with linear RGBA16F scene outputs and temporal histories, followed by
stable half-code dithering at the final encoded 8-bit boundary. Dither remains outside temporal feedback. The extra
precision increases memory and bandwidth; it is an appearance correction, not a claimed performance optimization.
An isolated GPU attachment test passed on D3D12 and Vulkan. The private RGBA8 readback helper still needs to select
the encoded display texture before native regression tests run. Native cloud-ring appearance remains unverified;
missing moon illumination and authored low night ambient energy must not be confused with display quantization.

Native build follow-up: the full DevDist build passed (282 steps, about 29.3 minutes). The RGBA8 readback helper
now selects the published encoded display texture rather than the linear half-float scene texture. Its new native
regression passed six frames spanning None and TAA on D3D12 and Vulkan (32 assertions each), checking dark color
means and alpha through the complete render/readback path. Runtime Game UI and fullscreen spatial-effect tests
passed on D3D12 (76 assertions). The incremental editor relink and 42.9-second DevDist stage passed. These checks
do not establish sky appearance, motion performance, or the 10 ms target.

Native midnight inspection after this batch still shows dark cloud silhouettes and a broad horizon band; the
appearance is not accepted. The QA profile's midnight sun intensity is zero and ambient is linear RGB
(0.08, 0.12, 0.25) times intensity 0.08. MakeCloudLightingUniforms passes this directly to the cloud integrator,
which applies cloud albedo and foreground-air transport; no additional fallback-IBL multiplier appears on this
path. This explains low source energy, but does not prove the band is intended or identify every cause of darkness.
Keep ambient/art-direction assessment separate from ray-march, atmosphere, and temporal reconstruction artifacts.
The scene was inspected while an importer-version-triggered 93-asset rebuild was active; no performance acceptance
is inferred from that inspection. The foreground game and overlay also prevent treating it as an isolated GPU run.

### October 28 native time-preview follow-up

After asset processing completed (60 cache hits and 33 imports), the staged DevDist Editor was reopened maximized.
The same stationary view was used for two 30-second captures with asynchronous renderer telemetry:

| Preview | Application intervals | Average application FPS | P95 | P99 | Maximum |
| --- | --- | --- | --- | --- | --- |
| Night | 3,000 | 99.98 | 11.15 ms | 11.92 ms | 12.87 ms |
| Daylight | 2,280 | 76.00 | 14.54 ms | 15.15 ms | 16.58 ms |

Evidence is stored locally under `Build/Validation/CloudHdr55` as `night-preview-optimized-20261028.csv` and
`day-preview-optimized-20261028.csv`. All 3,001 night renderer observations reused the cloud cache; all 2,281 daylight
observations rebuilt it. Observed GPU duration averaged 9.99 ms at night and 13.13 ms in daylight. These asynchronous
observations are not an exact join to application intervals. A video overlay was active, and camera framing differs
from older captures; this is not an isolated GPU benchmark or a controlled historical speedup claim.

Neither capture reproduced an 80 ms spike, but neither satisfies the requested 10 ms maximum. Camera movement still
needs a separate capture. Midnight dark cloud silhouettes and the broad horizon band remain reproducible: disabling
the volumetric cloud layer removes them, and restoring it brings them back. This isolates the visible contribution,
not its root cause or a successful appearance fix. The cloud baseline was restored and time preview paused afterward.
Fog rendering and seamless world travel remain separate implementation and acceptance gaps.

### Zero-solar atmosphere work elimination

The atmosphere integrator now skips its solar-scattering and sunlight-transmission evaluation when all solar-radiance
channels are zero, while still integrating medium extinction. This complements the cloud shadow-work optimization;
it does not change night sky defaults or resolve the remaining silhouette appearance issue.
The focused atmosphere probe checks zero radiance and unchanged attenuation for both sky and authored-surface
transport. D3D12 and Vulkan each passed 384,000 probe samples; DXIL, SPIR-V and MSL compilation passed.
The first Vulkan run exposed that exact attenuation equality across specialized shader paths was too strict; the
comparison uses the existing attenuation check's 1e-6 absolute tolerance. Native staging and performance measurement
of this additional optimization remain pending. No frame-time improvement is claimed from the invariant test.
