# Overnight editor readiness QA — September 26–27, 2026

## Scope and handoff

User authorized native Windows testing and reproducible fixes overnight, with a usable development editor/Hub
for game creation tomorrow. Work stays on master. Preserve unrelated changes and the user's game projects.
Use disposable QA projects. No Git publication or website release is authorized by this QA pass.

The `Kéire overnight editor QA` heartbeat resumes this thread every 15 minutes until September 27 at 08:00
America/Denver. Resume active work rather than launching overlapping builds or desktop input. Computer Use is
currently authorized. If the user stops it or the desktop becomes locked, stop UI input and record the blocker.

## Current build and UI state

- Latest development editor staging completed successfully with `Scripts/project.ps1 stage-editor -Generator ninja -Toolset msc`.
- Editor manifest validation passed; the updated player-support archive was verified. Log: `Build/Validation/overnight-qa-editor-stage.log`.
- Hub staging and manifest validation passed; log `Build/Validation/overnight-qa-hub-stage.log`.
- Dist editor stage: `Build/Distributions/keire-editor-windows-x86_64-Dist`.
- Hub stage: `Build/Distributions/keire-hub-windows-x86_64-Dist`.
- Native launcher required the `process:` prefix to select the staged executable instead of the older installed Hub.
- External editor 0.4.4 registration refresh succeeded and verified all 6,509 declared files. New project creation
  became available without reinstalling.
- Created `D:/Projects/KeireProjects/OvernightEditorQA0926` through Hub using 3D Starter; editor launched successfully.
- QA editor was closed cleanly through Alt+F4 before rebuilding; Hub returned. Re-select native windows before input. Scene is
  `Assets/Scenes/OvernightWorkflow.keirescene`, saved with a cube at X=2.5 and a prefab instance at X=-3.0 (latest autosave retest).
- Debug and DebugASan recovery builds/tests passed 7 cases / 103 assertions each;
  logs `Build/Validation/overnight-qa-recovery-{tests,asan-tests}.log`.
- Recovery-fixed Dist editor staging completed successfully; its manifest validation passed.
  Log `Build/Validation/overnight-qa-recovery-editor-stage.log`. Hub verification/reopening is underway.
  Native recovery retest is still required. No build sessions remain active.
- New project dialog rendered correctly and safely disabled creation without a compatible ready editor. It was
  cancelled without creating a project. No game project was edited.

## Coverage matrix

| Workflow | Status | Evidence / next step |
| --- | --- | --- |
| Native-control connection and Hub launch | Observed | Native window capture and input working |
| Hub new-project dialog | Passed smoke | Blocked state, naming, template choice, creation, automatic editor launch |
| External package refresh / verification | Passed | 0.4.4 verified after intentional local rebuild |
| Create and reopen disposable project | Passed smoke | Created through Hub and reopened after full editor restart |
| Scene primitives, hierarchy, transforms, undo/redo | Passed smoke | Cube creation, X=2.5 edit, undo/redo, exact value after scene reopen |
| Scene Save As and prefab workflow | Passed smoke | Save As/reopen and prefab instance transform/material persistence passed |
| Asset creation/import/search/rename | Pending | Include scene, material, script, texture/model fixtures |
| Script save, compile, failure recovery, queued Play | Partial | Latest text reached Play; deliberate CS1029 diagnosed; last-good Play and corrected build passed. Active-build queue not yet observed |
| Material editing and persistence | Partial | Created QA_TealSurface, roughness 0.23 survived reselection and persisted on disk; viewport material drop passed |
| Shader / Material Graph, fullscreen effect | Pending | Verify output and save/reload |
| UI Builder and runtime UI | Partial | Native title edit/save reached Game view; small preview text readability needs investigation; styles/bindings pending |
| Lighting and shadows | Pending | Deferred Hybrid/Irradyn and readable fallback |
| Animation / controller / IK | Pending | Use known test fixtures when practical |
| Audio and VFX authoring | Pending | Avoid excessive desktop volume |
| Player build and packaged launch | Pending | Development package; no public publishing |
| Final editor/Hub stage and morning checklist | Pending | Report exact tested coverage and remaining blockers |

## Findings

- Hub project availability copy was misleading: projects using the already installed 0.4.4 editor displayed
  "Requires newer engine" when that editor only needed registration refresh. Changed the shared Hub label to
  "Compatible editor needed". Opening/compatibility decisions are unchanged. Rebuilt native retest passed.
- A development package needing registration refresh is expected recovery behavior, not a renderer failure.
- Template search filtered to Sandbox correctly. The template details correctly explain why editor 0.1.0 cannot
  open schema-4 projects. Project list/grid navigation rendered normally. Light/Auto/Dark theme cycling worked;
  Dark was restored. Settings navigation rendered normally. Do not claim untested workflows are ready.
- "Find editor" on a project with a damaged matching external installation opens a new-install review. No install
  was submitted. Potential usability improvement: prioritize the existing installation's recovery path.
- Focused Hub opening/compatibility tests passed 2 cases / 8 assertions after rebuilding KeireHubTests Debug.
- Reproduced recovery banner appearing after the current session's normal autosave. Fixed WriteRecovery to avoid
  raising a restore prompt for its own new snapshot and to preserve an unresolved snapshot detected on opening.
  Added regression coverage for repeated autosaves, reopening, protection, restore, and resumed autosaves.
  Debug and ASan builds and 7 recovery cases / 103 assertions each passed; rebuilt native retest passed.
