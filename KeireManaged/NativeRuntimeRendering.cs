namespace Keire;

internal enum NativeRenderingComponent : byte
{
    Camera,
    MeshRenderer,
    DirectionalLight,
    PointLight,
    SpotLight
}

internal enum NativeRenderingScalarProperty : byte
{
    VerticalFieldOfView,
    OrthographicSize,
    NearPlane,
    FarPlane,
    LightmapScale,
    Intensity,
    Range,
    ColorTemperature,
    InnerAngle,
    OuterAngle,
    ShadowStrength,
    ShadowBias,
    CookieRotation,
    IndirectMultiplier
}

internal enum NativeRenderingIntegerProperty : byte
{
    Priority,
    Projection,
    ClearMode,
    GIReceive,
    Shadows,
    BakeMode,
    ShadowResolution
}

internal enum NativeRenderingFlagProperty : byte
{
    Primary,
    Visible,
    CastShadows,
    ReceiveShadows,
    StaticLighting,
    PreserveLightmapUVs,
    UseColorTemperature,
    ContactShadows
}

internal enum NativeRenderingVectorProperty : byte
{
    CookieScale,
    CookieOffset
}

internal enum NativeRenderingColorProperty : byte
{
    ClearColor,
    Tint,
    LightColor
}

internal enum NativeRenderingAssetProperty : byte
{
    Mesh,
    Cookie
}

internal static unsafe class NativeRuntimeRendering
{
#pragma warning disable CS0649
    internal static delegate* unmanaged<ulong, ulong, byte, byte, float*, byte> GetScalarIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, float, byte> SetScalarIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, int*, byte> GetIntegerIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, int, byte> SetIntegerIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, byte*, byte> GetFlagIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, byte, byte> SetFlagIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, Vector2*, byte> GetVectorIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, Vector2, byte> SetVectorIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, Color*, byte> GetColorIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, Color, byte> SetColorIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, AssetId*, byte> GetAssetIcall;
    internal static delegate* unmanaged<ulong, ulong, byte, byte, AssetId, byte> SetAssetIcall;
    internal static delegate* unmanaged<ulong, ulong, AssetId*, int, int> GetMaterialsIcall;
    internal static delegate* unmanaged<ulong, ulong, AssetId*, int, byte> SetMaterialsIcall;
    internal static delegate* unmanaged<ulong, ulong, NativeString, float, byte> SetMaterialFloatIcall;
    internal static delegate* unmanaged<ulong, ulong, NativeString, Vector2, byte> SetMaterialVector2Icall;
    internal static delegate* unmanaged<ulong, ulong, NativeString, Vector3, byte> SetMaterialVector3Icall;
    internal static delegate* unmanaged<ulong, ulong, NativeString, Vector4, byte> SetMaterialVector4Icall;
    internal static delegate* unmanaged<ulong, ulong, NativeString, Color, byte> SetMaterialColorIcall;
    internal static delegate* unmanaged<ulong, ulong, NativeString, AssetId, byte> SetMaterialTextureIcall;
    internal static delegate* unmanaged<ulong, ulong, NativeString, byte> ResetMaterialPropertyIcall;
    internal static delegate* unmanaged<ulong, ulong, byte> ClearMaterialPropertiesIcall;
#pragma warning restore CS0649

