# Desktop Player Builds

Kéire builds standalone desktop players from persistent project settings. The editor and automation use the same
`KeireAssetTool build-player` entrypoint, and successful output is published beneath
`<Project>/Build/<profile-output-slug>/`.

## Editor Workflow

Open **Build > Build Settings** to create, duplicate, rename, delete, or select a profile. Each profile chooses Windows,
Linux, or macOS; x86_64 or ARM64; and Development, Release, or Dist. Development uses the Debug player template and
includes symbols by default. Release uses the optimized Release template. Dist uses the windowed production template
and omits symbols by default. Strict target-specific asset validation applies to every configuration.

**Scenes In Build** is the ordered player-scene authority. Add the open scene or choose any Scene asset, enable or
disable rows without losing their position, and reorder rows by dragging or with **Move Up** and **Move Down**. The first
enabled row is the startup scene and enabled rows receive deterministic build indices in displayed order. **Set as
Startup** enables the selected row and moves it first. A missing scene is shown as an error and a player build requires
at least one valid enabled scene.

The same panel edits the product name, semantic version, reverse-DNS application identifier, window title, optional
platform icons, and signing policy. Each icon field is a searchable Texture2D asset picker that also accepts compatible
Project-panel drag and drop. Selecting **Kéire default icon** uses the embedded player artwork. Settings are saved to:

- `ProjectSettings/Player.keiresettings`
- `ProjectSettings/BuildProfiles.keiresettings`
- `ProjectSettings/BuildScenes.keiresettings`

Projects without these files receive in-memory defaults: the project name, version `0.1.0`, a project-ID-derived
identifier, one host Development profile, and a scene list migrated from the descriptor's legacy startup scene. The
project descriptor schema does not change; once saved, `BuildScenes.keiresettings` is the player-build authority.

**Build** assembles any target with installed Build Support. **Build & Run** is available only when the target platform
and architecture match the editor host. **Cancel** requests cooperative cancellation, and **Reveal Build** opens the
last successful output. A dirty scene or dirty Project Settings document prompts for **Save All**, **Build Saved State**,
or **Cancel**; unsaved in-memory state is never included silently.

## Automation

```text
KeireAssetTool build-player --project <path> --profile <id-or-name> [--status <path>]
```

The command exits nonzero on validation, managed compilation, cooking, packaging, signing, cancellation, or publication
failure. When `--status` is supplied it atomically replaces a schema-versioned JSON document throughout the build. The
document contains `state`, `phase`, `progress`, `message`, `output`, and `executable`. The editor runs this command in an
isolated child process.

The pipeline validates saved settings, every enabled build scene, source modules, and Build Support; compiles
runtime-classified C# assemblies; cooks the dependency closures of all enabled scenes plus the project default input
and mixer; copies an immutable native template and managed runtime into
`Build/.staging/<build-id>`; applies branding and signing; validates the complete player; and atomically replaces the
profile output. Failure and cancellation remove staging while preserving the previous successful build. A locked
previous build fails explicitly.

Player templates are managed-host consumers. Their generated projects therefore build the managed API and stage Coral,
`nethost`, and the private .NET runtime for every configuration; direct IDE builds have the same packaging contract as
launcher builds.

## Output Layouts

Windows players contain `<Product>.exe`, `PlayerBuild.json`, `Content/`, `Managed/`, native dependencies, a generated
ICO, and optional `Symbols/`. On Windows hosts, the selected or generated icon is also written into the executable's PE
resources so Explorer, shortcuts, and the running window use it; the native runtime template already carries the Kéire
fallback for foreign-host assembly. Build Support carries the five architecture-matched MSVC runtime DLLs imported by
the native template; it does not redistribute Windows 10+ Universal CRT system DLLs. Packaged Windows executables use
the GUI subsystem and therefore do not allocate a
console when launched normally. Linux players contain `<Product>`, the descriptor and runtime directories, a desktop
entry, hicolor PNG icons, and optional symbols. macOS players use `<Product>.app/Contents/{MacOS,Resources}` with
`Info.plist`, `PlayerIcon.icns`, content, the managed runtime, and optional external symbols.

