namespace Keire;

[StableAssetTypeId("4b454952-454d-4553-4841-535345540001")]
public sealed class Mesh : Asset;
[StableAssetTypeId("4b454952-454d-4154-4552-49414c000001")]
public sealed class Material : Asset;
[StableAssetTypeId("4b454952-454d-5043-4f4c-4c4543540001")]
public sealed class MaterialParameterCollection : Asset;
[StableAssetTypeId("4b454952-4553-4841-4445-520000000001")]
public sealed class Shader : Asset;
[StableAssetTypeId("4b454952-4554-4558-5455-524532440001")]
public sealed class Texture : Asset;
[StableAssetTypeId("4b454952-4553-4752-4150-480000000001")]
public sealed class ShaderGraph : Asset;
[StableAssetTypeId("4b454952-4553-4749-4e53-540000000001")]
public sealed class ShaderGraphInstance : Asset;
[StableAssetTypeId("4b454952-454d-4752-4150-480000000001")]
public sealed class MaterialGraph : Asset;
[StableAssetTypeId("4b454952-454d-494e-5354-414e43450001")]
public sealed class MaterialInstance : Asset;

public enum CameraProjection
{
    Perspective,
    Orthographic
}

public enum CameraClearMode
{
    Skybox,
    SolidColor
}

public enum GIReceiveMode
{
    LightProbes,
    Lightmaps,
    Disabled
}

public enum ShadowQuality
{
    Disabled,
    Hard,
    Soft
}

public enum LightBakeMode
{
    Realtime,
    Mixed,
    Baked
}

public enum ShadowResolution
{
    Low,
    Medium,
    High,
    VeryHigh
}

[StableComponentId("4b454952-4543-414d-4552-410000000001")]
public sealed class Camera : Component
{
    internal Camera(Entity entity) : base(entity) { }
    private const NativeRenderingComponent Component = NativeRenderingComponent.Camera;
    public CameraProjection Projection
    {
        get => (CameraProjection)NativeRuntimeRendering.GetInteger(Entity, Component,
                                                                   NativeRenderingIntegerProperty.Projection);
        set => NativeRuntimeRendering.SetInteger(Entity, Component, NativeRenderingIntegerProperty.Projection,
                                                 (int)value);
    }
    public CameraClearMode ClearMode
    {
        get => (CameraClearMode)NativeRuntimeRendering.GetInteger(Entity, Component,
                                                                  NativeRenderingIntegerProperty.ClearMode);
        set => NativeRuntimeRendering.SetInteger(Entity, Component, NativeRenderingIntegerProperty.ClearMode,
                                                 (int)value);
    }
    public bool Primary
    {
        get => NativeRuntimeRendering.GetFlag(Entity, Component, NativeRenderingFlagProperty.Primary);
        set => NativeRuntimeRendering.SetFlag(Entity, Component, NativeRenderingFlagProperty.Primary, value);
    }
    public int Priority
    {
        get => NativeRuntimeRendering.GetInteger(Entity, Component, NativeRenderingIntegerProperty.Priority);
        set => NativeRuntimeRendering.SetInteger(Entity, Component, NativeRenderingIntegerProperty.Priority, value);
    }
    public float VerticalFieldOfView
    {
        get => NativeRuntimeRendering.GetScalar(Entity, Component,
                                                NativeRenderingScalarProperty.VerticalFieldOfView);
        set => NativeRuntimeRendering.SetScalar(Entity, Component,
                                                NativeRenderingScalarProperty.VerticalFieldOfView, value);
    }
    public float OrthographicSize
    {
        get => NativeRuntimeRendering.GetScalar(Entity, Component, NativeRenderingScalarProperty.OrthographicSize);
        set => NativeRuntimeRendering.SetScalar(Entity, Component, NativeRenderingScalarProperty.OrthographicSize,
                                                value);
    }
    public float NearPlane
    {
        get => NativeRuntimeRendering.GetScalar(Entity, Component, NativeRenderingScalarProperty.NearPlane);
        set => NativeRuntimeRendering.SetScalar(Entity, Component, NativeRenderingScalarProperty.NearPlane, value);
    }
    public float FarPlane
    {
        get => NativeRuntimeRendering.GetScalar(Entity, Component, NativeRenderingScalarProperty.FarPlane);
        set => NativeRuntimeRendering.SetScalar(Entity, Component, NativeRenderingScalarProperty.FarPlane, value);
    }
    public Color ClearColor
    {
        get => NativeRuntimeRendering.GetColor(Entity, Component, NativeRenderingColorProperty.ClearColor);
        set => NativeRuntimeRendering.SetColor(Entity, Component, NativeRenderingColorProperty.ClearColor, value);
    }
}

