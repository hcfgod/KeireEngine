# Project Settings

Rendering settings include environment identity/rotation, diffuse and specular IBL intensity, sky visibility, and the
directional-shadow distance, cascade count, power-of-two resolution, and practical-split lambda. Shadow settings are
validated to one through four cascades, 256 through 8192 texels, and a split lambda in 0..1 before an atomic save.

The environment identity accepts an imported Texture2D environment. Supported sky sources are LDR or Radiance HDR
equirectangular panoramas and common horizontal/vertical cubemap cross or six-face strip atlases. Texture import
metadata records the projection layout; sky rotation, visibility, specular intensity, and exposure update Scene, Game,
and the main-camera preview together.

Project-owned settings live below `<Project>/ProjectSettings` and are suitable for source control. They are distinct
from per-user editor state under `Library`, such as dock layouts, Scene-camera navigation, and Scene-tool preferences.

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
