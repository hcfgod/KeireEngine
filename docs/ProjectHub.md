# Project Hub

`KeireHub` is the normal editor entrypoint. Running the repository launcher without direct-editor options builds and opens
the Hub, which remains alive while editor processes run.

The Hub provides:

- Starter and Empty project creation with an asynchronous native parent-folder picker.
- Existing-project opening by folder picker or pasted path.
- Searchable recent cards with status, pin/unpin, remove, reveal, and open actions.
- A discoverable packaged or repository `KeireSandbox` sample.
- Clear diagnostics for invalid, missing, incompatible, or already-open projects.

Opening launches `KeireClient --project <canonical-root>` as a detached process with the project as its working directory.
The editor independently validates the marker and acquires the exclusive lock; the Hub never treats a card status as
authorization to bypass those checks.

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

