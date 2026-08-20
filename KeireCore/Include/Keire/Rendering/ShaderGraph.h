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
    /// Latest canonical source schema emitted by ShaderGraphAsset::EncodeSource.
    inline constexpr std::uint32_t ShaderGraphSourceSchemaVersion = 3;
    /// Version of the deterministic HLSL generator contract embedded in every generated shader manifest.
    inline constexpr std::uint32_t ShaderGraphGeneratedShaderVersion = 3;
    /// Renderer-facing vertex input and interpolator contract required by generated Shader Graph shaders.
    inline constexpr std::uint32_t ShaderGraphVertexLayoutVersion = 3;

    enum class ShaderGraphOutput : std::uint8_t
    {
        Surface,
        Transparent,
        Decal,
        Unlit,
        Hair,
        Eye,
        Fullscreen
    };

    enum class ShaderGraphTemplate : std::uint8_t
    {
        Lit,
        Unlit,
        Transparent,
        Decal,
        Fullscreen,
        Hair,
        Eye
    };

    enum class ShaderGraphPurpose : std::uint8_t
    {
        Shader,
        MaterialFunction,
        ShaderFunction,
        MaterialLayer,
        MaterialLayerBlend
    };

    enum class ShaderGraphValueType : std::uint8_t
    {
        Scalar,
        Vector2,
        Vector3,
        Vector4,
        Color,
        Texture2D,
        MaterialAttributes,
        Bsdf
    };

    enum class ShaderGraphPinDirection : std::uint8_t
    {
        Input,
        Output
    };

    enum class ShaderGraphShaderStage : std::uint8_t
    {
        None = 0,
        Vertex = 1U << 0U,
        Fragment = 1U << 1U,
        Compute = 1U << 2U,
        All = (1U << 0U) | (1U << 1U) | (1U << 2U)
    };

    enum class ShaderGraphNodeKind : std::uint8_t
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
        Posterize,
        Round,
        Truncate,
        Sign,
        Modulo,
        SquareRoot,
        ReciprocalSquareRoot,
        Exponential2,
        Logarithm2,
        Tangent,
        ArcSine,
        ArcCosine,
        ArcTangent2,
        Cross,
        Distance,
        Reflect,
        Refract,
        AppendVector,
        ComponentMask,
        UV1,
        WorldTangent,
        CameraPosition,
        ObjectPosition,
        Time,
        DeltaTime,
        ScreenPosition,
        DerivativeX,
        DerivativeY,
        FilterWidth,
        DepthFade,
        Luminance,
        HueShift,
        Checkerboard,
        VoronoiNoise,
        Panner,
        PolarCoordinates,
        SphereMask,
        RadialGradient,
        LinearGradient,
        Contrast,
        Saturation,
        BlendOverlay,
        Blackbody,
        ReflectionVector,
        FacingRatio,
        Dither,
        GradientNoise,
        Wave,
        TriplanarSample,
        TextureSampleLevel,
        HeightToNormal,
        FlattenNormal,
        MakeMaterialAttributes,
        BreakMaterialAttributes,
        BlendMaterialAttributes,
        StandardSurfaceBsdf,
        ClearCoatBsdf,
        SheenBsdf,
        SubsurfaceBsdf,
        TransmissionBsdf,
        BsdfToMaterialAttributes,
        FunctionCall,
        Reroute,
        If,
        Compare,
        BooleanAnd,
        BooleanOr,
        BooleanNot,
        ArcTangent,
        HyperbolicSine,
        HyperbolicCosine,
        HyperbolicTangent,
        DegreesToRadians,
        RadiansToDegrees,
        Negate,
        ScaleAndBias,
        Exponential,
        Logarithm
    };

    enum class ShaderGraphDiagnosticSeverity : std::uint8_t
    {
        Info,
        Warning,
        Error
    };

    enum class ShaderGraphPreviewMesh : std::uint8_t
    {
        Sphere,
        Plane,
        Cube,
        Custom
    };

    struct ShaderGraphMaterialAttributesValue
    {
        bool operator==(const ShaderGraphMaterialAttributesValue&) const = default;
    };

    struct ShaderGraphBsdfValue
    {
        bool operator==(const ShaderGraphBsdfValue&) const = default;
    };

    using ShaderGraphValue = std::variant<float, Vector2, Vector3, Vector4, Color, AssetId,
                                          ShaderGraphMaterialAttributesValue, ShaderGraphBsdfValue>;

    struct ShaderGraphParameterMetadata
    {
        std::string Description;
        std::string Category;
        std::int32_t SortPriority = 0;
        std::optional<float> Minimum;
        std::optional<float> Maximum;
        std::optional<float> Step;

        bool operator==(const ShaderGraphParameterMetadata&) const = default;
    };

    struct ShaderGraphPin
    {
        AssetId Id;
        std::string Name;
        ShaderGraphValueType Type = ShaderGraphValueType::Scalar;
        ShaderGraphPinDirection Direction = ShaderGraphPinDirection::Input;
        ShaderGraphValue DefaultValue = 0.0F;

        bool operator==(const ShaderGraphPin&) const = default;
    };

    struct ShaderGraphNode
    {
        AssetId Id;
        ShaderGraphNodeKind Kind = ShaderGraphNodeKind::Constant;
        std::string TypeId;
        std::string Name;
        Vector2 EditorPosition;
        ShaderGraphValueType ValueType = ShaderGraphValueType::Scalar;
        ShaderGraphValue Value = 0.0F;
        ShaderTextureSemantic TextureSemantic = ShaderTextureSemantic::Generic;
        std::string Symbol;
        std::filesystem::path Include;
        std::string Function;
        /// Stable referenced graph asset for function-call and other asset-backed nodes.
        AssetId ReferencedAsset;
        ShaderGraphParameterMetadata ParameterMetadata;
        std::vector<ShaderGraphPin> Pins;

        bool operator==(const ShaderGraphNode&) const = default;
    };

    struct ShaderGraphEndpoint
    {
        AssetId Node;
        AssetId Pin;

        bool operator==(const ShaderGraphEndpoint&) const = default;
    };

    struct ShaderGraphConnection
    {
        AssetId Id;
        ShaderGraphEndpoint Output;
        ShaderGraphEndpoint Input;
        /// Editor-only cable routing knots in graph space. They do not affect shader evaluation.
        std::vector<Vector2> RoutingPoints;

        bool operator==(const ShaderGraphConnection&) const = default;
    };

    struct ShaderGraphKeyword
    {
        std::string Name;
        std::vector<std::string> Options;
        std::string DefaultOption;
        bool Exposed = true;

        bool operator==(const ShaderGraphKeyword&) const = default;
    };

    struct ShaderGraphDefinition
    {
        std::uint32_t SchemaVersion = ShaderGraphSourceSchemaVersion;
        ShaderGraphPurpose Purpose = ShaderGraphPurpose::Shader;
        ShaderGraphOutput Output = ShaderGraphOutput::Surface;
        std::vector<ShaderGraphNode> Nodes;
        std::vector<ShaderGraphConnection> Connections;
        std::vector<ShaderGraphKeyword> Keywords;
        std::vector<std::filesystem::path> IncludeRoots{"Assets"};
        /// Optional migration anchor used to retain generated shader subasset IDs after graph extraction.
        AssetId GeneratedAssetOwner;

        bool operator==(const ShaderGraphDefinition&) const = default;
    };

    struct ShaderGraphDiagnostic
    {
        ShaderGraphDiagnosticSeverity Severity = ShaderGraphDiagnosticSeverity::Error;
        std::string Code;
        std::string Message;
        AssetId Node;
        AssetId Pin;
        std::size_t GeneratedLine = 0;

        bool operator==(const ShaderGraphDiagnostic&) const = default;
    };

    struct ShaderGraphShaderVariant
    {
        std::vector<std::string> Keywords;
        std::string StableSuffix;
        std::filesystem::path GeneratedSource;
        std::string Hlsl;
        std::string Manifest;

        bool operator==(const ShaderGraphShaderVariant&) const = default;
    };

    struct ShaderGraphStatistics
    {
        std::size_t NodeCount = 0;
        std::size_t ConnectionCount = 0;
        std::size_t ReachableNodeCount = 0;
        std::size_t UnusedNodeCount = 0;
        std::size_t TextureSampleCount = 0;
        std::size_t EstimatedAluInstructions = 0;
        std::size_t VariantCount = 0;

        bool operator==(const ShaderGraphStatistics&) const = default;
    };

    struct ShaderGraphCompilation
    {
        std::vector<ShaderGraphShaderVariant> Variants;
        std::vector<ShaderPropertyDefinition> Properties;
        std::vector<std::filesystem::path> Dependencies;
        std::vector<ShaderGraphDiagnostic> Diagnostics;
        ShaderGraphStatistics Statistics;

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    struct ShaderGraphCompileOptions
    {
        std::filesystem::path GeneratedSource = "Assets/Generated/ShaderGraph.hlsl";
        std::size_t MaximumVariants = 64;
        std::size_t MaximumNodes = 1024;
        std::size_t MaximumConnections = 4096;
        std::size_t MaximumCustomIncludes = 64;
        std::function<std::optional<std::string>(const std::filesystem::path&)> ReadInclude;
        /// Resolves reusable function/layer graph bodies without exposing asset-system ownership to the compiler.
        std::function<std::optional<ShaderGraphDefinition>(AssetId)> ResolveFunction;
    };

    struct ShaderGraphNodeDescriptor
    {
        ShaderGraphNodeKind Kind = ShaderGraphNodeKind::Constant;
        std::string_view TypeId;
        std::string_view DisplayName;
        std::string_view Category;
        ShaderGraphValueType DefaultValueType = ShaderGraphValueType::Scalar;
        ShaderGraphShaderStage Stages = ShaderGraphShaderStage::All;
        std::size_t EstimatedAluInstructions = 0;
        bool UserCreatable = true;
    };

    struct ShaderGraphInstanceDefinition
    {
        std::uint32_t SchemaVersion = 1;
        AssetId Parent;
        std::map<std::string, MaterialPropertyValue, std::less<>> Properties;
        std::map<std::string, std::string, std::less<>> KeywordOverrides;

        bool operator==(const ShaderGraphInstanceDefinition&) const = default;
    };

    struct ResolvedShaderGraphInstance
    {
        std::map<std::string, MaterialPropertyValue, std::less<>> Properties;
        std::vector<std::string> Keywords;
    };

    class KEIRE_API ShaderGraphAsset final : public Asset
    {
      public:
        explicit ShaderGraphAsset(ShaderGraphDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245534752ULL, 0x4150480000000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const ShaderGraphDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<ShaderGraphAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const ShaderGraphDefinition& definition);
        [[nodiscard]] static ShaderGraphDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const ShaderGraphDefinition& definition);
        [[nodiscard]] static Ref<ShaderGraphAsset> Error();

      private:
        ShaderGraphDefinition m_Definition;
    };

    class KEIRE_API ShaderGraphInstanceAsset final : public Asset
    {
      public:
        explicit ShaderGraphInstanceAsset(ShaderGraphInstanceDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245534749ULL, 0x4e53540000000001ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const ShaderGraphInstanceDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<ShaderGraphInstanceAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const ShaderGraphInstanceDefinition& definition);
        [[nodiscard]] static ShaderGraphInstanceDefinition DecodeSource(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> EncodeSource(const ShaderGraphInstanceDefinition& definition);
        [[nodiscard]] static Ref<ShaderGraphInstanceAsset> Error();

      private:
        ShaderGraphInstanceDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API ShaderGraphDefinition
    CreateDefaultShaderGraph(ShaderGraphOutput output = ShaderGraphOutput::Surface);
    [[nodiscard]] KEIRE_API ShaderGraphDefinition CreateShaderGraphTemplate(ShaderGraphTemplate graphTemplate);
    [[nodiscard]] KEIRE_API ShaderGraphNode
    CreateShaderGraphNode(ShaderGraphNodeKind kind, ShaderGraphValueType valueType = ShaderGraphValueType::Scalar);
    [[nodiscard]] KEIRE_API ShaderGraphNode
    CreateShaderGraphNode(std::string_view typeId, ShaderGraphValueType valueType = ShaderGraphValueType::Scalar);
    [[nodiscard]] KEIRE_API ShaderGraphNode
    CreateShaderGraphFunctionCallNode(AssetId function, const ShaderGraphDefinition& functionDefinition);
    [[nodiscard]] KEIRE_API ShaderGraphDefinition
    ExpandShaderGraphFunctions(const ShaderGraphDefinition& definition,
                               const std::function<std::optional<ShaderGraphDefinition>(AssetId)>& resolveFunction,
                               std::size_t maximumDepth = 32);
    [[nodiscard]] KEIRE_API std::vector<AssetId> ShaderGraphReferencedAssets(const ShaderGraphDefinition& definition);
    [[nodiscard]] KEIRE_API std::string_view ShaderGraphNodeTypeId(ShaderGraphNodeKind kind) noexcept;
    [[nodiscard]] KEIRE_API const ShaderGraphNodeDescriptor*
    FindShaderGraphNodeDescriptor(std::string_view typeId) noexcept;
    [[nodiscard]] KEIRE_API std::span<const ShaderGraphNodeDescriptor> ShaderGraphNodeCatalog() noexcept;
    KEIRE_API void ValidateShaderGraph(const ShaderGraphDefinition& definition);
    [[nodiscard]] KEIRE_API ShaderGraphCompilation CompileShaderGraph(const ShaderGraphDefinition& definition,
                                                                      const ShaderGraphCompileOptions& options = {});
    [[nodiscard]] KEIRE_API std::vector<std::vector<std::string>>
    EnumerateShaderGraphKeywordVariants(std::span<const ShaderGraphKeyword> keywords, std::size_t maximumVariants = 64);
    [[nodiscard]] KEIRE_API std::string
    MakeShaderGraphVariantSubAssetKey(std::string_view target,
                                      const std::map<std::string, std::string, std::less<>>& keywords);
    [[nodiscard]] KEIRE_API std::string
    MakeShaderGraphVariantSubAssetKey(std::string_view target, std::span<const std::string> canonicalKeywords);
    [[nodiscard]] KEIRE_API ResolvedShaderGraphInstance ResolveShaderGraphInstance(
        const ShaderGraphDefinition& graph, std::span<const ShaderGraphInstanceDefinition> ancestry);
    [[nodiscard]] KEIRE_API MaterialAssetDefinition
    BakeShaderGraphInstance(const ShaderGraphDefinition& graph, const ResolvedShaderGraphInstance& instance,
                            const std::function<AssetId(std::span<const std::string>)>& resolveShaderVariant);
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateShaderGraphAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateShaderGraphAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateShaderGraphInstanceAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateShaderGraphInstanceAssetDecoder();
} // namespace Keire