- Play, pause, stop returned correctly to Edit. Starter UI looked tiny in the deliberately short Game pane,
  but was legible after enlarging the Game view. Script-edit/failure results are recorded below.
- Material/prefab sources currently live in the QA project's Scenes folder because that was the active folder;
  valid but not a recommended project organization. No user game project was edited.
- Native C# test edited StarterUi.cs to show `QA Build 1` on click, then deliberately introduced
  `#error QA_EXPECTED_SCRIPT_FAILURE`. Console showed CS1029 at the correct source/line, and last-good Play remained
  functional. Removing the error and changing the button to `QA Recovered 2` compiled and ran correctly.
  The source is now valid; Console retains the expected historical errors. No real script failures remain in this fixture.
- Representative script measurements from the QA project Core.log: initial build preparation 18.17 ms,
  compiler setup 426.17 ms, build 2271.56 ms, publication 4.48 ms. Subsequent successful edit builds took
  1321.05 ms and 963.95 ms in the build phase; compiler setup was 0.25/0.17 ms. These are individual observations,
  not a statistical benchmark. Play startup measured 94.24, 94.25, 2.83, and 41.99 ms across these tests.
- The starter UI was legible in the larger ultrawide Game pane and its button responded correctly to native clicks.
  Small-pane text scaling, UI Builder editing, keyboard/gamepad navigation, and other layout presets remain pending.
- clang-format dry-run passed for the four C++ files changed in this overnight pass. `git diff --check` passed;
  Git reported only existing line-ending conversion warnings. Work remains on master without Git publication.
- Full Debug editor suite passed 421 cases / 18,266 assertions, with one skipped case;
  `Build/Validation/overnight-qa-editor-suite.log`. This supplements native checks; it does not prove every editor feature.
  The skipped case is the opt-in Asset worker timing benchmark, not a correctness failure.
- Asset operation observations from Client.log: scene Save As executed in 63.239 ms, material creation in
  841.021 ms, and the material edit import in 53.946 ms (queue times excluded). Initial project import was 3767.313 ms.

## Latest native pass (September 26 evening)

- Latest Dist editor reopened successfully through Hub after refreshing external registration.
- Opened OvernightWorkflow and verified prefab X=-2.5 and QA_TealSurface assignment after full process restart.
  Edited X=-3.0, waited for autosave, verified a `.keirescene.recovery` file existed, and inspected Scene view:
  no false recovery banner. Ctrl+S cleared the recovery file and unsaved state. Scene remains saved at X=-3.0.
- UI Builder opened StarterHud. Native Inspector text editing changed the title to `Overnight UI QA`; Save
  completed, the source contains the new text, and Game view displays the title. Preview 1080p/720p selection
  and zoom responded. Small text has poor legibility at reduced pane sizes; do not mark scaling as passed.
  The template card background also seemed less visible in Builder than Game view; investigate at useful scale.
- Restart initially showed StarterScene rather than OvernightWorkflow. Current EditorSession.state correctly
  records OvernightWorkflow. Reproduce another clean restart before deciding whether this is a persistence bug.
- No new C++ changes in this heartbeat. Earlier build/test results remain valid.

## Next continuation

1. No active builds. Native editor is open in Edit/Game view, 1280x752 window, Starter UI Document selected.
   Re-observe native window before input. Disposable project only; all scene/UI edits saved.
2. Continue UI Builder preview/runtime comparison at usable zoom and pane size, styles/bindings, and investigate
   reduced-scale font readability. StarterHud title is `Overnight UI QA`; valid StarterUi.cs says `QA Recovered 2`.
3. Reproduce clean application reopen remembering last scene; current session state records OvernightWorkflow.
4. Continue asset rename/search, graphs, lighting/shadows, animation/audio/VFX, and player build.
   C# latest-build, diagnostics, last-good failure behavior, and corrected-build behavior already passed.
   Actual queued-Play-during-build behavior remains unobserved.
## 04:06 UTC heartbeat follow-up

- No overlapping build processes; branch remains master. Native editor and Hub were the only relevant running apps.
- Enlarged Builder to render its 1080p canvas at about 1600x900 displayed pixels. The card background and border
  are present, and title/body/button text are readable. The earlier missing-card concern was low contrast against
  the preview clear color, not missing template content. Reduced fit previews still lose small glyph detail.
- Native Styles workflow passed: selected UI/Starter.keirestyle, selected .starter-copy, opened Typography using
  its disclosure arrow, changed Font Size 18 -> 24, observed immediate preview reflow, saved via Builder toolbar,
  and confirmed both disk source and Game view use 24px. No engine source changes were made in this pass.
- At 24px, the first letter of the wrapped body text appears slightly clipped at the left edge in both views.
  Investigate font glyph bounds/text clipping before claiming full typography correctness.
- Current editor is maximized (3440x1392), in Edit/Game view, Builder closed, Starter UI Document selected.
  All edits saved. No active build/test sessions. Prior 421-case suite remains the latest complete editor run.
- Next: finish typography clipping diagnosis, then clean reopen/session persistence and remaining workflow matrix.
  Do not repeat successful basic material/script/recovery checks without a new reason.

## 04:21 UTC heartbeat — confirmed wrapping defect

- Reproduced leading-glyph clipping as a deterministic text-layout defect: the starter body at 24px/380px
  produced a 388.348px line. A second AA/space boundary case also overflowed its available width.
