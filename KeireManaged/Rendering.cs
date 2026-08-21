namespace Keire;

public sealed class Mesh;
public sealed class Material;
public sealed class MaterialParameterCollection;
public sealed class Shader;
public sealed class Texture;

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

public readonly record struct CameraHandle(Entity Entity)
{
    private const NativeRenderingComponent Component = NativeRenderingComponent.Camera;
    public bool IsValid => Entity.IsValid && Entity.HasComponent<CameraComponent>();
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

public readonly record struct MeshRendererHandle(Entity Entity)
{
    private const NativeRenderingComponent Component = NativeRenderingComponent.MeshRenderer;
    public bool IsValid => Entity.IsValid && Entity.HasComponent<MeshRendererComponent>();
    public AssetReference<Mesh> Mesh
    {
        get => new(NativeRuntimeRendering.GetAsset(Entity, Component, NativeRenderingAssetProperty.Mesh));
        set => NativeRuntimeRendering.SetAsset(Entity, Component, NativeRenderingAssetProperty.Mesh, value.Id);
    }
    public IReadOnlyList<AssetReference<Material>> Materials
    {
        get => NativeRuntimeRendering.GetMaterials(Entity);
        set => NativeRuntimeRendering.SetMaterials(Entity, value);
    }
    public AssetReference<Material> Material
    {
        get => Materials.Count > 0 ? Materials[0] : default;
        set => Materials = [value];
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

    public MaterialInstanceHandle GetMaterialInstance(int materialSlot = 0)
    {
        if (materialSlot is < 0 or > 255)
            throw new ArgumentOutOfRangeException(nameof(materialSlot), "Material slots must be in the range 0..255.");
        return new MaterialInstanceHandle(Entity, (uint)materialSlot);
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
    public void SetTexture(string name, AssetReference<Texture> value) =>
        NativeRuntimeRendering.SetMaterialProperty(Entity, name, value.Id);
    public bool Reset(string name) => NativeRuntimeRendering.ResetMaterialProperty(Entity, name);
    public void Clear() => NativeRuntimeRendering.ClearMaterialProperties(Entity);
}

public readonly record struct MaterialInstanceHandle(Entity Entity, uint MaterialSlot)
{
    public AssetReference<Material> SharedMaterial
    {
        get
        {
            IReadOnlyList<AssetReference<Material>> materials = NativeRuntimeRendering.GetMaterials(Entity);
            return MaterialSlot < (uint)materials.Count ? materials[(int)MaterialSlot] : default;
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
    public void SetTexture(string name, AssetReference<Texture> value) =>
        NativeRuntimeRendering.SetMaterialInstanceProperty(Entity, MaterialSlot, name, value.Id);
    public bool Reset(string name) =>
        NativeRuntimeRendering.ResetMaterialInstanceProperty(Entity, MaterialSlot, name);
    public void Clear() => NativeRuntimeRendering.ClearMaterialInstanceProperties(Entity, MaterialSlot);
}

public readonly record struct MaterialParameterCollectionHandle(AssetReference<MaterialParameterCollection> Asset)
{
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
    public void SetTexture(string name, AssetReference<Texture> value) =>
        NativeRuntimeRendering.SetMaterialParameter(Asset.Id, name, value.Id);
    public bool Reset(string name) => NativeRuntimeRendering.ResetMaterialParameter(Asset.Id, name);
    public void Clear() => NativeRuntimeRendering.ClearMaterialParameters(Asset.Id);
}

public static class GlobalMaterialParameters
{
    public static MaterialParameterCollectionHandle Open(
        AssetReference<MaterialParameterCollection> collection)
    {
        if (!collection.IsValid)
            throw new ArgumentException("A valid Material Parameter Collection asset is required.", nameof(collection));
        _ = NativeRuntimeRendering.MaterialParameterCollectionReady(collection.Id);
        return new MaterialParameterCollectionHandle(collection);
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
    internal AssetReference<Texture> GetCookie() =>
        new(NativeRuntimeRendering.GetAsset(Entity, Component, NativeRenderingAssetProperty.Cookie));
    internal void SetCookie(AssetReference<Texture> value) =>
        NativeRuntimeRendering.SetAsset(Entity, Component, NativeRenderingAssetProperty.Cookie, value.Id);
    internal bool GetContactShadows() =>
        NativeRuntimeRendering.GetFlag(Entity, Component, NativeRenderingFlagProperty.ContactShadows);
    internal void SetContactShadows(bool value) =>
        NativeRuntimeRendering.SetFlag(Entity, Component, NativeRenderingFlagProperty.ContactShadows, value);
    internal float GetIndirectMultiplier() =>
        NativeRuntimeRendering.GetScalar(Entity, Component, NativeRenderingScalarProperty.IndirectMultiplier);
    internal void SetIndirectMultiplier(float value) =>
        NativeRuntimeRendering.SetScalar(Entity, Component, NativeRenderingScalarProperty.IndirectMultiplier, value);
}

public readonly record struct DirectionalLightHandle(Entity Entity)
{
    private LightHandle Common => new(Entity, NativeRenderingComponent.DirectionalLight);
    public bool IsValid => Entity.IsValid && Entity.HasComponent<DirectionalLightComponent>();
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
    public AssetReference<Texture> Cookie { get => Common.GetCookie(); set => Common.SetCookie(value); }
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

public readonly record struct PointLightHandle(Entity Entity)
{
    private LightHandle Common => new(Entity, NativeRenderingComponent.PointLight);
    public bool IsValid => Entity.IsValid && Entity.HasComponent<PointLightComponent>();
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
    public AssetReference<Texture> Cookie { get => Common.GetCookie(); set => Common.SetCookie(value); }
    public bool ContactShadows { get => Common.GetContactShadows(); set => Common.SetContactShadows(value); }
    public float IndirectMultiplier
    {
        get => Common.GetIndirectMultiplier();
        set => Common.SetIndirectMultiplier(value);
    }
}

public readonly record struct SpotLightHandle(Entity Entity)
{
    private LightHandle Common => new(Entity, NativeRenderingComponent.SpotLight);
    public bool IsValid => Entity.IsValid && Entity.HasComponent<SpotLightComponent>();
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
    public AssetReference<Texture> Cookie { get => Common.GetCookie(); set => Common.SetCookie(value); }
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
