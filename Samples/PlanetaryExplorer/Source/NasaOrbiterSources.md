# NASA Space Shuttle orbiter source

Model: Space Shuttle (B), credited by NASA Science to NASA/Johnson Space Center.

- Source page: https://science.nasa.gov/3d-resources/space-shuttle-b/
- Original GLB: https://assets.science.nasa.gov/content/dam/science/cds/3d/resources/model/space-shuttle-%28b%29/Space%20Shuttle%20%28B%29.glb
- Original SHA256: FBB2443EFD9FC1F06C1C267EFD2EA0E02C3D3CC442F925B1D5D222B4DE4B736A
- Usage guidance: https://www.nasa.gov/nasa-brand-center/images-and-media/

NASA media usage guidance applies; NASA does not endorse this sample. Preserve this credit when redistributing the model. Do not use NASA identifiers as this project's branding.

The original contains Draco-compressed geometry. The sample uses an uncompressed derivative produced by `@gltf-transform/cli` 4.5.0 `copy`, which decodes Draco without a lossy re-encode. No geometry, material, or animation changes are requested by that command.

- Converted SHA256: F983739A0185DA1756C9A28F55A2CDA2EA869EABE939953EFF88B4D61B4389B2
- Tool documentation: https://gltf-transform.dev/cli
- Contents: one mesh, one untextured material, 1,440 vertices, 2,476 triangles; no cockpit interior or collision shapes.
- Source bounds: X -2.886356 to 2.886356, Y -4.461737 to 4.461737, Z -1.711404 to 1.711404 (source units).

The sample prefab rotates the imported model to face engine +Z and scales its length to 12 metres to fit the existing demonstration camera. This is not a physically scaled shuttle simulation. Orbiter geometry and camera alignment still require visual acceptance; this model is a better silhouette than the block placeholder, not finished close-up art.
