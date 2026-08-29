# Unified Graph Authoring

Kéire 0.4.0 gives Shader Graph, Material Graph, and VFX Graph one shared authoring interaction model while preserving
their separate validation and runtime contracts. This guide covers canvas selection, layout, comments, annotations,
clipboard fragments, navigation, undo, and schema migration. Graph-specific node semantics remain in
[Shaders and Materials](ShadersAndMaterials.md) and [VFX Authoring and Runtime](Vfx.md).

## Scope And Ownership

The stable node canvas owns presentation and input state. Each graph document owns the editable definition, validation,
dirty state, and undo transaction; each panel translates canvas requests into document edits. Shared authoring metadata
is stored with the source asset but remains editor-only:

- node annotations do not become shader code, material bindings, or VFX instructions;
- comment regions do not change graph topology or execution order;
- bookmarks are panel-session view state and are not part of the asset;
- the compiler and runtime continue to consume only the graph's executable definition.

This boundary lets the three graph editors share predictable editing behavior without coupling their compilers or
runtime ABIs.

## Selection And Movement

Editable Shader, Material, and VFX graphs use an ordered multi-selection plus one primary item. The primary item drives
the single-item Inspector; a mixed selection shows only operations valid for the entire selection.

| Action | Result |
| --- | --- |
| Click a node | Replace the selection and make that node primary. |
| Ctrl-click a node | Toggle that node without discarding the rest of the ordered selection. |
| Drag on the background | Replace the selection with every intersecting node. |
| Ctrl-drag on the background | Add the marquee result to the current selection. |
| Ctrl+A | Select every node in stable canvas order. |
| Drag any selected node | Move the complete selected set while retaining relative offsets. |
| Delete | Delete all deletable selected nodes and their incident cables in one transaction. |

Shader Output, Material Output, and required VFX Context anchors are protected. A batch delete removes editable nodes,
keeps protected anchors, and reports which requested items were retained. The accepted deletion is one undo operation;
it cannot leave half of a selected topology removed.

Audio Mixer and Animator authoring retain their existing single-selection models. The graph behavior described here
must not be inferred for those tools.

## Framing, Bookmarks, And Diagnostics

Press `F` to frame the selection and `Shift+F` to frame the complete graph. The toolbar's **Frame All** action has the
same complete-graph behavior. A diagnostic carrying a node identity can frame that node directly, keeping navigation
stable even when display labels are duplicated.

Each open panel can retain up to nine named viewport bookmarks. A bookmark stores pan and zoom for the current panel
session; choosing it restores the view without editing or dirtying the asset. Bookmark names are bounded to 64
characters and invalid viewports are rejected.

## Arrange Commands

The **Arrange** menu applies deterministic edits to the selected nodes:

- align left, horizontal center, right, top, vertical center, or bottom;
- distribute horizontally or vertically while retaining the outermost selected bounds;
- straighten cables whose two endpoints are both inside the selection.

Alignment and distribution require a meaningful multi-selection. Straightening removes authored routing points only
from internal cables. Every accepted arrange command is committed as one undoable graph edit.

## Node Annotations

A node annotation is a bounded text bubble attached by stable node identity. It can be visible or hidden and can be
pinned. Pinned bubbles remain presented when the node is not selected; bubble geometry and text scale with graph zoom.
Deleting a node removes its annotation in the same topology edit.

Annotations are limited to 1,024 per graph and 4,096 UTF-8 bytes each. Duplicate node annotations, references to nodes
outside the graph scope, and oversized text fail validation before publication.

## Comment Regions

A comment can be created around the current selection or at an empty canvas position. Its editable properties are
title, description, tint and alpha, font size, movement mode, and collapsed state. Comments support selection,
movement, resizing, nesting, and container-only deletion.

Movement mode controls drag behavior:

- **Group** moves the comment, directly contained nodes, and nested comments together.
- **Comment only** moves the visual container without moving its contents.

Membership updates to the smallest comment rectangle containing the center of a moved node. Nested comments use an
explicit parent identity. Deleting a comment deletes only the container: direct nodes and child comments are reparented
to the removed comment's parent when one exists.

Collapsed comments preserve topology. The canvas replaces their expanded contents with a compact presentation and
typed input/output boundary summaries; compilation still sees the original nodes and cables. Expanding the comment
restores the normal visualization without a graph conversion.

The canonical bounds are 256 comments, 1,024 members per comment, and 16 parent levels. Titles are limited to 256
UTF-8 bytes and descriptions to 4,096 bytes. Duplicate identities, invalid members, non-finite geometry, invalid sizes,
and parent cycles fail validation.

## Copy, Cut, Paste, And Duplicate

