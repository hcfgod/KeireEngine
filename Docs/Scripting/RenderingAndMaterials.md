# Rendering And Materials From C#

Managed rendering uses stable entity and asset identities. Scripts can configure scene Cameras, Mesh Renderers, and
lights without receiving a native component, GPU resource, material definition, or graphics handle.

## Component Handles

Every entity exposes typed views for the supported rendering components:

```csharp
CameraHandle camera = Entity.Camera;
MeshRendererHandle renderer = Entity.MeshRenderer;
DirectionalLightHandle sun = Entity.DirectionalLight;
PointLightHandle lamp = Entity.PointLight;
SpotLightHandle flashlight = Entity.SpotLight;
```

Check `IsValid` when the component is optional. Reading or writing through an absent component throws an
`InvalidOperationException`; it never creates a component implicitly. Add required components explicitly through
`Entity.AddComponent<T>()` during a scene-safe gameplay operation.

## Cameras

`CameraHandle` exposes projection and clear modes, primary selection, priority, perspective field of view,
orthographic size, clip planes, and clear color:

```csharp
CameraHandle camera = Entity.Camera;
camera.Projection = CameraProjection.Perspective;
camera.VerticalFieldOfView = 72.0f;
camera.NearPlane = 0.05f;
camera.FarPlane = 1500.0f;
camera.ClearMode = CameraClearMode.SolidColor;
camera.ClearColor = new Color(0.01f, 0.02f, 0.04f, 1.0f);
```

Native component validation remains authoritative. Invalid lens, plane, color, light, shadow, cone, and range values
are rejected without changing the previous state.

## Meshes And Material Slots

Asset markers make Inspector references and runtime assignments type-safe:

```csharp
[SerializeField, StableFieldId("ef7c54f0-35a1-42f0-bdae-0c4c15c99801")]
private AssetReference<Mesh> _mesh;

[SerializeField, StableFieldId("ef7c54f0-35a1-42f0-bdae-0c4c15c99802")]
private AssetReference<Material> _bodyMaterial;

[SerializeField, StableFieldId("ef7c54f0-35a1-42f0-bdae-0c4c15c99803")]
private AssetReference<Material> _detailMaterial;

protected override void Awake()
{
    MeshRendererHandle renderer = Entity.MeshRenderer;
    renderer.Mesh = _mesh;
    renderer.Materials = [_bodyMaterial, _detailMaterial];
    renderer.Visible = true;
    renderer.CastShadows = true;
    renderer.ReceiveShadows = true;
}
```

Assigning `Materials` replaces the complete material override array in one native transaction. The array is limited to
256 entries and preserves empty asset IDs when a mesh slot should use its imported default material.

## Material Property Blocks

A property block changes values for one renderer while keeping the shared material asset immutable:

```csharp
[SerializeField, StableFieldId("ef7c54f0-35a1-42f0-bdae-0c4c15c99804")]
private AssetReference<Texture> _damageMask;

private void ApplyDamagePresentation(float damage)
{
    MaterialPropertyBlock properties = Entity.MeshRenderer.PropertyBlock;
    properties.SetFloat("Damage", Math.Clamp(damage, 0.0f, 1.0f));
    properties.SetColor("Emission", new Color(2.0f, 0.15f, 0.02f, 1.0f));
    properties.SetVector("HitDirection", Entity.Transform.Forward);
    properties.SetTexture("DamageMask", _damageMask);
}
```

Names match exposed properties in the compiled Material/Shader Graph. Supported values are `float`, `Vector2`,
`Vector3`, `Vector4`, `Color`, and `AssetReference<Texture>`. Each renderer supports at most 64 overrides, and each
UTF-8 name supports at most 128 bytes. Unknown names and values whose type does not match the compiled shader are
ignored by that shader; they remain available for a later compatible material assignment.

Use `Reset(name)` to remove one override or `Clear()` to remove the block. Property blocks are transient Play/runtime
state: they are not written into scenes or prefabs and do not survive scene replacement. A renderer with an active
property block is drawn separately instead of entering an incompatible shared instance batch.

## Lights

All light handles expose color, intensity, shadow quality/strength/bias/resolution, bake mode, cookie, contact shadows,
and indirect multiplier. Point and Spot lights add range; Spot lights add cone angles and cookie transform; Directional
lights add color temperature and cookie transform.

```csharp
SpotLightHandle light = Entity.SpotLight;
light.Color = new Color(0.72f, 0.84f, 1.0f, 1.0f);
light.Intensity = 18.0f;
light.Range = 32.0f;
light.InnerAngle = 22.0f;
light.OuterAngle = 38.0f;
light.Shadows = ShadowQuality.Soft;
light.ContactShadows = true;
```

Material/Shader Graph authoring remains editor-owned. Managed runtime graph construction is intentionally outside the
gameplay contract. Runtime render settings, dynamic material instances, and global material parameter collections are
tracked as the remaining rendering parity work in the
[Managed API Capability Matrix](ManagedApiMatrix.md).
