# Material cook, player, and SDK evidence

This lane supplies a deterministic native regression for the new property-only material source. It does not claim
packaged-player or SDK acceptance before those artifacts are built and exercised.

## Deterministic regression

`KeireTests/Source/Assets/MaterialCookParityTests.cpp` creates a schema-5 `.keirematerial` source that references a
code-shader fixture by stable property ID. It changes the roughness override, strictly cooks the material as the root,
and loads the generated runtime `MaterialAsset` through `AssetSystem` in `Cooked` mode. The assertions require:

- the material source declares the shader dependency;
- the cooked closure contains the source, shader, and generated runtime material;
- the cooked runtime loads the shader dependency and retains its stable roughness property ID, default value, and
  synthetic DXIL, SPIR-V, and MSL variants;
- the cooked material keeps the code-shader ID and the edited 0.8 roughness value; and
- property replacement and cooking do not invoke the shader fixture importer again.

The fixture uses synthetic three-format shader bytes and a counting fixture importer. The count proves only that
this dependency is not reimported by property replacement/cook. It does not invoke a real shader compiler, prove zero
compiler invocations in the editor, provide executable GPU binaries, or measure compiler time.

## Current validation evidence

On September 12, 2026, the new test passed an MSVC x64 `/Zs` syntax-only check using the canonical Debug target's
definitions and include paths, while holding the shared native-build lock. No object file, link step, test executable,
or generated build file was produced. The current Ninja graph predates this test and its `KeireTests` dry run requires
283 steps, including a broad core rebuild, so regeneration/build/run is deferred until the peer edits have a coordinated
baseline.

## Required Windows acceptance after a coordinated native-build slot

1. Build and run the focused `KeireTests` case under the shared native-build lock with Ninja limited to two jobs.
2. Cook the disposable material acceptance project and run its player to verify material assignment, save/reopen,
   shader break/repair, and editor/player rendering parity. Record the produced catalog, player command, hardware,
   observed output, and failures in `Docs/RevampProductionAcceptance.md` during integration.
3. Produce an extracted SDK archive and use the package validation workflow. The Windows package script validates:
   the direct low-level C++ consumer; the managed C++ entrypoint consumer and `--managed-smoke`; the managed C# API
   consumer; and both CMake consumer builds/runs. It also validates the source-module consumer. A successful
   compilation alone is insufficient because each executable must run from the extracted archive.

## Open gates

- No packaged player has yet run this material fixture.
- Neither extracted SDK consumer has been rebuilt or run for this revamp.
- Linux/Vulkan and macOS/Metal cook/player evidence remains unavailable on this Windows host.
- The deterministic test does not establish compilation latency, CPU/GPU timings, memory, visual baselines, or compute
  execution.
