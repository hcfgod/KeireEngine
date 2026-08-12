# Project Settings

Rendering settings include environment identity/rotation, diffuse and specular IBL intensity, sky visibility, and the
directional-shadow distance, cascade count, power-of-two resolution, and practical-split lambda. Shadow settings are
validated to one through four cascades, 256 through 8192 texels, and a split lambda in 0..1 before an atomic save.

An unassigned environment renders the built-in Kéire studio sky, so new and upgraded projects have a useful sky
without adding content. The Skybox Asset picker accepts imported Texture2D environments. Supported custom sources are
LDR or Radiance HDR
equirectangular panoramas and common horizontal/vertical cubemap cross or six-face strip atlases. Texture import
metadata records the projection layout; sky rotation, visibility, specular intensity, and exposure update Scene, Game,
and the main-camera preview together.

Project Settings never asks users to type an AssetId or filesystem path. The searchable picker lists compatible
project assets, accepts validated Project-panel drops, can clear back to the default sky, and reveals the selected
source in the Project panel. The same picker implementation backs typed component and material asset properties.

Project-owned settings live below `<Project>/ProjectSettings` and are suitable for source control. They are distinct
from per-user editor state under `Library`, such as dock layouts, Scene-camera navigation, and Scene-tool preferences.

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

`Rendering.keiresettings` is a bounded, versioned document containing:

- ambient light color in linear RGBA;
- ambient intensity in the supported `0..16` engine range;
- exposure in the supported `0.01..16` engine range.

New Empty and Starter projects receive this file transactionally. Older projects without it use stable defaults. The
editor opens **Project Settings** from the Edit or Window menu and applies edits to Scene and Game submissions in the
same frame. Each change is validated and written atomically, so an interrupted write cannot publish partial JSON.

A malformed or unsupported file produces a persistent Rendering diagnostic and falls back to defaults for the current
session. It does not disable assets, scenes, input, or other project services. Fix the diagnostic and reopen the project
to reload the tracked values.

## Public API

SDK tools can call `LoadRenderEnvironmentSettings(projectRoot)` and
`SaveRenderEnvironmentSettings(projectRoot, settings)`. The supported value type contains no JSON object, backend
handle, synchronization primitive, or editor ownership. `SceneRenderRequest::Environment` transfers a validated copy
into one render submission.
