# Maintainability Boundaries

## Repository layout contract

Repository paths are part of the cross-platform build contract. Canonical documentation lives under `Docs/` with an
uppercase `D`. First-party C++ declarations live under each project’s `Include/` tree, while `.c`, `.cpp`, and `.mm`
implementation units live under `Source/`. Private implementation headers still live under `Include/`, but use an
internal include namespace and are not exported as supported public API. The distribution service and the website
documentation project also use `Source/`; lowercase `src/` paths are not first-party conventions.

`Scripts/Tests/check-repository-layout.py` walks the first-party tree using case-sensitive name comparisons, rejects
lowercase `src` directories, rejects headers outside `Include`, and rejects implementation files outside `Source`.
`Scripts/Premake/ManagedBuildAnchor.cpp` is the sole explicit exception because it is an intentionally empty Premake
policy input rather than a project implementation unit. Both platform regression harnesses run the validator.

## Source-unit budgets

Kéire treats source-unit size as a design signal, not a quality score. New first-party C++, C#, and public-header
files have a 1,500-line ceiling; test units have a 2,500-line ceiling. The checked-in
`Config/SourceFileBudgets.json` records tighter non-growth ceilings for legacy units already above those defaults, and
both local script harnesses and CI enforce them through `Scripts/Tests/check-source-budgets.py`.

An exception is a refactoring queue, not permission to keep growing a file. The validator ratchets every exception to
the exact observed line count: shrinkage requires lowering the ceiling in the same change, and reaching the default
requires removing the exception. A change that would exceed a recorded ceiling must first move a cohesive
responsibility behind an existing private boundary. Splits preserve public APIs, ownership, callback order, and test
coverage; line movement alone is not a useful refactor.

The highest-value seams are:

- `ScriptSystem.cpp`: runtime hosting, native-call adapters, reload transactions, and managed jobs. Managed build workspace
  generation, diagnostics parsing, source fingerprinting, and atomic text publication live behind the private
  `ManagedBuildWorkspace` boundary; managed reflection state and metadata parsing live behind `ManagedReflection`.
- `MaterialGraph.cpp`: schema/validation, lowering, shader emission, and asset import/publication. The immutable node
  descriptor catalog and its lookup/type-ID contract live in `MaterialGraphNodeCatalog.cpp`; deterministic generated
  shader manifest assembly lives in `MaterialGraphManifest.cpp`. Both use private boundaries behind the existing typed
  material-graph API.
- `RenderSceneRecording.cpp`: snapshot extraction, preparation, pass recording, and submission telemetry.
- `VfxAssets.cpp` and `VfxSystem.cpp`: encoding/import, compilation, CPU simulation, and GPU publication. Reusable JSON
  encoding and strict decoding of asset IDs, vectors, matrices, colors, curves, and gradients now live behind the
  private `VfxAssetValueCodec` boundary.
- `RuntimeServices.cpp` and `SceneRuntime.cpp`: service orchestration and scene control stay readable by keeping audio
  implementation state and scene implementation state in private internal headers, with scene VFX and physics work in
  dedicated implementation units.
- editor asset/VFX panels: document commands, background operations, canvas interaction, and presentation. Editor file
  validation/diagnostics use `EditorAssetFileService`; asset inspection, VFX workspace operations, and architecture
  dashboard presentation have dedicated implementation units; VFX canvas node construction, stable IDs, colors, and
  compatibility rules use the typed `VfxEffectPanelModel` boundary.

When a unit falls below the default ceiling, remove its exception in the same change. Do not increase a ceiling to
make a check pass.

Static analysis uses the explicitly selected LLVM 18 binaries in CI so hosted-image upgrades cannot silently change
the enforced diagnostic set. The policy enables broad bug-prone, performance, portability, modernization, and
redundancy checks, with four documented exclusions: empty catches and exception escape require lifecycle-aware review;
standard-namespace modification misidentifies permitted user-type specializations; and unchecked-optional analysis is
path-insensitive around doctest assertion macros. Those contracts remain covered by warning-clean builds, focused
lifecycle/failure tests, and AddressSanitizer. New exclusions require a repository-wide clean-baseline justification.
