# Project Settings

## Finding Settings

Open **Edit > Project Settings** or use the Window menu. Settings are grouped into **Scripting**, **Input**,
**Code Editor**, **Audio / Physics**, **Rendering**, and **Environment** tabs. **Save Settings** and the saved/unsaved
indicator stay above the categories. Switching tabs finishes and saves pending control edits; Undo/Redo changes remain
pending until saved or another commit occurs.

**Reset Rendering / Environment** appears above the scrolling content in the last two tabs. It resets render settings across both, including
exposure, ambient lighting, sky selection and shadow cascades; it is not a reset of every Environment control.
Daily time, daily-lighting keys and daily-light shadow settings are preserved. Use **Reset Daily Shadows** for only
the daily-light shadow settings. Resets participate in Project Settings Undo/Redo.

Rendering settings include environment identity/rotation, diffuse and specular IBL intensity, sky visibility, and the
directional-shadow distance, cascade count, power-of-two resolution, and practical-split lambda. Shadow settings are
validated to one through four cascades, 256 through 8192 texels, and a split lambda in 0..1 before an atomic save.

An unassigned environment renders the built-in Kéire studio sky, so new and upgraded projects have a useful sky
without adding content. The Skybox Asset picker accepts imported Texture2D environments. Supported custom sources are
LDR or Radiance HDR
equirectangular panoramas and common horizontal/vertical cubemap cross or six-face strip atlases. Texture import
metadata records the projection layout; sky rotation, visibility, specular intensity, and exposure update Scene, Game,
and the main-camera preview together.

Asset references in Project Settings do not require typing an AssetId or filesystem path. The searchable picker lists compatible
project assets, accepts validated Project-panel drops, can clear back to the default sky, and reveals the selected
source in the Project panel. The same picker implementation backs typed component and material asset properties.

Project-owned settings live below `<Project>/ProjectSettings` and are suitable for source control. They are distinct
from per-user editor state under `Library`, such as dock layouts, Scene-camera navigation, and Scene-tool preferences.
Focusing Project Settings routes Undo/Redo to its own edit history, including exposure and rendering settings;
it does not undo unrelated scene changes. Changes restored through history remain pending until settings are saved.

## Environment Workspace (Development)

Open **Window > Environment** for a dockable environment-authoring view. Its **Lighting**, **Sky**, **Clouds**,
and **Bloom** tabs edit the same project settings as Project Settings; opening both does not create a second
environment or preview clock. This workspace is under development and still requires desktop acceptance testing.
Each section has its own scrolling content area below the preview controls and tabs, keeping navigation available
while browsing long lighting-key or atmosphere settings. Very short windows still need enough height for transport.

The preview clock displays whether it is playing or paused. Expand **How time preview works** for the distinction
between temporary preview and saved time. Playback, scrubbing and clock shortcuts are temporary; **Apply Time to
Project** saves the preview day and time of day while preserving the authored rate and pause state. Edit-mode
preview is unavailable during Play Mode. Reset restores authored time and preview speed 1.
The day-fraction scrubber stays visible; expand **Day and playback settings** to change the preview day or speed.
Negative preview speed runs backward and does not change the saved gameplay rate.
The dedicated panel's Lighting section includes a 24-hour intensity graph sampled from the daily-light evaluator.
The line includes every authored key and the cyclic midnight boundary, preserving narrow intensity peaks.
Gold markers show authored keys and the white cursor shows preview time. Drag to scrub and pause preview without
saving; nearby keys snap to their exact time unless Shift is held. The intensity axis auto-scales, so it is not
an exposure or brightness comparison between profiles. The numeric preview controls remain available above.
Click near a key's time to select it: a ring marks the selection and the dedicated panel shows only that key's
editable fields. The key dropdown provides an alternative for closely spaced markers. Project Settings retains
the expandable all-key list. Selection is transient, clamped after key removal, and does not change saved data.
This initial graph does not yet drag keys or provide curve-handle editing.

**Insert Key at Preview Time** adds a key using the currently evaluated directional and ambient lighting,
preserving the existing interpolated look within floating-point precision. It selects the new key in the dedicated
panel. At an existing key time, the dedicated panel offers **Select Existing Preview Key** without modifying or
saving the project; Project Settings disables duplicate insertion. The underlying insertion API rejects duplicates
rather than overwriting them. The 128-key limit still applies. This action is
unavailable during Play, and Undo removes the insertion. The older **Add Lighting Key** action still chooses the
largest time gap and copies its preceding key, which can deliberately change the curve.

Edit a key's **Time of day (hours)** numerically or by dragging: `12.5` means 12:30, with one-minute drag steps.
The clock label shows the resulting time. Time edits clamp strictly between adjacent keys, preserving ordering;
they do not reorder or overwrite neighbors. Lighting and ambient help is collapsible, and key numeric controls
use labels above full-width fields for narrow panels.

Lighting keys show clock time alongside their precise day fraction. Expand a key and choose **Preview Key Time**
to pause the edit preview at that key on the current preview day. This does not save time or add an Undo action;
use **Apply Time to Project** only if you want to change the saved starting time. Key preview is disabled in Play.

