#pragma once

#include "KeireClient/Editor/SceneDocument.h"

#include "Keire/ECS/Components/VfxEmitterComponent.h"

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireEditor
{
    [[nodiscard]] inline Keire::EntityId CreateDroppedVfxEmitter(SceneDocument& document, std::string name,
                                                                 const Keire::AssetId effect,
                                                                 const Keire::Vector3 position)
    {
        if (!effect)
            throw std::invalid_argument("A dropped VFX effect requires a valid asset ID.");

        const auto entity = document.CreateEntity(std::move(name), {}, Keire::VfxEmitterComponent::StaticType());
        try
        {
            document.SetTransform(entity, {.Position = position});
            document.SetComponentProperty(entity, Keire::VfxEmitterComponent::StaticType(), "effect", effect);
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            try
            {
                document.DeleteEntity(entity);
            }
            catch (...)
            {
                // Preserve the failure which caused the placement to be rolled back.
            }
            std::rethrow_exception(failure);
        }
        return entity;
    }
} // namespace KeireEditor
