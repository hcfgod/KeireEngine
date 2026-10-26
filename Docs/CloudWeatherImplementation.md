# Cloud and weather implementation checkpoint

October 26, 2026. Layered-cloud settings, editor controls, clock/capture and cooked-player propagation are integrated.
The first Windows visual check proved enable/save/rendering but rejected the repeating white night-cloud appearance.
A natural-lit model with explicit legacy migration is now being integrated; its new tests and rendered acceptance
remain pending. Historical numerical results below apply to the earlier model unless explicitly stated otherwise.
This batch does not close LWE-5 or LWE-6. Weather remains a numerical foundation only.

## Files and contracts

| File | Contract |
| --- | --- |
| `KeireCore/Include/Keire/Rendering/Cloud.h` | Two optional artistic sheets, both disabled by default; value comparison. |
| `KeireCore/Include/KeireInternal/Environment/CloudEvaluation.h` | Strict validation, signed-time advection, periodic CPU density reference. |
| `KeireCore/Include/KeireInternal/Environment/CloudRecord.h` | Strict internal version-2 authoring JSON; version 1 preserves legacy appearance. Excludes runtime/preview phase. |
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
  advances rendering settings to schema 7. `RenderSettings.cpp` validates and serializes `clouds`; schema 6 and later
  require a valid cloud record, while schema 1–5 migrate to disabled defaults. Malformed records are not discarded.
- `RenderShaderDataInternal.h` appends CloudUniforms and evaluated ambient/sun lighting to SkyUniforms; the total
  size is 432 bytes (108 float-sized words, including the integer appearance/seed blocks).
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
- Windows/Unix built-in shader scripts fingerprint `CloudLayered.hlsl`; Windows also stages the include beside
  BuiltinSky. No generated header was modified directly and no shared generation/build was launched here.

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

## Validation

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
There is no editable texture, physical sun/moon scattering, self-shadow, ground cloud shadow, cloud thickness,
interior representation or volumetric render path. Atmosphere planet clipping does not make flat sheets spherical.
The sky is background composition, not a depth-integrated volume. `CreateSkyPipeline` binds a depth attachment but
does not enable depth testing, and `BuiltinSky.hlsl` does not sample scene depth. Merely binding that attachment
does not establish cloud/geometry occlusion. Forward rendering draws the sky before opaque geometry; deferred
recording draws it before deferred lighting, whose background discard preserves the sky. This ordering is distinct
from fogging geometry or placing a cloud volume in front of it. Depth-aware cloud/transparent-object interaction needs integrated
implementation and acceptance. These clouds must remain labeled layered.

### Volumetric integration requirements

The current frame graph exposes sampled scene depth before transparency, and HDR preparation precedes bloom and
tone mapping. These are integration points, not an existing volumetric pass. The actual command recording path must
also be checked: graph dependencies alone do not prove shader bindings or draw order.

- Reconstruct view rays and opaque termination distances from the active camera and sampled depth, including
  background pixels, projection variants, render scale and multisampling. Clip marches against planet geometry.
- Accumulate in-scattered HDR radiance and transmittance separately. Composite radiance plus transmittance times
  background radiance before exposure/tone mapping; do not replace transport with a sky tint or opacity slider.
- Define transparent-surface integration explicitly. Applying opaque-depth fog after all transparency incorrectly
  uses the opaque distance for nearer glass and particles; applying it only before transparency leaves those
  surfaces unfogged. Transparent shaders need an appropriate depth-indexed volume evaluation or equivalent method.
- Keep view/light sample counts and volume resolution bounded. Temporal reconstruction needs per-view owned history
  and rejection for camera cuts, settings changes, time jumps, resolution changes and reference-frame changes.
- Verify empty-volume identity, opaque occlusion, near/far transparent objects, cameras inside clouds, horizon and
  planet intersections, HDR/bloom interaction, and Edit/Play/player parity. Measure GPU time and memory on each
  supported backend before assigning production quality labels.

Volumetrics still need planet-aware shells/stable noise, weather maps, bounded view/light marches, extinction/scattering,
scene/planet depth clipping, shadows, temporal reconstruction and history, capability/budget fallback and stable near/far
transitions. Do not expose a selectable volumetric mode until a working renderer exists.

Regional volumes, seasons/altitude rules, procedural schedules, weather simulation persistence/replay, rain/snow collision
and exclusion, wetness/drying/melting, fog, lightning/thunder and accessibility remain unimplemented. No weather
consumers, production/backend qualification, GPU/memory budget measurements or soak results are claimed here.
