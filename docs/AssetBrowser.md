# Asset Browser

Files and folders can be dragged from the operating system. Dropping over a Project folder imports there; dropping
over the Scene viewport imports into the current Project folder and then performs the same typed viewport action as an
internal drag. Meshes create entities, materials assign to the entity under the pointer, and Scene/Input Actions assets
use their guarded open workflows. Texture and shader drops import and reveal the asset without guessing an assignment.

A single unambiguous asset imports directly. Texture files, batches, directories, and destination conflicts open the
**Import Assets** dialog. Texture settings include semantic, color space, environment layout, mip policy, maximum size,
normal green-channel flip, filters, address modes, and anisotropy. Radiance HDR files default to linear equirectangular
environments; LDR environment images can use equirectangular, horizontal/vertical cross, or horizontal/vertical strip
layouts. Conflicts default to a unique name; Replace is explicit and preserves a
compatible asset's stable ID. Imported files are copied into `Assets`; external links and source `.keiremeta` identities
are never adopted. Each dialog item can be excluded, in-progress validation can be cancelled, and the completed batch
is one Project undo operation, including recoverable replacement of an existing source and metadata pair.
Names containing conventional normal-map tokens (`normal`, `nrm`) default to Normal/Linear, while metallic, roughness,
occlusion, ORM, mask, and PBR tokens default to Data/Linear. These are editable suggestions. A batch is published only
when its new runtime catalog validates; failure leaves project files unchanged and remains visible in the dialog.
In-progress source records remain hidden from thumbnail loading until the cooked catalog is mounted, so a successful
mesh or texture import cannot leave a cached cube or checkerboard preview from a premature resolve.

The Asset Browser is a focused editor panel backed by `AssetDatabase`. It presents a persistent folder tree,
breadcrumbs, search, List and Grid modes, adjustable thumbnail size, multi-selection, double-click open, drag payloads,
rename, stable-identity duplicate, and recoverable Move to Trash operations.
Click establishes a selection anchor; Shift-click selects the inclusive visible asset range to the next item, while
Ctrl/Cmd+Shift-click adds that range to the current selection.

Both the thumbnail and extension-free label are drag handles. Folder targets retain move semantics; Scene-view targets
dispatch by asset type. Scenes open through the normal dirty-document guard, Input Actions open in their editor, and a
Material dropped over a visible Mesh Renderer is assigned to the ray-picked entity as an undoable scene edit.
The current built-in material path also previews its optional `Tint` property through the Mesh Renderer tint.

The Create toolbar and blank-area context menu share commands for Folder, Scene, Material, Unlit Shader, and Input
Actions templates. Material creation asks for its base name before the source/metadata transaction. New content is
created in the displayed folder, appears immediately, and is selected. Single-asset create, move, and rename operations
update the live source index without rescanning or hashing unrelated assets; required material catalog persistence runs
in the background. Import diagnostics are shown per asset without hiding a newly created source file; strict cooking
remains fail-fast.

View mode and thumbnail size are project-local preferences under `Library/Editor`. Generated data never enters source
control. Asset mutations use the database's confined transactional operations; rename retains the `.keiremeta` identity,
duplicate creates a new identity, and trash remains recoverable.

Asset and folder context menus support Open, Rename, Duplicate, Delete, Cut, Copy, Paste, Move by drag/drop, Reimport,
Refresh, reveal, relative-path copy, and stable-ID copy. Batch operations preflight confined destinations. Trash entries
persist beneath `Library/Trash`, retain source/metadata pairs and their original path, and can be restored unless that
destination has become occupied. Permanent deletion is deliberately separate from normal Delete.

Reveal resolves the project root to an absolute canonical path. Windows selects files with Explorer and opens folders
directly; macOS uses Finder reveal, and Unix opens the containing directory for files.

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
derived deterministically from source and metadata digests, dependency IDs, provider version, and normalized extension
and live under `Library/Thumbnails`.

Textures display their imported pixels at the correct aspect ratio with transparency composited over a checkerboard.
Materials use a neutral shaded sphere driven by their current Base Color semantic, tint, and texture. Mesh/model assets
use their imported geometry and catalog bounds for a framed isometric clay preview without material textures. Scene,
shader, input-action, folder,
unknown, missing, and failed assets use distinct procedural type/error icons. Provider versions and record digests
invalidate stale previews without blocking the Project panel.