- Added `Runtime UI wrapping excludes break opportunities beyond the available width` in UiToolkitTests.cpp.
  Before fix: 1 case failed, 2 failed / 9 assertions; log `overnight-qa-text-wrap-before.log`.
- Fixed RuntimeUiText.cpp to record the current glyph's break opportunity after overflow handling, so a break
  beyond the previous line width cannot be selected for that line. No public API change.
- Debug KeireTests build passed. Focused filter `Runtime UI wrapping excludes*,Runtime UI fallback text*,Runtime UI text*`
  passed 6 cases / 179 assertions; `Build/Validation/overnight-qa-text-wrap-fixed-tests.log`.
- clang-format applied and dry-run passed on both changed C++ files; git diff --check passed. Changelog updated.
- Dist stage-editor is RUNNING as exec session 19240; log `Build/Validation/overnight-qa-wrap-editor-stage.log`.
  Poll this before starting any other build. Native QA editor was closed cleanly; re-enumerate windows to find Hub.
  After staging validate manifest, refresh Hub registration, reopen QA, and visually retest body wrapping.
- Separate confirmed bug: after Alt+F4, EditorSession.state changes the saved scene UUID to `none`.
  ProcessSceneTransition Exit calls CloseScene before OnDetach SessionPreferences serializes the empty document.
  Fix still pending; preserve last-open scene without weakening normal close/exit cleanup. Add focused coverage.
- No user game projects changed. All QA scene/UI edits saved before closing.

## 04:36 UTC heartbeat — last-scene persistence fix

- Wrap-fixed stage session 19240 completed successfully; package manifest validation also passed (session 55576).
  Player support verified: SHA256 0b3b0df8e3492006e84664dc8256d402161f0dea09480188142e0ed143eefef8,
  407250003 bytes. Native text retest is deferred until the next combined stage is ready.
- Fixed the confirmed last-scene reset: preference-only writes now load the persisted session and update only
  MaximizeGameOnPlay. Scene identity remains owned by the existing successful-open/Save As persistence path.
  CloseScene and exit cleanup are unchanged. Internal helper SaveEditorSessionViewPreference has focused tests.
- Debug KeireEditorTests build passed; `Editor session*` passed 4 cases / 22 assertions, including empty-session
  creation, repeated updates after scene closure, and failed writes preserving the existing scene file.
  Logs: `overnight-qa-session-build.log`, `overnight-qa-session-tests.log`.
- clang-format dry-run and git diff --check passed. README and changelog updated. No public engine API change.
- Started full Debug editor suite followed by stage-editor ONLY if tests pass. Logs:
  `Build/Validation/overnight-qa-session-editor-suite.log` and `overnight-qa-session-editor-stage.log`.
  Check the current exec session before any further build. Hub window 1180212 is present; QA editor is closed.
- Next native pass: verify final stage manifest, refresh Hub external registration, open disposable QA project,
  open OvernightWorkflow manually once (old shutdown persisted none), confirm unclipped 24px body text,
  then close/reopen and confirm OvernightWorkflow restores without manual selection.
- Active combined test/stage exec session: 29544. Poll before starting a new build; stage log may not exist until the suite finishes.

## 04:51 UTC heartbeat — rebuilt native verification passed

- Combined session 29544 completed exit 0. Full Debug editor suite: 422 cases / 18,278 assertions passed,
  1 opt-in benchmark skipped. Development stage completed; editor manifest validation passed.
- Broader `Runtime UI*` regression filter passed 34 cases / 548 assertions:
  `Build/Validation/overnight-qa-runtime-ui-tests.log`.
- Hub detected the intentional local package manifest change, refreshed external registration successfully,
  and launched the current Dist editor on OvernightEditorQA0926.
- Native wrapping retest PASSED in Game view: 24px body now wraps before `to`, and the leading D is intact.
  Rechecked at two Game pane heights with no leading clipping.
- Native last-scene retest PASSED: opened OvernightWorkflow, closed via Alt+F4; stored UUID remained
  9c5e2b5f-742a-4ee5-8302-22d45ba83ffc. Reopening from Hub automatically restored OvernightWorkflow and 7 entities.
  Console showed 0 warnings / 0 errors after reopening. Both fixes are in the current development stage.
- Current native editor handle 460332, same staged executable, maximized 3440x1392, Edit/Game view,
  Console bottom pane. All QA changes saved. Hub hidden while editor runs. No builds or test sessions active.
- Next prioritize new coverage: asset search/rename, Shader/Material Graph authoring, lighting, animation/audio,
  and a native player-build workflow. UI bindings and keyboard navigation remain untested this overnight pass.
  Avoid spending the remaining night repeating already-passed basic workflows.

## 05:06 UTC heartbeat — asset and fullscreen graph authoring

- Asset search is scoped to current folder: QA_TealSurface did not appear from Assets root, appeared in Scenes.
  No global-search claim; scope could be clearer in the UI.
- Native context-menu rename QA_TealSurface -> QA_TealSurface_Renamed passed. Roughness remains 0.23.
  Compiled subasset ID 3f7c8273-d31b-5ca0-b53d-5134e5a181b5 remains referenced by QA_SurfaceCube prefab.
  Both cubes remain rendered. No user project touched.
- Created Scenes/QA_Vignette via + > Shader Graph > Fullscreen Presets > Vignette. Graph opened with
  GENERATED SHADER READY, 10/10 active nodes, clear diagnostics. Approximation preview explicitly explains
  assigning a material to the camera to view actual GPU output.
