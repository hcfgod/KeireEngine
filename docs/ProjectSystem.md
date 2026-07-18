# Project System

Kéire projects are isolated content and editor-state roots. A directory is a project only when it contains the fixed
`ProjectSettings/Project.keireproject` marker. `Project::Open` accepts either the root or that marker, canonicalizes the
path, validates schema and engine compatibility, and never falls back to the process working directory.

## Project Layout

```text
MyGame/
  Assets/                         Source assets and adjacent .keiremeta identities
  ProjectSettings/
    Project.keireproject          Versioned project identity and startup references
  Library/                        Ignored import cache and per-project editor state
  Logs/                           Ignored Core and Client logs
  Build/                          Ignored project-local cooked output
  .gitignore
```

The descriptor uses schema version 1 and stores a stable project UUID, display name, creating/minimum engine versions,
startup scene asset ID, and default input asset ID. Asset and scene references use stable IDs, so files may move inside
`Assets/` without breaking project settings. `Project::Save` preserves the immutable project ID/schema and replaces the
descriptor atomically.

## Creation And Opening

`Project::Create` is transactional: it validates the name and destination, creates the complete directory shape, writes
the descriptor, and rolls the new root back if a later step fails. The Empty template has no content. Starter creates and
imports a default Input Actions asset plus a sample scene with stable metadata.

Editor opens are exclusive. The OS lock is held for the full `Project` lifetime at `Library/Editor.lock`; a second editor
receives an explicit in-use result. Read-only opens support Hub inspection without claiming editor ownership. Application
editor mode rebases asset catalogs, input override profiles, UI workspace files, and logs beneath the opened project.

## Recent Projects

`ProjectRegistry` stores at most 50 unpinned recent entries in the platform preference directory. It records canonical
paths, stable project IDs, last-opened time, and pin state. Refresh distinguishes ready, missing, invalid, newer-engine,
and in-use projects. A malformed registry is moved aside as `.corrupt` and replaced with an empty safe registry.

The registry contains discovery state only. Removing an entry never deletes a project or its assets.

## Threading And Boundaries

Project creation, open, save, and registry mutation are owner-workflow operations. Public headers expose only Kéire IDs,
standard values, and paths; JSON, SDL preference lookup, and native lock handles remain private. Project shutdown happens
after editor layers and asset services release project-backed state, then releases the exclusive lock deterministically.

