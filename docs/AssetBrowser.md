# Asset Browser

The Asset Browser is a focused editor panel backed by `AssetDatabase`. It presents a persistent folder tree,
breadcrumbs, search, List and Grid modes, adjustable thumbnail size, multi-selection, double-click open, drag payloads,
rename, stable-identity duplicate, and recoverable Move to Trash operations.

The Create toolbar and blank-area context menu share commands for Folder, Scene, Material, Unlit Shader, and Input
Actions templates. New content is created in the displayed folder, appears immediately after its source/metadata
transaction commits, is selected, and starts an extension-preserving base-name rename. Import diagnostics are shown per
asset without hiding a newly created source file; strict cooking remains fail-fast.

View mode and thumbnail size are project-local preferences under `Library/Editor`. Generated data never enters source
control. Asset mutations use the database's confined transactional operations; rename retains the `.keiremeta` identity,
duplicate creates a new identity, and trash remains recoverable.

Asset and folder context menus support Open, Rename, Duplicate, Delete, Cut, Copy, Paste, Move by drag/drop, Reimport,
Refresh, reveal, relative-path copy, and stable-ID copy. Batch operations preflight confined destinations. Trash entries
persist beneath `Library/Trash`, retain source/metadata pairs and their original path, and can be restored unless that
destination has become occupied. Permanent deletion is deliberately separate from normal Delete.

List and Grid labels omit source extensions. A delayed hover card supplies the complete filename, extension, type,
project-relative path, size, stable ID, importer, and latest import result so visually identical stems remain
distinguishable without cluttering the content view.

The folder tree and content area use a resizable split when enough width is available. At compact widths, the tree
collapses and the breadcrumb/content view remains usable instead of forcing both panes below their minimum sizes. Grid
columns are equal-width, non-persistent presentation columns recalculated from the current content width; they are not
user-resizable data columns and never restore stale stretch weights after a window-size change.

## Thumbnail Service

Thumbnail generation runs on a bounded background worker. Requests deduplicate by stable asset ID, can be cancelled as
a group when the project closes, and publish completed RGBA8 pixels only through the UI owner thread. Cache names are
derived deterministically from source digest, provider version, and normalized extension and live under
`Library/Thumbnails`.

The built-in providers generate polished procedural folder, scene, input-action, unknown, and missing-asset images.
Providers are versioned so a visual change invalidates only its own cache entries. Future renderer-backed or typed asset
providers can register without changing the panel, but scene-camera preview rendering is deliberately outside this
milestone.