- Created Scenes/QA_VignetteMaterial through Material from Shader; selected shader automatically carried over.
  Inspector exposed Radius 0.75, Softness 2, Strength 0.6.
- Selected Main Camera, scrolled via the Inspector scrollbar, dragged the material into After Tonemapping.
  Actual Game view immediately displayed vignette edge darkening while HUD remained readable. Ctrl+S saved scene.
  Native author/create/assign/render smoke PASSED; parameter editing and restart/package persistence still pending.
- Current editor handle 460332, restored 1280x752 window. Game view on top; Project bottom in Scenes folder.
  Main Camera selected; Inspector scrolled to fullscreen slots. QA_VignetteMaterial selected in asset browser.
  All edits saved; no builds or commands running. sky.scroll did not reliably target this pane; scrollbar drag did.
- Next: verify fullscreen strength edit and saved effect on reopen, then native player build; remaining lighting,
  animation/audio/VFX and bindings coverage should follow as time allows. No engine code changes this pass.

## 05:21 UTC heartbeat — native standalone player passed

- Native fullscreen Strength edit 0.6 -> 0 removed the vignette immediately; 0 -> 0.4 restored moderate
  darkening. Enter committed the material; source propertyOverrides confirms Strength 0.4000000059604645.
- In Build Settings, Add Open Scene and Set as Startup correctly moved OvernightWorkflow to index 0,
  preserving StarterScene as enabled index 1. Only disposable OvernightEditorQA0926 was changed.
- Native Build & Run completed successfully for Windows x64 / development / Include Symbols.
  Status JSON reports state=succeeded, phase=complete, no errorCode. UI reports Player build completed.
- Standalone visual/input smoke PASSED: both cubes, world UI, screen HUD, saved vignette and unclipped
  wrapped 24px body text rendered. Clicking Continue invoked current C# code and displayed QA Recovered 2.
  Player was closed with Alt+F4; editor remains open at Build Settings, handle 460332.
- Output: D:/Projects/KeireProjects/OvernightEditorQA0926/Build/Desktop-Development/OvernightEditorQA0926.exe
  Evidence: Library/PlayerBuild/c0ffb1b7-c58b-40db-b926-9b907bdf3aa7/{status.json,builder.log} in QA project.
  Managed build timing: preparation 12.98 ms, setup 666.44 ms, build 1052.15 ms, publication 6.42 ms.
  These are one observed build, not benchmark medians. No engine changes or rebuild necessary this pass.
- git diff --check passed; existing broad working-tree changes preserved. No staging/commit/publish performed.
- Next coverage: graph node editing, lighting/shadow quality, animation/audio/VFX, UI keyboard/binding workflows.
  These remain untested or partial; standalone smoke does not establish exhaustive game readiness.

## 05:36 UTC heartbeat — directional shadow fixture

- Confirmed master, no active build/test commands. Current staged editor remains running on the disposable project.
- Native light Enabled off made both cubes unlit; Ctrl+Z restored enabled state and direct illumination.
- Created an additional built-in Cube using Entity > 3D Object/Cube. Renamed QA Shadow Ground,
  position (0,-1,0), scale (20,1,20). Transform edits previewed immediately and committed on focus change.
- Both cubes cast visible directional shadows onto the ground. Shadows Soft -> Disabled removed the cast
  ground shadows; Ctrl+Z restored Soft and visible shadows. Ctrl+S saved OvernightWorkflow (now 8 entities).
- Inspector widening and Game/Project pane resizing worked. Current editor is 1280x752, handle 460332;
  Inspector starts x894, Game bottom y562, Project bottom pane is short. Directional Light selected.
- No defect reproduced in this pass; no engine code changes or rebuild. Existing player package predates ground.

### Current coverage checkpoint (native smoke, not exhaustive)

| Workflow | Status | Evidence / limits |
| --- | --- | --- |
| Hub create/open/reopen, template, registration | Passed smoke | Disposable 3D Starter project; current development stage |
| Scene edit, save/reopen, undo, prefab instance | Passed smoke | 8-entity OvernightWorkflow; earlier last-scene fix retested |
| Material create/edit/rename/reference | Passed smoke | Roughness and fullscreen Strength persisted |
| Fullscreen shader preset/material/camera/player | Passed smoke | Vignette created natively; packaged output and script callback verified |
| Script edit, compiler failure/recovery, Play | Passed smoke | Last-good code and recovered build; queued Play still untested |
| UI Builder edit/style/runtime wrapping | Partial | Native edits and player text passed; bindings/navigation/DPI matrix pending |
| Directional lighting and shadow controls | Partial | Enabled and Soft/Disabled/Undo passed; simple ground fixture only |
| Rendering modes / Irradyn / moving-light stability | Not tested this pass | Requires mode comparisons and temporal observation |
| Graph node editing, animation, audio/VFX | Not tested this overnight pass | Preset authoring alone does not cover these |
| Native player build/run | Passed smoke | Windows x64 development output, not release certification |
| Long soak / representative game performance | Not tested | No frame-rate or memory stability guarantee |

- Next prioritize rendering-mode comparisons with the new ground, then animation/audio and UI keyboard/bindings.
  No staging, commits, publishing, or changes to another project.

## 05:51 UTC heartbeat — Forward+ / Deferred Hybrid / Irradyn smoke

