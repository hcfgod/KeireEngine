#include "Keire/Rendering/MaterialEcosystem.h"
#include "Keire/Rendering/MaterialGraph.h"

#include <algorithm>
#include <stdexcept>

namespace Keire
{
    MaterialPropertyValue DefaultMaterialGraphValue(const ShaderPropertyDefinition& property)
    {
        switch (property.Type)
        {
        case ShaderPropertyType::Scalar:
            return property.DefaultValue.X;
        case ShaderPropertyType::Vector2:
            return Vector2{property.DefaultValue.X, property.DefaultValue.Y};
        case ShaderPropertyType::Vector3:
            return Vector3{property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z};
        case ShaderPropertyType::Vector4:
            return property.DefaultValue;
        case ShaderPropertyType::Color:
            return Color{property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z,
                         property.DefaultValue.W};
        case ShaderPropertyType::Texture2D:
            return property.DefaultTexture;
        }
        throw std::invalid_argument("Shader property type cannot be represented by a Material Graph.");
    }

    std::size_t MaterialGraphAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this) + m_Definition.Shader.Target.size();
        for (const auto& [name, option] : m_Definition.Shader.Keywords)
            result += name.size() + option.size();
        for (const auto& property : m_Definition.Properties)
            result += sizeof(property) + property.Name.size();
        for (const auto& node : m_Definition.Nodes)
            result += sizeof(node) + node.Name.size();
        result += m_Definition.Connections.size() * sizeof(MaterialGraphConnection);
        for (const auto& connection : m_Definition.Connections)
            result += connection.RoutingPoints.capacity() * sizeof(Vector2);
        result += m_Definition.SurfaceGraph.Nodes.size() * sizeof(ShaderGraphNode);
        result += m_Definition.SurfaceGraph.Connections.size() * sizeof(ShaderGraphConnection);
        for (const auto& connection : m_Definition.SurfaceGraph.Connections)
            result += connection.RoutingPoints.capacity() * sizeof(Vector2);
        for (const auto& annotation : m_Definition.Authoring.NodeAnnotations)
            result += sizeof(annotation) + annotation.Text.capacity();
        for (const auto& comment : m_Definition.Authoring.Comments)
            result += sizeof(comment) + comment.Title.capacity() + comment.Description.capacity() +
                      comment.Members.capacity() * sizeof(AssetId);
        return result;
    }

    std::size_t MaterialInstanceAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& [name, value] : m_Definition.Properties)
        {
            (void)value;
            result += name.size() + sizeof(MaterialPropertyValue);
        }
        return result;
    }

    MaterialNumericUniformCache::MaterialNumericUniformCache(const std::size_t maximumUniforms)
        : m_MaximumUniforms(maximumUniforms)
    {
        if (maximumUniforms == 0 || maximumUniforms > MaximumMaterialNumericUniforms)
            throw std::invalid_argument("Material numeric uniform cache bound must be between 1 and 256.");
    }

    MaterialNumericUniformUpdate MaterialNumericUniformCache::Update(const MaterialNumericUniformSnapshot& snapshot)
    {
        if (snapshot.Values.size() > m_MaximumUniforms)
            throw std::invalid_argument("Material numeric uniform snapshot exceeds the cache bound.");
        if (std::ranges::any_of(snapshot.Values, [](const Vector4 value) { return !Math::IsFinite(value); }))
            throw std::invalid_argument("Material numeric uniform snapshot contains a non-finite value.");

        MaterialNumericUniformUpdate result;
        result.Revision = snapshot.Revision;
        result.Values = snapshot.Values;
        if (!m_Initialized || snapshot.Values.size() != m_Values.size())
        {
            result.FullUpload = true;
            if (!snapshot.Values.empty())
                result.DirtyRanges.push_back({0, snapshot.Values.size()});
        }
        else
        {
            std::size_t index = 0;
            while (index < snapshot.Values.size())
            {
                if (snapshot.Values[index] == m_Values[index])
                {
                    ++index;
                    continue;
                }
                const auto first = index;
                do
                    ++index;
                while (index < snapshot.Values.size() && snapshot.Values[index] != m_Values[index]);
                result.DirtyRanges.push_back({first, index - first});
            }
        }

        m_Revision = snapshot.Revision;
        m_Values = snapshot.Values;
        m_Initialized = true;
        return result;
    }

    void MaterialNumericUniformCache::Reset() noexcept
    {
        m_Revision = 0;
        m_Values.clear();
        m_Initialized = false;
    }
} // namespace Keire
