# ExplorationTest validation and improvements

Project: `D:/Projects/KeireProjects/ExplorationTest`.

The user confirmed project creation/reopening, level authoring, FPS package import,
and initial C# gameplay worked. Reopening after the scripting step exposed a startup failure.

## Current work

- [ ] Fix project reopening: installed Editor 0.4.7 exits with `Perspective projection values are invalid.`
  The saved camera parameters are valid. Closing the saved Game panel allows the project to open.
- [ ] Add components to all selected entities, with coherent undo and existing-component handling.
- [ ] Remove components from all selected entities, preserving required components and coherent undo.
- [ ] Accept arithmetic expressions in numeric component fields, including `0.35 / 2`.
- [ ] Offer explicit choices before applying imported project settings and input bindings.
- [ ] Support floating editor panels outside the main application window.
- [ ] Keep the current component header visible while scrolling its fields.
- [ ] Add axis/angle `Transform.Rotate` while retaining quaternion rotation.
- [ ] Provide the requested `transform` scripting shorthand alongside existing accessors.
- [ ] Investigate and fix slow lateral FPS drift while pushing into a wall.
- [ ] Fix project.bat option 2 failing on an empty patched dependency cache.

## October 6 checkpoint

- Reproduced installed-editor startup exit 1 with the original Game panel layout. Hiding
  that panel allows startup and gameplay assembly loading. Original logs, scene and user
  settings are retained under ignored `Build/Validation/ExplorationQA/Original*` paths.
  The temporary hidden-Game-panel workaround was subsequently removed for visual validation.
- Added a drawable Game viewport size guard and regression cases. A standalone compiled
  check of the real layout helper passed; the full editor regression/build is pending.
- Added the requested `transform` alias and axis/angle rotation overload. Release managed
  build passed with zero warnings/errors; all 48 production test groups passed.
- Bulk component operations, direct numeric expressions, sticky headers, optional package
  rendering/default-input adoption and live project settings refresh are implemented in
  source with tests. Native compilation and visual acceptance remain pending.
- Reproduced the option 2 failure in an empty patched Assimp cache. Empty cache recovery
  retains the old directory and verifies a fresh clone. Regression harness passed on
  Windows PowerShell 5.1 and PowerShell 7, including invalid-cache preservation. Actual
  VS2022 generation progressed beyond that failure into dependency builds; completion pending.
- Floating panels remain unimplemented: secondary windows need immutable render packets,
  texture remapping, window lifetime coordination, and device-loss recovery coverage.
- FPS drift remains unverified. The project's imported controller differs from the current
  source package; test perpendicular versus oblique wall approach and key release before
  changing it. The user's controller script is unchanged.
- Follow-up validation: the real ImGui numeric-entry regression and sticky-title/parser tests
  passed in an isolated executable (3 cases, 30 assertions). This does not replace editor acceptance.
  The same tests also passed with LLVM AddressSanitizer using its matching runtime DLL.
- All changed C++ translation units passed standalone Clang syntax checking. That check exposed
  a pre-existing uncaptured constexpr string-view use in the retarget-name helper; moving the
  constant calculation outside the lambda fixes portability. A compiled name-validation check passed.
- Windows bootstrap also omitted the locked FFmpeg submodule. Added it to the dependency list;
  restoration and commit verification passed. Generation now builds FFmpeg after reusing the
  completed Debug/Release native dependency caches.
- Build-job regression tests passed. VS2022 and VS2026 C++20 compile/link/run smoke tests passed;
  VS2019 was unavailable. The broad Windows Fast harness was stopped before its installer
  prerequisites switched this checkout to Ninja during the active VS2022 generation; it is not a pass.
- FFmpeg and the shader compiler now build successfully from the restored checkout. Another bootstrap omission
  prevented Coral from finding its .NET SDK; Windows now installs a verified versioned fallback SDK. Installer
  failure/reuse/preservation tests passed on PowerShell 5.1 and 7.
- Per the user's follow-up, Windows SDK selection also supports newer stable compatible SDKs. Live selection
  and host-pack tests passed with SDK 10.0.401, and a separate Coral Release build using it passed with zero errors.
  Runtime staging excludes previews and incompatible major versions; staging and Player Support regression
  harnesses passed on PowerShell 5.1 and 7. Full SDK consumer packaging validation remains pending.
- Added sustained perpendicular-wall contact, intentional angled sliding and input-release regression checks
  to the character landing test. It passes syntax checking; native execution remains pending.

## Acceptance

- User acceptance passed the listed editor checks, including package input adoption after reopening the
  updated Dist executable. The initially missing input prompt was reproduced in the installed 0.4.7 editor,
  which predates these changes. Release packaging and publication are the next gates.

### Follow-up test run

- The repository managed API launcher passed in Release: runtime API, editor extension API,
  and incremental native-binding generator suites. The separate runtime run on SDK 10.0.401
  also passed all 48 test groups.
- Windows PowerShell 5.1 passed the pinned SDK installer, compatible SDK selection,
  patched dependency cache, build parallelism, managed host staging, and Player Support
  runtime harnesses. Live SDK selection selected 10.0.401 and its canonical host pack.
- All 20 changed/new PowerShell scripts parsed successfully. All 28 changed/new C++ files
  passed clang-format dry-run. Native integrated tests and editor acceptance are still pending.
- The full Dist editor suite passed: 508 tests, 19,658 assertions, one skipped test.
  Numeric/sticky-header/parser tests passed again under LLVM AddressSanitizer (3 tests,
  30 assertions); the compiled viewport guard check also passed.
- The website inventory check exposed this internal checklist being placed among published guides.
  Moving it beside the test harnesses fixed the check; all nine public HTML pages now validate.
- The Unix Fast suite passed on its final run, including Player Support runtime closure checks.
  Earlier attempts found the checklist location, two missing explicit test includes, and the
  oversized architecture test file; those were corrected. One lock-race attempt timed out under
  load, then passed on rerun. POSIX publication-lock tests require Linux/macOS (`fcntl` is unavailable here).
- All three managed suites also passed after regeneration selected SDK 10.0.401 for the bundled SDK path.
- The first focused core run passed numeric/sticky-header/parser, invalid settings, settings rollback,
  and sustained wall-contact checks. The adoption/save test used a read-only fixture handle; it now
  verifies read-only rejection and opens exclusively before simulating the editor save workflow.
  That corrected fixture still needs rebuilding and rerunning.
- The broad Dist core run, excluding that known fixture failure, passed 1,321 tests and
  302,428 assertions (24 skipped, including the explicit exclusion). This exercised native
  managed assembly loading and hot reload using the newly selected SDK/runtime.
- The Dist editor opened ExplorationTest with the Game panel visible without the previous projection
  failure. Numeric entry `0.25 / 2` produced `0.125` on the selected entities, and Undo restored `0.250`.
- Visual inspection found that scrolling inside a component card still hid its title. The title now
  follows that child scroll region; a focused native regression was added and awaits execution.
- Multi-selection removal menus now use an unwrapped notice to avoid auto-size feedback on opening.
  A focused regression checks stable sizing from the first visible frame; execution and visual acceptance
  of this refinement are pending. The earlier full-suite results predate these last two UI refinements.

Verify the original project can reopen with its Game panel visible, scripts compile and run,
and changes persist after saving and reopening. Exercise multi-selection additions/removals
and undo, invalid numeric expressions, optional package settings, detached panels and layout
restoration, rotation overloads, and wall contact. Record actual checks and remaining limits;
do not infer a pass from code inspection alone.
