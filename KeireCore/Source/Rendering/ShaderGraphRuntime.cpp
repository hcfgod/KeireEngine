#include "Keire/Rendering/ShaderGraphRuntime.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool IsTextureSample(const ShaderGraphNodeKind kind) noexcept
        {
            return kind == ShaderGraphNodeKind::TextureSample || kind == ShaderGraphNodeKind::TextureSampleLevel ||
                   kind == ShaderGraphNodeKind::TriplanarSample;
        }

        [[nodiscard]] std::string QualitySuffix(const ShaderGraphQualityTier quality)
        {
            return "quality-" + std::string(ShaderGraphQualityDefine(quality));
        }

        [[nodiscard]] std::vector<std::string> KeywordChoices(const ShaderGraphKeyword& keyword)
        {
            if (keyword.Options.empty())
                return {"false", "true"};
            return keyword.Options;
        }

        [[nodiscard]] std::string KeywordToken(const ShaderGraphKeyword& keyword, const std::string_view choice)
        {
            if (keyword.Options.empty())
                return choice == "true" ? keyword.Name : std::string{};
            return keyword.Name + '_' + std::string(choice);
        }

        [[nodiscard]] bool Contains(const std::span<const std::string> values, const std::string_view value)
        {
            return std::ranges::find(values, value) != values.end();
        }
    } // namespace

    std::string_view ShaderGraphQualityDefine(const ShaderGraphQualityTier quality) noexcept
    {
        constexpr std::array names{"low", "medium", "high", "ultra"};
        const auto index = static_cast<std::size_t>(quality);
        return index < names.size() ? names[index] : std::string_view{};
    }

    bool ShaderGraphCompilation::Succeeded() const noexcept
    {
        return !Variants.empty() &&
               std::ranges::none_of(Diagnostics, [](const ShaderGraphDiagnostic& diagnostic)
                                    { return diagnostic.Severity == ShaderGraphDiagnosticSeverity::Error; });
    }

    std::vector<std::vector<std::string>>
    EnumerateShaderGraphKeywordVariants(const std::span<const ShaderGraphKeyword> keywords,
                                        const std::size_t maximumVariants)
    {
        if (maximumVariants == 0 || maximumVariants > 1024)
            throw std::invalid_argument("Shader Graph variant limit must be between 1 and 1,024.");
        std::vector<std::vector<std::string>> result(1);
        for (const auto& keyword : keywords)
        {
            std::vector<std::string> choices = keyword.Options;
            if (choices.empty())
                choices = {"false", "true"};
            if (result.size() > maximumVariants / choices.size())
                throw std::invalid_argument("Shader Graph keyword permutations exceed the configured variant limit.");
            std::vector<std::vector<std::string>> expanded;
            expanded.reserve(result.size() * choices.size());
            for (const auto& existing : result)
                for (const auto& choice : choices)
                {
                    auto variant = existing;
                    if (auto token = KeywordToken(keyword, choice); !token.empty())
                        variant.push_back(std::move(token));
                    expanded.push_back(std::move(variant));
                }
            result = std::move(expanded);
        }
        return result;
    }

    std::vector<ShaderGraphRuntimeVariant> PruneShaderGraphVariants(const ShaderGraphDefinition& definition,
                                                                    const ShaderGraphVariantPruningOptions& options)
    {
        ValidateShaderGraph(definition);
        if (options.MaximumVariants == 0 || options.MaximumVariants > 1024 || options.QualityTiers.empty() ||
            options.QualityTiers.size() > 4)
            throw std::invalid_argument("Shader Graph variant-pruning bounds are invalid.");

        std::set<ShaderGraphQualityTier> qualities;
        for (const auto quality : options.QualityTiers)
            if (ShaderGraphQualityDefine(quality).empty() || !qualities.insert(quality).second)
                throw std::invalid_argument("Shader Graph quality tiers must be valid and unique.");

        std::map<std::string, const ShaderGraphKeyword*, std::less<>> keywords;
        for (const auto& keyword : definition.Keywords)
            keywords.emplace(keyword.Name, &keyword);
        for (const auto& [name, allowed] : options.AllowedKeywordOptions)
        {
            const auto keyword = keywords.find(name);
            if (keyword == keywords.end() || allowed.empty())
                throw std::invalid_argument("Shader Graph variant pruning references an unknown or empty keyword.");
            const auto authored = KeywordChoices(*keyword->second);
            std::set<std::string, std::less<>> unique;
            for (const auto& choice : allowed)
                if (!Contains(authored, choice) || !unique.insert(choice).second)
                    throw std::invalid_argument("Shader Graph variant pruning contains an invalid keyword option.");
        }

        std::vector<std::vector<std::string>> keywordVariants(1);
        for (const auto& keyword : definition.Keywords)
        {
            auto choices = KeywordChoices(keyword);
            if (const auto allowed = options.AllowedKeywordOptions.find(keyword.Name);
                allowed != options.AllowedKeywordOptions.end())
                choices = allowed->second;
            if (keywordVariants.size() > options.MaximumVariants / choices.size())
                throw std::invalid_argument("Pruned Shader Graph variants exceed the configured bound.");
            std::vector<std::vector<std::string>> expanded;
            expanded.reserve(keywordVariants.size() * choices.size());
            for (const auto& existing : keywordVariants)
                for (const auto& choice : choices)
                {
                    auto variant = existing;
                    if (auto token = KeywordToken(keyword, choice); !token.empty())
                        variant.push_back(std::move(token));
                    expanded.push_back(std::move(variant));
                }
            keywordVariants = std::move(expanded);
        }
        if (keywordVariants.size() > options.MaximumVariants / qualities.size())
            throw std::invalid_argument("Quality-tier Shader Graph variants exceed the configured bound.");

        std::vector<ShaderGraphRuntimeVariant> result;
        result.reserve(keywordVariants.size() * qualities.size());
        for (const auto quality : qualities)
            for (const auto& keywordVariant : keywordVariants)
            {
                auto suffix = MakeShaderGraphVariantSubAssetKey("default", keywordVariant);
                suffix += '-' + QualitySuffix(quality);
                result.push_back({keywordVariant, quality, std::move(suffix)});
            }
        return result;
    }

    ShaderGraphAnalysis AnalyzeShaderGraph(const ShaderGraphDefinition& definition,
                                           const ShaderGraphAnalysisLimits& limits)
    {
        ValidateShaderGraph(definition);
        if (limits.MaximumReachableNodes == 0 || limits.MaximumReachableNodes > 1024 ||
            limits.MaximumEstimatedAluInstructions == 0 || limits.MaximumEstimatedAluInstructions > 65536 ||
            limits.MaximumTextureSamples == 0 || limits.MaximumTextureSamples > 1024 ||
            limits.MaximumDependencyDepth == 0 || limits.MaximumDependencyDepth > 1024)
            throw std::invalid_argument("Shader Graph analysis limits are invalid.");

        ShaderGraphAnalysis result;
        result.Statistics.NodeCount = definition.Nodes.size();
        result.Statistics.ConnectionCount = definition.Connections.size();
        const auto master = std::ranges::find(definition.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
        std::unordered_map<AssetId, std::vector<AssetId>> dependencies;
        for (const auto& connection : definition.Connections)
            dependencies[connection.Input.Node].push_back(connection.Output.Node);
        std::unordered_set<AssetId> reachable;
        std::vector<std::pair<AssetId, std::size_t>> pending{{master->Id, 1}};
        while (!pending.empty())
        {
            const auto [nodeId, depth] = pending.back();
            pending.pop_back();
            if (!reachable.insert(nodeId).second)
                continue;
            result.MaximumDependencyDepth = std::max(result.MaximumDependencyDepth, depth);
            if (const auto found = dependencies.find(nodeId); found != dependencies.end())
                for (const auto dependency : found->second)
                    pending.emplace_back(dependency, depth + 1);
        }
        result.Statistics.ReachableNodeCount = reachable.size();
        result.Statistics.UnusedNodeCount = definition.Nodes.size() - reachable.size();
        for (const auto& node : definition.Nodes)
        {
            if (!reachable.contains(node.Id))
                continue;
            if (const auto* descriptor = FindShaderGraphNodeDescriptor(node.TypeId))
                result.Statistics.EstimatedAluInstructions += descriptor->EstimatedAluInstructions;
            result.Statistics.TextureSampleCount += IsTextureSample(node.Kind) ? 1U : 0U;
        }
        result.Statistics.VariantCount = 1;
        for (const auto& keyword : definition.Keywords)
        {
            const auto choices = keyword.Options.empty() ? 2U : keyword.Options.size();
            if (result.Statistics.VariantCount > std::numeric_limits<std::size_t>::max() / choices)
            {
                result.Statistics.VariantCount = std::numeric_limits<std::size_t>::max();
                break;
            }
            result.Statistics.VariantCount *= choices;
        }
        result.WithinLimits = result.Statistics.ReachableNodeCount <= limits.MaximumReachableNodes &&
                              result.Statistics.EstimatedAluInstructions <= limits.MaximumEstimatedAluInstructions &&
                              result.Statistics.TextureSampleCount <= limits.MaximumTextureSamples &&
                              result.MaximumDependencyDepth <= limits.MaximumDependencyDepth;
        return result;
    }

    void ValidateShaderGraphNodePreview(const ShaderGraphDefinition& definition,
                                        const ShaderGraphNodePreviewRequest& request)
    {
        ValidateShaderGraph(definition);
        constexpr std::uint32_t MaximumDimension = 1024;
        constexpr std::uint64_t MaximumPixels = std::uint64_t{1024} * 1024U;
        if (!request.Node || !request.OutputPin || request.Width == 0 || request.Height == 0 ||
            request.Width > MaximumDimension || request.Height > MaximumDimension ||
            static_cast<std::uint64_t>(request.Width) * request.Height > MaximumPixels ||
            ShaderGraphQualityDefine(request.Quality).empty() || request.MaximumReachableNodes == 0 ||
            request.MaximumReachableNodes > 1024 || request.MaximumEstimatedAluInstructions == 0 ||
            request.MaximumEstimatedAluInstructions > 65536)
            throw std::invalid_argument("Shader Graph node-preview request exceeds its portable bounds.");
        const auto node = std::ranges::find(definition.Nodes, request.Node, &ShaderGraphNode::Id);
        if (node == definition.Nodes.end() || node->Kind == ShaderGraphNodeKind::Master)
            throw std::invalid_argument("Shader Graph node preview references an unavailable node.");
        const auto pin = std::ranges::find(node->Pins, request.OutputPin, &ShaderGraphPin::Id);
        if (pin == node->Pins.end() || pin->Direction != ShaderGraphPinDirection::Output ||
            pin->Type == ShaderGraphValueType::Texture2D || pin->Type == ShaderGraphValueType::MaterialAttributes ||
            pin->Type == ShaderGraphValueType::Bsdf)
            throw std::invalid_argument("Shader Graph node preview requires a numeric or color output pin.");

        const auto analysis =
            AnalyzeShaderGraph(definition, {.MaximumReachableNodes = request.MaximumReachableNodes,
                                            .MaximumEstimatedAluInstructions = request.MaximumEstimatedAluInstructions,
                                            .MaximumTextureSamples = 64,
                                            .MaximumDependencyDepth = request.MaximumReachableNodes});
        if (!analysis.WithinLimits)
            throw std::invalid_argument("Shader Graph node preview exceeds its evaluation budget.");
    }
} // namespace Keire