[StableComponentId("4b454952-454d-4553-4852-454e44455201")]
public sealed class MeshRenderer : Component
{
    internal MeshRenderer(Entity entity) : base(entity) { }
    private const NativeRenderingComponent Component = NativeRenderingComponent.MeshRenderer;
    public Mesh? Mesh
    {
        get => Asset.FromId<Mesh>(NativeRuntimeRendering.GetAsset(Entity, Component, NativeRenderingAssetProperty.Mesh));
        set => NativeRuntimeRendering.SetAsset(Entity, Component, NativeRenderingAssetProperty.Mesh, value?.Id ?? default);
    }
    public IReadOnlyList<Material> Materials
    {
        get => NativeRuntimeRendering.GetMaterials(Entity);
        set => NativeRuntimeRendering.SetMaterials(Entity, value);
    }
    public Material? Material
    {
        get => Materials.Count > 0 ? Materials[0] : null;
        set => Materials = value is null ? [] : [value];
    }
    public Color Tint
    {
        get => NativeRuntimeRendering.GetColor(Entity, Component, NativeRenderingColorProperty.Tint);
        set => NativeRuntimeRendering.SetColor(Entity, Component, NativeRenderingColorProperty.Tint, value);
    }
    public bool Visible
    {
        get => NativeRuntimeRendering.GetFlag(Entity, Component, NativeRenderingFlagProperty.Visible);
        set => NativeRuntimeRendering.SetFlag(Entity, Component, NativeRenderingFlagProperty.Visible, value);
    }
    public bool CastShadows
    {
        get => NativeRuntimeRendering.GetFlag(Entity, Component, NativeRenderingFlagProperty.CastShadows);
        set => NativeRuntimeRendering.SetFlag(Entity, Component, NativeRenderingFlagProperty.CastShadows, value);
    }
    public bool ReceiveShadows
    {
        get => NativeRuntimeRendering.GetFlag(Entity, Component, NativeRenderingFlagProperty.ReceiveShadows);
        set => NativeRuntimeRendering.SetFlag(Entity, Component, NativeRenderingFlagProperty.ReceiveShadows, value);
    }
    public bool StaticLighting
    {
        get => NativeRuntimeRendering.GetFlag(Entity, Component, NativeRenderingFlagProperty.StaticLighting);
        set => NativeRuntimeRendering.SetFlag(Entity, Component, NativeRenderingFlagProperty.StaticLighting, value);
    }
    public bool PreserveLightmapUVs
    {
        get => NativeRuntimeRendering.GetFlag(Entity, Component, NativeRenderingFlagProperty.PreserveLightmapUVs);
        set => NativeRuntimeRendering.SetFlag(Entity, Component, NativeRenderingFlagProperty.PreserveLightmapUVs,
                                              value);
    }
    public GIReceiveMode GIReceive
    {
        get => (GIReceiveMode)NativeRuntimeRendering.GetInteger(Entity, Component,
                                                                NativeRenderingIntegerProperty.GIReceive);
        set => NativeRuntimeRendering.SetInteger(Entity, Component, NativeRenderingIntegerProperty.GIReceive,
                                                 (int)value);
    }
    public float LightmapScale
    {
        get => NativeRuntimeRendering.GetScalar(Entity, Component, NativeRenderingScalarProperty.LightmapScale);
        set => NativeRuntimeRendering.SetScalar(Entity, Component, NativeRenderingScalarProperty.LightmapScale,
                                                value);
    }
    public MaterialPropertyBlock PropertyBlock => new(Entity);

    public DynamicMaterial GetMaterialInstance(int materialSlot = 0)
    {
        if (materialSlot is < 0 or > 255)
            throw new ArgumentOutOfRangeException(nameof(materialSlot), "Material slots must be in the range 0..255.");
        return new DynamicMaterial(Entity, (uint)materialSlot);
    }
}

