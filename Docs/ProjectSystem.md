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
    Player.keiresettings          Player identity, window, and platform icon settings
    BuildProfiles.keiresettings   Named target, configuration, output, and signing profiles
    BuildScenes.keiresettings     Ordered enabled player scenes; first enabled scene starts
  Library/                        Ignored import cache and per-project editor state
  Logs/                           Ignored Core and Client logs
  Build/                          Ignored project-local cooked output
  .gitignore
```

The current descriptor uses schema version 4. It stores a stable project UUID, display name, creation timestamp,
creating/last-saved/minimum engine versions, optional template provenance, startup scene and default input asset IDs,
and a sorted source-module requirement catalog. Asset and scene references use stable IDs, so files may move inside
`Assets/` without breaking project settings. Schemas 1 and 2 remain inspectable as upgrade candidates; the Hub or
`KeireAssetTool upgrade-project` must complete the transactional upgrade before the editor opens them for mutation.
`Project::Save` preserves the immutable project ID and schema, advances the last-saved engine version, and replaces the
descriptor atomically.

The descriptor startup scene remains the initial Editor fallback and the migration source for projects that do not yet
have `BuildScenes.keiresettings`. Player builds use the ordered build-scene file instead: all enabled scenes are cooked
and the first enabled scene is the runtime startup scene. The last successfully opened Editor scene is a per-user,
ignored record at `Library/UserSettings/Workspace/EditorSession.state`; it never changes shared project settings.

Project upgrades are explicit dry-run plans followed by a journaled transaction. The built-in v1 → v2, v2 → v3,
and v3 → v4 steps are composed in order, snapshot every affected file, validate staged metadata before publication,
retain a recoverable active transaction after interruption, and keep timestamped backups. This allows older projects to
remain discoverable and upgradeable without silently rewriting them during inspection.

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
