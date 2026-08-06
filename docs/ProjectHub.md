# Project Hub

`KeireHub` is the normal editor entrypoint. Running the repository launcher without direct-editor options builds and opens
the Hub, which remains alive while editor processes run.

The Hub uses compact charcoal surfaces, restrained blue accents, a persistent navigation rail, and responsive project
views while preserving keyboard and narrow-window usability. It provides:

- A persistent left navigation rail for Projects, creation, existing-project discovery, and sample quick start.
- A default sortable list with Name, Status, Last Opened, and Path columns plus a persisted responsive card view.
- Starter and Empty template cards in a validated two-pane creation workflow with destination preview and an
  asynchronous native parent-folder picker.
- Existing-project opening by folder picker or pasted path.
- Searchable recent projects with single selection, arrow-key navigation, Enter/double-click open, and context actions
  for reveal, pin, and non-destructive removal.
- A discoverable packaged or repository `KeireSandbox` sample.
- Clear diagnostics for invalid, missing, incompatible, or already-open projects.

Opening launches `KeireClient --project <canonical-root>` as a detached process with the project as its working directory.
The editor independently validates the marker and acquires the exclusive lock; the Hub never treats a card status as
authorization to bypass those checks.

After launch succeeds, the Hub hides and remains alive at a low event-pump rate. Its system-tray menu provides **Show
Hub** and **Quit**. Minimizing a visible Hub hides it while tray support is active; closing the Hub exits the complete
process. Show is one deferred, idempotent operation that synchronizes state, makes the window visible, restores it from
the minimized state, raises it, and focuses it. Synchronization refreshes every recent project's OS lock-derived status,
so an editor that exited while the Hub was hidden is immediately shown as ready. Native tray callbacks enqueue their
actions until SDL polling completes,
so a single click cannot race a reentrant window mutation. The hidden Hub retains its minimized state internally
so it continues at the bounded background pump rate. If the platform cannot create a tray entry, the Hub minimizes
instead so it can always be recovered from the taskbar. Tray Quit follows the same deferred action boundary and performs
normal layer/UI/render/window/log shutdown. Exiting an editor does not restore the Hub automatically.

Only one Hub process owns the window and tray entry for a canonical Hub executable. A later repository launch, editor
Build Support request, or direct executable launch sends its activation to that primary process and exits before
creating UI or tray state. Window-system shutdown closes any surviving tray handle before SDL teardown, including when
layer cleanup was interrupted, so normal exit and tray Quit do not leave a stale notification-area entry.

## Launcher Workflows

Windows:

```powershell
./Scripts/project.ps1 run -Generator vs2022 -Configuration Debug
./Scripts/project.ps1 run -Editor -ProjectPath C:\Projects\MyGame
./Scripts/project.ps1 run -SmokeProject
```

Linux and macOS:

```sh
bash Scripts/project.sh run --generator ninja --configuration Debug
bash Scripts/project.sh run --editor --project /projects/MyGame
bash Scripts/project.sh run --smoke-project
```

`--smoke-ui` validates the Hub renderer. `--smoke-project` opens the sample through the real project, asset, input, scene,
workspace, and editor lifecycle, renders a bounded number of frames, and exits cleanly.

Removing a recent card is intentionally non-destructive. Project deletion is outside the Hub contract.
