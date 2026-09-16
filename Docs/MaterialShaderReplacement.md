# Material and shader replacement

Status: property-only shader-selected creation and Inspector import path implemented; full replacement and
interactive acceptance remain incomplete.

## Current integration status

The four parallel tasks and their twelve subagents were stopped at the user's request on September 12, 2026.
Their lane changes are preserved in the original `master` checkout; work continues in the original task with one agent.
The lane evidence documents distinguish implementation from tests actually run. The integrated Debug native/editor
build passed. The seed build passed 47 cases / 13,225 assertions and its Windows checks found
offscreen asset reveal and short-panel Frame All defects.
The first integrated regression run passed 143/148 native cases and 118/125 editor cases. After repairs, the focused
editor suite passed 129 cases / 14,156 assertions. Native failure retests initially passed 6/7 cases. The remaining
Windows migration failure was repaired by moving validation into a unique system temporary directory, avoiding
additional project-path nesting. On September 15, a fresh Debug/MSVC build and all 17 migration/publication cases
passed with 118 assertions, including temporary cleanup on failure and repeat application. Durable journals and
backups remain in the project. The same migration suite passed DebugASan, and the broader Debug material/shader/
compute filter passed 164 cases / 3,231 assertions. Broader acceptance remains open.
Windows control retests passed asset reveal, narrow five-pin framing, save/reopen, and tall Lit framing after lowering
the Frame All minimum zoom to the canvas's supported 20%. See `RevampProductionAcceptance.md` for provenance/timings.

Single-agent integration connected SPIR-V buffer ABI validation to compute binary compilation and added production
compiler mismatch tests for buffer symbols and strides. The C# compute bridge now keeps pointer-bearing signatures
private; managed API tests passed in Debug and Release, including a new ownership/thread/disposal regression.
The opt-in Windows/D3D12 compute hardware test passed one case / 2,074 assertions, including actual direct and indirect
dispatch, bounds checks, readback, rejected bindings/thread access, and disposal. Log:
`Temp/revamp-single-compute-D3D12.log`. This validates the independent compute device, not shared render scheduling,
device-loss injection, Linux/Vulkan, macOS/Metal, or complete migration/UI acceptance.

## Current remaining work

The sections below are historical evidence, not separate requests to repeat already completed work. The remaining
material/shader revamp work is tracked here; historical audio and scripting notes do not broaden this milestone.

- Material authoring: multi-selection with mixed values and transactional undo, and texture tiling/offset semantics.
- Shared shaders: package dependency integration, an explicit upgrade workflow, and pinning of compiler/include
  inputs and visual fixtures in addition to source bytes.
- Migration: reviewed editor UI and validation of the complete conversion before atomically publishing source index
  and runtime catalog, including interrupted publication and rollback evidence.
- Shader authoring: the complete dockable graph/code workflow, blackboard and typed subgraph extraction, node/pin
  diagnostics and navigation, compatible creation/wire insertion, generated source inspection, stage/target
  restrictions, and bounded target-specific previews.
- Compilation and consumers: common graph/code artifacts and reflection, asynchronous cancellation and stale-job
  rejection, complete dependency/cache keys, reconciled static variants, all graphics targets and required passes,
  and evidence that value edits cause zero shader compilations.
- Compute: typed graph operations and HLSL generation, backend compilation/execution, kernel/resource validation,
  opaque C++ and C# resources, binding sets, direct/indirect dispatch, scheduling/hazards, completion/readback, and
  shutdown/disposal/reload/device-loss handling.
- Production validation: cooked fixture players and both SDK consumers, visual baselines, recorded compilation and
  editing performance/memory/CPU/GPU measurements, full Windows/D3D12 interaction, Linux/Vulkan, and macOS/Metal.
- Legacy cutover: remove creation workflows only after their replacement and migration are operational; retain
  decoders and editing for unconverted assets.

## Asset reveal and narrow Shader Graph layout follow-up

Explicit asset reveal now updates the destination folder and clears the Project search before selection. The record
view refreshes even when its source revision has not changed, so creation cannot remain hidden by a previous search.
Focused tests cover same-folder reveal, another Unicode folder, and repeat reveal without redundant cache refresh.

Shader Graph pane sizing no longer forces a 320-pixel canvas into narrower docks. Compact layouts put authoring
controls on separate rows and bound combo widths. Newly opened graphs frame their nodes once without editing their
serialized positions. Pane tests cover narrow widths and both sides of the preview-visibility threshold.

The initial incremental Debug editor build and 47 focused tests / 13,225 assertions passed. The normal editor launcher
subsequently invalidated binaries after its recorded build identity changed and started a full, unrestricted build.
That build was interrupted and restarted with two compiler jobs. Windows interaction verification is pending the
replacement editor build; the VFX/custom-pass preview and broader production gates remain open.

## Ordinary material keywords and picker follow-up