Focus routes Undo/Redo to the shared settings history. Leaving or closing a settings surface finishes the current
gesture without automatically saving it. Use **Save Environment** or **Save Settings** when the unsaved indicator
is present. These actions save the shared settings document, not just the currently visible tab. Save errors are
shared by both views and pending edits are retained for recovery.

Atmosphere and layered clouds remain experimental. The panel does not establish volumetric cloud interiors,
weather simulation, planetary navigation, or a production-ready timeline editor. Technical skybox asset selection,
IBL defaults and render-path options remain in Project Settings.

## Player Build Settings

The **Build Settings** panel owns `Player.keiresettings`, `BuildProfiles.keiresettings`, and
`BuildScenes.keiresettings`. The first stores product/window identity and platform icons; the second stores named
platform, architecture, configuration, output, symbol, and signing policies; the third stores the ordered enabled Scene
assets included in the player. The first enabled scene starts the game. All three files are validated, atomically saved,
and suitable for source control; unsaved scene or Project Settings state is never included silently in a build.

`Project.keireproject` retains its startup scene as the Editor fallback and migration source for older projects. It
does not override an existing build-scene list. The per-user last-open-scene record lives below `Library/UserSettings`
and is intentionally excluded from shared Project Settings.

## Audio Runtime

`Authoring.keiresettings` schema 2 stores the mix sample rate, period size, mono/stereo/5.1/7.1 speaker layout,
resident voice budget, virtual voice budget, and optional playback-device identity. Project Settings discovers current
playback devices and provides **Rescan Devices** plus a safe **System Default** selection. Device and format changes
take effect after restarting the editor; malformed or missing device identities fall back to the system default and
surface that state in the Profiler.

The project's **Default Mixer** is inherited by Audio Sources and Audio Reverb Zones whose Mixer override is empty.
Cooked players receive portable format and voice-budget values but never inherit the workstation-specific device ID.
Schema-1 projects migrate to 48 kHz stereo, a 256-frame period, 256 resident voices, and 1024 virtual voices.

## External Script Editor

`Authoring.keiresettings` stores the project's external-editor profile and optional executable override. Project
Settings discovers platform-appropriate installations and presents one active selection: System Default, Visual Studio
Code/Insiders, VSCodium, Cursor, Zed, Rider, CLion, Visual Studio, Xcode, Sublime Text, Neovim, or Emacs where
available. **Rescan** refreshes discovery after installing an editor. **Custom Executable** accepts an explicit path for
portable or nonstandard installations. Opening a script or generated solution always uses this project setting.

## Scripting SDK

`Scripting.keiresettings` selects the .NET 10 SDK used for managed gameplay builds. New Empty and Starter projects
persist **Bundled** as the default. The editor resolves that SDK from its own repository or packaged installation tree,
independently of the project's working directory. **System PATH** checks `DOTNET_ROOT` and `PATH`; **Custom** validates
the selected `dotnet` executable and requires an adjacent .NET 10 SDK directory.

## Entity And Physics Layers

`Authoring.keiresettings` owns 32 stable layer slots and the symmetric physics collision matrix. Layer 0 is **Default**;
the remaining display names are project-authored and may be renamed without changing serialized entity indices. The
Inspector's entity-header **Layer** dropdown uses these names. Physics converts the selected index to its corresponding
single-bit layer and applies the project collision matrix plus the Collider or Character Controller mask. Empty names
remain valid in settings and appear as `Layer N` in the Inspector so every slot stays selectable.

## Rendering Environment

`Rendering.keiresettings` is a bounded document (current schema 8) containing:

- ambient light color in linear RGBA;
- ambient intensity in the supported `0..16` engine range;
- exposure in the supported `0.01..16` engine range;
- skybox asset identity, sky visibility, rotation, and diffuse/specular environment intensity;
- experimental atmosphere parameters and two optional artistic layered-cloud sheets;
- directional-shadow distance, cascade count, resolution, and split lambda;
- requested render path, anti-aliasing, GPU occlusion, global illumination, and Irradyn quality;
- render scale, dynamic-resolution mode, minimum/maximum scale, and target frame time.

Requested rendering modes are project preferences, not a guarantee that every backend supports them. The Rendering
tab reports requested and effective settings separately so a fallback is visible.

Daily time and daily directional lighting are separate documents: `EnvironmentTime.keiresettings` and
`EnvironmentLighting.keiresettings`. Both use outer schema 1; directional-only profiles use record version 2 and
profiles with ambient animation use version 3. Rendering schemas 1–5 load with atmosphere and cloud layers disabled.
Within rendering schema 6 and later, cloud record version 2 adds appearance and pattern seed. Version-1 records migrate to
Legacy unlit rather than changing their look automatically. Choose Natural lit in a layer's Appearance control to
upgrade explicitly; Undo restores the previous choice. New layers and Reset Layer use Natural lit with seed 1.
The development sky controls are experimental, not volumetric clouds or a complete weather system. Their authored
values are included in cooked runtime manifest schema 9; use matching development player binaries when recooking.

