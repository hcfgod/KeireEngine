#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t GraphFunctionSourceSchemaVersion = 1;
    inline constexpr std::uint32_t MaterialParameterCollectionSourceSchemaVersion = 1;
    inline constexpr std::string_view MaterialFunctionAssetSourceExtension = ".keirematerialfunction";
    inline constexpr std::string_view ShaderFunctionAssetSourceExtension = ".keireshaderfunction";
    inline constexpr std::string_view MaterialLayerAssetSourceExtension = ".keiremateriallayer";
    inline constexpr std::string_view MaterialLayerBlendAssetSourceExtension = ".keirematerialblend";
    inline constexpr std::string_view MaterialParameterCollectionAssetSourceExtension = ".keirematerialcollection";

    struct GraphFunctionDefinition
    {
        std::uint32_t SchemaVersion = GraphFunctionSourceSchemaVersion;
        std::string Description;
        std::string Category = "Project";
        std::int32_t SortPriority = 0;
        bool ExposeToLibrary = true;
        ShaderGraphDefinition Body;

        bool operator==(const GraphFunctionDefinition&) const = default;
    };

    struct MaterialParameterCollectionParameter
    {
        AssetId Id;
        std::string Name;
        std::string DisplayName;
        std::string Description;
        std::string Category = "Global";
        std::int32_t SortPriority = 0;
        ShaderPropertyType Type = ShaderPropertyType::Scalar;
        MaterialPropertyValue DefaultValue = 0.0F;

        bool operator==(const MaterialParameterCollectionParameter&) const = default;
    };

    struct MaterialParameterCollectionDefinition
    {
        std::uint32_t SchemaVersion = MaterialParameterCollectionSourceSchemaVersion;
        std::vector<MaterialParameterCollectionParameter> Parameters;

        bool operator==(const MaterialParameterCollectionDefinition&) const = default;
    };

    class KEIRE_API MaterialFunctionAsset final : public Asset
    {
      public:
        explicit MaterialFunctionAsset(GraphFunctionDefinition definition = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d4655ULL, 0x4e4354494f4e0001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const GraphFunctionDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] static Ref<MaterialFunctionAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const GraphFunctionDefinition& definition);
        [[nodiscard]] static GraphFunctionDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const GraphFunctionDefinition& definition);
        [[nodiscard]] static Ref<MaterialFunctionAsset> Error();

      private:
        GraphFunctionDefinition m_Definition;
    };

    class KEIRE_API ShaderFunctionAsset final : public Asset
    {
      public:
        explicit ShaderFunctionAsset(GraphFunctionDefinition definition = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245534655ULL, 0x4e4354494f4e0001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const GraphFunctionDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] static Ref<ShaderFunctionAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const GraphFunctionDefinition& definition);
        [[nodiscard]] static GraphFunctionDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const GraphFunctionDefinition& definition);
        [[nodiscard]] static Ref<ShaderFunctionAsset> Error();

      private:
        GraphFunctionDefinition m_Definition;
    };

    class KEIRE_API MaterialLayerAsset final : public Asset
    {
      public:
        explicit MaterialLayerAsset(GraphFunctionDefinition definition = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d4c41ULL, 0x5945520000000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const GraphFunctionDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] static Ref<MaterialLayerAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const GraphFunctionDefinition& definition);
        [[nodiscard]] static GraphFunctionDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const GraphFunctionDefinition& definition);
        [[nodiscard]] static Ref<MaterialLayerAsset> Error();

      private:
        GraphFunctionDefinition m_Definition;
    };

    class KEIRE_API MaterialLayerBlendAsset final : public Asset
    {
      public:
        explicit MaterialLayerBlendAsset(GraphFunctionDefinition definition = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d4c42ULL, 0x4c454e4400000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const GraphFunctionDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] static Ref<MaterialLayerBlendAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const GraphFunctionDefinition& definition);
        [[nodiscard]] static GraphFunctionDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const GraphFunctionDefinition& definition);
        [[nodiscard]] static Ref<MaterialLayerBlendAsset> Error();

      private:
        GraphFunctionDefinition m_Definition;
    };

    class KEIRE_API MaterialParameterCollectionAsset final : public Asset
    {
      public:
        explicit MaterialParameterCollectionAsset(MaterialParameterCollectionDefinition definition = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d5043ULL, 0x4f4c4c4543540001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const MaterialParameterCollectionDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] static Ref<MaterialParameterCollectionAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const MaterialParameterCollectionDefinition& definition);
        [[nodiscard]] static MaterialParameterCollectionDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte>
        EncodeSource(const MaterialParameterCollectionDefinition& definition);
        [[nodiscard]] static Ref<MaterialParameterCollectionAsset> Error();

      private:
        MaterialParameterCollectionDefinition m_Definition;
    };

    class KEIRE_API DynamicMaterialInstance final : public RefCounted
    {
      public:
        explicit DynamicMaterialInstance(MaterialAssetDefinition parent);
        ~DynamicMaterialInstance() override;

        DynamicMaterialInstance(const DynamicMaterialInstance&) = delete;
        DynamicMaterialInstance& operator=(const DynamicMaterialInstance&) = delete;

        [[nodiscard]] MaterialAssetDefinition Snapshot() const;
        [[nodiscard]] std::uint64_t Revision() const noexcept;
        void SetProperty(std::string name, MaterialPropertyValue value);
        [[nodiscard]] bool ResetProperty(std::string_view name);
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API MaterialParameterCollectionState final : public RefCounted
    {
      public:
        explicit MaterialParameterCollectionState(MaterialParameterCollectionDefinition definition);
        ~MaterialParameterCollectionState() override;

        MaterialParameterCollectionState(const MaterialParameterCollectionState&) = delete;
        MaterialParameterCollectionState& operator=(const MaterialParameterCollectionState&) = delete;

        [[nodiscard]] MaterialParameterCollectionDefinition Definition() const;
        [[nodiscard]] std::map<AssetId, MaterialPropertyValue> Snapshot() const;
        [[nodiscard]] std::uint64_t Revision() const noexcept;
        void Set(AssetId parameter, MaterialPropertyValue value);
        [[nodiscard]] bool Reset(AssetId parameter);
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    [[nodiscard]] KEIRE_API GraphFunctionDefinition CreateDefaultGraphFunction(ShaderGraphPurpose purpose);
    KEIRE_API void ValidateGraphFunction(const GraphFunctionDefinition& definition, ShaderGraphPurpose expected);
    KEIRE_API void ValidateMaterialParameterCollection(const MaterialParameterCollectionDefinition& definition);

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialFunctionAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialFunctionAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateShaderFunctionAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateShaderFunctionAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialLayerAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialLayerAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialLayerBlendAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialLayerBlendAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialParameterCollectionAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialParameterCollectionAssetDecoder();
} // namespace Keire