Ordinary materials now expose graph shader boolean/named keyword choices, individual shader defaults, and reset-all.
Document-level validation rejects hidden keywords and invalid options. Applying an available selection preserves
properties and uses the existing material save/undo path. Keyword reflection for handwritten code remains open.

Windows interaction uncovered a dropped-first-click bug when the selected compiled variant was not yet loaded.
Pending selections now live separately from serialized source, coalesce later choices, and retain the last-good
runtime revision until the variant becomes available. Cancel discards pending work; changing shader references or
opening a different material invalidates it. Intervening property edits survive application. Broken imports retain
repair/cancel guidance instead of silently discarding the request. Within each Inspector draw, variant resolution and
keyword controls reuse one graph decode rather than reading the same source twice.

In the disposable Windows project, a prepared graph fixture exposed `UseDetail` and `Quality`. The rebuilt editor
applied `UseDetail=true` on the first click and saved `Quality=High`. Undo cleared only Quality; Redo restored it.
Reset-all returned both to shader defaults, and Undo restored the exact source bytes with both selections. The warm
material-refresh worker operation `571a6da1-af5c-4c55-ad10-702f067bac84` took 14.657 ms queued / 221.288 ms executing.
The initial four-variant graph fixture import took 15,149.323 ms. These samples include other active desktop apps and
are not a controlled compilation benchmark or proof of zero recompilation.

The shader picker also exposed generated code beside its authoring graph. Generated code in an existing graph's
generated-source directory is now excluded from that picker. Handwritten shaders and orphaned legacy sources remain
selectable, and existing references are preserved. Full code-shader target reflection/filtering remains open.

Focused material tests passed in Debug, Release, and DebugASan: 56 cases / 706 assertions in each. Coverage includes
invalid/hidden keyword rejection, no-op resolution, unavailable-variant rollback, pending choice coalescing/cancellation,
intervening value edits, material changes, and generated-source filtering. Formatting and source budgets passed.
Packaged players, SDK consumers, managed tests, and non-Windows validation were not rerun for this editor-only change.

## Fullscreen authoring and graph history follow-up

Windows control created `FullscreenAcceptance0912` through Project > Shader Graph > Fullscreen Effect. Operation
`650931dd-c42b-4726-ad79-c2799baf59e1` recorded 1.466 ms queue wait / 616.650 ms execution. The graph opened with clear
diagnostics and a flat preview. Docking, undocking, resizing, and Frame All worked; a narrow dock clipped toolbar
controls and initially placed the output outside the visible canvas. Responsive narrow-panel authoring remains open.
Editing the named Opacity input to 0.5 showed checkerboard transparency, and Ctrl+S saved the value in the source.

Keyboard Undo then exposed incorrect history routing: it undid asset creation rather than the graph edit. Redo
restored the fixture. Shader Graph now activates its own document history when the panel or its children have focus.
Output Inspectors also expand named input defaults and hide the generic master-node value, which compilation tests
confirm has no effect on the shader. Graph-history tests exercise direct context undo/redo without changing another
open document and verify that named opacity inputs do change executable HLSL.

The rebuilt Windows editor passed the graph-history reproduction: changing Opacity from 0.5 to 0.25, Ctrl+Z,
and Ctrl+Y restored the expected values without removing the asset. Ctrl+S saved 0.25, verified in the source.
Focused graph tests passed in Debug, Release, and DebugASan: 37 cases / 13,064 assertions in each configuration.
The final output-Inspector layout rebuild also passed the same Debug tests. These checks do not close the broader
graph authoring, packaged-player, SDK, or non-Windows gates.

The final layout build reopened the saved fullscreen graph with Opacity 0.25, clear diagnostics, and the transparency
preview. Selecting the output showed named defaults without expanding a tree; the ineffective generic color was absent.

Windows control also created `VfxAcceptance0912` and `CustomGraphicsAcceptance0912` through their Shader Graph menus.
Both opened with clear diagnostics. VFX operation `d279cd10-ce47-4641-a1ca-440a4f9b8a8a` measured 3.814 ms queued /
1,371.699 ms executing; custom-graphics operation `9ba8a81a-ba16-4ab5-aa8a-908e5a79a767` measured 3.292 ms queued /
523.321 ms executing. Both currently show generic mesh previews, not the required VFX sample or custom-pass scene.
Creation with an existing asset-search filter also leaves the new asset hidden in the Project list even though its
Inspector and graph open; reveal must reconcile the search filter. These remain open authoring issues. The startup
log reported a 5,322 ms managed-script build; this is not an isolated editor-loading benchmark.

Reopening the keyword material in the final picker build retained `UseDetail=true` and `Quality=High`. Searching its
shader picker for `Generated` returned no matches while the authored graph and handwritten default remained available.

## Asset operation timing and Windows creation follow-up

