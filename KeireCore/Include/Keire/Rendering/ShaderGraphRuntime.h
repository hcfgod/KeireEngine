#pragma once

#include "Keire/Api.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class ShaderGraphQualityTier : std::uint8_t
    {
        Low,
        Medium,
        High,
        Ultra
    };

    struct ShaderGraphVariantPruningOptions
    {
        /// Missing keywords retain every authored option. Boolean selections use "false" and "true".
        std::map<std::string, std::vector<std::string>, std::less<>> AllowedKeywordOptions;
        std::vector<ShaderGraphQualityTier> QualityTiers{ShaderGraphQualityTier::High};
        std::size_t MaximumVariants = 64;
    };

    struct ShaderGraphRuntimeVariant
    {
        std::vector<std::string> Keywords;
        ShaderGraphQualityTier Quality = ShaderGraphQualityTier::High;
        std::string StableSuffix;

        bool operator==(const ShaderGraphRuntimeVariant&) const = default;
    };

    struct ShaderGraphAnalysisLimits
    {
        std::size_t MaximumReachableNodes = 1024;
        std::size_t MaximumEstimatedAluInstructions = 512;
        std::size_t MaximumTextureSamples = 64;
        std::size_t MaximumDependencyDepth = 256;
    };

    struct ShaderGraphAnalysis
    {
        ShaderGraphStatistics Statistics;
        std::size_t MaximumDependencyDepth = 0;
        bool WithinLimits = true;

        bool operator==(const ShaderGraphAnalysis&) const = default;
    };

    struct ShaderGraphNodePreviewRequest
    {
        AssetId Node;
        AssetId OutputPin;
        std::uint32_t Width = 256;
        std::uint32_t Height = 256;
        ShaderGraphQualityTier Quality = ShaderGraphQualityTier::High;
        std::size_t MaximumReachableNodes = 256;
        std::size_t MaximumEstimatedAluInstructions = 256;
    };

    [[nodiscard]] KEIRE_API std::string_view ShaderGraphQualityDefine(ShaderGraphQualityTier quality) noexcept;
    [[nodiscard]] KEIRE_API std::vector<ShaderGraphRuntimeVariant>
    PruneShaderGraphVariants(const ShaderGraphDefinition& definition,
                             const ShaderGraphVariantPruningOptions& options = {});
    [[nodiscard]] KEIRE_API ShaderGraphAnalysis AnalyzeShaderGraph(const ShaderGraphDefinition& definition,
                                                                   const ShaderGraphAnalysisLimits& limits = {});
    KEIRE_API void ValidateShaderGraphNodePreview(const ShaderGraphDefinition& definition,
                                                  const ShaderGraphNodePreviewRequest& request);
} // namespace Keire
