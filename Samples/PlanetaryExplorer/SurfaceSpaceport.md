# Earth surface spaceport

Close the Planetary Explorer editor before authoring, then run:

```powershell
./Samples/PlanetaryExplorer/Author-SurfaceSpaceport.ps1 -Project D:/Projects/KeireValidation/PlanetaryExplorer
```

This adds a surface launch complex at the existing Earth flight origin. It preserves the original scene and its metadata byte-for-byte under `AuthoringBackups/BeforeSpaceport` before updating them. A second run refuses to overwrite the authored assets or backup. The original QA project is never accessed.

The generated objects are grouped under one `Earth Spaceport` hierarchy root. Authoring rejects a moved, rotated or scaled Earth launch cap so geometry cannot silently appear at the wrong planet location.

The complex includes six open hangars with structural roof ribs, a glazed operations tower, antenna, launch gantries, apron and approach-road markings, illuminated pad edges, a fuel farm with service pipes, and a solar field. Seven opaque material groups combine the visual geometry into seven additional renderers rather than creating a draw for every structural part. The terrain is a 16 km curved patch with a flat central apron region and ridged hills. Its mesh uses 51,200 terrain triangles plus 2,304 boundary-stitch triangles; the smaller structural meshes share the same seven-material budget.

The authored Earth cap is replaced by a separate clipped cap asset: the 16 km terrain footprint is removed rather than left as a second almost-coplanar surface. The clip preserves the original globe UVs and normals. A vertical boundary strip joins the exact clipped cap edge to the terrain grid edge, preventing a visible hole. The original globe source is preserved.

The seven generated materials expose normal Kéire material properties. The glass group is opaque tinted metal for a low-cost architectural window treatment; it is not transmissive glass. Beacon material emission is visible but does not create physical point lights. Materials and source OBJ files remain editable assets in `Assets/PlanetaryExplorer/Spaceport`.

There are 38 static box colliders covering the launch/service aprons, major hangar walls and roofs, tower base/crown, gantries and fuel tanks. The distant hill mesh is **visual scenery only**. Solar panels, thin trim and tower windows are visual details. This is a bounded authored demonstration area, not planetary terrain streaming or arbitrary-surface collision. The existing ship remains a kinematic flight demonstration and does not perform collision-resolved landing or docking. The clear origin launch corridor preserves the current flight path and camera controls.

Validation:

```powershell
./Samples/PlanetaryExplorer/Test-SurfaceSpaceport.ps1
```

The isolated regression checks scene backup preservation, unique stable entity identities, seven-renderer budget, static collider coverage, finite mesh positions, valid triangle indices, upward terrain winding, OBJ handedness compensation against an asymmetric collider position, terrain counts and safe repeat-run refusal. The cutout generator verifies projected area conservation, no retained triangle inside the footprint, complete boundary coverage and outward stitch winding. It also imports all eight OBJ meshes with the staged production asset importer and validates their runtime catalog. This test does not prove material rendering, collision behavior or visual quality; inspect the live scene after import, including daylight/night, flight ascent/descent and the primary launch pad. No engine rebuild is needed to author the scene.


Initial editor acceptance found depth striping between the terrain and underlying globe cap. Hiding the cap removes the stripes; disabling only its shadows does not. Current imported float32 mesh payloads retain at least 0.5 m separation in the sampled launch region. After replacing the redundant cap under the terrain, the same editor camera and launch-area Game view no longer show those broad stripes. This validates the sample geometry correction, not a general renderer depth-precision fix. Further visual polish and arbitrary-surface collision remain outside this demonstration.
