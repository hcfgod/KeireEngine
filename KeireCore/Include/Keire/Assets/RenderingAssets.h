#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Math/Math.h"

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t ShaderAssetSchemaVersion = 3;
    inline constexpr std::size_t ShaderAssetPassRoleHardLimit = 32;

    enum class ShaderBinaryFormat : std::uint8_t
    {
        Dxil,
        SpirV,
        Msl
    };

    enum class ShaderPrimitiveTopology : std::uint8_t
    {
        TriangleList,
        LineList,
        PointList
    };

    enum class ShaderCullMode : std::uint8_t
    {
        None,
        Front,
        Back
    };

    enum class ShaderOcclusionSupport : std::uint8_t
    {
        None = 0,
        ConservativeBounds = 1U << 0U,
        DepthOnlyGeometryMatch = 1U << 1U
    };

    [[nodiscard]] constexpr ShaderOcclusionSupport operator|(const ShaderOcclusionSupport left,
                                                             const ShaderOcclusionSupport right) noexcept
    {
        return static_cast<ShaderOcclusionSupport>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr bool HasShaderOcclusionSupport(const ShaderOcclusionSupport value,
                                                           const ShaderOcclusionSupport flag) noexcept
    {
        return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0U;
    }

    enum class ShaderPropertyType : std::uint8_t
    {
        Scalar,
        Vector2,
        Vector3,
        Vector4,
        Color,
        Texture2D
    };

    enum class ShaderTextureSemantic : std::uint8_t
    {
        Generic,
        BaseColor,
        Normal,
        MetallicRoughness,
        Occlusion,
        Emissive,
        Metallic,
        Roughness,
        Specular
    };

    struct ShaderPropertyDefinition
    {
        std::string Name;
        ShaderPropertyType Type = ShaderPropertyType::Scalar;
        Vector4 DefaultValue;
        AssetId DefaultTexture;
        std::string DisplayName;
        std::string Category;
        std::optional<float> Minimum;
        std::optional<float> Maximum;
        std::optional<float> Step;
        ShaderTextureSemantic TextureSemantic = ShaderTextureSemantic::Generic;
        /// Stable authoring identity. Names may change without invalidating material bindings when this is populated.
        AssetId Id;

        bool operator==(const ShaderPropertyDefinition&) const = default;
    };

    enum class ShaderInterfaceDomain : std::uint8_t
    {
        Surface,
        Vfx,
        Fullscreen,
        CustomGraphicsPass
    };

    struct ShaderInterfaceDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::uint32_t AbiVersion = 1;
        ShaderInterfaceDomain Domain = ShaderInterfaceDomain::Surface;
        std::vector<ShaderPropertyDefinition> Properties;
        std::vector<std::string> Keywords;

        bool operator==(const ShaderInterfaceDefinition&) const = default;
    };

    enum class MaterialShaderSourceKind : std::uint8_t
    {
        Builtin,
        ShaderAsset,
        ShaderGraph
    };

    struct MaterialShaderReference
    {
        MaterialShaderSourceKind Kind = MaterialShaderSourceKind::ShaderAsset;
        AssetId Asset;
        std::string Target = "default";
        std::map<std::string, std::string, std::less<>> Keywords;

        bool operator==(const MaterialShaderReference&) const = default;
    };

    struct ShaderVariant
    {
        ShaderBinaryFormat Format = ShaderBinaryFormat::SpirV;
        std::vector<std::byte> Vertex;
        std::vector<std::byte> Fragment;
        /// Exact cooked material/program pass role. Historical shader assets migrate to "primary".
        std::string PassRole = "primary";
    };

    struct ShaderAssetDefinition
    {
        std::uint32_t SchemaVersion = ShaderAssetSchemaVersion;
        std::filesystem::path Source;
        std::string VertexEntry = "VSMain";
        std::string FragmentEntry = "PSMain";
        std::uint8_t VertexLayoutVersion = 1;
        ShaderPrimitiveTopology Topology = ShaderPrimitiveTopology::TriangleList;
        ShaderCullMode Culling = ShaderCullMode::Back;
        bool DepthTest = true;
        bool DepthWrite = true;
        bool Blend = false;
        bool ReceivesShadows = false;
        bool UsesForwardPlus = false;
        bool UsesInstancing = false;
        std::vector<ShaderPropertyDefinition> Properties;
        std::vector<AssetSourceDependency> Dependencies;
        std::vector<ShaderVariant> Variants;
        bool UsesImageBasedLighting = false;
        std::uint8_t SpatialLightingAbiVersion = 0;
        bool UsesVertexMaterialParameters = false;
        /// Additional graph-authored combined texture/sampler slots and fragment read-only buffers.
        std::uint8_t UserResourceSlots = 0;
        std::uint8_t UserReadOnlyBuffers = 0;
        /// Version 2 reads an explicit instance-buffer base from vertex uniform b2/space1. Zero is legacy addressing.
        std::uint8_t InstanceAddressingAbiVersion = 0;
        /// Missing metadata fails closed; only explicitly compatible shaders may enter GPU occlusion paths.
        ShaderOcclusionSupport OcclusionSupport = ShaderOcclusionSupport::None;
        /// Maximum authored vertex displacement in world units. Missing legacy metadata is never occlusion-safe.
        std::optional<float> MaximumWorldPositionDisplacementRadius = 0.0F;
    };

    class KEIRE_API ShaderAsset final : public Asset
    {
      public:
        explicit ShaderAsset(ShaderAssetDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245534841ULL, 0x4445520000000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const ShaderAssetDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] const ShaderVariant* Variant(ShaderBinaryFormat format) const noexcept;
        [[nodiscard]] const ShaderVariant* Variant(ShaderBinaryFormat format, std::string_view passRole) const noexcept;

        [[nodiscard]] static Ref<ShaderAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const ShaderAssetDefinition& definition);
        [[nodiscard]] static ShaderAssetDefinition DecodeManifest(std::span<const std::byte> bytes);
        [[nodiscard]] static Ref<ShaderAsset> Error();

      private:
        ShaderAssetDefinition m_Definition;
    };

    using MaterialPropertyValue = std::variant<float, Vector2, Vector3, Vector4, Color, AssetId>;

    enum class MaterialAlphaMode : std::uint8_t
    {
        Opaque,
        Mask,
        Blend,
        Additive,
        Modulate,
        AlphaComposite,
        AlphaHoldout
    };

    [[nodiscard]] constexpr bool IsTransparentMaterial(const MaterialAlphaMode mode) noexcept
    {
        return mode >= MaterialAlphaMode::Blend && mode <= MaterialAlphaMode::AlphaHoldout;
    }

    struct MaterialSurfaceState
    {
        MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
        float AlphaCutoff = 0.5F;
        bool DoubleSided = false;

        auto operator<=>(const MaterialSurfaceState&) const noexcept = default;
    };

    struct MaterialAssetDefinition
    {
        std::uint32_t SchemaVersion = 3;
        AssetId Shader;
        MaterialSurfaceState Surface;
        bool ContributeEmissionToGI = true;
        float EmissiveGIIntensity = 1.0F;
        std::map<std::string, MaterialPropertyValue, std::less<>> Properties;

        void SetTexture(std::string name, AssetId texture);
        [[nodiscard]] bool RemoveTexture(std::string_view name);
        [[nodiscard]] std::optional<AssetId> Texture(std::string_view name) const;
    };

    /// Editable material source. Import resolves graph targets to an immutable runtime MaterialAssetDefinition.
    inline constexpr std::string_view LegacyMaterialAssetSourceExtension = ".keiremateriallegacy";

    struct MaterialAuthoringDefinition
    {
        std::uint32_t SchemaVersion = 4;
        MaterialShaderReference Shader;
        MaterialSurfaceState Surface;
        bool ContributeEmissionToGI = true;
        float EmissiveGIIntensity = 1.0F;
        std::map<std::string, MaterialPropertyValue, std::less<>> Properties;

        bool operator==(const MaterialAuthoringDefinition&) const = default;
    };

    class KEIRE_API MaterialAsset final : public Asset
    {
      public:
        explicit MaterialAsset(MaterialAssetDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d4154ULL, 0x455249414c000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const MaterialAssetDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<MaterialAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const MaterialAssetDefinition& definition);
        [[nodiscard]] static MaterialAssetDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const MaterialAssetDefinition& definition);
        [[nodiscard]] static MaterialAuthoringDefinition DecodeAuthoringSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte>
        EncodeAuthoringSource(const MaterialAuthoringDefinition& definition);
        [[nodiscard]] static Ref<MaterialAsset> Error();

      private:
        MaterialAssetDefinition m_Definition;
    };

    enum class BuiltinMesh : std::uint8_t
    {
        Error,
        Cube,
        Sphere,
        Capsule,
        Cylinder,
        Cone,
        Plane,
        Quad,
        Torus
    };

    struct BuiltinMeshDescriptor
    {
        BuiltinMesh Mesh = BuiltinMesh::Error;
        AssetId Id;
        std::string_view Name;
        std::string_view CollisionExpectation;
        bool Closed = true;
    };

    /// Stable built-in content catalog. All meshes use metres, a centered origin, and the engine's Y-up convention.
    [[nodiscard]] KEIRE_API std::span<const BuiltinMeshDescriptor> BuiltinMeshCatalog() noexcept;

    struct MeshVertex
    {
        Vector3 Position;
        Vector3 Normal{0.0F, 1.0F, 0.0F};
        Vector2 UV0;
        Color VertexColor;
        Vector4 Tangent{1.0F, 0.0F, 0.0F, 1.0F};
        Vector2 UV1;
    };

    struct MeshBounds
    {
        Vector3 Minimum;
        Vector3 Maximum;

        auto operator<=>(const MeshBounds&) const noexcept = default;
    };

    struct MeshSubmesh
    {
        std::uint32_t FirstIndex = 0;
        std::uint32_t IndexCount = 0;
        std::uint32_t MaterialSlot = 0;
        MeshBounds Bounds;
        ShaderPrimitiveTopology Topology = ShaderPrimitiveTopology::TriangleList;

        auto operator<=>(const MeshSubmesh&) const noexcept = default;
    };

    struct MeshMaterialSlot
    {
        std::string Name;
        AssetId DefaultMaterial;

        auto operator<=>(const MeshMaterialSlot&) const noexcept = default;
    };

    struct MeshLod
    {
        float MinimumScreenHeight = 0.0F;
        std::uint32_t FirstSubmesh = 0;
        std::uint32_t SubmeshCount = 0;
        MeshBounds Bounds;

        auto operator<=>(const MeshLod&) const noexcept = default;
    };

    class KEIRE_API MeshAsset final : public Asset
    {
      public:
        explicit MeshAsset(BuiltinMesh mesh = BuiltinMesh::Error);
        MeshAsset(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices, MeshBounds bounds);
        MeshAsset(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices,
                  std::vector<MeshSubmesh> submeshes, std::vector<MeshMaterialSlot> materialSlots,
                  std::vector<MeshLod> lods, MeshBounds bounds);

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d4553ULL, 0x4841535345540001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] BuiltinMesh Mesh() const noexcept { return m_Mesh; }
        [[nodiscard]] std::span<const MeshVertex> Vertices() const noexcept { return m_Vertices; }
        [[nodiscard]] std::span<const std::uint32_t> Indices() const noexcept { return m_Indices; }
        [[nodiscard]] const MeshBounds& Bounds() const noexcept { return m_Bounds; }
        [[nodiscard]] std::span<const MeshSubmesh> Submeshes() const noexcept { return m_Submeshes; }
        [[nodiscard]] std::span<const MeshMaterialSlot> MaterialSlots() const noexcept { return m_MaterialSlots; }
        [[nodiscard]] std::span<const MeshLod> Lods() const noexcept { return m_Lods; }
        [[nodiscard]] static constexpr AssetId CubeId() noexcept
        {
            return AssetId(0x4b45495245435542ULL, 0x454d455348000001ULL);
        }
        [[nodiscard]] static constexpr AssetId ErrorId() noexcept
        {
            return AssetId(0x4b45495245455252ULL, 0x4f524d4553480001ULL);
        }
        [[nodiscard]] static constexpr AssetId SphereId() noexcept
        {
            return AssetId(0x4b45495245535048ULL, 0x4552454d45534801ULL);
        }
        [[nodiscard]] static constexpr AssetId CapsuleId() noexcept
        {
            return AssetId(0x4b45495245434150ULL, 0x53554c454d455301ULL);
        }
        [[nodiscard]] static constexpr AssetId CylinderId() noexcept
        {
            return AssetId(0x4b4549524543594cULL, 0x494e4445524d5301ULL);
        }
        [[nodiscard]] static constexpr AssetId ConeId() noexcept
        {
            return AssetId(0x4b45495245434f4eULL, 0x454d455348000001ULL);
        }
        [[nodiscard]] static constexpr AssetId PlaneId() noexcept
        {
            return AssetId(0x4b45495245504c41ULL, 0x4e454d4553480001ULL);
        }
        [[nodiscard]] static constexpr AssetId QuadId() noexcept
        {
            return AssetId(0x4b45495245515541ULL, 0x444d455348000001ULL);
        }
        [[nodiscard]] static constexpr AssetId TorusId() noexcept
        {
            return AssetId(0x4b45495245544f52ULL, 0x55534d4553480001ULL);
        }
        [[nodiscard]] static AssetId BuiltinId(BuiltinMesh mesh) noexcept;
        [[nodiscard]] static std::optional<BuiltinMesh> BuiltinKind(AssetId id) noexcept;
        [[nodiscard]] static bool IsBuiltin(AssetId id) noexcept { return BuiltinKind(id).has_value(); }
        [[nodiscard]] static Ref<MeshAsset> Builtin(BuiltinMesh mesh);
        [[nodiscard]] static Ref<MeshAsset> ResolveBuiltin(AssetId id);
        [[nodiscard]] static Ref<MeshAsset> Cube();
        [[nodiscard]] static Ref<MeshAsset> Sphere();
        [[nodiscard]] static Ref<MeshAsset> Capsule();
        [[nodiscard]] static Ref<MeshAsset> Cylinder();
        [[nodiscard]] static Ref<MeshAsset> Cone();
        [[nodiscard]] static Ref<MeshAsset> Plane();
        [[nodiscard]] static Ref<MeshAsset> Quad();
        [[nodiscard]] static Ref<MeshAsset> Torus();
        [[nodiscard]] static Ref<MeshAsset> Error();
        [[nodiscard]] static Ref<MeshAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(std::span<const MeshVertex> vertices,
                                                           std::span<const std::uint32_t> indices);
        [[nodiscard]] static std::vector<std::byte> Encode(std::span<const MeshVertex> vertices,
                                                           std::span<const std::uint32_t> indices,
                                                           std::span<const MeshSubmesh> submeshes,
                                                           std::span<const MeshMaterialSlot> materialSlots,
                                                           std::span<const MeshLod> lods);

      private:
        BuiltinMesh m_Mesh;
        std::vector<MeshVertex> m_Vertices;
        std::vector<std::uint32_t> m_Indices;
        std::vector<MeshSubmesh> m_Submeshes;
        std::vector<MeshMaterialSlot> m_MaterialSlots;
        std::vector<MeshLod> m_Lods;
        MeshBounds m_Bounds;
    };

    enum class TextureSemantic : std::uint8_t
    {
        Color,
        Data,
        Normal,
        Environment
    };

    enum class TextureColorSpace : std::uint8_t
    {
        Linear,
        Srgb
    };

    enum class TextureMipPolicy : std::uint8_t
    {
        None,
        Generate
    };

    enum class TextureEnvironmentLayout : std::uint8_t
    {
        Auto,
        Equirectangular,
        HorizontalCross,
        VerticalCross,
        HorizontalStrip,
        VerticalStrip
    };

    enum class TextureFilter : std::uint8_t
    {
        Nearest,
        Linear
    };

    enum class TextureAddressMode : std::uint8_t
    {
        Repeat,
        Clamp,
        Mirror
    };

    struct SamplerDescription
    {
        TextureFilter Minimum = TextureFilter::Linear;
        TextureFilter Magnification = TextureFilter::Linear;
        TextureFilter Mip = TextureFilter::Linear;
        TextureAddressMode AddressU = TextureAddressMode::Repeat;
        TextureAddressMode AddressV = TextureAddressMode::Repeat;
        TextureAddressMode AddressW = TextureAddressMode::Repeat;
        std::uint8_t Anisotropy = 1;

        auto operator<=>(const SamplerDescription&) const = default;
    };

    struct TextureImportSettings
    {
        TextureSemantic Semantic = TextureSemantic::Color;
        TextureColorSpace ColorSpace = TextureColorSpace::Srgb;
        TextureMipPolicy Mips = TextureMipPolicy::Generate;
        std::uint32_t MaximumSize = 4096;
        bool FlipGreen = false;
        SamplerDescription Sampler;
        TextureEnvironmentLayout EnvironmentLayout = TextureEnvironmentLayout::Auto;
        bool HighDynamicRange = false;

        auto operator<=>(const TextureImportSettings&) const = default;
    };

    struct TextureMipLevel
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::vector<std::byte> Pixels;
    };

    class KEIRE_API Texture2DAsset final : public Asset
    {
      public:
        Texture2DAsset(TextureImportSettings settings, std::vector<TextureMipLevel> mips);

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245544558ULL, 0x5455524532440001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const TextureImportSettings& Settings() const noexcept { return m_Settings; }
        [[nodiscard]] std::span<const TextureMipLevel> Mips() const noexcept { return m_Mips; }
        [[nodiscard]] std::uint32_t Width() const noexcept { return m_Mips.empty() ? 0 : m_Mips.front().Width; }
        [[nodiscard]] std::uint32_t Height() const noexcept { return m_Mips.empty() ? 0 : m_Mips.front().Height; }

        [[nodiscard]] static Ref<Texture2DAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const TextureImportSettings& settings,
                                                           std::span<const TextureMipLevel> mips);
        [[nodiscard]] static Ref<Texture2DAsset> Checkerboard();

      private:
        TextureImportSettings m_Settings;
        std::vector<TextureMipLevel> m_Mips;
    };

    struct ShaderImporterSpecification
    {
        std::filesystem::path Compiler;
        std::chrono::milliseconds Timeout = std::chrono::seconds(30);
        std::size_t MaximumOutputBytes = std::size_t{64} * 1024U * 1024U;
        std::vector<ShaderBinaryFormat> Formats{ShaderBinaryFormat::Dxil, ShaderBinaryFormat::SpirV,
                                                ShaderBinaryFormat::Msl};
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration
    CreateShaderAssetImporter(ShaderImporterSpecification specification = {});
    KEIRE_API void ValidateMaterialAgainstShader(const MaterialAssetDefinition& material,
                                                 const ShaderAssetDefinition& shader);
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMeshAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateTexture2DAssetImporter(TextureImportSettings settings = {});
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateShaderAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMeshAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateTexture2DAssetDecoder();
} // namespace Keire
