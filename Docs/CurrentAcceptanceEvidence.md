# Current acceptance evidence

Acceptance belongs to a source revision **and its working tree**, a native host, toolchain, configuration, and retained
output. A historical passing package or a successful cross build does not establish current native acceptance.
`Scripts/Validation/acceptance_evidence.py` records those distinctions without running builds or changing existing
performance gates. It starts with no passes and preserves every recorded result.

The ledger is an operator attestation and evidence index, not a test-output parser or proof that a manual checklist
was executed. Record a pass only after inspecting the actual command result. Retain full commands, configurations,
hardware details, skipped cases, and limitations in the log or manual report. GPU performance still requires the
separate true-timing acceptance in [PerformanceGates.md](PerformanceGates.md).

## Candidate and native matrix

Create one ledger before running a candidate's checks, after concurrent source edits stop. Explicitly select the
platform/architecture targets advertised for that candidate; preview targets should have a separate ledger. The
default lanes cover build, core tests, editor tests, render, each SDK consumer, packaged player, install/update, and
interactive editor acceptance. A missing or unavailable lane leaves the matrix incomplete.

```powershell
python Scripts/Validation/acceptance_evidence.py --ledger Build/Validation/candidate.json begin --target windows-x86_64 --target linux-x86_64 --target macos-arm64 --target macos-x86_64
```

The source identity includes HEAD, the tracked diff against HEAD, and content digests of non-ignored untracked files.
Dirty submodule content is rejected; submodule commit changes are included in the diff. Generated ignored products
are not source identity: their content hashes belong in evidence artifacts. Do not change source files during a run.
Recording rejects a changed identity; create a new candidate ledger after edits and rerun affected acceptance.
The tool cannot detect a source change that was made and reverted entirely between its snapshots.

Use repository launchers for the actual build/tests/package operations and retain output under ignored `Build/`.
After inspecting the result, record it, for example:

```powershell
python Scripts/Validation/acceptance_evidence.py --ledger Build/Validation/candidate.json record --target windows-x86_64 --lane core-tests --result pass --mode native --toolchain "MSVC; Debug; exact compiler version in log" --command "./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Toolset msc" --artifact Build/Validation/core-tests.log
python Scripts/Validation/acceptance_evidence.py --ledger Build/Validation/candidate.json record --target windows-x86_64 --lane render --result unavailable --mode native --toolchain "MSVC Debug" --command "not run" --backend d3d12 --notes "No compatible device available on this validation host."
python Scripts/Validation/acceptance_evidence.py --ledger Build/Validation/candidate.json report --require-complete
```

These commands are usage examples, not recorded results. A pass/fail requires an existing artifact. Its SHA-256 and
size are verified again during reports. Multiple `--artifact` flags can retain logs, capture manifests, and manual
reports. Add `--backend` for rendering. A single render lane should record the full backend matrix required on that
target (for example `--backend d3d12+vulkan` with both logs); one backend's pass does not certify another.

Native pass records must match the executing host's OS and architecture. Record foreign-target compilation or package
assembly with `--mode cross`; cross passes never close native acceptance lanes. For unavailable remote platforms,
record cross/unavailable and explain the missing native host. Transfer the ledger and its relative-path artifacts
to each native machine with the identical source checkout, and serialize updates to the shared ledger. Different
working-tree contents produce a historical/different-source report even at the same HEAD. The exclusive writer lock
rejects competing writers instead of losing a record. Do not copy over newer ledger revisions when collecting hosts.

## Interactive and installed-product acceptance

The `interactive` artifact should identify the project fixture, OS, GPU/driver, display scale, editor configuration,
and operator. Record the result of each step, including failures and whether recovery preserved both copies:

1. Launch a clean installed editor from a Unicode path and create a project.
2. Import a model, assign a material, create a prefab, and add a small UI.
3. Compile scripts in two custom assemblies and use a type from the referenced assembly. Introduce and repair a
   compile error; confirm diagnostics identify the source and reload preserves supported state.
4. Edit/play/stop, save, close, reopen, and verify authored data. Change a dirty scene externally and verify conflict
   rejection, Save Copy, explicit overwrite, and reload decisions. Exercise recovery after interrupted editing.
5. Exercise keyboard navigation, DPI changes, native dialogs, and representative animation/audio/navigation content.
6. Build and run a packaged player on its target host, independently of the source checkout.

`install-update` additionally covers clean installation, upgrade from the previous supported version, uninstall/reinstall,
and preserved project/recovery data. Each SDK lane compiles and runs its consumer directly and through CMake against
the packaged SDK. The `player` lane retains native execution evidence, not just successful archive creation.
Package manifests and artifact hashes tie these results to the delivered product.

## Historical evidence and limitations

Existing results in [ProductionReadinessReview.md](ProductionReadinessReview.md),
[RevampProductionAcceptance.md](RevampProductionAcceptance.md), and
[the September engine/editor review](Reviews/2026-09-29-engine-editor-review.md) remain historical evidence.
This tooling does not retroactively assign their passes to the current checkout or fill missing native hosts.
No release-wide native matrix is closed by adding this document. Platforms without execution evidence stay pending
or unavailable; performance, sanitizer, long-session, accessibility, and workload-scale acceptance remain additional
requirements where applicable.

Run focused tooling regression tests on either Windows or Unix with:

```text
python Scripts/Validation/test_acceptance_evidence.py
```
