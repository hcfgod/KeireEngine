#include "Keire/Rendering/ShaderGraph.h"

#include "KeireInternal/Rendering/ShaderGraphCompilerInternal.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool MaterialValueMatches(const MaterialPropertyValue& value, const ShaderGraphValueType type)
        {
            return value.index() == static_cast<std::size_t>(type);
        }
    } // namespace

    ResolvedShaderGraphInstance
    ResolveShaderGraphInstance(const ShaderGraphDefinition& graph,
                               const std::span<const ShaderGraphInstanceDefinition> ancestry)
    {
        ValidateShaderGraph(graph);
        if (ancestry.empty() || ancestry.size() > 16)
            throw std::invalid_argument("Shader Graph instance ancestry must contain between 1 and 16 entries.");
        std::map<std::string, ShaderGraphValueType, std::less<>> propertyTypes;
        for (const auto& node : graph.Nodes)
            if (node.Kind == ShaderGraphNodeKind::Parameter)
                propertyTypes.emplace(node.Symbol, node.ValueType);
        ResolvedShaderGraphInstance result;
        for (const auto& node : graph.Nodes)
            if (node.Kind == ShaderGraphNodeKind::Parameter)
                result.Properties.emplace(node.Symbol, Detail::ToMaterialPropertyValue(node.Value));
        std::map<std::string, std::string, std::less<>> keywordValues;
        for (const auto& keyword : graph.Keywords)
            keywordValues[keyword.Name] = keyword.DefaultOption.empty()
                                              ? (keyword.Options.empty() ? "false" : keyword.Options.front())
                                              : keyword.DefaultOption;
        for (const auto& instance : ancestry)
        {
            Detail::ValidateShaderGraphInstanceDefinition(instance);
            for (const auto& [name, value] : instance.Properties)
            {
                const auto found = propertyTypes.find(name);
                if (found == propertyTypes.end() || !MaterialValueMatches(value, found->second))
                    throw std::invalid_argument("Shader Graph instance property is unknown or has the wrong type: " +
                                                name);
                result.Properties.insert_or_assign(name, value);
            }
            for (const auto& [name, value] : instance.KeywordOverrides)
            {
                const auto found = std::ranges::find(graph.Keywords, name, &ShaderGraphKeyword::Name);
                if (found == graph.Keywords.end())
                    throw std::invalid_argument("Shader Graph instance keyword is unknown: " + name);
                if (found->Options.empty())
                {
                    if (value != "true" && value != "false")
                        throw std::invalid_argument("Boolean Shader Graph keyword overrides must be true or false.");
                }
                else if (std::ranges::find(found->Options, value) == found->Options.end())
                    throw std::invalid_argument("Shader Graph enum keyword override is invalid: " + name);
                keywordValues[name] = value;
            }
        }
        for (const auto& keyword : graph.Keywords)
        {
            const auto& value = keywordValues.at(keyword.Name);
            if (keyword.Options.empty())
            {
                if (value == "true")
                    result.Keywords.push_back(keyword.Name);
            }
            else
                result.Keywords.push_back(keyword.Name + "_" + value);
        }
        return result;
    }

    MaterialAssetDefinition
    BakeShaderGraphInstance(const ShaderGraphDefinition& graph, const ResolvedShaderGraphInstance& instance,
                            const std::function<AssetId(std::span<const std::string>)>& resolveShaderVariant)
    {
        if (!resolveShaderVariant)
            throw std::invalid_argument("Baking a Shader Graph instance requires a shader-variant resolver.");
        ValidateShaderGraph(graph);
        if (instance.Properties.size() > Detail::MaximumShaderGraphProperties)
            throw std::invalid_argument("Shader Graph instance properties exceed their bound.");
        for (const auto& [name, value] : instance.Properties)
        {
            const auto parameter =
                std::ranges::find_if(graph.Nodes, [&](const ShaderGraphNode& node)
                                     { return node.Kind == ShaderGraphNodeKind::Parameter && node.Symbol == name; });
            if (parameter == graph.Nodes.end() || !MaterialValueMatches(value, parameter->ValueType))
                throw std::invalid_argument("Shader Graph instance property is unknown or has the wrong type: " + name);
            Detail::ValidateFiniteShaderGraphValue(value);
        }
        const auto variants = EnumerateShaderGraphKeywordVariants(graph.Keywords);
        if (std::ranges::find(variants, instance.Keywords) == variants.end())
            throw std::invalid_argument("Shader Graph instance selected an unavailable keyword variant.");
        MaterialAssetDefinition result;
        result.Shader = resolveShaderVariant(instance.Keywords);
        if (!result.Shader)
            throw std::runtime_error("Shader Graph shader variant is not published.");
        result.Properties = instance.Properties;
        if (graph.Output == ShaderGraphOutput::Transparent || graph.Output == ShaderGraphOutput::Decal)
            result.Surface.AlphaMode = MaterialAlphaMode::Blend;
        else if (graph.Output == ShaderGraphOutput::Hair)
            result.Surface.AlphaMode = MaterialAlphaMode::Mask;
        result.Surface.DoubleSided =
            graph.Output == ShaderGraphOutput::Decal || graph.Output == ShaderGraphOutput::Hair;
        return result;
    }
} // namespace Keire