    internal static float GetScalar(Entity entity, NativeRenderingComponent component,
                                    NativeRenderingScalarProperty property)
    {
        float value;
        if (GetScalarIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, &value) == 0)
            throw Unavailable(component, property);
        return value;
    }

    internal static void SetScalar(Entity entity, NativeRenderingComponent component,
                                   NativeRenderingScalarProperty property, float value)
    {
        if (SetScalarIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, value) == 0)
            throw Unchanged(component, property);
    }

    internal static int GetInteger(Entity entity, NativeRenderingComponent component,
                                   NativeRenderingIntegerProperty property)
    {
        int value;
        if (GetIntegerIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, &value) == 0)
            throw Unavailable(component, property);
        return value;
    }

    internal static void SetInteger(Entity entity, NativeRenderingComponent component,
                                    NativeRenderingIntegerProperty property, int value)
    {
        if (SetIntegerIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, value) == 0)
            throw Unchanged(component, property);
    }

    internal static bool GetFlag(Entity entity, NativeRenderingComponent component,
                                 NativeRenderingFlagProperty property)
    {
        byte value;
        if (GetFlagIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, &value) == 0)
            throw Unavailable(component, property);
        return value != 0;
    }

    internal static void SetFlag(Entity entity, NativeRenderingComponent component,
                                 NativeRenderingFlagProperty property, bool value)
    {
        if (SetFlagIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, value ? (byte)1 : (byte)0) == 0)
            throw Unchanged(component, property);
    }

    internal static Vector2 GetVector(Entity entity, NativeRenderingComponent component,
                                      NativeRenderingVectorProperty property)
    {
        Vector2 value;
        if (GetVectorIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, &value) == 0)
            throw Unavailable(component, property);
        return value;
    }

    internal static void SetVector(Entity entity, NativeRenderingComponent component,
                                   NativeRenderingVectorProperty property, Vector2 value)
    {
        if (SetVectorIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, value) == 0)
            throw Unchanged(component, property);
    }

    internal static Color GetColor(Entity entity, NativeRenderingComponent component,
                                   NativeRenderingColorProperty property)
    {
        Color value;
        if (GetColorIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, &value) == 0)
            throw Unavailable(component, property);
        return value;
    }

    internal static void SetColor(Entity entity, NativeRenderingComponent component,
                                  NativeRenderingColorProperty property, Color value)
    {
        if (SetColorIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, value) == 0)
            throw Unchanged(component, property);
    }

    internal static AssetId GetAsset(Entity entity, NativeRenderingComponent component,
                                     NativeRenderingAssetProperty property)
    {
        AssetId value;
        if (GetAssetIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, &value) == 0)
            throw Unavailable(component, property);
        return value;
    }

    internal static void SetAsset(Entity entity, NativeRenderingComponent component,
                                  NativeRenderingAssetProperty property, AssetId value)
    {
        if (SetAssetIcall(entity.Id.High, entity.Id.Low, (byte)component, (byte)property, value) == 0)
            throw Unchanged(component, property);
    }

    internal static IReadOnlyList<AssetReference<Material>> GetMaterials(Entity entity)
    {
        int count = GetMaterialsIcall(entity.Id.High, entity.Id.Low, null, 0);
        if (count < 0)
            throw new InvalidOperationException("The Mesh Renderer materials are unavailable.");
        var values = new AssetId[count];
        fixed (AssetId* destination = values)
        {
            if (GetMaterialsIcall(entity.Id.High, entity.Id.Low, destination, values.Length) != count)
                throw new InvalidOperationException("The Mesh Renderer materials changed while they were read.");
        }
        return values.Select(static value => new AssetReference<Material>(value)).ToArray();
    }

    internal static void SetMaterials(Entity entity, IReadOnlyList<AssetReference<Material>> materials)
    {
        ArgumentNullException.ThrowIfNull(materials);
        if (materials.Count > 256)
            throw new ArgumentOutOfRangeException(nameof(materials), "A Mesh Renderer supports at most 256 materials.");
        var values = new AssetId[materials.Count];
        for (int index = 0; index < values.Length; ++index)
            values[index] = materials[index].Id;
        fixed (AssetId* source = values)
        {
            if (SetMaterialsIcall(entity.Id.High, entity.Id.Low, source, values.Length) == 0)
                throw new InvalidOperationException("The Mesh Renderer materials could not be changed.");
        }
    }

    internal static void SetMaterialProperty(Entity entity, string name, float value) =>
        SetMaterialProperty(entity, name, value, SetMaterialFloatIcall);

    internal static void SetMaterialProperty(Entity entity, string name, Vector2 value) =>
        SetMaterialProperty(entity, name, value, SetMaterialVector2Icall);

    internal static void SetMaterialProperty(Entity entity, string name, Vector3 value) =>
        SetMaterialProperty(entity, name, value, SetMaterialVector3Icall);

    internal static void SetMaterialProperty(Entity entity, string name, Vector4 value) =>
        SetMaterialProperty(entity, name, value, SetMaterialVector4Icall);

    internal static void SetMaterialProperty(Entity entity, string name, Color value) =>
        SetMaterialProperty(entity, name, value, SetMaterialColorIcall);

    internal static void SetMaterialProperty(Entity entity, string name, AssetId value) =>
        SetMaterialProperty(entity, name, value, SetMaterialTextureIcall);

    internal static bool ResetMaterialProperty(Entity entity, string name)
    {
        ValidatePropertyName(name);
        using NativeString nativeName = name;
        return ResetMaterialPropertyIcall(entity.Id.High, entity.Id.Low, nativeName) != 0;
    }

    internal static void ClearMaterialProperties(Entity entity)
    {
        if (ClearMaterialPropertiesIcall(entity.Id.High, entity.Id.Low) == 0)
            throw new InvalidOperationException("The Mesh Renderer material property block could not be cleared.");
    }

    private static void SetMaterialProperty<T>(Entity entity, string name, T value,
                                                delegate* unmanaged<ulong, ulong, NativeString, T, byte> setter)
        where T : unmanaged
    {
        ValidatePropertyName(name);
        using NativeString nativeName = name;
        if (setter(entity.Id.High, entity.Id.Low, nativeName, value) == 0)
            throw new InvalidOperationException($"Material property '{name}' could not be changed.");
    }

    private static void ValidatePropertyName(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (System.Text.Encoding.UTF8.GetByteCount(name) > 128)
            throw new ArgumentOutOfRangeException(nameof(name), "Material property names support at most 128 UTF-8 bytes.");
    }

    private static InvalidOperationException Unavailable<T>(NativeRenderingComponent component, T property) =>
        new($"The {component} rendering property {property} is unavailable.");

    private static InvalidOperationException Unchanged<T>(NativeRenderingComponent component, T property) =>
        new($"The {component} rendering property {property} could not be changed.");
}