Rendering schema 7 introduced a versioned bloom record (enabled, intensity, threshold, soft knee, and radius).
Older rendering files migrate with bloom disabled. Runtime manifest schema 8 requires this record; older manifests
without bloom remain readable. Bloom renderer/UI integration is still in progress: persisted settings alone do not
establish visible glow or production acceptance.
The Environment tab previews time without saving it until **Apply Time to Project** is used. Daily lighting edits
during Play apply to the next Play session, not the already-running world's profile. These documents are not reset
by **Reset Rendering / Environment**. See the [rendering guide](Rendering.md) for preview and lighting workflows.

New Empty and Starter projects receive this file transactionally. Older projects without it use stable defaults. The
editor opens **Project Settings** from the Edit or Window menu and applies edits to Scene and Game submissions in the
same frame. Committing a control edit prepares all four settings payloads before publication. A recovery journal
retains original contents while the files are replaced. A failed publication attempts rollback; pending edits remain
in memory. Resolve the file error and retry **Save Settings** before closing the project. If recovery is required,
record pending values before reopening the project exclusively. Reopening recovers interrupted publication before
loading settings; read-only opening refuses pending recovery. Recovery refuses to overwrite externally changed files:
preserve the journal and changed files for diagnosis instead of deleting them to force the project open.
Undo/Redo restores pending settings; save those restored settings to persist them.
Saving also compares the four files against their exact bytes (or absence) when opened or last saved. An external
modification, addition or deletion blocks publication rather than overwriting that work. Preserve pending values
before reopening to review the external changes. Recovery checks each destination again immediately before restoring
it; this is not an atomic compare-and-replace against programs that ignore the project lock.

This is recoverable grouped publication, not simultaneous visibility of all four files to external readers or a
guarantee against hardware/power-loss failures. Unsaved in-memory edits are not crash recovery snapshots.
Error guidance appears above the category tabs; long technical details scroll within their own bounded area.
Dismissal clears only the message, not unsaved edits.
Merely viewing the Scripting tab or reading SDK preferences does not dismiss a failed settings-save diagnostic.

Normal editor exit saves pending Project Settings before closing the scene or tearing down the workspace. A save
failure or a read-only project with pending edits cancels exit and reports the error; fix it and retry. This guard
does not preserve unsaved in-memory edits against process termination, crashes or forced teardown. The separate
settings journal protects publication already in progress, not edits that have never been submitted to Save.

Malformed rendering or authoring settings are reported by their workspace consumers, which can use defaults for the
current session. This is not a blanket fallback for every settings file: malformed time or lighting records reject
the settings document, and unresolved recovery conflicts prevent project opening. Fix the reported file and reopen
to reload tracked values; do not assume a partially initialized workspace is safe to save over the original settings.

### Recovering a failed settings save

1. Keep the editor open and read the error above the tabs. Dismiss only hides the message; it does not save changes.
2. For an ordinary file error, restore write access or release the application holding the named file. A directory
   occupying a settings filename is not a valid settings file; preserve its contents before correcting that path.
3. Choose **Save Settings** again. Retry first recovers a prepared publication, then saves the values still held by
   the document. Confirm the status changes to **All settings saved** before closing.
4. After a process interruption, open the project normally with exclusive access. Recovery runs before loading the
   settings. Opening read-only cannot perform this recovery.

If recovery reports an external edit or corrupt backup, stop retrying until that conflict is understood. Preserve
`ProjectSettings` and `Library/ProjectSettingsSave`, including the journal and backup files. Do not delete the
journal, replace backups, or reset settings to bypass the check: those actions can discard the only recoverable
originals. Unknown files in the settings staging directory are also preserved and can block staging; inspect them
before deciding where they belong. Pending unsaved values in an open editor are separate from these disk backups.

## Asset Package Import

The import-review option **Apply packaged rendering settings** applies only `Rendering.keiresettings`, and is off
by default. It does not transfer daily time or daily-lighting profiles. The review lists unsupported project-settings
files as skipped; `EnvironmentTime.keiresettings` and `EnvironmentLighting.keiresettings` currently fall into that
category. Importing assets is not a complete project-environment migration. Keep the source project until you have
recreated and verified the desired environment settings in the destination.

For SDK import requests that resolve a rendering-settings entry with `ProjectAssetImportResolution::KeepLocal`,
import validates the retained local settings rather
than the discarded packaged settings. Invalid retained settings stop the import before assets are published.
Choosing replacement instead validates the incoming settings; archive integrity checks still cover the entire package.

Environment-profile package transfer is unfinished. Its implementation must offer a separate, default-off choice;
validate selected time/profile records before publishing any files; include their previous contents in transaction
backups; reject destination changes made after review; and retain adopted settings when the package is removed.
An editor refresh must also resolve unsaved Project Settings edits without overwriting them. The existing rendering
choice must continue to affect only its documented file.

## Public API

SDK tools can call `LoadRenderEnvironmentSettings(projectRoot)` and
`SaveRenderEnvironmentSettings(projectRoot, settings)`. The supported value type contains no JSON object, backend
handle, synchronization primitive, or editor ownership. `SceneRenderRequest::Environment` transfers a validated copy
into one render submission.