Asset worker completions now expose a stable operation ID, queue-wait milliseconds, and execution milliseconds.
The Console logs consumed completions with their final success, failure, or cancellation status. Execution includes
request staging, process startup, worker work, and the delay until the editor polls worker exit; it excludes catalog
adoption and preview presentation. Coalesced requests retain the original enqueue time. These measurements do not
close the shader compilation, click-to-preview, GPU, or memory performance gates above.

On September 12, 2026, Windows D3D12 UI control created `TimedMaterial0912` through Project > Material in the disposable
acceptance project. The dialog identified the shared Lit default, creation selected the new material, and the Inspector
displayed its Lit properties and sphere preview. Operation `9df683d1-caa2-4170-a261-1d0923769753` recorded 1.729 ms queue
wait and 216.904 ms execution. This is one warm-project sample, not a complete UI-action acceptance run.

The isolated Debug worker benchmark passed 1 case / 22 assertions, with five-sample medians of 409.671 ms for scene
creation and 1,368.98 ms for 20-file external imports after warming a 200-source project. Hardware: Intel Core
i7-12700F, NVIDIA GeForce RTX 3060 (driver 32.0.16.1664), 15.8 GiB reported physical memory. No builds or editor were
running during this benchmark. The results are close to the earlier 428 ms / 1,387 ms fixture measurements; the small
difference is not evidence of a new optimization. Full editor loading and all asset creation actions remain unverified.

Focused asset-operation tests passed in Debug, Release, and DebugASan: 3 cases / 107 assertions in each configuration.
Injected-clock coverage verifies malformed completion timing, empty-clock rejection, coalesced enqueue time, and
preemption retaining the newest generation while reporting cancellation. Formatting, the 1,456-file source budget
check, and `git diff --check` passed. This editor-only change did not rerun packaged players, SDK consumers, managed
tests, or Linux/macOS validation.

## UI/fullscreen preview correction found through Windows control

Project > Shader Graph > UI created and opened `TimedUiGraph0912` with clear generated-shader diagnostics.
Operation `08990a18-c91b-4f42-a001-68192d2bcbe0` measured 1.405 ms queue wait and 714.729 ms execution in the Debug
acceptance project. The creation menu still explicitly disables Compute; the compute delivery gate remains open.

This interaction exposed a target mismatch: the UI graph appeared on a lit sphere with mesh rotation and environment
controls. UI and fullscreen previews now evaluate pixel-center UVs across a flat image, composite opacity over a
checker background, and omit surface lighting and mesh acquisition. Their mesh, rotation, and environment controls
are hidden. They retain exposure and the existing bounded preview jobs. This is CPU authoring feedback, not a UI
document, framebuffer-dependent effect, or GPU backend execution test.

The rebuilt Windows editor reopened the saved UI graph with a flat white image, appropriate controls, and clear
diagnostics. Hide/show restored the same preview. Fullscreen behavior has deterministic rendering coverage but was
not exercised through Windows creation in this pass. Focused preview tests passed in Debug, Release, and DebugASan:
14 cases / 12,757 assertions in each. Coverage includes full-image color, transparency, ignoring mesh/light settings,
and cancellation during image evaluation, alongside existing mesh preview tests.

## HDR, descriptions, and variant clipboard follow-up

Graph parameter descriptions and HDR color metadata now flow through direct property reflection, generated code
manifests, and cooked shader reflection. Handwritten manifests accept the same optional fields. Validation rejects
HDR metadata on non-color declarations and descriptions over 512 bytes. Default metadata is omitted when encoding,
preserving pinned shared shader source bytes. Shader import versions advance to invalidate older reflected artifacts.
The material Inspector exposes descriptions as tooltips and uses finite HDR controls for marked properties and
legacy colors outside the display range. Graph defaults and retained legacy graph color editing also support HDR.

Ordinary materials and variants share a property clipboard. Variant copy includes resolved inherited values;
paste adds compatible explicit overrides in one source undo action, preserving ancestry, surface settings, keywords,
and incompatible history. Stable IDs match across renames; legacy clipboard entries fall back to names. Identical
repeated variant pastes are no-ops. Different stable IDs are never silently matched by reused names. Both Inspectors
now offer an explicit **Paste Across Shaders > Paste Matching Property Names** action for different interfaces.
It matches code symbols, retains destination identities, and uses the same validation and source undo paths.

Windows D3D12 interaction saved HDR metadata and a description through the graph Inspector, selected the edited
shader in a material, displayed its tooltip, entered and saved red 4.0, and undid/redid the value with preview updates.
Clipboard paste into a variant changed roughness 0.7 to 0.8 and created five explicit overrides. One Undo restored the
exact original variant source bytes; Redo restored the paste. This session also found that the graph panel had no
Ctrl+S handler despite the documented shortcut. A first routed handler still failed with text focus; the final handler
checks the exact key chord while the graph panel has focus and uses the existing staged-save path. The rebuilt
Windows editor saved a changed description with the text field still focused; source inspection verified the new
description and preserved HDR flag. Reopening the project retained the earlier HDR material value of 4.0.

