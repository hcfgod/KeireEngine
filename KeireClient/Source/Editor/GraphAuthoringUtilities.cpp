#include "KeireClient/Editor/AuthoringWidgets.h"

#include <algorithm>
#include <ranges>

namespace KeireEditor
{
    bool ShaderGraphPinsCanConnect(const Keire::ShaderGraphPin& first, const Keire::ShaderGraphPin& second) noexcept
    {
        if (first.Direction == second.Direction)
            return false;
        const auto& output = first.Direction == Keire::ShaderGraphPinDirection::Output ? first : second;
        const auto& input = first.Direction == Keire::ShaderGraphPinDirection::Input ? first : second;
        return output.Type == input.Type ||
               ((output.Type == Keire::ShaderGraphValueType::Color &&
                 input.Type == Keire::ShaderGraphValueType::Vector4) ||
                (output.Type == Keire::ShaderGraphValueType::Vector4 &&
                 input.Type == Keire::ShaderGraphValueType::Color) ||
                ((output.Type == Keire::ShaderGraphValueType::Vector4 ||
                  output.Type == Keire::ShaderGraphValueType::Color) &&
                 input.Type == Keire::ShaderGraphValueType::Vector3) ||
                (output.Type == Keire::ShaderGraphValueType::Vector3 &&
                 (input.Type == Keire::ShaderGraphValueType::Vector4 ||
                  input.Type == Keire::ShaderGraphValueType::Color))) ||
               (output.Type == Keire::ShaderGraphValueType::Scalar &&
                input.Type != Keire::ShaderGraphValueType::Texture2D &&
                input.Type != Keire::ShaderGraphValueType::MaterialAttributes &&
                input.Type != Keire::ShaderGraphValueType::Bsdf);
    }

    bool ShaderGraphNodesCanConnect(const Keire::ShaderGraphNode& first, const Keire::ShaderGraphNode& second) noexcept
    {
        return std::ranges::any_of(first.Pins,
                                   [&](const Keire::ShaderGraphPin& firstPin)
                                   {
                                       return std::ranges::any_of(
                                           second.Pins, [&](const Keire::ShaderGraphPin& secondPin)
                                           { return ShaderGraphPinsCanConnect(firstPin, secondPin); });
                                   });
    }

    StableNodeId StableNodeGraphIdMap::Assign(const Keire::AssetId source, StableNodeId preferred)
    {
        if (const auto existing = std::ranges::find(m_Assignments, source, &decltype(m_Assignments)::value_type::first);
            existing != m_Assignments.end())
            return existing->second;

        if (preferred == 0)
            preferred = 1;
        while (std::ranges::find(m_Used, preferred) != m_Used.end())
        {
            ++preferred;
            if (preferred == 0)
                preferred = 1;
        }
        m_Assignments.emplace_back(source, preferred);
        m_Used.push_back(preferred);
        return preferred;
    }

    std::optional<StableNodeId> StableNodeGraphIdMap::Find(const Keire::AssetId source) const noexcept
    {
        const auto found = std::ranges::find(m_Assignments, source, &decltype(m_Assignments)::value_type::first);
        if (found == m_Assignments.end())
            return std::nullopt;
        return found->second;
    }
} // namespace KeireEditor
