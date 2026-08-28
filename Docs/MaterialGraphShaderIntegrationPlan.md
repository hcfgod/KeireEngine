# Material Graph and Shader Graph Integration Plan

## Implemented authoring boundary

Shader Graph is the sole owner of executable texture sampling, UV manipulation, channel operations, and surface-output
logic for newly authored content. Material Graph now opens with Shader Graph parameters visible and creates only value
overrides. This removes the requirement to duplicate shader logic in `surfaceGraph`.

The serialized `surfaceGraph` member remains as a compatibility container so existing projects continue to load and
compile. The importer composes it only when a legacy asset contains authored surface connections; ordinary and newly
authored materials resolve the selected Shader Graph variant directly.

## Objective

Make Shader Graph the single source of executable surface logic. Material Graph should bind values, textures,
material functions, and per-instance overrides to the shader's published property contract. Users must not recreate
texture sampling, UV transforms, channel extraction, or other shader operations in a second `surfaceGraph`.

## Target contract

Shader Graph compilation publishes a versioned `SurfaceProgramArtifact` containing generated variants, stable
property IDs, types and defaults, required vertex streams, render-state capabilities, and conservative geometry
metadata. Material assets reference that artifact and contain only property bindings and overrides.

Material Graph remains useful for reusable parameter expressions and material-instance relationships, but its output
is a typed binding table. It does not emit a second surface shader or own shader-stage logic.

## Data model

Material schema version 4 has these authoritative fields:

- `shader`: the Shader Graph or compiled shader asset ID.
- `bindings`: stable shader property ID to literal, texture, collection, or material-function output.
- `renderStateOverrides`: the bounded states the shader explicitly allows materials to override.
- `legacySurfaceGraph`: decode-only migration data, never evaluated after successful conversion.

The shader artifact owns UV operations, samplers, channel logic, vertex displacement, alpha/depth behavior, and all
stage-specific code. Property names remain display metadata; stable property IDs are serialization keys.

## Compiler flow

1. Compile Shader Graph into one typed intermediate representation.
2. Validate stage legality, property IDs, resource limits, and render-state capabilities once.
3. Emit runtime variants plus the `SurfaceProgramArtifact` property schema.
4. Resolve Material Graph against that schema and emit a compact binding table.
5. Reject missing or incompatible bindings with node/property diagnostics instead of generating duplicate logic.
6. Cache shader artifacts independently so material-only edits never recompile a shader.

## Migration

Phase 1 introduced schema v3, dual-read support, stable property IDs, and compiler diagnostics. Current writers emit
schema v4 with shared authoring metadata while retaining the legacy readers.

Phase 2 converts legacy `surfaceGraph` nodes that only bind or transform parameters into typed bindings. Executable
surface behavior missing from the referenced Shader Graph becomes a reusable Shader Graph function before duplicate
nodes are removed.

Phase 3 adds a transactional migration preview showing created functions, changed bindings, unresolved properties,
and before/after variant hashes. Any failure retains the original source.

Phase 4 removes `surfaceGraph` authoring and compilation after shipped templates and examples migrate. The decoder
remains for the documented compatibility window.

## Acceptance criteria

- Shader-authored texture sampling, UV transforms, and channel splits are never duplicated in Material Graph.
- Material edits do not trigger Shader Graph compilation or a project-wide import.
- Shader property renames preserve bindings through stable IDs.
- Type changes produce targeted diagnostics and retain the last-good runtime material.
- Legacy migration is deterministic, undoable, and byte-stable when rerun.
- Runtime binding cost and shader variant counts do not increase relative to schema v2.

## Validation matrix

Cover scalar/vector/texture bindings, UV and channel-function migration, missing properties, renames and type changes,
cyclic functions, shader hot reload, last-good fallback, undo/redo, asset move/rename, cooking, and v2-to-v3 round
trips on Windows and Linux.