Cross-shader Windows interaction switched from the editable Lit shader to shared Unlit. Ordinary identity paste
changed zero properties; explicit name paste restored HDR BaseColor 4.0 using Unlit's property identity while
retaining the Lit history as inactive data. Undoing paste and the shader switch restored the original material bytes
exactly. All six pinned shared shader source hashes still matched the project lock after these tests.

Debug and DebugASan focused native tests passed 315 cases / 32,716 assertions; Release passed 313 cases / 31,899
assertions. Final Debug, Release, and DebugASan editor runs each passed 89 cases / 1,464 assertions. Native symbolic-link subcases reported unavailable
creation privileges on this Windows host; that coverage was not exercised.
The native filter was `*ImGui*,*material*,*Material*,*shader*,*Shader*,*project*,*Project*,*UI*`; the editor filter was
`*material*,*Material*,*shader*,*Shader*,*Asset picker*,*Asset operation service*`.
Managed API regression launchers passed in Debug and Release. The changed public UI, rendering-assets, and graph
headers each passed standalone MSVC C++20 syntax compilation. Formatting, 1,456-file source budgets, and whitespace
checks passed for this follow-up.
These authoring checks do not close compute, atomic migration publication, SDK/player, performance, or additional
platform release gates.

## Ordinary material authoring follow-up

The ordinary material Inspector groups properties by shader category, offers individual reset with explicit
override/default indicators, and copies/pastes compatible property values. Clipboard matching uses stable IDs,
with name matching for legacy entries; incompatible or missing declarations are skipped. A complete paste is one
asset undo action. Resets and value edits retain incompatible type/range history until explicit cleanup.

Windows D3D12 interaction verified the Surface category, roughness 0.8 copying, individual reset to shader default
0.5, paste back to 0.8, and whole-paste Undo/Redo. Source inspection after Undo found zero explicit overrides.
The narrow-panel check also removed misleading fallback text and wrapped material help/status text.
Debug, Release, and DebugASan each passed 86 focused editor cases / 1,423 assertions using the material/shader/picker/
asset-operation filter recorded below. An older reset test was updated to require preservation and explicit cleanup
of incompatible history. No compute, migration-publication, packaged-player, SDK, or additional platform gate is
closed by this authoring follow-up.

## Individual variant inheritance follow-up

The variant Inspector now exposes Property Inheritance controls for keeping or clearing individual overrides,
including renamed properties, and a separate Inherit Surface Settings action. Compatible historical overrides are
removed together on reset so an older value cannot reactivate; incompatible values remain saved. Inactive Property
Overrides lists unavailable, incompatible, and superseded entries with explicit cleanup using the common resolver.
All these actions use source snapshot undo. Narrow-panel help wraps, and Reimport no longer follows a conditional
reset button on the same line.

Windows D3D12 interaction verified keeping inherited roughness explicitly, typing 0.8, clearing it back to the
parent's 0.5, undoing to 0.8, and redoing to inheritance. Source inspection confirmed empty property overrides while
the Double Sided override remained. Inherit Surface Settings cleared that separate override; Undo restored it.
An injected inactive-override fixture exposed an obsolete property and a superseded roughness value in the Inspector.
Cleanup retained the active 0.8 roughness and surface override; Undo restored the exact original source bytes, and
Redo reapplied cleanup. A subsequent Play edit persisted after Stop but revealed missing asset-history activation
on Inspector focus. Asset Inspector focus now restores the asset history independently of scene and Play histories.
Reset All Property Overrides also retains inactive history for the separate cleanup action.
The fixed Windows editor was retested through Play, changing Double Sided, Stop, Inspector focus, Undo, and Redo.
The edit persisted after Stop, and Undo/Redo correctly restored and reapplied the surface value. Reopening the
project also retained the earlier saved 0.7 roughness override.

Final focused editor validation passed in Debug, Release, and DebugASan: 84 cases / 1,392 assertions each, using
`--test-case=*material*,*Material*,*shader*,*Shader*,*Asset picker*,*Asset operation service*`. Changed C++ formatting,
the 1,456-file source-budget check, and `git diff --check` passed. This follow-up changes editor authoring only;
native GPU backend, managed, SDK/package, Linux/Vulkan, and macOS/Metal suites were not rerun for it.
The full revamp, multi-selection, migration publication, compute, and cross-platform release gates remain open.

## Shared graphics defaults follow-up