`Ctrl+C`, `Ctrl+X`, and `Ctrl+V` use Kéire's versioned canonical graph-fragment JSON rather than an implementation object
dump. A fragment is limited to 1 MiB and includes only the selected editable topology:

- selected nodes and their pins or VFX Blocks;
- cables whose endpoints are both inside the fragment;
- fully contained comments and their nested relationships;
- annotations for copied nodes.

Paste regenerates every persisted node, pin, Block, cable, comment, and annotation reference, validates the remapped
fragment, offsets its placement, and publishes it as one undoable transaction. Unsupported, malformed, empty, or
oversized clipboard text is rejected without changing the document. Cut serializes first and deletes only after a valid
fragment is available.

`Ctrl+D` uses the same remap and containment rules without round-tripping through the operating-system clipboard.
Protected output or Context anchors are not duplicated. The new nodes become the ordered selection after the single
transaction commits.

## Schema Migration

Kéire 0.4.0 advances Shader Graph and Material Graph source schemas from 3 to 4 and VFX source schema from 4 to 5.
The current Shader Graph schema is 5; it adds a conservative maximum world-position-displacement radius, while
schemas 1–4 migrate in memory with a zero radius. Material Graph schema 5 removes redundant surface authoring from new
files and preserves executable legacy expressions only under `legacySurfaceGraph`. Schema migration follows the same
fail-before-mutation rule across all three formats:

1. supported historical sources decode and migrate in memory;
2. opening or previewing does not rewrite the source file;
3. explicit publication emits the current canonical schema and validated authoring metadata;
4. a future unsupported schema is rejected before the live document or last-good preview changes.

Shader and Material schema 4 add the shared authoring metadata and renderer-neutral resource declarations. Shader
schema 5 retains those declarations and adds the displacement bound used by the occlusion-safety contract. Portable
samplers, Texture2D-array, cube, 3D, and bounded read-only structured or byte-address buffer contracts round-trip,
reflect, and participate in dependency extraction and typed overrides. Generic backend GPU realization for those
array/cube/3D/user-buffer resources remains deferred, so runtime import rejects them rather than silently binding an
incorrect fallback.

VFX schema 5 adds the same authoring metadata and complete schema-1 VFX Subgraph assets. Operator, ordered Block, and
complete System subgraphs validate typed boundaries, dependencies, purpose, recursion, and bounded expansion before
CPU or GPU activation. The parity ledger still has 30 disabled rows, including 23 P0/P1 rows; disabled entries remain
rejected and are not implied by subgraph support.

## Deliberate Gaps

The 0.4.0 source does not provide named reroute declarations or persisted nested local-graph stacks. Cable routing
points remain presentation geometry, and reusable executable composition is supplied by the graph-specific function,
layer, or VFX Subgraph contracts. Documentation and UI should not label either missing feature as complete.

Scene Inspector arrays and lists can preserve and edit existing serialized collection values, but collection
add/remove/reorder controls are a separate scripting-Inspector gap and are not supplied by graph multi-selection.

## Failure And Undo Guarantees

Canvas gestures may create a draft interaction, but the document changes only through its validated edit boundary.
Malformed fragments, protected-only deletes, invalid comment graphs, incompatible cables, failed subgraph expansion,
and future schemas leave the prior definition and last-good preview intact. Multi-node movement, deletion, arrange,
comment edits, paste, and duplication each commit as a single undo command.

The shared behavior is covered by Core metadata tests, Editor canvas/clipboard/layout/navigation tests, graph document
tests, and Shader/Material/VFX panel tests. Relevant authorities include:

| Area | Source |
| --- | --- |
| Public metadata and validation | `KeireCore/Include/Keire/Authoring/GraphAuthoring.h`, `KeireCore/Source/Authoring/GraphAuthoring.cpp` |
| Stable canvas and selection | `KeireClient/Include/KeireClient/Editor/AuthoringWidgets.h`, `KeireClient/Source/Editor/GraphSelection.cpp` |
| Comments and annotations | `KeireClient/Source/Editor/GraphComments.cpp`, `KeireClient/Source/Editor/GraphNodeAnnotations.cpp` |
| Clipboard and identity remap | `KeireClient/Include/KeireClient/Editor/GraphClipboard.h`, `KeireClient/Source/Editor/GraphClipboard.cpp` |
| Arrange and navigation | `KeireClient/Source/Editor/GraphLayout.cpp`, `KeireClient/Source/Editor/GraphNavigation.cpp` |
| Focused tests | `KeireTests/Source/Authoring/GraphAuthoringTests.cpp`, `KeireEditorTests/Source/GraphClipboardTests.cpp`, `KeireEditorTests/Source/GraphLayoutTests.cpp` |