public readonly record struct MaterialPropertyBlock(Entity Entity)
{
    public void SetFloat(string name, float value)
    {
        if (!float.IsFinite(value))
            throw new ArgumentOutOfRangeException(nameof(value), "Material floats must be finite.");
        NativeRuntimeRendering.SetMaterialProperty(Entity, name, value);
    }

    public void SetVector(string name, Vector2 value) => NativeRuntimeRendering.SetMaterialProperty(Entity, name, value);
    public void SetVector(string name, Vector3 value) => NativeRuntimeRendering.SetMaterialProperty(Entity, name, value);
    public void SetVector(string name, Vector4 value) => NativeRuntimeRendering.SetMaterialProperty(Entity, name, value);
    public void SetColor(string name, Color value) => NativeRuntimeRendering.SetMaterialProperty(Entity, name, value);
    public void SetTexture(string name, Texture? value) =>
        NativeRuntimeRendering.SetMaterialProperty(Entity, name, value?.Id ?? default);
    public bool Reset(string name) => NativeRuntimeRendering.ResetMaterialProperty(Entity, name);
    public void Clear() => NativeRuntimeRendering.ClearMaterialProperties(Entity);
}

public sealed class DynamicMaterial
{
    internal DynamicMaterial(Entity entity, uint materialSlot) => (Entity, MaterialSlot) = (entity, materialSlot);
    public Entity Entity { get; }
    public uint MaterialSlot { get; }
    public Material? SharedMaterial
    {
        get
        {
            IReadOnlyList<Material> materials = NativeRuntimeRendering.GetMaterials(Entity);
            return MaterialSlot < (uint)materials.Count ? materials[(int)MaterialSlot] : null;
        }
    }

    public void SetFloat(string name, float value)
    {
        if (!float.IsFinite(value))
            throw new ArgumentOutOfRangeException(nameof(value), "Material floats must be finite.");
        NativeRuntimeRendering.SetMaterialInstanceProperty(Entity, MaterialSlot, name, value);
    }

    public void SetVector(string name, Vector2 value) =>
        NativeRuntimeRendering.SetMaterialInstanceProperty(Entity, MaterialSlot, name, value);
    public void SetVector(string name, Vector3 value) =>
        NativeRuntimeRendering.SetMaterialInstanceProperty(Entity, MaterialSlot, name, value);
    public void SetVector(string name, Vector4 value) =>
        NativeRuntimeRendering.SetMaterialInstanceProperty(Entity, MaterialSlot, name, value);
    public void SetColor(string name, Color value) =>
        NativeRuntimeRendering.SetMaterialInstanceProperty(Entity, MaterialSlot, name, value);
    public void SetTexture(string name, Texture? value) =>
        NativeRuntimeRendering.SetMaterialInstanceProperty(Entity, MaterialSlot, name, value?.Id ?? default);
    public bool Reset(string name) =>
        NativeRuntimeRendering.ResetMaterialInstanceProperty(Entity, MaterialSlot, name);
    public void Clear() => NativeRuntimeRendering.ClearMaterialInstanceProperties(Entity, MaterialSlot);
}

public sealed class MaterialParameterCollectionInstance
{
    internal MaterialParameterCollectionInstance(MaterialParameterCollection asset) => Asset = asset;
    public MaterialParameterCollection Asset { get; }
    public bool IsReady => Asset.IsValid && NativeRuntimeRendering.MaterialParameterCollectionReady(Asset.Id);

    public void SetFloat(string name, float value)
    {
        if (!float.IsFinite(value))
            throw new ArgumentOutOfRangeException(nameof(value), "Material parameters must be finite.");
        NativeRuntimeRendering.SetMaterialParameter(Asset.Id, name, value);
    }