`KeireRuntime --content <path>` remains available for tests and low-level consumers. With no `--content`, a packaged
runtime locates `PlayerBuild.json` beside the executable or in its macOS Resources directory, validates its target and
relative paths, and applies player identity, content, managed-runtime, and window settings. The cooked runtime manifest
records the ordered enabled scene IDs and requires its first entry to match the startup scene. An explicit `--content`
always takes precedence. The player presents the game surface directly to the native swapchain and composites authored
Game UI draw commands with its dedicated SDL_GPU pipeline. Dear ImGui is neither initialized nor framed by the player;
it remains an editor-only presentation backend. Managed gameplay uses the same scene-runtime presentation services as
Play Mode for audio, VFX playback and events, authored UI interaction, and cursor control.

Desktop player templates inherit Kéire's production SDL input profile: Windows, Linux, and macOS builds include their
native joystick backend, HIDAPI gamepad support, and normalized motor rumble. Linux discovery remains subject to the
host distribution's `/dev/input` and `/dev/hidraw` access policy; player packages never install permissive udev rules or
request elevated device access.

## Build Support

Build Support is installed independently of projects beneath Kéire's per-user preference directory:

```text
BuildSupport/<engine-version>/<pack-id>/
```

Open **Installs**, choose a healthy editor, and select **Manage Components** to import a `.keireplayersupport` file,
monitor or cancel installation, repair an installed module from a matching package, inspect its size/status, or remove
it with confirmation. Counts and actions are scoped to that editor's exact engine version and typed Asset Tool;
selecting a missing target in the editor opens the same modal filtered to that platform and architecture. Offline
packages remain available through the filtered file picker. Generic `.keirepackage` import is not exposed through this
legacy workflow.

**Check Online** fetches `player-support-catalog.json` from the versioned GitHub release derived from the binary's
repository slug and engine version. Redirects remain HTTPS-only. Before installation, the downloaded archive must match
the catalog's exact byte size and SHA-256. Windows uses WinHTTP, macOS uses NSURLSession, and Linux uses libcurl; release
toolchains must supply the locked libcurl build rather than an ambient command-line downloader.

Packages are zstd-compressed streaming archives containing regular files only. Their manifest binds schema and engine
versions, player ABI, platform, architecture, source-module catalog, configuration variants, paths, sizes, SHA-256
digests, executable modes, and bounded branding slots. Installation rejects traversal, symlinks, duplicate or
case-colliding paths, oversized entries, corruption, incompatible ABIs, and mismatched source modules. Extraction,
registry update and repair are transactional. Removal uses a bounded atomic journal around its tombstone publish, and
inventory startup completes or rolls back an interrupted removal before reporting installed modules.

Release maintainers produce native modules on the target OS/toolchain:

```powershell
./Scripts/Windows/player-support.ps1 -Architecture x86_64
./Scripts/Windows/player-support.ps1 -Architecture arm64
```

```sh
bash Scripts/Unix/player-support.sh x86_64
bash Scripts/Unix/player-support.sh arm64
```

The Unix script emits Linux modules on Linux and macOS modules on macOS. Each invocation builds Development, Release,
and Dist templates, creates and verifies the archive, and writes a catalog entry containing the archive size and
SHA-256. Archives use content-addressed filenames, and catalog updates are serialized and published last so parallel
architecture jobs cannot lose entries or make an existing catalog reference replaced bytes. Every variant contains a
mandatory `Licenses/` inventory for Kéire and its redistributed runtime dependencies; missing notices fail packaging.
Passing an output directory, signing-key ID, and channel additionally publishes the verified archive as a signed
generic `.keirepackage` component for editor-install dependency resolution. Publishing the six
Windows/Linux/macOS × x86_64/ARM64 modules remains an explicit release operation.

