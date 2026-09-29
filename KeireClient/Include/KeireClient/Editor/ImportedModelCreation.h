#pragma once

#include "KeireClient/Editor/ImportedModelAnimation.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <exception>
#include <string>
#include <utility>

namespace KeireEditor
{
    [[nodiscard]] inline Keire::EntityId CreateImportedModel(SceneDocument& document, std::string name,
                                                             const Keire::AssetId mesh, const Keire::Vector3 position,
                                                             const ImportedModelAnimation& animation)
    {
        const auto entity = document.CreateEntity(std::move(name), {}, Keire::MeshRendererComponent::StaticType());
        try
        {
            document.SetTransform(entity, {.Position = position});
            document.SetComponentProperty(entity, Keire::MeshRendererComponent::StaticType(), "mesh", mesh);
            document.SetComponentProperty(entity, Keire::MeshRendererComponent::StaticType(), "tint",
                                          Keire::Color{1.0F, 1.0F, 1.0F, 1.0F});
            animation.Apply(document.ActiveScene()->FindEntity(entity));
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
