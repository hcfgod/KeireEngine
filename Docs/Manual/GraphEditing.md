# Graph Editing

Shader Graph, Material Graph, and VFX Graph share the same 0.4.0 canvas interaction model. Their compilation semantics
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

The primary item drives the single-item Inspector. Protected Shader Output, Material Output, and required VFX Context
anchors cannot be deleted or duplicated. Batch operations apply only where every selected item supports them.

The **Arrange** menu aligns edges/centers, distributes selected nodes, and straightens cables whose endpoints are both
inside the selection. Arrange operations are deterministic and create one undo command.

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

Double-click a Shader/Material cable to insert a routing knot. Drag knots to reshape the cable; select and Delete or
double-click a knot to remove it. Knots do not change evaluation.

## Reuse Is Explicit

| Graph family | Reusable asset |
| --- | --- |
| Shader Graph expressions | Material Function |
| Shader Graph surface composition | Material Layer |
| Material Graph | Parameter overrides exposed by its selected Shader Graph; it does not create executable shader expressions |
| VFX typed value, Block, or system | VFX Subgraph with Operator, Block, or System purpose |

Kéire 0.4.0 does not provide a universal **Collapse Selection To Subgraph** command. A collapsed comment is not a
subgraph. Create the appropriate reusable asset and give it a deliberate typed boundary.

## Undo And Save

Move, arrange, comment edit, paste, cut, duplicate, knot edit, and connection edit each participate in the graph
document's undo context. Invalid changes remain editable when repair is possible, while compile/save publication stays
blocked until the graph is valid. Shader/Material documents autosave after their short idle delay; VFX uses its panel's
explicit validated Save workflow.

Graph schema migrations preserve authoring metadata in memory, but an explicit save publishes the current schema. Do
not save a migrated project in 0.4.0 if it must still be authored by an older Editor.