- Confirmed master and no active builds/tests before UI work.
- Project Settings originally showed Forward+, no AA, render scale 1, dynamic resolution disabled, GI Disabled.
  This identifies the mode used by the preceding directional-shadow and player checks.
- Native Render Path -> Deferred Hybrid: active-mode label updated; Game output retained both cubes,
  their ground shadows, HUD/world UI, and vignette. No obvious visual discrepancy in this simple fixture.
- Native GI -> Irradyn Dynamic GI exposed Balanced quality; active-mode label confirmed Deferred Hybrid /
  Irradyn. Indirect illumination substantially brightened the ground and cubes; shadows remained visible.
- Changed directional-light Y rotation from approximately -30.009 to 60 degrees. Cast shadows moved to the
  opposite side as expected and remained visible after committing. Saved scene with Ctrl+S.
- Persistence read confirms ProjectSettings/Rendering.keiresettings: renderPath=deferredHybrid,
  globalIllumination=irradyn, irradynQuality=balanced. QA project now uses these settings.
- Coverage update: rendering modes/Irradyn is PARTIAL smoke coverage, no longer wholly untested.
  No persistent black output or missing geometry observed. Screenshots cannot certify transient flicker,
  temporal convergence, energy correctness, large scenes, all materials, or performance.
- Still pending: Irradyn Quality/Performance, realtime-environment and baked modes, AA combinations,
  point/spot shadows, graph-node editing, animation/audio/VFX, and fuller UI navigation/binding coverage.
- No engine defect reproduced, code changes, or builds this pass. Existing standalone package predates
  the ground and new rendering settings. Current editor handle 460332, Directional Light selected,
  Game view visible; 8-entity scene saved. Other projects untouched.

## 06:06 UTC heartbeat — controller and mixer authoring

- Native + > Animator Controller created Scenes/QA_Controller.keireanimgraph. Opened editor, added layer,
  added float parameter, renamed it Speed, validated and saved/imported successfully. Source confirms layer
  and Speed parameter persistence. Empty layer validates; no clip/state playback claimed.
- Animator window initially appeared only ~240px tall, hiding graph controls. Native bottom-right resize
  exposed all controls. This is a usability concern to investigate (saved layout vs initial size); no code fix yet.
  State canvas context menu directs users to drop animation clips to create states. Disposable project has
  no imported animation clip/rig fixture yet, so playback/transitions remain untested.
- Native + > Audio Mixer created Scenes/QA_Mixer.keiremixer. Opened mixer, Create Reverb Return added
  a second bus and algorithmic reverb effect. Routing view, Validate, and Save all worked.
  Source confirms Reverb Return parent=Master, Room Reverb effect, wet=1; no source buses/sends exist yet.
  Audio output/spatialization/reverb-zone behavior have NOT been heard or measured.
- Coverage: animation controller authoring and mixer authoring now PARTIAL; runtime animation/audio still pending.
  No confirmed engine defect or engine source changes in this pass. No build or test commands started.
- Current editor handle 460332, Audio Mixer open on Routing, all asset edits saved. Game/Project divider y348,
  Inspector starts x894. Scene remains saved, 8 entities, Deferred Hybrid + Irradyn Balanced.
- Next: close mixer, prepare bounded animation/audio playback fixtures or continue VFX/UI bindings; investigate
  animator initial sizing if reproducible independently of saved layout. Preserve all unrelated working changes.

## 06:21 UTC heartbeat — Animator first-open size fix / stage running

- Confirmed master, no overlapping builds. Investigated the previous native 900x240 Animator window:
  AnimatorControllerPanel::Draw lacked the initial sizing request used by AudioMixerPanel and ShaderGraphPanel.
- Added ui.SetNextWindowSize({1040,640}) before BeginPanel, using existing first-use-only semantics.
  Saved user workspace geometry remains authoritative. Changelog updated; no public API or ownership change.
- Existing Stable node graph* tests passed: 14 cases / 101 assertions; this tests graph behavior, not initial
  window geometry. Log Build/Validation/overnight-qa-animator-layout-tests.log. clang-format applied and
  dry-run passed; git diff --check passed. Native first-open regression verification is still REQUIRED.
- Closed the saved QA editor through Alt+F4 and confirmed its process exited before starting stage-editor.
- ACTIVE exec session 39966: Scripts/project.ps1 stage-editor -Generator ninja -Toolset msc.
  Log Build/Validation/overnight-qa-animator-layout-stage.log. Do not overlap another build.
- Next: poll stage, validate manifest, refresh Hub registration, reopen QA. To verify first-use sizing,
  back up the disposable QA layout and remove ONLY the editor.animator-controller docking ini block while
  editor is closed; preserve other panels and the original backup. Open QA_Controller and inspect graph visibility.
  Do not report the fix verified until this native retest succeeds. No separate C++ test was added for the
  constant initial presentation size; native reproduction/retest is the direct regression check.
- QA_Controller and QA_Mixer edits were saved in the previous pass; scene stays at 8 entities.
  No commits/staging/publishing or other-project changes. Other pending coverage remains as in the matrix above.
- Prepared fresh-layout retest while editor is closed: saved session.keirelayout.before-animator-size-qa
  alongside the disposable project layout, then removed exactly one [Window][editor.animator-controller]
  block from docking data. All other layout fields/panels preserved. Backup must remain available.
  Stage session 39966 is still compiling AnimatorControllerPanel.cpp at handoff; not yet ready to relaunch.