Create > Material now uses Kéire/Lit without shader selection. First use installs a version 1.0.0 source library for
Lit, Unlit, UI, Fullscreen, VFX, and Custom Graphics under Assets/Keire/SharedShaders. Stable source and property IDs
match across projects. ProjectSettings/SharedShaders.lock records hashes and the selected version; existing pinned
sources are verified without regeneration, and opening a project does not install the library. Installation uses
the recovery journal and refuses occupied paths or identities owned by another source. This is a project source
library, not a completed integration with registry package mounts or an upgrade-selection UI.

Shared sources are protected by the editor and asset mutation APIs. Copy to Project writes an editable source with a
new shader identity. Windows D3D12 interaction verified default material creation, its inherited Inspector properties,
and rendered preview; read-only graph node inspection; copying; editing and saving the copy; and an unchanged hash
for the original. This exposed and fixed a missing source-index publication before the isolated worker created the
first material. The material source was schema 5, with no executable graph and no explicit property overrides.

The remaining historical sections below describe earlier checkpoints. Full authoring, compute, package integration,
cross-platform and cooked-player acceptance gates remain open.

Variant editing now offers parent selection before runtime-parent loading, validates ancestry before changing the
source, and keeps rejected-parent diagnostics visible. Source snapshot undo covers parent, property, keyword, and
reset edits, with continuous edit grouping and GUID-based path resolution. The Windows D3D12 Inspector was used to
create VariantParentAcceptance, edit Double Sided, undo and redo it, switch to another parent, undo the parent switch
while retaining the surface override, and navigate from variant to parent to shared shader with the magnifier buttons.
The focused Debug editor suite passed 78 cases / 1,334 assertions.

An interactive startup also exposed a null texture binding after device recreation. The sandbox's injected device-loss
smoke reproduced the fault under LLDB in ImGui_ImplSDLGPU3_RenderDrawData. Unpublished surface commands were retained
with null texture IDs; packet resolution now omits those commands until an output is available. Regression coverage
checks missing resolvers, mixed available/unavailable surfaces, and later publication from the same captured packet.
The corrected Debug D3D12 device-loss smoke completed with exit code 0, reporting recovery from device generation
1 to 2 before subsequent Play transitions. The focused Debug native suite passed 180 cases / 5,266 assertions;
symbolic-link-specific checks reported unavailable Windows privileges. Shared-library validation earlier in this
follow-up passed 236 native cases / 6,257 assertions in both Debug and Release, and 168 cases / 3,200 assertions in
DebugASan. Those earlier results precede the subsequent variant and render-surface fixes.

Final Windows checks for this follow-up:

- Native Debug and DebugASan material/shader/project/ImGui/UI selections: 314 cases / 32,679 assertions each.
- Native Release matching selection: 312 cases / 31,862 assertions (configuration-specific hooks differ).
- Editor Debug, Release, and DebugASan material/shader/picker/asset-operation selections: 80 cases / 1,352 assertions
  each. An initially incomplete shader fixture was corrected before these final passing runs.
- Rebuilt Debug EditorDev, repaired a missing variant parent through the Inspector, undid that repair while retaining
  the last-good preview, and redid it. Choosing a descendant as parent displayed a persistent cycle diagnostic and
  left the source unchanged. A roughness drag from 0.5 to 0.8 was undone in one step, restoring inheritance while
  preserving the independent surface override. The editor exited cleanly after these interactions.
- Mesh-material graph candidates are filtered by target with source-digest caching. The live picker showed shared
  Lit/Unlit but omitted shared non-surface graphs; deterministic coverage includes compute rejection. Inspector
  properties with equal display names use distinct stable widget identities, retained across display-name changes.

These checks do not close the full revamp. Remaining implementation and release gates include complete material
multi-edit and per-property inheritance controls, shared package upgrades, atomic migration catalog publication,
the graph/code authoring redesign and all graphics consumers, compute APIs/backend execution and managed resources,
packaged players and SDK validation, recorded visual/performance baselines, Linux/Vulkan, and macOS/Metal. No new
managed, SDK-package, Linux, or macOS validation was performed in this follow-up.

The follow-up revamp adds persistent inactive overrides to schema-4 property sources. Shader selection and document
reopening reconcile missing declarations, type changes, and range changes without discarding saved values. The
Inspector lists inactive names with explicit cleanup through save/undo. Runtime revisions and imported dependencies
exclude archived data. Distinct archived values sharing a name are retained independently. This does not
yet deliver the whole revamp. The subsequent identity and migration work below adds stable serialization and recovery;
shared built-ins, target editor redesign, and compute dispatch remain open.

## Property-source implementation progress

**Create > Material from Shader** now writes schema-4 material authoring sources tagged `kind: material`, containing a
shader reference, property overrides, and surface settings. Defaults stay on the shader until explicitly overridden.
The `.keirematerial` importer preserves the historical source type and `material/default` runtime ID while importing
these sources without shader compilation. An internal adapter keeps existing material-instance consumers compatible.
Double-click, creation completion, Inspector preview, and undo use the property document and generated runtime ID.
Nested instances resolve shader keyword selections from parent to child. Surface Shader Graph creation labels are
current, and unsupported Compute creation is disabled.

