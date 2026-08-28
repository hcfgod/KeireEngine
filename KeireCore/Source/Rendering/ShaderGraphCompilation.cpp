#include "Keire/Rendering/ShaderGraph.h"

#include "KeireInternal/Rendering/ShaderGraphCompilerInternal.h"
#include "KeireInternal/Rendering/ShaderGraphManifest.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Keire
{
    ShaderGraphCompilation CompileShaderGraph(const ShaderGraphDefinition& definition,
                                              const ShaderGraphCompileOptions& options)
    {
        ShaderGraphCompilation result;
        try
        {
            ValidateShaderGraph(definition);
            if (definition.Purpose != ShaderGraphPurpose::Shader)
                throw std::invalid_argument("Reusable graph bodies must be called from a Shader or Material Graph.");
            const auto expanded = ShaderGraphReferencedAssets(definition).empty()
                                      ? definition
                                      : ExpandShaderGraphFunctions(definition, options.ResolveFunction);
            if (options.MaximumNodes == 0 || options.MaximumNodes > Detail::MaximumShaderGraphNodes ||
                options.MaximumConnections == 0 || options.MaximumConnections > Detail::MaximumShaderGraphConnections ||
                options.MaximumCustomIncludes == 0 || options.MaximumCustomIncludes > 256 ||
                !Detail::IsSafeShaderGraphRelativePath(options.GeneratedSource) ||
                expanded.Nodes.size() > options.MaximumNodes ||
                expanded.Connections.size() > options.MaximumConnections)
                throw std::invalid_argument("Shader Graph compile options or graph bounds are invalid.");
            if (!expanded.Resources.empty() && !options.AllowOfflineResourceDeclarations)
                throw std::invalid_argument(
                    "Shader Graph portable resources require a backend binding implementation before runtime import.");
            result.Statistics = Detail::AnalyzeShaderGraph(expanded);
            const auto variants = EnumerateShaderGraphKeywordVariants(expanded.Keywords, options.MaximumVariants);
            result.Statistics.VariantCount = variants.size();
            std::size_t unusedDiagnostics = 0;
            constexpr std::size_t MaximumUnusedDiagnostics = 16;
            if (result.Statistics.UnusedNodeCount != 0)
            {
                const auto master =
                    std::ranges::find(expanded.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
                std::unordered_set<AssetId> reachable{master->Id};
                std::vector<AssetId> pending{master->Id};
                while (!pending.empty())
                {
                    const auto inputNode = pending.back();
                    pending.pop_back();
                    for (const auto& connection : expanded.Connections)
                        if (connection.Input.Node == inputNode && reachable.insert(connection.Output.Node).second)
                            pending.push_back(connection.Output.Node);
                }
                for (const auto& node : expanded.Nodes)
                {
                    if (reachable.contains(node.Id) || unusedDiagnostics == MaximumUnusedDiagnostics)
                        continue;
                    result.Diagnostics.push_back(
                        {ShaderGraphDiagnosticSeverity::Warning,
                         "SG1001",
                         "Node '" + node.Name + "' does not contribute to the material output.",
                         node.Id,
                         {},
                         0});
                    ++unusedDiagnostics;
                }
                if (result.Statistics.UnusedNodeCount > MaximumUnusedDiagnostics)
                    result.Diagnostics.push_back(
                        {ShaderGraphDiagnosticSeverity::Warning,
                         "SG1002",
                         std::to_string(result.Statistics.UnusedNodeCount - MaximumUnusedDiagnostics) +
                             " additional unused nodes were omitted from diagnostics.",
                         {},
                         {},
                         0});
            }
            if (result.Statistics.VariantCount > 16)
                result.Diagnostics.push_back(
                    {ShaderGraphDiagnosticSeverity::Warning,
                     "SG1101",
                     "This graph produces " + std::to_string(result.Statistics.VariantCount) +
                         " shader variants. Consider reducing independent keywords to control build and memory cost.",
                     {},
                     {},
                     0});
            if (result.Statistics.EstimatedAluInstructions > 192)
                result.Diagnostics.push_back({ShaderGraphDiagnosticSeverity::Warning,
                                              "SG1201",
                                              "The reachable graph has a high estimated arithmetic cost (" +
                                                  std::to_string(result.Statistics.EstimatedAluInstructions) + " ALU).",
                                              {},
                                              {},
                                              0});
            else if (result.Statistics.EstimatedAluInstructions > 96)
                result.Diagnostics.push_back({ShaderGraphDiagnosticSeverity::Info,
                                              "SG1200",
                                              "The reachable graph has a moderate estimated arithmetic cost (" +
                                                  std::to_string(result.Statistics.EstimatedAluInstructions) + " ALU).",
                                              {},
                                              {},
                                              0});
            const auto master = std::ranges::find(expanded.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
            if (master != expanded.Nodes.end())
            {
                const auto attributes =
                    Detail::FindShaderGraphPin(*master, "MaterialAttributes", ShaderGraphPinDirection::Input);
                const bool attributesConnected =
                    attributes &&
                    std::ranges::any_of(expanded.Connections, [&](const ShaderGraphConnection& value)
                                        { return value.Input == ShaderGraphEndpoint{master->Id, attributes->Id}; });
                if (attributesConnected)
                {
                    std::vector<std::string_view> ignoredInputs;
                    for (const auto& connection : expanded.Connections)
                    {
                        if (connection.Input.Node != master->Id || connection.Input.Pin == attributes->Id)
                            continue;
                        const auto& pin = Detail::RequireShaderGraphPin(*master, connection.Input.Pin);
                        if (pin.Name != "WorldPositionOffset" && pin.Name != "PixelDepthOffset")
                            ignoredInputs.push_back(pin.Name);
                    }
                    if (!ignoredInputs.empty())
                    {
                        std::string names;
                        for (const auto name : ignoredInputs)
                        {
                            if (!names.empty())
                                names += ", ";
                            names += name;
                        }
                        result.Diagnostics.push_back(
                            {ShaderGraphDiagnosticSeverity::Warning, "SG1300",
                             "MaterialAttributes overrides these connected Master inputs: " + names + '.', master->Id,
                             attributes->Id, 0});
                    }
                }
            }
            for (const auto& keywords : variants)
            {
                Detail::ShaderGraphCompiler compiler(expanded, options, keywords, result.Properties,
                                                     result.Dependencies);
                auto hlsl = compiler.BuildHlsl();
                auto suffix = Detail::ShaderGraphKeywordSuffix(keywords);
                auto generatedSource = Detail::ShaderGraphVariantSourcePath(options.GeneratedSource, suffix);
                result.Variants.push_back({keywords, std::move(suffix), generatedSource, std::move(hlsl),
                                           Detail::BuildShaderGraphManifest(
                                               expanded, generatedSource, result.Properties, keywords,
                                               compiler.UsesVertexMaterialParameters(), compiler.OcclusionSupport(),
                                               compiler.MaximumWorldPositionDisplacementRadius())});
            }
            std::ranges::sort(result.Dependencies);
            result.Dependencies.erase(std::unique(result.Dependencies.begin(), result.Dependencies.end()),
                                      result.Dependencies.end());
        }
        catch (const std::exception& error)
        {
            result.Variants.clear();
            result.Properties.clear();
            result.Dependencies.clear();
            result.Diagnostics.push_back({ShaderGraphDiagnosticSeverity::Error, "SG0001", error.what(), {}, {}, 0});
        }
        return result;
    }
} // namespace Keire
