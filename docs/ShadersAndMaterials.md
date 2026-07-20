# Shaders And Materials

## Source Assets

A `.keireshader` manifest names a project-relative HLSL source, vertex and fragment entry points, bounded defines and
include roots, fixed-function render state, and scalar/vector/color/Texture2D material properties. Paths are relative to the
project `Assets` directory. Absolute paths, traversal, symlinks, include cycles, incompatible stages, duplicate
properties, and unsupported resource bindings are rejected before publication.

`.keirematerial` assets reference a stable shader asset ID and store validated property overrides. `ShaderAsset`,
`MaterialAsset`, and the built-in `MeshAsset` are immutable runtime assets with safe fallbacks. Failed shaders and
materials resolve to a conspicuous error material instead of leaving an invalid GPU pipeline. Numeric properties are
limited to 64 `float4` slots and fragment textures to 16 declaration-ordered bindings.

The fixed vertex ABI is position, normal, UV0, and color at locations 0 through 3. Object/view/projection and normal
data use vertex `b0/space1`; scene lighting/exposure use fragment `b0/space3`; packed numeric material data uses
fragment `b1/space3`; Texture2D properties use matching `tN/sN/space2` pairs.

The Project menu creates an Unlit Shader transactionally: its manifest, HLSL template, and metadata are either all
published or all rolled back. It can also create a Material that references the selected shader.

## Compiler Boundary

Bootstrap builds `KeireShaderCompiler` from the exact recursive SDL_shadercross lock. Compiler libraries never link
into KeireCore or runtime applications. Import stages source files in a bounded ASCII-safe temporary directory, invokes
the host compiler with a timeout and bounded output, and produces DXIL, SPIR-V, and MSL. SPIR-V reflection validates the
fixed Kéire graphics ABI before deterministic canonical bytes enter the import cache.

Contextual importers receive a confined dependency reader and return dependency path/digest records plus structured
diagnostics. The legacy byte-only callback remains supported. Hot reload publishes only successful imports, so a
compile error leaves the last working shader active. Compiler errors are shown on the selected shader and in the editor
Console, and are written to `Logs/Core.log` and `Logs/Client.log` alongside their terminal output.

Automatic compiler discovery is anchored to the running executable and searches a bounded set of development and SDK
tool locations before consulting the working directory. This keeps Hub-launched editors reliable even though their
working directory is the opened project root. `KEIRE_SHADER_COMPILER` remains the explicit override.

Run bootstrap after cloning to build the cached compiler:

```powershell
./Scripts/project.ps1 bootstrap -Generator ninja -Toolset msc
```

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset clang
```

Set `KEIRE_SHADER_COMPILER` only when intentionally selecting another packaged copy of the pinned compiler.

## Cooking

Cooking keeps only the selected target's shader variant: DXIL for Windows, SPIR-V for Linux, and MSL for macOS. Host
preserves all variants. The asset tool exposes this explicitly:

```text
KeireAssetTool cook --project <path> --output <path> --profile Dist --target windows
```

Supported target values are `host`, `windows`, `linux`, and `macos`. Renderer caches track loaded and attempted asset
revisions, build complete replacements before swapping, and retain last-good GPU resources after failed reloads. SDK packages include the compiler, required runtime
libraries, licenses/notices, and locked recursive commits so packaged asset workflows remain reproducible.
