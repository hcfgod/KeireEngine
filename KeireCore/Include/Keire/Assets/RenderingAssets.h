#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Math/Math.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <variant>

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
        Color
    };

    struct ShaderPropertyDefinition
    {
        std::string Name;
        ShaderPropertyType Type = ShaderPropertyType::Scalar;
        Vector4 DefaultValue;
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

    using MaterialPropertyValue = std::variant<float, Vector2, Vector3, Vector4, Color>;

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

    class KEIRE_API MeshAsset final : public Asset
    {
      public:
        explicit MeshAsset(BuiltinMesh mesh = BuiltinMesh::Error) noexcept;

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d4553ULL, 0x4841535345540001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override { return sizeof(*this); }
        [[nodiscard]] BuiltinMesh Mesh() const noexcept { return m_Mesh; }
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

      private:
        BuiltinMesh m_Mesh;
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
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateShaderAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialAssetDecoder();
} // namespace Keire
