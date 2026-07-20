#pragma once

#include "Keire/Core.h"

#include <functional>
#include <optional>

namespace KeireEditor
{
    using MeshBoundsResolver = std::function<std::optional<Keire::MeshBounds>(Keire::AssetId)>;

    [[nodiscard]] Keire::EntityId PickSceneEntity(const Keire::Ref<Keire::Scene>& scene, Keire::UiItemRect viewport,
                                                  Keire::UiPosition pointer, const Keire::RenderCamera& camera,
                                                  const MeshBoundsResolver& resolveMeshBounds = {});
} // namespace KeireEditor
