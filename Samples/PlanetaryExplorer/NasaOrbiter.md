# Optional NASA orbiter visual

`Fetch-NasaOrbiter.ps1` obtains the official [NASA/Johnson Space Center orbiter model](https://science.nasa.gov/3d-resources/space-shuttle-b/), verifies its SHA256, and uses pinned glTF Transform 4.5.0 to decode its Draco geometry. The engine's current Assimp build does not include Draco support, so import the generated `SpaceShuttleB-Uncompressed.glb`, not the original compressed file. Credits, hashes, conversion details, and [NASA usage guidance](https://www.nasa.gov/nasa-brand-center/images-and-media/) are preserved beside the source and copied into the authored project. No NASA endorsement is implied.

After closing the showcase editor, run:

```powershell
./Samples/PlanetaryExplorer/Fetch-NasaOrbiter.ps1
./Samples/PlanetaryExplorer/Test-NasaOrbiterImport.ps1
./Samples/PlanetaryExplorer/Author-NasaOrbiter.ps1 -Project D:/Projects/KeireValidation/PlanetaryExplorer -WireScene
```

Authoring refuses to overwrite existing orbiter assets. Without `-WireScene`, it only adds the model and a reusable prefab. With that switch, it backs up the scene and metadata under `AuthoringBackups/BeforeNasaOrbiter`, disables the existing Hull/Wings/Engine placeholder children, and adds the orbiter visual under Spacecraft. It preserves the ship controller, camera, and original baseline scene. The scene uses the model directly; the reusable prefab is also available for other placements. The normal editor import processes the assets on reopening.

The focused import test runs the actual staged AssetTool and worker against an isolated temporary project. It verifies both mesh and prefab types, their dependency, and the resulting catalog. It does not establish rendering or physical flight correctness.

This model has 1,440 vertices, 2,476 triangles, one mesh, one untextured material, and no cockpit interior. The sample rotates its nose toward engine +Z and scales its length to 12 metres to fit the existing camera. It is intentionally a modest visual improvement over the block placeholder, not a finished production spacecraft. Recheck model orientation, cockpit obstruction, shading, follow-camera framing, and surface clearance in the editor before accepting it. Existing kinematic collision and flight limitations still apply; no physical collider or rigid body is added.
