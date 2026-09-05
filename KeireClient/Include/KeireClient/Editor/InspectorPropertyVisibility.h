#pragma once

#include "Keire/ECS/Component.h"

#include <string_view>

namespace KeireEditor
{
    [[nodiscard]] bool IsInspectorPropertyVisible(Keire::ComponentTypeId component, std::string_view property) noexcept;
    [[nodiscard]] bool IsMeshRendererBakedLightingProperty(std::string_view property) noexcept;
} // namespace KeireEditor
