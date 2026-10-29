#pragma once

#include "Keire/Core.h"

#include <functional>
#include <optional>
#include <vector>

namespace KeireEditor
{
    using MeshBoundsResolver = std::function<std::optional<Keire::MeshBounds>(Keire::AssetId)>;

    using PoseBoundsResolver = std::function<std::optional<Keire::MeshBounds>(const Keire::Entity&)>;

    struct SceneEntityBounds
    {
        Keire::Vector3d Minimum{};
        Keire::Vector3d Maximum{};
        bool Valid = false;
        Keire::Vector3d WorldOrigin{};

        [[nodiscard]] Keire::Vector3 Center() const noexcept;
        [[nodiscard]] Keire::Vector3d CenterPrecise() const noexcept;
        [[nodiscard]] float Radius() const noexcept;
    };

    [[nodiscard]] SceneEntityBounds CalculateSceneEntityBounds(const Keire::Entity& entity,
                                                               const MeshBoundsResolver& resolveMeshBounds = {},
                                                               const PoseBoundsResolver& resolvePoseBounds = {},
                                                               Keire::Vector3d worldOrigin = {});
    [[nodiscard]] SceneEntityBounds CalculateSceneEntityBounds(std::span<const Keire::Entity> entities,
                                                               const MeshBoundsResolver& resolveMeshBounds = {},
                                                               const PoseBoundsResolver& resolvePoseBounds = {},
                                                               Keire::Vector3d worldOrigin = {});

    [[nodiscard]] Keire::EntityId PickSceneEntity(const Keire::Ref<Keire::Scene>& scene, Keire::UiItemRect viewport,
                                                  Keire::UiPosition pointer, const Keire::RenderCamera& camera,
                                                  const MeshBoundsResolver& resolveMeshBounds = {},
                                                  const PoseBoundsResolver& resolvePoseBounds = {});
    [[nodiscard]] std::vector<Keire::EntityId>
    SelectSceneEntitiesInRectangle(const Keire::Ref<Keire::Scene>& scene, Keire::UiItemRect viewport,
                                   Keire::UiItemRect selection, const Keire::RenderCamera& camera,
                                   const MeshBoundsResolver& resolveMeshBounds = {},
                                   const PoseBoundsResolver& resolvePoseBounds = {});
    [[nodiscard]] std::vector<Keire::EntityId> ResolveMaterialDropTargets(const Keire::Entity& target);
    // Intersects the Y-up ground plane; horizon/upward rays use a plane ten units in front of the camera.
    [[nodiscard]] Keire::Vector3d ResolveSceneDropPositionPrecise(Keire::UiItemRect viewport, Keire::UiPosition pointer,
                                                                  const Keire::RenderCamera& camera);
    [[nodiscard]] Keire::Vector3 ResolveSceneDropPosition(Keire::UiItemRect viewport, Keire::UiPosition pointer,
                                                          const Keire::RenderCamera& camera);
} // namespace KeireEditor
