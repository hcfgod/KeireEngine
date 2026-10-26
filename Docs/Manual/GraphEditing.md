# Graph Editing

Shader Graph and VFX Graph share a canvas interaction model. Materials use the Inspector, not this canvas.
Older graph-owned materials retain a compatibility editor using the same interactions. Compilation semantics
remain separate: visual organization never changes evaluation by itself.

## Select And Navigate

| Action | Result |
| --- | --- |
| Click node | Replace selection and make the node primary. |
| Ctrl-click node | Toggle it in the ordered selection. |
| Drag background | Replace selection with the marquee result. |
| Ctrl-drag background | Add the marquee result. |
| `Ctrl+A` | Select all nodes in stable canvas order. |
| `F` | Frame the current selection. |
| `Shift+F` | Frame the complete graph. |

The primary item drives the single-item Inspector. Protected Shader Output, legacy Material Output, and required VFX Context
anchors cannot be deleted or duplicated. Batch operations apply only where every selected item supports them.

The **Arrange** menu aligns edges/centers, distributes selected nodes, and straightens cables whose endpoints are both
inside the selection. Arrange operations are deterministic and create one undo command.

## Save And Manage Views

1. Pan/zoom to an area you return to often, then choose the **Bookmarks** button. The saved-view count appears beside it.
2. Enter a readable **Name**, such as `Lighting overview`, and choose **Save Current View**. Leaving the name empty
   creates the next available `Bookmark N`.
3. Choose a saved entry to return to it. To replace its view, pan/zoom again, reopen **Bookmarks**, and choose
   **Update Current View** without changing Name. Do not choose the saved entry again first; that restores its old view.
4. To rename a saved entry, choose it, reopen **Bookmarks**, change Name, and use **Rename Selected**.
   **Save Current View** with a different unused name creates another view instead. **Remove Selected** removes only
   the selected saved view, not graph content.

Each graph supports 9 views; VFX systems keep separate sets. Saving views does not dirty the graph or add graph
undo commands. They persist as local project preferences through Editor restarts, but are removed if you delete the
project's Library directory. If a saved-state warning appears, session navigation still works; follow the recovery
instructions in [Graph Authoring](../GraphAuthoring.md#framing-bookmarks-and-diagnostics) before removing any local file.

## Comments, Annotations, And Collapse

Create a comment around the selection or at an empty canvas position. A comment stores title, description, tint/alpha,
font size, movement mode, and collapsed state. **Group** movement carries directly contained nodes and nested comments;
the other movement mode moves only the comment.

Collapsed comments show compact typed boundary summaries while preserving the original nodes, cables, Block order, and
compilation. Deleting a comment deletes only the container and reparents its contents. An annotation belongs to one
node and is authoring metadata only.

Comments and annotations do not become shader code, material bindings, VFX instructions, runtime assets, or profiler
markers.

## Clipboard And Duplication

`Ctrl+C`, `Ctrl+X`, and `Ctrl+V` use bounded schema-versioned graph-fragment JSON. A fragment contains selected editable
nodes, internal cables, VFX Blocks, annotations, fully contained comments, and nested comment relationships. Paste
regenerates every persistent identity and validates the remapped result before one atomic edit.

`Ctrl+D` uses the same remap rules without touching the operating-system clipboard. Protected anchors are omitted.
Malformed, oversized, incompatible, or unsupported fragments are rejected without changing the document. Cross-graph
paste succeeds only for the graph family and node contracts accepted by the destination.

Double-click a Shader Graph cable to insert a routing knot. Drag knots to reshape the cable; select and Delete or
double-click a knot to remove it. Knots do not change evaluation.

## Reuse Is Explicit

| Graph family | Reusable asset |
| --- | --- |
| Shader Graph expressions | Shader Function or Material Function |
| Shader Graph surface composition | Material Layer |
| Existing Material Graph | Material Functions and Layers for executable surface expressions; new property-only Materials use the Inspector instead |
| VFX typed value, Block, or system | VFX Subgraph with Operator, Block, or System purpose |

In Shader Graph, select expression nodes and use **Extract Selection to Shader Function...** from the node context
menu. Output and parameter nodes are excluded from extraction. The function is created beside the graph and the
selection is replaced with its call after validation. This is different from collapsing a comment: collapse changes
only presentation. Do not infer a matching selection-extraction command for VFX; its reuse contract is VFX Subgraph.

## Undo And Save

Move, arrange, comment edit, paste, cut, duplicate, knot edit, and connection edit each participate in the graph
document's undo context. Invalid changes remain editable when repair is possible, while compile/save publication stays
blocked until the graph is valid. Shader Graph requires explicit **Save** after edits; idle preview compilation does
not persist the source. **Apply** transfers pending node properties into the document but does not save it. **Save** or
**Ctrl+S** also applies a pending shader node draft before saving. VFX uses its panel's
explicit validated **Save** workflow. Material Inspector edits commit at edit boundaries instead of a graph Save step.

Graph schema migrations preserve authoring metadata in memory, but an explicit save publishes the current schema. Do
not save a migrated project with a newer Editor if it must still be authored by an older one; keep a project backup.

## Practice Without Changing The Result

1. Open a working Shader Graph and select two ordinary expression nodes with Ctrl-click.
2. Move the selection, then press **F** to frame it. Undo once to restore both positions together.
3. Double-click a cable to add a routing knot, move it, then double-click the knot to remove it.
4. Add a comment around the selection, collapse it, and expand it again.
5. Save and reopen the graph. Confirm the layout persists and the shader still produces the same result.

If shortcuts do nothing, focus the canvas rather than a text field. If deletion leaves the output in place, that is
its protected-anchor behavior. If a preview stops updating, read the first validation diagnostic; a retained last-good
image can hide the fact that the current draft is invalid.