## 06:36 UTC heartbeat — Animator fix rebuilt and visually verified

- Stage session 39966 completed exit 0. Development editor stage updated successfully; package manifest
  validation passed using write-package-manifest.py validate --artifact editor.
- Native Hub Verify detected the intentional external manifest change; Refresh registration validated the
  rebuilt package and returned Verified. QA project opened successfully and restored OvernightWorkflow.
  Console showed 0 warnings / 0 errors on reopening.
- Native first-open regression PASSED: with only the Animator window geometry removed from the disposable
  layout, QA_Controller opened at 1040x640, graph/parameters/layers/inspector all visible without resizing.
  Previously it auto-sized to roughly 900x240 with graph controls clipped. Saved Speed and layer reopened.
- The one-line presentation fix is now in the staged editor and verified. Prior graph tests 14/101 and
  clang-format/diff checks remain applicable; no additional code changes this pass.
- Layout backup session.keirelayout.before-animator-size-qa remains alongside QA layout. No user layout touched.
- Current editor window handle changed to 1245778, same staged executable. Animator panel open, saved asset;
  QA scene 8 entities, Deferred Hybrid/Irradyn Balanced. Hub hidden during editor lifetime. No builds running.
- Remaining priorities: runtime animation/audio fixtures, VFX, UI bindings/navigation, quality-mode combinations,
  representative performance and soak. Existing tested/partial matrix still applies except Animator sizing resolved.

## 06:51 UTC heartbeat — VFX authoring and GPU playback smoke test

- Native asset creation made Scenes/QA_Particles.keirevfx, opened VFX Graph, switched backend to GPU (Runtime),
  and compiled one system program (2741 bytes, zero warnings). Changed emission block input from 10 to 30
  particles/second, saved and queued import. Source confirms graph pin default=30 and executionSource=graph.
- Added one disposable GameObject with VFX Emitter, assigned QA_Particles by dragging its asset onto Effect,
  retained Play On Awake, and saved OvernightWorkflow (now 9 entities).
- Native Play Mode visibly emitted white particle sprites at the emitter, with changing positions across captures.
  Console showed 0 warnings / 0 errors. Stopping Play removed the particles; Game view in Edit was clear again.
  This verifies basic GPU authoring/import/play/stop, not every block, event, collision, trail, or mesh output.
- No engine defect reproduced or source fix needed this pass. No builds started. git diff --check passed
  (existing line-ending notices); inspected git status, preserved unrelated dirty tree and stayed on master.
- Editor handle 1245778 remains open in Edit, Game tab and Console visible, new emitter selected. Scene and asset
  saved. No overlapping build process. Existing standalone player package predates this VFX fixture.
- Coverage update: VFX basic GPU workflow PASS; advanced VFX and packaged VFX still NOT TESTED. Remaining
  runtime animation/audio, UI binding/navigation, DPI, rendering quality combinations and soak coverage remain pending.

## 07:06 UTC heartbeat — runtime keyboard focus investigation

- Confirmed master; only staged editor/Hub active, no overlapping builds. Native Play repeated VFX output.
- Reproduced initial keyboard navigation limitation: click empty Game area, then Tab focuses editor panel lock,
  not the HUD Continue button. Clicking HUD title then Tab has the same result. No runtime click fired.
- Code inspection: SelectRuntimeUiKeyboardPresentation and RouteRuntimeUiKeyboard explicitly require a retained
  FocusedUiEntity; current unit test enforces this. Do not remove ownership gates without regression coverage.
- Ran existing Debug editor tests filtered Editor keyboard routing*,Editor world-surface UI*,Game runtime UI*:
  4 cases / 36 assertions PASS. Log Build/Validation/overnight-qa-keyboard-routing-tests.log.
- Prepared explicit focus probe ONLY in disposable QA Assets/Scripts/Runtime/StarterUi.cs: after resolving
  Continue, call _continue?.Focus(). Original backup Library/StarterUi.before-keyboard-qa.cs.txt preserved.
  Editor picked up/rebuilt script (info count 34->41, no warnings/errors), entered Play. Clicked Game tab then
  Enter: no visible button activation. This result is NOT yet diagnosed; initial-focus ordering, viewport input
  ownership, and focus persistence need distinguishing. Do not claim runtime keyboard navigation passed.
- Stopped Play; scene unchanged/saved, 9 entities. Editor handle 1245778, Scene tab, Console visible. No engine
  source changes or package rebuild this pass. Explicit-focus probe remains in disposable script for continuation.
- NEXT PRIORITY: inspect runtime focus timing and add bounded HasFocus diagnostics to the QA script; establish
  whether Focus() survives initialization and whether keyboard reaches Game. Reproduce root cause, then focused
  regression/fix and native retest. UI bindings, audio/animation runtime, broader rendering/soak remain pending.

## 11:01–11:20 UTC resume after shutdown — keyboard submit and small-text fixes

**LATEST USER CONTROL RESTRICTION:** User is starting another project and restarting the PC. STOP all Windows
control until explicitly reauthorized. Finish safe headless work only; user requested a pause after this change.
Do not let the older heartbeat UI authorization override this restriction.

- Reopened staged Hub and OvernightEditorQA0926 through native UI before restriction. Nine-entity scene restored,
  no initial console errors. Current editor handle 263842, Hub handle 132632; invalid after restart.