Historical graph files remain unchanged. The standard Material command now opens shader selection as well.
Automatic shared default shader installation, transactional extraction of executable material graphs, and full
runtime/cook acceptance still require implementation.
The table below records the original audit; its selected-shader creation, property Inspector, and Compute-menu findings
are addressed for the new property-only path.

Windows control is restored. A disposable `MaterialAcceptance0911` project was created through the Hub using the
3D Starter template and opened in the current Debug editor. Interactive creation of a Lit Shader Graph produced
clear diagnostics and a live sphere preview. Creating a material from that selected graph opened the property
Inspector with the correct shader reference and rendered thumbnail. Audio-device output remains unverified.
After restarting the editor, double-clicking the saved material reopened the Inspector. Its Double Sided setting
was changed, undone, and redone through keyboard shortcuts; the saved source reflected each resulting state.
The unified shader selector and surface controls for shaders with no exposed properties were also observed.

A generated one-second, mono, 48 kHz WAV and a malformed WAV were placed in the disposable project's Assets folder
and imported through Refresh and Import. The valid clip displayed the expected duration, channel count, and 48,000
frames. Preview and Stop Preview were exercised. The malformed clip received a failed-import thumbnail. Testing
found stale preview text and missing worker diagnostics in the Inspector; fixes now transfer worker statuses into
the editor database and show audio import errors. This does not establish audible output or the full audio gates.
The follow-up live check displayed the media-container error, then cleared it after the malformed source was replaced
with valid WAV data and reimported. The same asset ID remained selected and its decoded metadata appeared. Stop Preview
displayed `Audio preview stopped.` as expected.

The requested direction supersedes the graph-owned material workflow in
[the previous overhaul contract](MaterialGraphShaderIntegrationPlan.md). Materials must become shader references,
property overrides, keyword selections, and supported render-state overrides. Only shaders own executable code or
graphs. New material creation and double-click must lead to the Inspector, with no Material Graph canvas.

## Workflow baseline

Unity's [Material Inspector](https://docs.unity3d.com/6000.0/Documentation/Manual/class-Material.html) exposes a shader
selector and shader-dependent properties. Its [Shader Graph getting-started workflow](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.shadergraph/Documentation~/First-Shader-Graph.md)
assigns a graph shader to a material and that material to a renderer. These are the requested interaction model.

Native workflow equivalence and Unity file compatibility are different deliverables. Importing Unity `.shadergraph`
or ShaderLab `.shader` files requires an explicit supported Unity/package version, pipeline target, node/type mapping,
include-library translation, dependency/GUID remapping, and unsupported-feature diagnostics. The existing Kéire graph
codec and HLSL manifest importer do not provide that compatibility. No Unity file-compatibility claim is made here.

## Current blockers identified in code

| Boundary | Current behavior | Required replacement |
| --- | --- | --- |
| New material | `CreateOpenPbrMaterial` embeds an executable surface graph in `.keirematerial`. | A shared default shader and a property-only material source. |
| Source identity | `.keirematerial` imports as `MaterialGraphAsset`, with a generated `material/default` subasset. | Preserve both source IDs and existing renderer references during migration. |
| Inspector | Property-based editing is restricted to `.keiremateriallegacy`. | One standard material Inspector for graph and code shaders. |
| Creation menu | Material Graph is offered; Lit/Unlit shader graphs are labeled legacy. | Material, Shader Graph, Shader Code, and Shader Subgraph workflows. |
| Undo | Fixed: authored references resolve before a runtime revision is published; missing shaders retain last-good. | Validate the same contract through the replacement Inspector and migration. |
| Compilation | Standalone material imports invoke the Shader Graph compiler. | Value-only material edits must not compile shader programs. |
| Shader Graph | Compute is selectable but compilation is explicitly unsupported. | Implement its runtime ABI/dispatch or visibly disable creation until supported. |
| Interchange | Native graph JSON and HLSL manifests only. | A separately tested Unity importer if requested. |

The initial Inspector fixes preserve unresolved shader references and authored values while allowing replacement,
and add reset-to-shader-defaults through the existing material commit/undo path. These fixes do not switch the
canonical material asset format or remove the old graph workflow.

Creation from a selected shader now preserves the selected code shader or surface Shader Graph and its exposed
defaults. Previously that action discarded the selection and created an unrelated OpenPBR material. This corrects
the existing creation path; its graph document and source format still await replacement.

## Implementation acceptance gates

1. **Source model and migration.** Introduce a versioned property-only material source. Extract each historical
   material's executable surface into a shader asset while preserving parameter identities, texture references,
   render state, keywords, source GUIDs, and generated runtime IDs. Preview conversion before writing. Publish the
   whole source/metadata/dependency transaction atomically and retain readable legacy assets. Test interrupted writes,
   duplicate names, renamed parameters, missing shaders, nested material instances, and rollback.