`KeireAssetTool` is a managed-host consumer. Repository launchers and generated IDE projects build the managed API and
stage Coral plus the bundled .NET runtime beside the tool. `build-player` initializes that host before reflecting
managed data types for strict cooking; invoking an unstaged tool binary is rejected instead of silently omitting type
validation.

### Supported Linux matrix

Linux Editor and Hub releases use glibc with the supported GCC or Clang host toolchain selected by the release builder.
The Build Support generator currently uses Clang for Linux player templates. The release gate is explicit; a row is not
considered supported merely because it compiles on a maintainer workstation.

| Distribution | x86_64 Editor / player | ARM64 Editor / player | Release validation |
| --- | --- | --- | --- |
| Ubuntu 22.04 LTS and 24.04 LTS | Supported | Preview | Native build, tests, packaged Build & Run |
| Ubuntu 26.04 LTS | Validation target | Unobserved | Distro-aware setup and Podman matrix added; native graphics/package evidence pending |
| Debian 12 | Supported | Preview | Native build, tests, packaged Build & Run |
| Fedora (current supported release) | Supported | Preview | Source-build matrix plus 0.3.2 RPM install/version acceptance on Fedora 44 |
| Arch Linux (current) | Supported | Preview | Podman bootstrap plus native packaged smoke |
| openSUSE Tumbleweed | Supported | Preview | Source-build matrix plus 0.3.2 RPM install/version acceptance |
| Rocky Linux 9 | Supported | Preview | Exact-release package suites, SDK consumers, Vulkan/WSLg Play Mode smoke, and RPM acceptance |

`x86_64` is the production Linux architecture. ARM64 artifacts may be authored and installed for preview validation,
but must not be advertised as a stable release until native ARM64 build, test, and packaged-game execution complete.
Alpine/musl is outside the supported matrix. A Linux catalog release must include an exact-version Build Support
component for every advertised target; a missing component keeps Build disabled and links directly to the Hub's
filtered **Manage Components** workflow.

The equivalent low-level commands are `pack-player-support`, `verify-player-support`, `install-player-support`,
`list-player-support`, and `remove-player-support` on `KeireAssetTool`.

## Signing Hooks

Signing policy is `Disabled`, `SignIfConfigured`, or `Required`. Kéire launches the configured executable directly,
without a shell, after branding:

```text
<hook> <profile-arguments...> --request <request.json> --response <response.json>
```

The schema-1 request contains the staging root, relative main artifact, target platform/architecture/configuration,
product identity and version, and the SHA-256 of every staged file. A successful schema-1 response is:

```json
{
  "schemaVersion": 1,
  "success": true,
  "modifiedFiles": ["relative/path/changed/by/the/hook"]
}
```

The hook must declare exactly the files it changed. An unsafe path, undeclared modification, missing required environment
variable, timeout, nonzero exit, or malformed response fails the build. Credentials stay in environment variables or
external credential stores and are never serialized into the project. Local hooks can wrap `signtool` or `codesign`;
foreign-target hooks may call a remote signing service. Apple notarization and store submission are post-build release
steps.

## Native Release Matrix

| Player target | Native template toolchain | Cross-host assembly | Matching-host execution |
| --- | --- | --- | --- |
| Windows x86_64 / ARM64 | MSVC Windows toolchain | Yes | Windows only |
| Linux x86_64 | Supported Linux Clang toolchain | Yes | Linux x86_64 only |
| Linux ARM64 | Supported Linux Clang toolchain (preview) | Yes | Linux ARM64 only after native validation |
| macOS x86_64 / ARM64 | Apple Clang and SDK | Yes | macOS only |

Foreign outputs are structurally validated on the assembly host. Executable smoke testing, native signing, and platform
distribution validation run on the target OS and architecture.