- Bounded QA script diagnostic at frame 60 showed Focus retained; Enter still did not trigger ClickedThisFrame,
  whereas clicking the button did. Root cause: Navigate(Accept) emitted Submit only, with no button/toggle default Click.
- Added default Submit -> Click for visible/enabled/interactable Button/Toggle in shared scene event draining,
  after callback dispatch. PreventDefault suppresses activation; text fields retain Submit-only behavior.
- Regression reproduced before fix: 1 case / 28 assertions, 9 failures. After fix and Debug editor test build:
  5 cases / 64 assertions PASS (new activation test plus keyboard/world-surface/routing tests).
  Logs: overnight-qa-submit-before-build/tests.log, overnight-qa-submit-fixed-build/tests.log in Build/Validation.
- User reported visibly broken small text. Font GPU uploads had only base level despite 48px fallback/custom atlas
  rasterization. Added bounded four-level coverage mip chain, white RGB with averaged alpha, 16px atlas gutters,
  and multi-level GPU upload using existing trilinear sampler. Fallback and custom font atlas paths covered.
- Debug KeireTests build exit 0; focused font/text tests: 4 cases / 383 assertions PASS.
  Logs Build/Validation/overnight-qa-font-mips-build.log and overnight-qa-font-mips-tests.log.
  Two nodiscard test warnings were subsequently corrected with explicit void casts; these warning-only edits have
  not been rebuilt. clang-format dry-run on all seven changed C++ files and git diff --check passed afterward.
- Docs: CHANGELOG, README, Docs/UiWorkspace.md, Docs/Scripting/UiAndEvents.md updated. No public API change.
- IMPORTANT: both new fixes are NOT yet in the staged editor/Hub or standalone package and NOT visually retested.
  Need headless rebuild/check first, then native retest only after user reauthorizes. Small-text improvement remains
  visually unverified; tiny text cannot preserve unlimited detail. Initial Tab focus/editor ownership remains a
  separate limitation; this change addresses activation of an already focused runtime control.
- Disposable StarterUi.cs retains temporary _qaFrames/Focus retained diagnostic and initial Focus call. Original
  backup remains Library/StarterUi.before-keyboard-qa.cs.txt. Restore diagnostic after retest, preserving initial-focus
  choice deliberately. No other project changed. Scene saved; editor left stopped before control restriction.
- No active shell build at pause. KeireClient may still be running until user restart. Keep master; no staging,
  commit, push, publishing. Broad unrelated working changes preserved.

## September 28 resume — native QA reauthorized

- User explicitly resumed work including Windows control. Prior pause/control prohibition is lifted for this task.
- Confirmed master and no running engine/build processes before resuming. Rebuilding font test warning cleanup,
  then staging the editor before native text and keyboard activation retests. Earlier overnight deadline no longer
  limits this newly requested work. Preserve unrelated changes; no Git staging/commit/push/publishing requested.
- Sep 28 validation: Debug KeireTests rebuild exited 0 (warning cleanup included). Focused fonts 4/383,
  custom-font limits/fallback 3/112, broader Runtime UI 35/910, editor submit/routing 5/64 all passed.
  Logs Build/Validation/qa0928-{font-build,font-tests,custom-font-tests,runtime-ui-tests,submit-tests}.log.
  C++ clang-format dry-run and git diff --check passed. Counts overlap; do not sum them as unique assertions.
- stage-editor exited 0; log Build/Validation/qa0928-editor-stage.log. Updated Dist editor plus Windows player
  support (sha256 71adeb08758f8d84055de18808c04e5d7f128c7f1536549b13daf21acdadbfe0).
  Editor manifest validation passed. Native Hub verification/reopen and visual comparison now underway.
- Native Sep 28 retest PASSED: Hub refreshed intentional external rebuild registration, QA project reopened,
  nine-entity scene restored, Console 0 warnings / 0 errors. Current editor handle 524312 (staged Dist executable).
- Small Game view at ~648x356 and reduced ~648x218 now shows more consistent glyph strokes than the user's
  earlier broken-up text screenshot. Maximized Game view also checked. This is a visual comparison, not a
  universal legibility guarantee at arbitrarily tiny sizes. No missing glyphs or new console errors observed.
- Native Enter activation PASSED: clicked Game tab, pressed Enter while runtime button focused; button changed
  to QA Recovered 2 and script click log appeared. Repeated after removing temporary frame-60 diagnostic also
  passed. Disposable script now retains only explicit initial _continue?.Focus(); original backup preserved.
- UI Builder opened StarterHud; checked fitted canvas and 83% zoom, text remained smooth. Returned to Fit.
  Editor is maximized on the ultrawide desktop, stopped, UI Builder open. Saved floating UI Builder geometry
  exceeds a 1280-wide restored host window; maximize makes it fully accessible. That workspace-resizing issue
  remains a usability limitation and was not changed as part of the font/activation fix.
- Enabled FPS and Maximize On Play during native scale testing; these QA workspace preferences remain enabled.
  No scene/asset content edits other than diagnostic cleanup. Package player-support is current but a NEW
  standalone QA player has not been built/launched since these fixes. Existing earlier standalone is stale.
- Remaining broader matrix: initial keyboard focus bootstrap/Tab vs editor navigation, bindings/navigation,
  runtime animation/audio, advanced VFX, DPI combinations, representative performance and long soak remain
  partial or not tested. Do not claim exhaustive coverage. No active shell builds; no commits or publishing.

### Sep 28 Continue spacing investigation

