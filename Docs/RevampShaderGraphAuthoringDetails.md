# Shader Graph blackboard, connected creation, and framing

September 12, 2026. This lane continues the seeded material/shader replacement work.
It adds connected authoring operations; historical graph history routing, graph layout, previews, and material
changes remain baseline work. The coordinator transferred this lane's increments from the isolated worktree to
`C:/Users/keith/Desktop/KéireEngine` after the requested workspace change.

## Implemented behavior

- Creating a compatible node from a pin now connects it immediately. Creating from an occupied input replaces
  that input's cable. Creating from an output retains its existing fanout.
- Cable context menus offer **Insert Compatible Node**. Candidates must have an input compatible with the cable's
  source and an output compatible with its destination. Built-in nodes and typed reusable function calls use
  the same authoring transaction.
- Document operations validate a complete candidate before publishing the edit. A single undo restores the
  previous nodes and cables, including an input's replaced cable and its authored reroutes. Redo restores the
  generated identities. Failed or stale requests leave graph content and undo history unchanged.
- Insertion retains the original cable's identity and routing on the downstream half, adding one upstream cable.
  The first compatible pin combination in declared pin order is selected deterministically.
- Connected keyword creation adds its declaration and node in the same transaction.
- The bounded, collapsible Blackboard groups parameters by category and priority, searches their names/symbols/
  descriptions, and selects and frames nodes for the existing property Inspector. Equal display names retain distinct
  stable identities. It offers typed property creation and lists boolean keywords and each named keyword option,
  including defaults and hidden state. Selecting an unplaced keyword token creates its node through document undo;
  named option tokens reuse their existing declaration.
- Explicit Frame All and diagnostic/blackboard node framing now use the canvas viewport after compact toolbar rows
  have consumed their space. Popup Frame All requests defer to the next canvas draw rather than measuring the popup.
  Validation found that the previous explicit toolbar command clipped a fullscreen output's lower pins while reopening
  the same graph framed correctly; the toolbar's earlier height measurement was the cause.

These changes do not add stage inference or claim that every node offered by the menu can compile for every
target. Structural graph validation is authoritative for these edits; asynchronous compiler diagnostics still
report shader-stage and target errors. Node-context creation without a particular pin retains the existing
unconnected behavior because there is no selected endpoint.

## Changed-file manifest beyond the seed

- `KeireClient/Include/KeireClient/Editor/ShaderGraphDocument.h`: two editor document authoring operations.
- `KeireClient/Source/Editor/ShaderGraphDocumentAuthoring.cpp`: new transaction implementation.
- `KeireClient/Include/KeireClient/Editor/ShaderGraphPanel.h`: authoring context arguments and shared creation helper.
- `KeireClient/Source/Editor/ShaderGraphPanel.cpp`: pin creation and cable insertion menus and built-in dispatch.
- `KeireClient/Source/Editor/ShaderGraphPanelFunctions.cpp`: shared connected creation for reusable functions.
- `KeireEditorTests/Source/ShaderGraphConnectionAuthoringTests.cpp`: seven focused regression cases.
- `KeireClient/Include/KeireClient/Editor/ShaderGraphBlackboard.h`: owned blackboard entries and keyword token queries.
- `KeireClient/Source/Editor/ShaderGraphBlackboard.cpp`: deterministic grouped/filterable entry construction.
- `KeireClient/Source/Editor/ShaderGraphPanelBlackboard.cpp`: bounded blackboard UI and document selection/creation.
- `KeireEditorTests/Source/ShaderGraphBlackboardTests.cpp`: grouping, search/identity, keyword and viewport coverage.
- `Docs/RevampShaderGraphAuthoringDetails.md`: this evidence and proposed integrated documentation.

The lead owns registration of the new document and blackboard implementations in the editor-test Premake target. Shared headers
also contain separately owned generated-source and compilation changes; this list identifies this lane's edits.

## Validation

Executed in the canonical checkout:

- LLVM `clang-format -i`, then `clang-format --dry-run --Werror` on the ten changed/new first-party C++ files: passed.
- `python -X utf8 Scripts/Tests/check-source-budgets.py`: passed, 1,502 first-party files at the time of execution.
- `git diff --check`: passed (Git printed line-ending normalization warnings).
- `git status --short`: inspected; existing staged work and other active lanes remain present. This lane introduced
  only the new source, tests, and this document, and performed no staging or other Git mutations.

Native build and execution are pending the coordinator's serialized build interval. No test-pass claim is made here.
The eleven new cases cover occupied input replacement, preserved fanout, atomic undo/redo, cable identity and routing,
source codec roundtrip, incompatible/stale/duplicate requests, keyword declaration undo, typed function insertion,
named keyword token reuse, grouped blackboard ordering, duplicate names, metadata filtering, unplaced/hidden keyword
tokens, and fullscreen pin bounds in a 430-by-217 canvas before and after resizing. The viewport test verifies bounds;
Windows interaction remains necessary to confirm the toolbar and popup command paths.

Windows editor interaction is requested through the validation lane: create a node from each pin direction,
insert a math node and a typed reusable function into a cable, then undo/redo and save/reopen. UI interaction,
GPU output parity, Release/ASan, packaged consumers, Linux, and macOS were not run by this lane.
This change is editor-only and adds no supported KeireCore public ABI or package contract.

## Proposed integrated documentation

README addition:

> In Shader Graph, right-click a pin and choose Add Compatible Node to create and connect a node in one undoable
> edit. Right-click a cable and choose Insert Compatible Node to place a compatible operation or reusable function
> between its endpoints. Creating from an occupied input replaces its cable; Undo restores it.
> The collapsible Blackboard searches grouped properties and keyword options. Select an entry to frame and inspect
> its node; select an unplaced keyword option to add its node, or use Add Property for a new typed parameter.

CHANGELOG addition:

> Shader Graph supports connected pin creation and insertion into existing cables, including typed reusable
> functions. Each operation is one validated undo transaction and preserves existing cable routing on undo.
> A grouped, searchable Blackboard exposes property and keyword nodes. Frame All now fits the actual viewport in
> compact panels and from canvas context menus.

Architecture addition:

> ShaderGraphDocument owns connected creation and cable insertion transactions. Panels submit the selected endpoint
> or cable and the new node; the document validates the complete candidate before changing document history or
> scheduling compilation. The panel does not issue separate node and connection mutations for these workflows.
