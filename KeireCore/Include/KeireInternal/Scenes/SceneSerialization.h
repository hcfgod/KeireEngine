#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/Scenes/SceneAsset.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace Keire::Detail
{
    [[nodiscard]] bool IsMeshMaterialSlotKey(std::string_view key) noexcept;

    [[nodiscard]] std::string EncodeComponentPropertyBag(const ComponentPropertyBag& bag);

    [[nodiscard]] ComponentPropertyBag DecodeComponentPropertyBag(std::string_view data,
                                                                  const ComponentRegistration& registration);

    [[nodiscard]] std::string EncodeLegacyTransform(const SceneTransform& transform);

    [[nodiscard]] std::string RemapManagedStateReferences(std::string_view state,
                                                          const std::unordered_map<EntityId, EntityId>& remapped);
} // namespace Keire::Detail
