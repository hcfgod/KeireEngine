# Undo And Redo

`UndoService` is an application-owned, owner-thread-affine history service. It is available through
`Application::Undo()` and is configured with `ApplicationSpecification::Undo`. The default limits are 256 commands and
64 MiB per context.

## Contexts

Create one `UndoContext` for each independently editable document or tool. Scene, Input Actions, Project asset, and
theme histories therefore do not invalidate each other's redo stacks. Close a context when its document closes; all
commands and captured state are released, and retained context references become safely inert.

```cpp
auto history = application.Undo()->CreateContext({.Name = "Material Inspector"});
float roughness = 0.5F;

history->Execute(Keire::CreateUndoCommand(
    "Change Roughness", [&roughness] { roughness = 0.8F; }, [&roughness] { roughness = 0.5F; }, sizeof(float)));
```

`Execute` runs Redo first and records only after it succeeds. `RecordApplied` records a UI edit that was already
previewed. A failed operation leaves the redo stack and existing history unchanged. Commands can reject stale targets
through their availability callback, and custom `UndoCommand` implementations may merge adjacent continuous edits.

## Transactions

Use `BeginTransaction` when several operations must appear as one history item. Transactions may nest. Commit collapses
the children into one command; cancel rolls applied children back in reverse order. If rollback follows a child failure,
the original exception remains the one observed by the caller.

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
Project asset operations, scene edits, Input Actions authoring, and theme previews use this shared service. Continuous
Transform and Mesh Renderer tint drags merge into one history entry. Docking geometry remains layout state rather than
document history.
