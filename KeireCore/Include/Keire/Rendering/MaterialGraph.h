#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/RenderingAssets.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t MaterialGraphSourceSchemaVersion = 1;

    struct MaterialGraphPropertyBinding
    {
        AssetId Property;
        std::string Name;
        MaterialPropertyValue Value = 0.0F;

        bool operator==(const MaterialGraphPropertyBinding&) const = default;
    };

    struct MaterialGraphDefinition
    {
        std::uint32_t SchemaVersion = MaterialGraphSourceSchemaVersion;
        MaterialShaderReference Shader;
        MaterialSurfaceState Surface;
        bool ContributeEmissionToGI = true;
        float EmissiveGIIntensity = 1.0F;
        std::vector<MaterialGraphPropertyBinding> Properties;

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

        bool operator==(const MaterialGraphDiagnostic&) const = default;
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

    KEIRE_API void ValidateMaterialGraph(const MaterialGraphDefinition& definition);
    [[nodiscard]] KEIRE_API std::vector<MaterialGraphDiagnostic>
    ValidateMaterialGraphAgainstInterface(const MaterialGraphDefinition& definition,
                                          const ShaderInterfaceDefinition& interfaceDefinition);
    [[nodiscard]] KEIRE_API MaterialAssetDefinition
    BakeMaterialGraph(const MaterialGraphDefinition& definition,
                      const std::function<AssetId(const MaterialShaderReference&)>& resolveShader);
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialGraphAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialGraphAssetDecoder();
} // namespace Keire
