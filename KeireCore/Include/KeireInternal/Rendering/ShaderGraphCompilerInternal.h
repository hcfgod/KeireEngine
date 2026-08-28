#pragma once

#include "Keire/Rendering/ShaderGraph.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Keire::Detail
{
    constexpr std::size_t MaximumShaderGraphNodes = 1024;
    constexpr std::size_t MaximumShaderGraphConnections = 4096;
    constexpr std::size_t MaximumShaderGraphRoutingPointsPerConnection = 64;
    constexpr std::size_t MaximumShaderGraphKeywords = 16;
    constexpr std::size_t MaximumShaderGraphProperties = 80;
    constexpr std::size_t MaximumShaderGraphPinsPerNode = 32;
    constexpr std::size_t MaximumShaderGraphIncludeRoots = 16;
    constexpr std::size_t MaximumShaderGraphText = 128;
    constexpr std::size_t MaximumShaderGraphPath = 1024;
    constexpr std::size_t MaximumShaderGraphAssetBytes = std::size_t{32} * 1024U * 1024U;

    struct ShaderGraphEndpointHash final
    {
        [[nodiscard]] std::size_t operator()(const ShaderGraphEndpoint& value) const noexcept;
    };

    struct ShaderGraphExpression final
    {
        std::string Code;
        ShaderGraphValueType Type = ShaderGraphValueType::Scalar;
    };

    [[nodiscard]] bool IsUnlitShaderGraphOutput(ShaderGraphOutput output) noexcept;
    [[nodiscard]] bool IsValidShaderGraphIdentifier(std::string_view value);
    [[nodiscard]] bool IsSafeShaderGraphRelativePath(const std::filesystem::path& value);
    [[nodiscard]] const ShaderGraphPin* FindShaderGraphPin(const ShaderGraphNode& node, std::string_view name,
                                                           ShaderGraphPinDirection direction);
    [[nodiscard]] const ShaderGraphNode& RequireShaderGraphNode(const ShaderGraphDefinition& definition, AssetId id);
    [[nodiscard]] const ShaderGraphPin& RequireShaderGraphPin(const ShaderGraphNode& node, AssetId id);
    void ValidateFiniteShaderGraphValue(const ShaderGraphValue& value);
    void ValidateFiniteShaderGraphValue(const MaterialPropertyValue& value);
    [[nodiscard]] MaterialPropertyValue ToMaterialPropertyValue(const ShaderGraphValue& value);
    [[nodiscard]] ShaderGraphExpression MakeShaderGraphLiteral(const ShaderGraphValue& value,
                                                               ShaderGraphValueType type);
    [[nodiscard]] std::string ShaderGraphSwizzle(ShaderGraphValueType type);
    [[nodiscard]] std::string ShaderGraphPropertySymbol(std::string_view name);
    [[nodiscard]] std::string ShaderGraphVertexPropertySymbol(std::string_view name);
    [[nodiscard]] bool SupportsShaderGraphStage(ShaderGraphShaderStage stages, ShaderGraphShaderStage stage) noexcept;
    [[nodiscard]] ShaderGraphExpression CoerceShaderGraphExpression(ShaderGraphExpression expression,
                                                                    ShaderGraphValueType target);
    [[nodiscard]] ShaderGraphStatistics AnalyzeShaderGraph(const ShaderGraphDefinition& definition);
    [[nodiscard]] std::string ShaderGraphKeywordSuffix(std::span<const std::string> keywords);
    [[nodiscard]] std::filesystem::path ShaderGraphVariantSourcePath(const std::filesystem::path& base,
                                                                     std::string_view suffix);
    void ValidateShaderGraphInstanceDefinition(const ShaderGraphInstanceDefinition& definition);

    class ShaderGraphCompiler final
    {
      public:
        ShaderGraphCompiler(const ShaderGraphDefinition& definition, const ShaderGraphCompileOptions& options,
                            std::span<const std::string> keywords, std::vector<ShaderPropertyDefinition>& properties,
                            std::vector<std::filesystem::path>& dependencies);

        [[nodiscard]] std::string BuildHlsl();
        [[nodiscard]] bool UsesVertexMaterialParameters() const noexcept { return m_UsesVertexMaterialParameters; }
        [[nodiscard]] ShaderOcclusionSupport OcclusionSupport() const noexcept { return m_OcclusionSupport; }
        [[nodiscard]] std::optional<float> MaximumWorldPositionDisplacementRadius() const noexcept
        {
            return m_MaximumWorldPositionDisplacementRadius;
        }

      private:
        void RegisterProperty(const ShaderGraphNode& node);
        [[nodiscard]] ShaderGraphExpression Input(const ShaderGraphNode& node, const ShaderGraphPin& pin);
        [[nodiscard]] ShaderGraphExpression EvaluatePrepared(ShaderGraphEndpoint endpoint);
        [[nodiscard]] ShaderGraphExpression Evaluate(ShaderGraphEndpoint endpoint);
        void ValidateIncludes();
        void DiscoverInclude(const std::filesystem::path& path, std::set<std::filesystem::path>& visited,
                             std::set<std::filesystem::path>& visiting);

        const ShaderGraphDefinition& m_Definition;
        const ShaderGraphCompileOptions& m_Options;
        std::span<const std::string> m_Keywords;
        std::vector<ShaderPropertyDefinition>& m_Properties;
        std::vector<std::filesystem::path>& m_Dependencies;
        std::unordered_map<ShaderGraphEndpoint, ShaderGraphEndpoint, ShaderGraphEndpointHash> m_Incoming;
        std::unordered_map<ShaderGraphEndpoint, ShaderGraphExpression, ShaderGraphEndpointHash> m_Cache;
        std::unordered_set<AssetId> m_Visiting;
        std::unordered_set<AssetId> m_Preparing;
        std::vector<std::filesystem::path> m_CustomIncludes;
        ShaderGraphShaderStage m_CurrentStage = ShaderGraphShaderStage::Fragment;
        bool m_UsesVertexMaterialParameters = false;
        ShaderOcclusionSupport m_OcclusionSupport = ShaderOcclusionSupport::None;
        std::optional<float> m_MaximumWorldPositionDisplacementRadius = 0.0F;
    };
} // namespace Keire::Detail
