# Project Hub

`KeireHub` is the normal editor entrypoint. Running the repository launcher without direct-editor options builds and opens
the Hub, which remains alive while editor processes run.

The Hub provides:

- A persistent left navigation rail for Projects, creation, existing-project discovery, and sample quick start.
- Starter and Empty project creation with an asynchronous native parent-folder picker.
- Existing-project opening by folder picker or pasted path.
- Responsive searchable recent cards with status, path, pin/unpin, remove, reveal, and open actions.
- A discoverable packaged or repository `KeireSandbox` sample.
- Clear diagnostics for invalid, missing, incompatible, or already-open projects.

Opening launches `KeireClient --project <canonical-root>` as a detached process with the project as its working directory.
The editor independently validates the marker and acquires the exclusive lock; the Hub never treats a card status as
authorization to bypass those checks.

After launch succeeds, the Hub hides and remains alive at a low event-pump rate. Its system-tray menu provides **Show
Hub** and **Quit**. Minimizing a visible Hub hides it while tray support is active; closing the Hub exits the complete
process. Show is one deferred, idempotent operation that synchronizes state, makes the window visible, restores it from
the minimized state, raises it, and focuses it. Native tray callbacks enqueue their actions until SDL polling completes,
so a single click cannot race a reentrant window mutation. The hidden Hub retains its minimized state internally
so it continues at the bounded background pump rate. If the platform cannot create a tray entry, the Hub minimizes
instead so it can always be recovered from the taskbar. Tray Quit follows the same deferred action boundary and performs
normal layer/UI/render/window/log shutdown. Exiting an editor does not restore the Hub automatically.

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