    public void SetVector(string name, Vector2 value) => NativeRuntimeRendering.SetMaterialParameter(Asset.Id, name, value);
    public void SetVector(string name, Vector3 value) => NativeRuntimeRendering.SetMaterialParameter(Asset.Id, name, value);
    public void SetVector(string name, Vector4 value) => NativeRuntimeRendering.SetMaterialParameter(Asset.Id, name, value);
    public void SetColor(string name, Color value) => NativeRuntimeRendering.SetMaterialParameter(Asset.Id, name, value);
    public void SetTexture(string name, Texture? value) =>
        NativeRuntimeRendering.SetMaterialParameter(Asset.Id, name, value?.Id ?? default);
    public bool Reset(string name) => NativeRuntimeRendering.ResetMaterialParameter(Asset.Id, name);
    public void Clear() => NativeRuntimeRendering.ClearMaterialParameters(Asset.Id);
}

public static class GlobalMaterialParameters
{
    public static MaterialParameterCollectionInstance Open(MaterialParameterCollection collection)
    {
        if (!collection.IsValid)
            throw new ArgumentException("A valid Material Parameter Collection asset is required.", nameof(collection));
        _ = NativeRuntimeRendering.MaterialParameterCollectionReady(collection.Id);
        return new MaterialParameterCollectionInstance(collection);
    }
}

internal readonly record struct LightHandle(Entity Entity, NativeRenderingComponent Component)
{
    internal Color GetColor() =>
        NativeRuntimeRendering.GetColor(Entity, Component, NativeRenderingColorProperty.LightColor);
    internal void SetColor(Color value) =>
        NativeRuntimeRendering.SetColor(Entity, Component, NativeRenderingColorProperty.LightColor, value);
    internal float GetIntensity() =>
        NativeRuntimeRendering.GetScalar(Entity, Component, NativeRenderingScalarProperty.Intensity);
    internal void SetIntensity(float value) =>
        NativeRuntimeRendering.SetScalar(Entity, Component, NativeRenderingScalarProperty.Intensity, value);
    internal ShadowQuality GetShadows() =>
        (ShadowQuality)NativeRuntimeRendering.GetInteger(Entity, Component, NativeRenderingIntegerProperty.Shadows);
    internal void SetShadows(ShadowQuality value) =>
        NativeRuntimeRendering.SetInteger(Entity, Component, NativeRenderingIntegerProperty.Shadows, (int)value);
    internal float GetShadowStrength() =>
        NativeRuntimeRendering.GetScalar(Entity, Component, NativeRenderingScalarProperty.ShadowStrength);
    internal void SetShadowStrength(float value) =>
        NativeRuntimeRendering.SetScalar(Entity, Component, NativeRenderingScalarProperty.ShadowStrength, value);
    internal float GetShadowBias() =>
        NativeRuntimeRendering.GetScalar(Entity, Component, NativeRenderingScalarProperty.ShadowBias);
    internal void SetShadowBias(float value) =>
        NativeRuntimeRendering.SetScalar(Entity, Component, NativeRenderingScalarProperty.ShadowBias, value);
    internal LightBakeMode GetBakeMode() =>
        (LightBakeMode)NativeRuntimeRendering.GetInteger(Entity, Component, NativeRenderingIntegerProperty.BakeMode);
    internal void SetBakeMode(LightBakeMode value) =>
        NativeRuntimeRendering.SetInteger(Entity, Component, NativeRenderingIntegerProperty.BakeMode, (int)value);
    internal ShadowResolution GetShadowResolution() =>
        (ShadowResolution)NativeRuntimeRendering.GetInteger(Entity, Component,
                                                            NativeRenderingIntegerProperty.ShadowResolution);
    internal void SetShadowResolution(ShadowResolution value) =>
        NativeRuntimeRendering.SetInteger(Entity, Component, NativeRenderingIntegerProperty.ShadowResolution,
                                          (int)value);
    internal Texture? GetCookie() =>
        Asset.FromId<Texture>(NativeRuntimeRendering.GetAsset(Entity, Component, NativeRenderingAssetProperty.Cookie));
    internal void SetCookie(Texture? value) =>
        NativeRuntimeRendering.SetAsset(Entity, Component, NativeRenderingAssetProperty.Cookie, value?.Id ?? default);
    internal bool GetContactShadows() =>
        NativeRuntimeRendering.GetFlag(Entity, Component, NativeRenderingFlagProperty.ContactShadows);
    internal void SetContactShadows(bool value) =>
        NativeRuntimeRendering.SetFlag(Entity, Component, NativeRenderingFlagProperty.ContactShadows, value);
    internal float GetIndirectMultiplier() =>
        NativeRuntimeRendering.GetScalar(Entity, Component, NativeRenderingScalarProperty.IndirectMultiplier);
    internal void SetIndirectMultiplier(float value) =>
        NativeRuntimeRendering.SetScalar(Entity, Component, NativeRenderingScalarProperty.IndirectMultiplier, value);
}

