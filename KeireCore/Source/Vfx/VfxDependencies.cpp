#include "Keire/Vfx/VfxSystem.h"

#include <algorithm>
#include <ranges>
#include <span>
#include <variant>

namespace Keire
{
    VfxEffectDefinition MigrateVfxEffectToSchema4(const VfxEffectDefinition& definition)
    {
        return MigrateVfxEffectToCurrentSchema(definition);
    }

    namespace
    {
        template <class... Types> struct Overloaded : Types...
        {
            using Types::operator()...;
        };
    } // namespace

    std::vector<AssetId> VfxEffectDependencies(const VfxEffectDefinition& definition)
    {
        ValidateVfxEffect(definition);
        std::vector<AssetId> result;
        const auto appendValue = [&result](const auto& value)
        {
            if (const auto* asset = std::get_if<AssetId>(&value); asset && *asset)
                result.push_back(*asset);
        };
        const auto appendProperties = [&result](const std::span<const VfxGraphProperty> properties)
        {
            for (const auto& property : properties)
                if (const auto* asset = std::get_if<AssetId>(&property.Value); asset && *asset)
                    result.push_back(*asset);
        };
        for (const auto& module : definition.Modules)
        {
            std::visit(
                Overloaded{
                    [&result](const VfxShapeModule& value)
                    {
                        if (value.Shape == VfxShape::Mesh && value.Mesh)
                            result.push_back(value.Mesh);
                        if (value.Shape == VfxShape::Volume && value.Volume)
                            result.push_back(value.Volume);
                    },
                    [&result](const VfxRendererModule& value)
                    {
                        if (value.Type != VfxRendererType::Mesh && value.Sprite)
                            result.push_back(value.Sprite);
                        if (value.Type == VfxRendererType::Mesh && value.Mesh)
                            result.push_back(value.Mesh);
                        if (value.Material)
                            result.push_back(value.Material);
                    },
                    [](const auto&) {},
                },
                module.Payload);
        }
        for (const auto& parameter : definition.Blackboard)
            appendValue(parameter.DefaultValue);
        for (const auto& system : definition.Systems)
        {
            for (const auto& node : system.Nodes)
            {
                if (node.Kind == VfxGraphNodeKind::Subgraph && node.Reference)
                    result.push_back(node.Reference);
                appendProperties(node.Properties);
                for (const auto& pin : node.Pins)
                    if (pin.DefaultValue)
                        appendValue(*pin.DefaultValue);
                for (const auto& block : node.Blocks)
                {
                    if (block.TypeId.View() == "keire.block.subgraph" && block.Reference)
                        result.push_back(block.Reference);
                    appendProperties(block.Properties);
                    for (const auto& pin : block.Pins)
                        if (pin.DefaultValue)
                            appendValue(*pin.DefaultValue);
                }
            }
        }
        std::ranges::sort(result);
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
} // namespace Keire
