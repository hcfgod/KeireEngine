# Near-surface globe geometry

The original showcase scaled the engine's general-purpose sphere (16 latitude segments, 24 longitude segments) to Earth size. At the north-pole launch point, its first ring was roughly 1,241 km away and 122 km lower. A face midpoint could be about 30.6 km inside the analytic spherical surface. This cannot represent the roughly 13 km horizon seen from a camera only 13.5 m above the surface. Atmosphere ground intersections and the coarse mesh therefore described substantially different shapes.

`Author-PlanetGlobeMeshes.ps1` replaces only the showcase's Earth/Mars sphere references. It does not alter engine primitives, atmosphere, lighting, or physics. Close the editor first, then run:

```powershell
./Samples/PlanetaryExplorer/Test-PlanetGlobeMeshes.ps1
./Samples/PlanetaryExplorer/Author-PlanetGlobeMeshes.ps1 -Project D:/Projects/KeireValidation/PlanetaryExplorer
```

Each planet gets two meshes with a shared four-degree latitude boundary: a dense north-pole cap and the remaining globe body. They contain no overlapping faces. The cap is anchored at the launch surface, so its small local heights are computed in double precision before conversion to mesh floats. It keeps the existing global longitude/latitude texture coordinates, including the OBJ importer's handedness and V conversion. The body remains planet-centered. Materials are retained. The previous scene and metadata are backed up outside Assets in `AuthoringBackups/BeforeGlobeMeshes`; the script refuses to overwrite an earlier result.

The fixed sample mesh budget is 65,280 triangles per part, 130,560 per planet, two surface renderers per planet. Earth cap face-centroid error is at most 2.274 m near its distant edge, and below 0.25 m in the first four launch-region rings. The far body has up to approximately 833 m centroid error; it is intended for orbital viewing rather than landing elsewhere. Shared seam positions and UVs agree in double precision. Float conversion may introduce less than 0.5 m vertical mismatch at the approximately 444 km distant boundary. The seam and render performance still require visual acceptance.

This is static, location-specific sample geometry, not a dynamic planetary terrain/LOD system. It supports the demonstrated polar ascent/descent better than a scaled primitive. Arbitrary landing sites, close-up terrain, collision meshes, surface streaming, and dynamic tessellation remain separate work. The Earth map is still a coarse global image at ground level.

`Test-PlanetGlobeMeshes.ps1` checks both radii, all vertex radii and UV ranges, all triangle winding, seam identity, float seam tolerance, and launch-region geometry error. It also runs the production model/scene importers in an isolated temporary project and validates the resulting catalog. Use `-SkipImport` for numerical checks alone.

The 12-metre NASA orbiter's straight-ahead cockpit view does not require a camera offset change based on its geometry: vertices ahead of cockpit Z=2.5 m stay below local Y=-0.39 m, while the camera is at Y=1.5 m. Its full bounds are approximately X ±3.88 m, Y ±2.30 m, Z ±6 m. The follow camera is outside those bounds. Lower peripheral views can naturally include the nose. There is no modeled cockpit interior. Brightness problems that remain after geometry replacement still need a separate lighting/shadow check; this geometry audit does not prove their cause.