[StableComponentId("4b454952-4544-4952-4c49-474854000001")]
public sealed class DirectionalLight : Component
{
    internal DirectionalLight(Entity entity) : base(entity) { }
    private LightHandle Common => new(Entity, NativeRenderingComponent.DirectionalLight);
    public Color Color { get => Common.GetColor(); set => Common.SetColor(value); }
    public float Intensity { get => Common.GetIntensity(); set => Common.SetIntensity(value); }
    public ShadowQuality Shadows { get => Common.GetShadows(); set => Common.SetShadows(value); }
    public float ShadowStrength { get => Common.GetShadowStrength(); set => Common.SetShadowStrength(value); }
    public float ShadowBias { get => Common.GetShadowBias(); set => Common.SetShadowBias(value); }
    public LightBakeMode BakeMode { get => Common.GetBakeMode(); set => Common.SetBakeMode(value); }
    public ShadowResolution ShadowResolution
    {
        get => Common.GetShadowResolution();
        set => Common.SetShadowResolution(value);
    }
    public Texture? Cookie { get => Common.GetCookie(); set => Common.SetCookie(value); }
    public bool ContactShadows { get => Common.GetContactShadows(); set => Common.SetContactShadows(value); }
    public float IndirectMultiplier
    {
        get => Common.GetIndirectMultiplier();
        set => Common.SetIndirectMultiplier(value);
    }
    public bool UseColorTemperature
    {
        get => NativeRuntimeRendering.GetFlag(Entity, NativeRenderingComponent.DirectionalLight,
                                              NativeRenderingFlagProperty.UseColorTemperature);
        set => NativeRuntimeRendering.SetFlag(Entity, NativeRenderingComponent.DirectionalLight,
                                              NativeRenderingFlagProperty.UseColorTemperature, value);
    }
    public float ColorTemperature
    {
        get => NativeRuntimeRendering.GetScalar(Entity, NativeRenderingComponent.DirectionalLight,
                                                NativeRenderingScalarProperty.ColorTemperature);
        set => NativeRuntimeRendering.SetScalar(Entity, NativeRenderingComponent.DirectionalLight,
                                                NativeRenderingScalarProperty.ColorTemperature, value);
    }
    public Vector2 CookieScale
    {
        get => NativeRuntimeRendering.GetVector(Entity, NativeRenderingComponent.DirectionalLight,
                                                NativeRenderingVectorProperty.CookieScale);
        set => NativeRuntimeRendering.SetVector(Entity, NativeRenderingComponent.DirectionalLight,
                                                NativeRenderingVectorProperty.CookieScale, value);
    }
    public Vector2 CookieOffset
    {
        get => NativeRuntimeRendering.GetVector(Entity, NativeRenderingComponent.DirectionalLight,
                                                NativeRenderingVectorProperty.CookieOffset);
        set => NativeRuntimeRendering.SetVector(Entity, NativeRenderingComponent.DirectionalLight,
                                                NativeRenderingVectorProperty.CookieOffset, value);
    }
    public float CookieRotation
    {
        get => NativeRuntimeRendering.GetScalar(Entity, NativeRenderingComponent.DirectionalLight,
                                                NativeRenderingScalarProperty.CookieRotation);
        set => NativeRuntimeRendering.SetScalar(Entity, NativeRenderingComponent.DirectionalLight,
                                                NativeRenderingScalarProperty.CookieRotation, value);
    }
}

