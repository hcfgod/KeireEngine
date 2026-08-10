# Built-In Meshes

Kéire exposes a small stable mesh catalog for prototypes, material previews, scene authoring, VFX mesh shapes, and
tests. Choose **Built-in** in any Mesh asset picker; built-ins do not create source files and are available in editor,
cooked player, and SDK builds.

## Geometry Contract

All built-ins use metres, a centered origin, Y up, finite unit normals, normalized UV0 coordinates, tangents, one
material slot, and one LOD. Closed primitives fit inside a one-metre bounding box. Plane is a 1 x 1 XZ surface facing
+Y; Quad is a 1 x 1 XY surface facing +Z. The same mesh thumbnail renderer presents imported and built-in geometry.

| Mesh | Dimensions and orientation | Expected collision |
| --- | --- | --- |
| Cube | 1 x 1 x 1 | Box or convex collider |
| Sphere | 1 m diameter | Sphere collider |
| Capsule | 1 m tall, 0.5 m diameter | Capsule collider |
| Cylinder | 1 m tall, 1 m diameter | Convex collider |
| Cone | 1 m tall, 1 m base diameter | Convex collider |
| Plane | 1 x 1 on XZ, facing +Y | Static plane; no volume |
| Quad | 1 x 1 on XY, facing +Z | Static plane; no volume |
| Torus | 1 m outer diameter, Y-axis ring | Static mesh or compound collider |

`Keire::BuiltinMeshCatalog()` is the supported enumeration boundary. Each descriptor carries its stable `AssetId`,
display name, closed/open classification, and collision expectation. Use `MeshAsset::ResolveBuiltin(id)` when a value
may be built-in; ordinary project assets continue through `AssetSystem`.

Built-in IDs are serialized like normal mesh references and must remain stable. Adding a mesh requires catalog and
geometry tests, picker/runtime integration, documentation, and a consistent thumbnail before it is supported.

Existing Cube and Error IDs are unchanged. Callers with exhaustive `BuiltinMesh` switches must add the new values;
ID-driven code should prefer `BuiltinMeshCatalog()` or `MeshAsset::ResolveBuiltin()` so later catalog additions remain
source-compatible.
