# Kéire Outpost

This private scene supplies real renderer and editor footage for the website. The
generator contains original scene composition and gameplay code. Licensed model
and texture sources are supplied separately and must remain outside the repository.

Generate into a **new** directory:

```powershell
python Scripts/Examples/create-keire-outpost.py --destination D:/Projects/KeireOutpost --assets "C:/path/to/POLYGON_War/Source Files" --gallery D:/Projects/PreDemoHardening043Smoke
```

The gallery argument identifies the existing imported POLYGON War project with
`Assets/Polygon` and `Assets/Examples/FeatureGallery/SyntyWar`. The generator refuses
to overwrite an existing project. Vehicle exports use centimetres, while the
modular environment exports use metres; these scales are explicit in the scene.
The tank binds its body atlas in slot 0 and the supplied tread texture in slot 1.
Building plaster wear comes from the original atlas. The scene uses a 0.07 sunlight
shadow bias, 4096-pixel soft directional shadows, and 4× MSAA. Its 0.3–180 metre camera clip range preserves depth precision
on closely layered building trim.

Open `Assets/Scenes/KeireOutpost.keirescene` in the editor. Play starts an 84-second
camera sequence with short dolly moves and cuts between stations. The same scene can be cooked for the standalone runtime:

```powershell
./Scripts/project.ps1 build -Generator ninja -Configuration Release -Toolset msc -Target KeireAssetTool
./Scripts/project.ps1 build -Generator ninja -Configuration Release -Toolset msc -Target KeireRuntime
./Build/Bin/Release-windows-x86_64/KeireAssetTool/KeireAssetTool.exe cook --project D:/Projects/KeireOutpost --output D:/Projects/KeireOutpost/Build/Content --target windows --compression-level 3
./Build/Bin/Release-windows-x86_64/KeireRuntime/KeireRuntime.exe --content D:/Projects/KeireOutpost/Build/Content
```

| Control | Action |
| --- | --- |
| Space | Pause/resume the camera |
| R | Restart the tour, reset the character, and close the gate |
| Tab | Switch between the tour and third-person exploration |
| WASD | Move along the central exploration lane |
| Arrow keys | Turn the camera |
| E near the signal gate | Open/close the gate |
| C | Toggle free camera capture mode; Q/E changes height |
| F1–F6 | Frame the arrival, aircraft, tower, materials, effects, or tank |
| F7 | Toggle the character animation preview |
| F8 | Compare the scene with and without shadows |
| Escape | Close the standalone player |

The base generator includes the Sandbox character and its idle clip. To replace it
with the user's local Vanguard model and sixteen motion clips, run the following
before cooking (all FBX sources remain in the private project):

```powershell
python Scripts/Examples/Outpost/configure-vanguard.py --project D:/Projects/KeireOutpost --source C:/Users/keith/Downloads --asset-tool Build/Bin/Release-windows-x86_64/KeireAssetTool/KeireAssetTool.exe
python Scripts/Examples/Outpost/test-outpost.py
```

The Vanguard version uses WASD for walking and strafing, Left Shift for running,
Left Ctrl for crouching, and Space for the jump animation during exploration.
F7 cycles through all sixteen imported motion states, four seconds per state.
Clips bake horizontal motion in place; the script controls movement within the
lane. Jump is an animation preview, not a physics traversal mechanic.
The controller crossfades only when the requested motion changes. Imported
materials stay bound to the Vanguard's own slots.

Exploration stays within the unobstructed central lane. Capture flight is bounded
to the scene area. The standalone player requests a 1920 × 1080 window; editor Play
does not resize the editor. Source packs and the cooked interactive demo are private.
Only rendered media goes into the website's `Source/media/outpost` directory.

For performance evidence, run the Release runtime with `--render-benchmark` and an
output JSON path. Its report contains 300 warm-up and 2,000 measured frames. Verify
the actual surface dimensions and frame-time distribution before making a 1080p60
claim. An FPS overlay from one screenshot is not a benchmark.
