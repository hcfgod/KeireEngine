#pragma once

#include "Keire/Core.h"

#include <functional>
#include <optional>
#include <vector>

namespace KeireEditor
{
    using MeshBoundsResolver = std::function<std::optional<Keire::MeshBounds>(Keire::AssetId)>;

    struct SceneEntityBounds
    {
        Keire::Vector3 Minimum{};
        Keire::Vector3 Maximum{};
        bool Valid = false;

        [[nodiscard]] Keire::Vector3 Center() const noexcept;
        [[nodiscard]] float Radius() const noexcept;
    };

    [[nodiscard]] SceneEntityBounds CalculateSceneEntityBounds(const Keire::Entity& entity,
                                                               const MeshBoundsResolver& resolveMeshBounds = {});
    [[nodiscard]] SceneEntityBounds CalculateSceneEntityBounds(std::span<const Keire::Entity> entities,
                                                               const MeshBoundsResolver& resolveMeshBounds = {});

    [[nodiscard]] Keire::EntityId PickSceneEntity(const Keire::Ref<Keire::Scene>& scene, Keire::UiItemRect viewport,
                                                  Keire::UiPosition pointer, const Keire::RenderCamera& camera,
                                                  const MeshBoundsResolver& resolveMeshBounds = {});
    [[nodiscard]] std::vector<Keire::EntityId>
    SelectSceneEntitiesInRectangle(const Keire::Ref<Keire::Scene>& scene, Keire::UiItemRect viewport,
                                   Keire::UiItemRect selection, const Keire::RenderCamera& camera,
                                   const MeshBoundsResolver& resolveMeshBounds = {});
} // namespace KeireEditor