2. **Inspector workflow.** One shader picker for compatible Shader Graph and code shader assets; grouped properties,
   ranges, HDR colors, texture slots, tiling/offset, surface options, reset, copy/paste values, undo/redo, and preview.
   Changing shaders preserves compatible values; invalid imports leave the last-good rendered result intact.
3. **Shader authoring.** Lit/Unlit graph templates and code templates use the same reflection, variant, resource, and
   pass contract. Exposed properties, static keywords, reusable subgraphs, diagnostics, generated-code inspection,
   last-good compilation, and cancellation need end-to-end tests. Unsupported targets must be explicit.
4. **Runtime and cook.** Shared shader programs, per-material values, per-renderer overrides, material variants, pass
   routing, transparency, batching, and hot reload must produce identical editor/player results. Test dependency
   closure and both SDK consumers. Measure cold/warm shader compile time, value-edit latency, variant counts,
   CPU/GPU frame time, and memory on a documented reference scene and hardware.
5. **Interactive acceptance.** In a disposable Hub-created project, create materials and both shader types, assign
   them by drag/drop, edit values during Play, break and repair shaders, save/reopen, undo/redo, and cook/run. Capture
   visible evidence and record expected versus observed behavior. Unit tests and smoke tests are separate evidence.

## Audio and scripting acceptance

Audio testing must cover import formats and invalid sources, clip preview/seek/loop, source/listener setup,
play/pause/stop and lifetime, mixer buses/effects/sends/snapshots, spatial attenuation, voice limits, hot reload,
device fallback, and cooked playback. Offline DSP assertions do not prove audible device output.

Game-style scripting scenarios must cover all log severities, formatted and object logs, assertions, nested
exceptions, console filtering/collapse/clear/scroll, constructor defaults, reload and serialization, delayed destroy,
coroutine cancellation, asset loads, transforms and math, animator parameters/transitions/events, and audio control.
Tests must distinguish managed bridge fixtures from native integration and actual editor interaction.

## Validation availability

The installed Computer Use package uses `node_repl` plus `@oai/sky`. Initialization, window capture, and native input
now succeed with the restored connection. Interactive validation is underway; the observations above are a limited
creation/preview check, not completion of the acceptance gates.

The initial recovery/logging changes passed Debug builds of `KeireEditorTests` and `KeireTests`, 57 selected
material/shader/audio editor tests (1,059 assertions), 92 selected audio/animation/math/scripting native tests
(2,535 assertions), and 44 managed regression groups. Four changed/new C++ files passed clang-format dry-run;
`git diff --check` and the source-budget check passed. These are initial automated results, not full-stack acceptance.
No new ASan, Release, cross-platform, packaged-consumer, audible-device, or interactive UI validation was performed
for this slice. The additions do not change the native public ABI or ownership model.

The subsequent property-source and Windows acceptance pass built the Debug Client, EditorTests, and Tests targets.
The selected editor suite passed 63 tests / 1,183 assertions; the selected native material/shader/audio/animation/math/
scripting suite passed 201 tests / 5,161 assertions. The worker-status and protocol regressions passed 2 tests / 55 assertions,
including failure diagnostics, recovery, and rejection without partial state changes. The managed API harness passed
44 groups. Formatting and source-budget checks passed. Full migration, default-shader installation, packaged consumer
validation, Release/ASan validation, and the remaining interactive acceptance gates are still outstanding.

## Inactive override recovery validation — September 11, 2026

The follow-up recovery change passed the following checks on Windows with MSVC and SDL's dummy video driver:

- `Scripts/project.ps1 build -Generator ninja -Configuration Debug -Toolset msc -Target KeireEditorTests`, followed by
  `Build/Bin/Debug-windows-x86_64/KeireEditorTests/KeireEditorTests.exe --test-case=*material*,*Material*,*shader*,*Shader*`:
  64 tests and 1,156 assertions passed.
- The equivalent `Release` build and executable with the same filter: 64 tests and 1,156 assertions passed.
- `Scripts/project.ps1 build -Generator ninja -Configuration Debug -Toolset msc -Target KeireTests`, followed by
  `Build/Bin/Debug-windows-x86_64/KeireTests/KeireTests.exe --test-case=*material*,*Material*,*shader*,*Shader*`:
  111 tests and 2,665 assertions passed.
- `Scripts/Tests/test-managed-api.ps1`: 44 groups passed.
- LLVM `clang-format --dry-run --Werror` on all seven C++ files changed in this follow-up, `git diff --check`, and
  `python -X utf8 Scripts/Tests/check-source-budgets.py`: passed; the budget check covered 1,439 first-party files.
- `git status --short`: existing staged and unstaged work preserved; no generated outputs introduced or Git mutations
  performed. Build and test logs are under the ignored `Temp/material-revamp-*` paths.

