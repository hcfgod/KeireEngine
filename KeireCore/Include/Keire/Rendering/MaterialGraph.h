#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/RenderingAssets.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Keire
{
    enum class MaterialGraphOutput : std::uint8_t
    {
        Surface,
        Transparent,
        Decal,
        Unlit
    };

    enum class MaterialGraphValueType : std::uint8_t
    {
        Scalar,
        Vector2,
        Vector3,
        Vector4,
        Color,
        Texture2D
    };

    enum class MaterialGraphPinDirection : std::uint8_t
    {
        Input,
        Output
    };

    enum class MaterialGraphNodeKind : std::uint8_t
    {
        Master,
        Parameter,
        Constant,
        TextureSample,
        UV,
        UVTransform,
        NormalMap,
        DetailNormal,
        Parallax,
        Add,
        Multiply,
        Lerp,
        OneMinus,
        Clamp,
        Keyword,
        StaticSwitch,
        Custom,
        Subtract,
        Divide,
        Power,
        Minimum,
        Maximum,
        Absolute,
        Floor,
        Ceiling,
        Fraction,
        Sine,
        Cosine,
        Normalize,
        Length,
        Dot,
        Remap,
        SmoothStep,
        Step,
        Fresnel,
        VertexColor,
        WorldPosition,
        WorldNormal,
        ViewDirection,
        RotateUV,
        SimpleNoise,
        Desaturate,
        Posterize
    };

    enum class MaterialGraphDiagnosticSeverity : std::uint8_t
    {
        Info,
        Warning,
        Error
    };

    enum class MaterialGraphPreviewMesh : std::uint8_t
    {
        Sphere,
        Plane,
        Cube,
        Custom
    };

    using MaterialGraphValue = std::variant<float, Vector2, Vector3, Vector4, Color, AssetId>;

    struct MaterialGraphPin
    {
        AssetId Id;
        std::string Name;
        MaterialGraphValueType Type = MaterialGraphValueType::Scalar;
        MaterialGraphPinDirection Direction = MaterialGraphPinDirection::Input;
        MaterialGraphValue DefaultValue = 0.0F;

        bool operator==(const MaterialGraphPin&) const = default;
    };

    struct MaterialGraphNode
    {
        AssetId Id;
        MaterialGraphNodeKind Kind = MaterialGraphNodeKind::Constant;
        std::string Name;
        Vector2 EditorPosition;
        MaterialGraphValueType ValueType = MaterialGraphValueType::Scalar;
        MaterialGraphValue Value = 0.0F;
        ShaderTextureSemantic TextureSemantic = ShaderTextureSemantic::Generic;
        std::string Symbol;
        std::filesystem::path Include;
        std::string Function;
        std::vector<MaterialGraphPin> Pins;

        bool operator==(const MaterialGraphNode&) const = default;
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

        bool operator==(const MaterialGraphConnection&) const = default;
    };

    struct MaterialGraphKeyword
    {
        std::string Name;
        std::vector<std::string> Options;
        std::string DefaultOption;
        bool Exposed = true;

        bool operator==(const MaterialGraphKeyword&) const = default;
    };

    struct MaterialGraphDefinition
    {
        std::uint32_t SchemaVersion = 1;
        MaterialGraphOutput Output = MaterialGraphOutput::Surface;
        std::vector<MaterialGraphNode> Nodes;
        std::vector<MaterialGraphConnection> Connections;
        std::vector<MaterialGraphKeyword> Keywords;
        std::vector<std::filesystem::path> IncludeRoots{"Assets"};

        bool operator==(const MaterialGraphDefinition&) const = default;
    };

    struct MaterialGraphDiagnostic
    {
        MaterialGraphDiagnosticSeverity Severity = MaterialGraphDiagnosticSeverity::Error;
        std::string Code;
        std::string Message;
        AssetId Node;
        AssetId Pin;
        std::size_t GeneratedLine = 0;

        bool operator==(const MaterialGraphDiagnostic&) const = default;
    };

    struct MaterialGraphShaderVariant
    {
        std::vector<std::string> Keywords;
        std::string StableSuffix;
        std::filesystem::path GeneratedSource;
        std::string Hlsl;
        std::string Manifest;

        bool operator==(const MaterialGraphShaderVariant&) const = default;
    };

    struct MaterialGraphStatistics
    {
        std::size_t NodeCount = 0;
        std::size_t ConnectionCount = 0;
        std::size_t ReachableNodeCount = 0;
        std::size_t UnusedNodeCount = 0;
        std::size_t TextureSampleCount = 0;
        std::size_t EstimatedAluInstructions = 0;
        std::size_t VariantCount = 0;

        bool operator==(const MaterialGraphStatistics&) const = default;
    };

    struct MaterialGraphCompilation
    {
        std::vector<MaterialGraphShaderVariant> Variants;
        std::vector<ShaderPropertyDefinition> Properties;
        std::vector<std::filesystem::path> Dependencies;
        std::vector<MaterialGraphDiagnostic> Diagnostics;
        MaterialGraphStatistics Statistics;

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    struct MaterialGraphCompileOptions
    {
        std::filesystem::path GeneratedSource = "Assets/Generated/MaterialGraph.hlsl";
        std::size_t MaximumVariants = 64;
        std::size_t MaximumNodes = 1024;
        std::size_t MaximumConnections = 4096;
        std::size_t MaximumCustomIncludes = 64;
        std::function<std::optional<std::string>(const std::filesystem::path&)> ReadInclude;
    };

    struct MaterialGraphInstanceDefinition
    {
        std::uint32_t SchemaVersion = 1;
        AssetId Parent;
        std::map<std::string, MaterialPropertyValue, std::less<>> Properties;
        std::map<std::string, std::string, std::less<>> KeywordOverrides;

        bool operator==(const MaterialGraphInstanceDefinition&) const = default;
    };

    struct ResolvedMaterialGraphInstance
    {
        std::map<std::string, MaterialPropertyValue, std::less<>> Properties;
        std::vector<std::string> Keywords;
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

    class KEIRE_API MaterialGraphInstanceAsset final : public Asset
    {
      public:
        explicit MaterialGraphInstanceAsset(MaterialGraphInstanceDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d4749ULL, 0x4e53540000000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const MaterialGraphInstanceDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<MaterialGraphInstanceAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const MaterialGraphInstanceDefinition& definition);
        [[nodiscard]] static MaterialGraphInstanceDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const MaterialGraphInstanceDefinition& definition);
        [[nodiscard]] static Ref<MaterialGraphInstanceAsset> Error();

      private:
        MaterialGraphInstanceDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API MaterialGraphDefinition
    CreateDefaultMaterialGraph(MaterialGraphOutput output = MaterialGraphOutput::Surface);
    [[nodiscard]] KEIRE_API MaterialGraphNode CreateMaterialGraphNode(
        MaterialGraphNodeKind kind, MaterialGraphValueType valueType = MaterialGraphValueType::Scalar);
    KEIRE_API void ValidateMaterialGraph(const MaterialGraphDefinition& definition);
    [[nodiscard]] KEIRE_API MaterialGraphCompilation
    CompileMaterialGraph(const MaterialGraphDefinition& definition, const MaterialGraphCompileOptions& options = {});
    [[nodiscard]] KEIRE_API std::vector<std::vector<std::string>>
    EnumerateMaterialGraphKeywordVariants(std::span<const MaterialGraphKeyword> keywords,
                                          std::size_t maximumVariants = 64);
    [[nodiscard]] KEIRE_API ResolvedMaterialGraphInstance ResolveMaterialGraphInstance(
        const MaterialGraphDefinition& graph, std::span<const MaterialGraphInstanceDefinition> ancestry);
    [[nodiscard]] KEIRE_API MaterialAssetDefinition
    BakeMaterialGraphInstance(const MaterialGraphDefinition& graph, const ResolvedMaterialGraphInstance& instance,
                              const std::function<AssetId(std::span<const std::string>)>& resolveShaderVariant);
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialGraphAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialGraphAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateMaterialGraphInstanceAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateMaterialGraphInstanceAssetDecoder();
} // namespace Keire
