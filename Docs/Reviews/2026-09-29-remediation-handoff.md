# Review remediation and validation handoff

This implements changes requested after the [engine/editor review](2026-09-29-engine-editor-review.md). All work stays
in the existing `master` checkout. Three implementation subagents owned document persistence, bounded reads/evidence,
and GPU timing/compute respectively. The coordinating chat owns dependency integration and shared documentation.
The separate **Play Mode Player Behavior** chat owns builds, test execution, native editor checks, and packaging.

## Implementation mapping

| Finding | Change | Remaining acceptance |
| --- | --- | --- |
| R1: external scene overwrite | Exact source-byte baselines; conflict rejection preserves dirty/recovery state; scene Reload/Save Copy/Overwrite/Cancel UI; shared protection for input, animator, and UI documents | Focused tests, full editor suite, native conflict/recovery workflow |
| R2: unbounded source reads | Shared format-specific bounded reader; one allocation; truncated, growing, and failed reads reject; duplicate inspector readers removed | Reader regressions and relevant authoring suites |
| R3: unavailable true GPU timing | Versioned SDL extension for D3D12/Vulkan/Metal; bounded asynchronous renderer query lifetime; source-frame attribution; isolated patched dependency builds and SDK provenance | Native backend compilation/readback/recovery tests, reference Release performance capture; Metal requires a Mac |
| R4: presentation-wide compute wait | Nonblocking completion retirement; retained submission identities/readback snapshots; explicit consumers and shutdown retain synchronization | Compute tests, native dispatch/readback, lifecycle/ASan, full render integration |
| R5: centralized editor responsibilities | Reusable document persistence owner and independent tests; extracted scene/UI persistence implementation | Focused reduction only; the whole workspace has not been decomposed or rewritten |
| R6: uneven release evidence | Current-revision/working-tree/native-host acceptance ledger with retained artifact hashes and explicit missing/cross/unavailable lanes | Actual native platform/SDK/install/player/interactive matrix must still be executed |

The timestamp extension lives in `Patches/SDL/0001-gpu-timestamp-queries.patch`; `Vendor/SDL` and its locked commit stay
unchanged. Dependency scripts clone the locked source into an isolated cache, apply the patch, verify final blob
identities, and incorporate a canonical patch digest into cache and SDK identities. A full dependency regeneration is
required before linking engine code against the new SDL API.

## Validation order for the owning chat

1. Finish source edits and record the current revision/dirty tree. Preserve unrelated animation/IK work. Do not stage
   these remediation changes without the user's separate Git authorization. Stop/restart native editor instances only
   within that chat's existing authority; this implementation chat has not controlled them.
2. Run the standalone tooling regressions:

   ```powershell
   python Scripts/Validation/test_acceptance_evidence.py
   $env:KEIRE_TEST_BASH = 'D:/Program Files/Git/bin/bash.exe'
   python Scripts/Validation/test_dependency_patches.py
   ```

   The Bash path is this workstation's installed Git Bash; choose an actual installed shell on other hosts. Tests
   operate only in disposable fixture repositories. Absent shells are explicit skips, not cross-platform acceptance.
3. Regenerate through `./Scripts/project.ps1 generate -Generator ninja -Toolset msc` so the SDL patch is applied and
   Debug/Release native dependencies are rebuilt. Use repository build/test launchers thereafter.
4. Rebuild editor/client and editor tests, then run focused filters:

   ```text
   KeireEditorTests.exe --no-colors --test-case="Scene persistence*,Document persistence*,Bounded editor reads*,Shared editor asset reads*"
   ```

   Also run existing scene recovery, Input Actions, Animator Controller, UI Builder, and broader editor suites.
   Native interaction must exercise each scene conflict choice, external deletion, Save Copy, and restart recovery.
5. Rebuild core and render tests. Run the compute/public-render contract cases, then explicitly enable the normally
   skipped native compute D3D12 and Vulkan cases. Run timestamp-focused and complete rendered-output suites separately
   with `KEIRE_GPU_TEST_BACKEND=direct3d12` and `vulkan`. Include device loss/recreation, post-submit failure cleanup,
   Runtime UI, and ImGui because frame recording order changed while submission order remains preserved.
6. Run focused DebugASan ownership/lifecycle tests and Release coverage for the changed paths. Run both Windows and
   Git Bash regression harnesses for the dependency/package changes. A Unix harness run on Windows does not establish
   a native Linux/macOS build or Metal acceptance.
7. Validate SDK packaging and both direct/CMake consumers. Check the SDL patch digest and shipped patch identity in
   `build-manifest.json` and `third-party/SDL3`. Exercise rejection of changed patch content with stale installed SDL.
8. Retain actual GPU timestamp snapshot/history/hardware artifacts for reference performance gates; functional GPU
   tests alone do not establish workload performance budgets. Use `Docs/CurrentAcceptanceEvidence.md` to record the
   native matrix without transferring old passes onto the new implementation.

## Static review already performed

Implementation owners formatted all changed first-party C++ and ran clang-format dry-run. The coordinating chat ran
PowerShell parsing, Bash syntax checks, source-file budgets, repository layout, text integrity, and `git diff --check`.
These are static checks only. No build or behavioral test was executed by this implementation chat for this remediation.

## Limits that remain explicit

- Comparing source bytes immediately before atomic replacement does not provide filesystem compare-and-swap against
  an arbitrary external writer during the final compare/replace interval.
- The complete conflict-choice modal is currently scene-specific; other protected document saves use their existing
  error handling and reload/copy workflow. Supported-size parsing remains synchronous.
- Optional GPU query support depends on the native device/backend. Unsupported timing stays unsupported; CPU latency
  is never substituted. Current native Metal and other unavailable-host evidence cannot be manufactured here.
- Ledger tooling records operator-attested results and artifact integrity; it does not itself execute or certify the
  checklist. R6's missing native acceptance and performance evidence remain open until the owning chat records them.

Build/test results are pending the owning chat's validation; this handoff is not a passing release certification.
