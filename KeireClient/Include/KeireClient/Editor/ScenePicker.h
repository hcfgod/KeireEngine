#pragma once

#include "Keire/Core.h"

#include <functional>
#include <optional>
#include <vector>

namespace KeireEditor
{
    using MeshBoundsResolver = std::function<std::optional<Keire::MeshBounds>(Keire::AssetId)>;

    [[nodiscard]] Keire::EntityId PickSceneEntity(const Keire::Ref<Keire::Scene>& scene, Keire::UiItemRect viewport,
                                                  Keire::UiPosition pointer, const Keire::RenderCamera& camera,
                                                  const MeshBoundsResolver& resolveMeshBounds = {});
    [[nodiscard]] std::vector<Keire::EntityId>
    SelectSceneEntitiesInRectangle(const Keire::Ref<Keire::Scene>& scene, Keire::UiItemRect viewport,
                                   Keire::UiItemRect selection, const Keire::RenderCamera& camera,
                                   const MeshBoundsResolver& resolveMeshBounds = {});
} // namespace KeireEditor