- Reproduced the apparent `Cont inue` spacing with the embedded ProggyForever fallback font. Source text is exactly
  `Continue`; inspected fallback advance/geometry code and found no inserted space. The narrow i has prominent
  sidebearings in this coding-oriented font. This pass did not justify changing engine glyph advances.
- Compared against licensed Inter from KeireHubContent/Fonts using the normal imported font face/family path in
  the disposable OvernightEditorQA0926 project. Family asset acc39b68-723f-4ea1-903e-644dd546dc49 is assigned to
  .starter-hud. Original stylesheet backed up to Library/Starter.before-font-qa.keirestyle.txt; Inter OFL included.
- Font-family requires stylesheet schema v2. Initial v1 edit correctly failed import and retained the previous
  preview; corrected @keire-style to 2 and watched successful hot reload. Historical Console error remains from
  that recovered authoring mistake; do not count it as an unresolved runtime failure.
- Native PASSED: UI Builder Fit preview, maximized Game Play, Continue mouse activation (QA Recovered 2), and
  Stop returning to the scene. Inter removes the apparent midword gap; both views agree. No new engine code
  changed for this spacing issue. Standalone player and alternate DPI still not tested in this pass.

### Sep 28 modern menu and FPS/combat HUD authoring test

- Disposable project: D:/Projects/KeireProjects/OvernightEditorQA0926, scene OvernightWorkflow.
  Original StarterHud.keireui and StarterUi.cs backed up under Library/BeforeModernUiQA.
- Authored teal/gold Horizon menu using native Windows control in UI Builder's Source editor, applied and saved
  through the GUI. This was source-assisted authoring, not an entirely drag-and-drop workflow. Added Inter text,
  padded buttons, chapter card, objective, crosshair, compass, live FPS, health/shield bars and ammunition panel.
- Menu asset: Assets/UI/StarterHud.keireui. Combat asset: Assets/UI/CombatHud.keireui.
  Duplicated the HUD document and wired StarterUi.cs using file edits; assigned CombatHud through the Inspector
  and saved the scene through native control. No engine implementation files changed during this authoring pass.
- Native PASSED: source apply/save, Full HD fit preview, ultrawide Game Play, Begin switching to the HUD,
  live FPS updating, Test Damage changing shield 75 -> 50 with matching fill, Fire changing ammo 30 -> 29,
  Return to Menu and Stop returning to authoring. UI Builder and runtime typography are readable at tested sizes.
- Demo limitations: Settings provides display-test guidance, compass/objective/ability hints are static, ammunition
  and damage are test controls, not gameplay bindings. Health depletion, reload, keyboard/gamepad navigation,
  smaller resolutions, alternate DPI, reopening the project and a new standalone player were not tested here.
- Reproduced usability defect: changing the selected UIDocument's VisualTreeAsset during Play reopens the closed
  UI Builder (both Begin and Return). Thin progress-bar fill caps and thin rounded borders still show rough edges.
- Authoring friction observed: completion help changes the Apply Source button's vertical position on focus loss;
  Ctrl+Enter did not visibly apply in this run. Literal > in an attribute, numeric newline entity and UTF-8 BOM
  input were rejected. Escaped >, natural wrapping and UTF-8 without BOM recovered import. Failed source apply
  retained the last good preview. Explicit menu align-items:start was needed to retain fixed button widths.
- These are recorded findings, not fixed engine defects. No claim of AAA production readiness or exhaustive QA.
  Editor left stopped with the menu open in UI Builder. No build, commit, push or publication performed.

### Sep 28 UI Builder source workflow fixes and native retest

- Replaced the source-completion panel below the editor with a caret-anchored overlay. Native XML test with `<Lab`
  showed a ranked `Label` result, inline documentation, keyboard guidance, and Tab acceptance without resizing the
  source area. Ctrl+Enter applied and validated a harmless source edit; the edit was then reverted to imported source.
- Completion is now context-aware and fuzzy-ranked for markup elements, relevant attributes, boolean/schema values,
  CSS selectors, properties, values, and design variables. Duplicate authored attributes are filtered.
- Visual-tree parsing now accepts UTF-8 BOMs, numeric decimal/hex entities, and `>` inside quoted attributes while
  rejecting invalid XML scalar values. Failed applies continue to retain the last good preview.
- Progress bars now render as progress bars rather than sliders: no inset track or handle, full-width fill, and cleaner
  per-corner rounded border coverage. Native CombatHud preview showed full-thickness rounded fills.
- Changing the selected UI Document's visual-tree/panel route no longer requests UI Builder focus unless the selected
  entity itself changes. Entering and stopping Play kept the manually closed panel closed. The menu-to-HUD click could
  not be completed in this restored workspace because Maximize On Play clipped its lower controls, so that exact
  asset-switch interaction remains a native coverage gap rather than a claimed pass.
- Fixed a newly confirmed workspace issue: a saved 2732x1202 floating UI Builder on a 684x680 host was restored inside
  the current viewport. The shared clamp applies to floating editor panels, preserving access after monitor or window
  size changes.
- `project.bat stage-editor` completed through the development Editor/Hub path. Fresh focused results: parser 2/2,
  progress geometry 6/6, CSS completion 83/83, markup completion 16/16 assertions. The earlier full repository run
  remains 1129/1129 core, 423/423 editor, 412/412 Hub, and both render backends 89/89. Existing unrelated compiler
  warnings were unchanged. No commit, push, or release archive was made.