The tests cover inactive save/reopen and runtime exclusion, declaration removal, type/range changes, repeated-name
history, reset and source-snapshot undo/redo, archive bounds, invalid shader transaction rollback, missing references,
and compatible packed-value normalization. They do not establish live Inspector interaction or GPU rendering parity.
No ASan, packaged SDK consumer, cooked-player, GPU/device-loss, Linux/Vulkan, macOS/Metal, or hardware performance
validation was run for this follow-up. There are no ownership or threading changes in this recovery implementation.
The full revamp remains incomplete: stable-ID serialization, shared pinned default shaders, reviewed transactional
migration, the authoring redesign and graphics-target integrations, compute APIs/backends, and production acceptance
still required implementation and validation at that checkpoint.

## Stable identities and migration recovery

Schema-5 material sources and schema-3 material variants now store ordered property-ID overrides, with names retained
only as diagnostic labels. Shared editor/import resolution preserves renames, archives incompatible values, and
prevents a new property from taking an old property's value by reusing its symbol. Nested variants resolve against
root shader reflection and can override shader defaults absent from parent overrides. Snapshot undo uses current
reflection. Inspector edits preserve the reflection storage used by the active property iteration.

Migration now supports direct property-binding conversion and exact extraction of standalone or composed surface
graphs. Extracted identities are deterministic; path and identity collisions prevent publication. The reviewed apply
overload checks a source/metadata/program fingerprint. Publication stages all source and metadata bytes and backups
before writing a prepared journal. Recovery validates every destination and backup before rollback, refuses external
edits, and is idempotent. Exclusive project open performs recovery before assets load; read-only open rejects pending
recovery. Committed backups remain under `Library/MaterialShaderUpgrade`.

Debug native validation: 162 material/shader/project cases and 3,100 assertions passed, including recovery, corrupt
backups, stale review, deterministic extraction, collisions, and repeated execution. Symbolic-link tests reported
unavailable Windows privilege. This does not establish editor-driven upgrades, atomic catalog publication, shader
visual parity, compute support, or cross-platform release readiness.

## Windows interaction follow-up

The current Debug editor was exercised through Windows input in the disposable
`Temp/MaterialAcceptance/MaterialAcceptance0911` project. Hardware: Intel Core i7-12700F, NVIDIA GeForce RTX 3060,
driver 32.0.16.1664. These observations are functional checks, not performance baselines.

- Opened the Project browser, searched for the acceptance assets, and reopened the material Inspector and preview.
- Entered Play, changed Double Sided, used Ctrl+Z and Ctrl+Y, stopped Play, and verified the saved source retained the edit.
- Created a scalar parameter in Shader Graph, saved the graph, edited the material to 0.73, and verified schema-5
  serialization retained the graph node's property ID.
- Found that node-property text stayed unapplied when Save was pressed because the separate Apply button was below the
  visible area. Save/Ctrl+S now apply staged metadata and wait for compilation; invalid drafts remain editable, and
  document switching and exit preserve pending edits. The save state is tested independently of panel drawing.
- Found that technical metadata crowded the Inspector at 1280×752. Material/shader metadata now lives under a collapsed
  Asset Details section. The Project browser's nested scrolling and overlapping default node placement remain QoL work.

The rebuilt D3D12 editor saved a typed symbol rename from Scalar to AcceptanceRoughness using Save without Apply.
The source retained the same property ID, and reopening the material preserved its 0.73 value. Editing that value to
0.74 and undoing restored 0.73. Collapsed Asset Details left the material controls visible after restart.

The native Alt+F4 test exposed an additional quit-event path that bypassed unsaved-document handling. The editor now
handles QuitEvent through the same transition checks as its window-close and Exit actions. After rebuilding, Alt+F4
displayed the unsaved Shader Graph prompt for an unapplied node draft; Cancel preserved the draft. Repeating Alt+F4
and choosing Save closed the editor after writing the new display name, unchanged property ID, and shader symbol.

Latest focused Windows validation:

- Debug and Release native material/shader/project selections: 164 cases / 3,117 assertions each.
- Debug, Release, and DebugASan editor material/shader selections: 70 cases / 1,206 assertions each; no ASan errors.
- Debug Application selection, including handled-quit behavior: 17 cases / 174 assertions.
- Managed Windows regression launcher: 44 groups.
- Rebuilt Debug EditorDev and repeated the interactive D3D12 scenarios above.
- clang-format dry-run across changed first-party C++ files, source budgets (1,450 files), and git diff --check passed.

These focused checks do not establish full release readiness. Shared shader package upgrade UI, the complete authoring
redesign, transactional catalog publication, compute execution and managed resources, packaged players and both SDK
consumers, visual/performance baselines, Linux/Vulkan, and macOS/Metal remain open implementation or validation gates.