[StableComponentId("4b454952-4550-4f49-4e54-4c4947485401")]
public sealed class PointLight : Component
{
    internal PointLight(Entity entity) : base(entity) { }
    private LightHandle Common => new(Entity, NativeRenderingComponent.PointLight);
    public Color Color { get => Common.GetColor(); set => Common.SetColor(value); }
    public float Intensity { get => Common.GetIntensity(); set => Common.SetIntensity(value); }
    public float Range
    {
        get => NativeRuntimeRendering.GetScalar(Entity, NativeRenderingComponent.PointLight,
                                                NativeRenderingScalarProperty.Range);
        set => NativeRuntimeRendering.SetScalar(Entity, NativeRenderingComponent.PointLight,
                                                NativeRenderingScalarProperty.Range, value);
    }
    public ShadowQuality Shadows { get => Common.GetShadows(); set => Common.SetShadows(value); }
    public float ShadowStrength { get => Common.GetShadowStrength(); set => Common.SetShadowStrength(value); }
    public float ShadowBias { get => Common.GetShadowBias(); set => Common.SetShadowBias(value); }
    public LightBakeMode BakeMode { get => Common.GetBakeMode(); set => Common.SetBakeMode(value); }
    public ShadowResolution ShadowResolution
    {
        get => Common.GetShadowResolution();
        set => Common.SetShadowResolution(value);
    }
    public Texture? Cookie { get => Common.GetCookie(); set => Common.SetCookie(value); }
    public bool ContactShadows { get => Common.GetContactShadows(); set => Common.SetContactShadows(value); }
    public float IndirectMultiplier
    {
        get => Common.GetIndirectMultiplier();
        set => Common.SetIndirectMultiplier(value);
    }
}

[StableComponentId("4b454952-4553-504f-544c-494748540001")]
public sealed class SpotLight : Component
{
    internal SpotLight(Entity entity) : base(entity) { }
    private LightHandle Common => new(Entity, NativeRenderingComponent.SpotLight);
    public Color Color { get => Common.GetColor(); set => Common.SetColor(value); }
    public float Intensity { get => Common.GetIntensity(); set => Common.SetIntensity(value); }
    public float Range
    {
        get => NativeRuntimeRendering.GetScalar(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingScalarProperty.Range);
        set => NativeRuntimeRendering.SetScalar(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingScalarProperty.Range, value);
    }
    public float InnerAngle
    {
        get => NativeRuntimeRendering.GetScalar(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingScalarProperty.InnerAngle);
        set => NativeRuntimeRendering.SetScalar(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingScalarProperty.InnerAngle, value);
    }
    public float OuterAngle
    {
        get => NativeRuntimeRendering.GetScalar(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingScalarProperty.OuterAngle);
        set => NativeRuntimeRendering.SetScalar(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingScalarProperty.OuterAngle, value);
    }
    public ShadowQuality Shadows { get => Common.GetShadows(); set => Common.SetShadows(value); }
    public float ShadowStrength { get => Common.GetShadowStrength(); set => Common.SetShadowStrength(value); }
    public float ShadowBias { get => Common.GetShadowBias(); set => Common.SetShadowBias(value); }
    public LightBakeMode BakeMode { get => Common.GetBakeMode(); set => Common.SetBakeMode(value); }
    public ShadowResolution ShadowResolution
    {
        get => Common.GetShadowResolution();
        set => Common.SetShadowResolution(value);
    }
    public Texture? Cookie { get => Common.GetCookie(); set => Common.SetCookie(value); }
    public bool ContactShadows { get => Common.GetContactShadows(); set => Common.SetContactShadows(value); }
    public float IndirectMultiplier
    {
        get => Common.GetIndirectMultiplier();
        set => Common.SetIndirectMultiplier(value);
    }
    public Vector2 CookieScale
    {
        get => NativeRuntimeRendering.GetVector(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingVectorProperty.CookieScale);
        set => NativeRuntimeRendering.SetVector(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingVectorProperty.CookieScale, value);
    }
    public Vector2 CookieOffset
    {
        get => NativeRuntimeRendering.GetVector(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingVectorProperty.CookieOffset);
        set => NativeRuntimeRendering.SetVector(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingVectorProperty.CookieOffset, value);
    }
    public float CookieRotation
    {
        get => NativeRuntimeRendering.GetScalar(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingScalarProperty.CookieRotation);
        set => NativeRuntimeRendering.SetScalar(Entity, NativeRenderingComponent.SpotLight,
                                                NativeRenderingScalarProperty.CookieRotation, value);
    }
}
