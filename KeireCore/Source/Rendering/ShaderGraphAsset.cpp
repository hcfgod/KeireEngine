#include "Keire/Rendering/ShaderGraph.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace Keire
{
    ShaderGraphAsset::ShaderGraphAsset(ShaderGraphDefinition definition) : m_Definition(std::move(definition)) {}

    std::size_t ShaderGraphAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& node : m_Definition.Nodes)
            result += sizeof(node) + node.TypeId.size() + node.Name.size() + node.Symbol.size() + node.Function.size() +
                      node.Include.native().size() * sizeof(std::filesystem::path::value_type) +
                      node.ParameterMetadata.Description.size() + node.ParameterMetadata.Category.size() +
                      node.Pins.size() * sizeof(ShaderGraphPin);
        result += m_Definition.Connections.size() * sizeof(ShaderGraphConnection);
        for (const auto& connection : m_Definition.Connections)
            result += connection.RoutingPoints.capacity() * sizeof(Vector2);
        for (const auto& resource : m_Definition.Resources)
            result += sizeof(resource) + resource.Name.capacity() + resource.Symbol.capacity();
        for (const auto& annotation : m_Definition.Authoring.NodeAnnotations)
            result += sizeof(annotation) + annotation.Text.capacity();
        for (const auto& comment : m_Definition.Authoring.Comments)
            result += sizeof(comment) + comment.Title.capacity() + comment.Description.capacity() +
                      comment.Members.capacity() * sizeof(AssetId);
        return result;
    }

    ShaderGraphInstanceAsset::ShaderGraphInstanceAsset(ShaderGraphInstanceDefinition definition)
        : m_Definition(std::move(definition))
    {
    }

    std::size_t ShaderGraphInstanceAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& [name, value] : m_Definition.Properties)
        {
            (void)value;
            result += name.size() + sizeof(MaterialPropertyValue);
        }
        for (const auto& [name, value] : m_Definition.KeywordOverrides)
            result += name.size() + value.size();
        return result;
    }

    std::vector<AssetId> ShaderGraphReferencedAssets(const ShaderGraphDefinition& definition)
    {
        std::vector<AssetId> result;
        for (const auto& node : definition.Nodes)
            if (node.ReferencedAsset)
                result.push_back(node.ReferencedAsset);
        std::ranges::sort(result);
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    AssetDecoderRegistration CreateShaderGraphAssetDecoder()
    {
        return {ShaderGraphAsset::StaticType(), ShaderGraphAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return ShaderGraphAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateShaderGraphInstanceAssetDecoder()
    {
        return {ShaderGraphInstanceAsset::StaticType(), ShaderGraphInstanceAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return ShaderGraphInstanceAsset::Decode(bytes); }};
    }
} // namespace Keire
