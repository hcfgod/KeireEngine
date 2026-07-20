#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Math/Math.h"

#include <chrono>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <variant>
#include <vector>

namespace Keire
{
    enum class ShaderBinaryFormat : std::uint8_t
    {
        Dxil,
        SpirV,
        Msl
    };

    enum class ShaderPrimitiveTopology : std::uint8_t
    {
        TriangleList,
        LineList
    };

    enum class ShaderCullMode : std::uint8_t
    {
        None,
        Front,
        Back
    };

    enum class ShaderPropertyType : std::uint8_t
    {
        Scalar,
        Vector2,
        Vector3,
        Vector4,
        Color,
        Texture2D
    };

    struct ShaderPropertyDefinition
    {
        std::string Name;
        ShaderPropertyType Type = ShaderPropertyType::Scalar;
        Vector4 DefaultValue;
        AssetId DefaultTexture;
    };

    struct ShaderVariant
    {
        ShaderBinaryFormat Format = ShaderBinaryFormat::SpirV;
        std::vector<std::byte> Vertex;
        std::vector<std::byte> Fragment;
    };

    struct ShaderAssetDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::filesystem::path Source;
        std::string VertexEntry = "VSMain";
        std::string FragmentEntry = "PSMain";
        ShaderPrimitiveTopology Topology = ShaderPrimitiveTopology::TriangleList;
        ShaderCullMode Culling = ShaderCullMode::Back;
        bool DepthTest = true;
        bool DepthWrite = true;
        bool Blend = false;
        std::vector<ShaderPropertyDefinition> Properties;
        std::vector<AssetSourceDependency> Dependencies;
        std::vector<ShaderVariant> Variants;
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

        [[nodiscard]] static Ref<ShaderAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const ShaderAssetDefinition& definition);
        [[nodiscard]] static Ref<ShaderAsset> Error();

      private:
        ShaderAssetDefinition m_Definition;
    };

    using MaterialPropertyValue = std::variant<float, Vector2, Vector3, Vector4, Color, AssetId>;

    struct MaterialAssetDefinition
    {
        std::uint32_t SchemaVersion = 1;
        AssetId Shader;
        std::map<std::string, MaterialPropertyValue, std::less<>> Properties;
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
        [[nodiscard]] static Ref<MaterialAsset> Error();

      private:
        MaterialAssetDefinition m_Definition;
    };

    enum class BuiltinMesh : std::uint8_t
    {
        Error,
        Cube
    };

    struct MeshVertex
    {
        Vector3 Position;
        Vector3 Normal{0.0F, 1.0F, 0.0F};
        Vector2 UV0;
        Color VertexColor;
    };

    struct MeshBounds
    {
        Vector3 Minimum;
        Vector3 Maximum;
    };

    class KEIRE_API MeshAsset final : public Asset
    {
      public:
        explicit MeshAsset(BuiltinMesh mesh = BuiltinMesh::Error);
        MeshAsset(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices, MeshBounds bounds);

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
        [[nodiscard]] static constexpr AssetId CubeId() noexcept
        {
            return AssetId(0x4b45495245435542ULL, 0x454d455348000001ULL);
        }
        [[nodiscard]] static constexpr AssetId ErrorId() noexcept
        {
            return AssetId(0x4b45495245455252ULL, 0x4f524d4553480001ULL);
        }
        [[nodiscard]] static Ref<MeshAsset> Cube();
        [[nodiscard]] static Ref<MeshAsset> Error();
        [[nodiscard]] static Ref<MeshAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(std::span<const MeshVertex> vertices,
                                                           std::span<const std::uint32_t> indices);

      private:
        BuiltinMesh m_Mesh;
        std::vector<MeshVertex> m_Vertices;
        std::vector<std::uint32_t> m_Indices;
        MeshBounds m_Bounds;
    };

    enum class TextureSemantic : std::uint8_t
    {
        Color,
        Data,
        Normal
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
        SamplerDescription Sampler;

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
        std::size_t MaximumOutputBytes = 64U * 1024U * 1024U;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration
    CreateShaderAssetImporter(ShaderImporterSpecification specification = {});
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMeshAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateTexture2DAssetImporter(TextureImportSettings settings = {});
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateShaderAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMeshAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateTexture2DAssetDecoder();
} // namespace Keire
