# Template Completion Plan

This checklist records the final, intentionally bounded foundation work for the template.

- [x] Add a static-first `KEIRE_API` boundary for same-toolchain shared-library preparation.
- [x] Add factory-only `Ref`/`WeakRef` ownership with race-safe weak locking.
- [x] Add semantic project versioning and compiler-accurate runtime build identity.
- [x] Add dependency-free `--help` and `--version` KeireClient behavior.
- [x] Add Debug/sanitizer assertions with source diagnostics and death-probe validation.
- [x] Add one canonical packaged SDK consumer example.
- [x] Add package-only CMake `find_package` interoperability.
- [x] Extend packaging, rename, bootstrap, doctor, script fixtures, CI, and documentation.
- [x] Complete the final local verification matrix and record the result in the implementation handoff.

## Guardrails

- Premake is the only project build system; CMake consumes extracted SDKs only.
- KeireCore remains static by default and no runtime dependency is added.
- Assertions are disabled in Release, Dist, and Coverage without argument evaluation.
- Rendering, input, networking, ECS, job scheduling, serialization, and other domain systems remain out of scope.
