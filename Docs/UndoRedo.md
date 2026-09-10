# Undo And Redo

`UndoService` is an application-owned, owner-thread-affine history service. It is available through
`Application::Undo()` and is configured with `ApplicationSpecification::Undo`. The default limits are 256 commands and
64 MiB per context.

## Contexts

Create one `UndoContext` for each independently editable document or tool. Scene, Input Actions, Project asset, and
theme histories therefore do not invalidate each other's redo stacks. Close a context when its document closes; all
commands and captured state are released, and retained context references become safely inert. `ContextCount()` and
`MaximumContexts` count only open contexts, so closing a document immediately frees its slot even while another caller
retains its context.

```cpp
auto history = application.Undo()->CreateContext({.Name = "Material Inspector"});
float roughness = 0.5F;

history->Execute(Keire::CreateUndoCommand(
    "Change Roughness", [&roughness] { roughness = 0.8F; }, [&roughness] { roughness = 0.5F; }, sizeof(float)));
```

`Execute` runs Redo first and records only after it succeeds. `RecordApplied` records a UI edit that was already
previewed. A failed operation leaves the redo stack and existing history unchanged. Commands can reject stale targets
through their availability callback, and custom `UndoCommand` implementations may merge adjacent continuous edits.
Recording a new edit also discards redo history and its byte accounting when that edit merges into an existing command.

## Transactions

Use `BeginTransaction` when several operations must appear as one history item. Transactions may nest. Commit collapses
the children into one command; cancel rolls applied children back in reverse order. If rollback follows a child failure,
the original exception remains the one observed by the caller. Cancellation attempts every child rollback even when a
callback throws and then reports the first failure; the transaction is already inactive and cannot be retried. Correct
document restoration still depends on successful inverse callbacks. Closing a context rolls back its pending work and
makes retained transaction handles report inactive. Rejected thread or nesting-order operations leave transactions active.

```cpp
bool manifestCreated = false;
bool shaderCreated = false;
auto transaction = history->BeginTransaction("Create Material Assets");
history->Execute(Keire::CreateUndoCommand("Create Manifest", [&manifestCreated] { manifestCreated = true; },
                                         [&manifestCreated] { manifestCreated = false; }));
history->Execute(Keire::CreateUndoCommand("Create Shader", [&shaderCreated] { shaderCreated = true; },
                                         [&shaderCreated] { shaderCreated = false; }));
transaction->Commit();
```

Do not retain documents through an undo command unless the context has exactly the same lifetime. Prefer stable IDs,
weak references, and an availability callback. Mutation, Undo, Redo, transaction control, and context closure all run
on the application construction thread; rejected worker-thread calls leave both history and target state unchanged.

## Editor Routing

The editor routes `Ctrl/Cmd+Z` to Undo and `Ctrl/Cmd+R`, `Ctrl/Cmd+Shift+Z`, and `Ctrl+Y` to Redo, alongside the Edit
menu, in the focused document context.
Inspector Duplicate and Move to Trash retain their mutation state through the asset worker and enter Project asset
history only after successful publication. Undo restores a trashed asset with its original identity; redo retains the
same history entry instead of recording another command. Failed worker mutations do not add history entries.
Project asset operations, scene edits, Input Actions authoring, and theme previews use this shared service. Continuous
Transform and Mesh Renderer tint drags merge into one history entry. Docking geometry remains layout state rather than
document history.

Scene snapshot commands preserve the current selection by stable entity ID when applying Undo or Redo. IDs absent from
the restored snapshot are removed, while surviving selections and the primary selection remain available to the next
Hierarchy, Inspector, gizmo, or shortcut command.
