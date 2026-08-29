#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t MaterialGraphSourceSchemaVersion = 5;
    inline constexpr std::uint32_t MaterialInstanceSourceSchemaVersion = 2;

    struct MaterialGraphPropertyBinding
    {
        AssetId Property;
        std::string Name;
        ShaderPropertyType Type = ShaderPropertyType::Scalar;
        AssetId Pin;
        MaterialPropertyValue Value = 0.0F;

        bool operator==(const MaterialGraphPropertyBinding&) const = default;
    };

    struct MaterialGraphValueNode
    {
        AssetId Id;
        std::string Name;
        Vector2 EditorPosition;
        ShaderPropertyType Type = ShaderPropertyType::Scalar;
        AssetId OutputPin;
        MaterialPropertyValue Value = 0.0F;

        bool operator==(const MaterialGraphValueNode&) const = default;
    };

    struct MaterialGraphEndpoint
    {
        AssetId Node;
        AssetId Pin;

        bool operator==(const MaterialGraphEndpoint&) const = default;
    };

    struct MaterialGraphConnection
    {
        AssetId Id;
        MaterialGraphEndpoint Output;
        MaterialGraphEndpoint Input;
        /// Editor-only cable routing knots in graph space. They do not affect material evaluation.
        std::vector<Vector2> RoutingPoints;

        bool operator==(const MaterialGraphConnection&) const = default;
    };

    struct MaterialGraphDefinition
    {
        std::uint32_t SchemaVersion = MaterialGraphSourceSchemaVersion;
        MaterialShaderReference Shader;
        MaterialSurfaceState Surface;
        bool ContributeEmissionToGI = true;
        float EmissiveGIIntensity = 1.0F;
        AssetId OutputNode;
        Vector2 OutputPosition{520.0F, 120.0F};
        std::vector<MaterialGraphPropertyBinding> Properties;
        std::vector<MaterialGraphValueNode> Nodes;
        std::vector<MaterialGraphConnection> Connections;
        /// Decode-only legacy compatibility graph. Schema v5 writes it only as `legacySurfaceGraph` when executable
        /// v1-v4 expressions still require migration. New Material Graph authoring has one shader authority.
        ShaderGraphDefinition SurfaceGraph;
        GraphAuthoringMetadata Authoring;

        bool operator==(const MaterialGraphDefinition&) const = default;
    };

    enum class MaterialGraphDiagnosticSeverity : std::uint8_t
    {
        Info,
        Warning,
        Error
    };

    struct MaterialGraphDiagnostic
    {
        MaterialGraphDiagnosticSeverity Severity = MaterialGraphDiagnosticSeverity::Error;
        std::string Code;
        std::string Message;
        AssetId Property;
        AssetId Node;
        AssetId Pin;

        bool operator==(const MaterialGraphDiagnostic&) const = default;
    };

    struct MaterialInstanceDefinition
    {
        std::uint32_t SchemaVersion = MaterialInstanceSourceSchemaVersion;
        AssetId Parent;
        std::map<std::string, MaterialPropertyValue, std::less<>> Properties;
        std::map<std::string, std::string, std::less<>> KeywordOverrides;
        std::optional<MaterialSurfaceState> Surface;
        std::optional<bool> ContributeEmissionToGI;
        std::optional<float> EmissiveGIIntensity;

        bool operator==(const MaterialInstanceDefinition&) const = default;
    };

    class KEIRE_API MaterialGraphAsset final : public Asset
    {
      public:
        explicit MaterialGraphAsset(MaterialGraphDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d4752ULL, 0x4150480000000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const MaterialGraphDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<MaterialGraphAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const MaterialGraphDefinition& definition);
        [[nodiscard]] static MaterialGraphDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const MaterialGraphDefinition& definition);
        [[nodiscard]] static Ref<MaterialGraphAsset> Error();

      private:
        MaterialGraphDefinition m_Definition;
    };

    class KEIRE_API MaterialInstanceAsset final : public Asset
    {
      public:
        explicit MaterialInstanceAsset(MaterialInstanceDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d494eULL, 0x5354414e43450001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const MaterialInstanceDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<MaterialInstanceAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const MaterialInstanceDefinition& definition);
        [[nodiscard]] static MaterialInstanceDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const MaterialInstanceDefinition& definition);
        [[nodiscard]] static Ref<MaterialInstanceAsset> Error();

      private:
        MaterialInstanceDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API MaterialPropertyValue DefaultMaterialGraphValue(const ShaderPropertyDefinition& property);
    [[nodiscard]] KEIRE_API MaterialGraphDefinition
    CreateMaterialGraph(MaterialShaderReference shader, const ShaderInterfaceDefinition& interfaceDefinition);
    [[nodiscard]] KEIRE_API ShaderGraphDefinition
    CreateMaterialSurfaceGraph(const ShaderGraphDefinition& shaderTemplate);
    KEIRE_API void SynchronizeMaterialGraphInterface(MaterialGraphDefinition& definition,
                                                     const ShaderInterfaceDefinition& interfaceDefinition);
    [[nodiscard]] KEIRE_API MaterialGraphValueNode CreateMaterialGraphValueNode(ShaderPropertyType type,
                                                                                MaterialPropertyValue value,
                                                                                Vector2 position = {120.0F, 120.0F});
    [[nodiscard]] KEIRE_API std::map<std::string, MaterialPropertyValue, std::less<>>
    EvaluateMaterialGraphProperties(const MaterialGraphDefinition& definition);
    /// Legacy composition entry point used only for Material Graph assets that already contain surface expressions.
    [[nodiscard]] KEIRE_API ShaderGraphDefinition
    ComposeMaterialGraphShader(const MaterialGraphDefinition& definition, const ShaderGraphDefinition& shaderTemplate);
    KEIRE_API void ValidateMaterialGraph(const MaterialGraphDefinition& definition);
    [[nodiscard]] KEIRE_API std::vector<MaterialGraphDiagnostic>
    ValidateMaterialGraphAgainstInterface(const MaterialGraphDefinition& definition,
                                          const ShaderInterfaceDefinition& interfaceDefinition);
    [[nodiscard]] KEIRE_API MaterialAssetDefinition
    BakeMaterialGraph(const MaterialGraphDefinition& definition,
                      const std::function<AssetId(const MaterialShaderReference&)>& resolveShader);
    KEIRE_API void ValidateMaterialInstance(const MaterialInstanceDefinition& definition);
    [[nodiscard]] KEIRE_API MaterialAssetDefinition BakeMaterialInstance(const MaterialAssetDefinition& parent,
                                                                         const MaterialInstanceDefinition& instance);
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialGraphAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialGraphAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialInstanceAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialInstanceAssetDecoder();
} // namespace Keire
