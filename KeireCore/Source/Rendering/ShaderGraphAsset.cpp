#include "Keire/Rendering/ShaderGraph.h"

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
} // namespace Keire
