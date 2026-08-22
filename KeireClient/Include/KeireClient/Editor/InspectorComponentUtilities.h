#pragma once

#include "Keire/Core.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace KeireEditor
{
    struct ComponentOrderPayload final
    {
        Keire::EntityId Entity;
        Keire::ComponentTypeId Type;
        std::size_t Ordinal = 0;
    };

    [[nodiscard]] std::string EncodeComponentOrderPayload(const ComponentOrderPayload& value);
    [[nodiscard]] std::optional<ComponentOrderPayload>
    DecodeComponentOrderPayload(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] std::size_t ComponentOrdinal(std::span<const Keire::Ref<Keire::Component>> components,
                                               const Keire::Ref<Keire::Component>& component) noexcept;
    [[nodiscard]] Keire::Ref<Keire::Component>
    ResolveComponentOrderPayload(std::span<const Keire::Ref<Keire::Component>> components,
                                 Keire::EntityId expectedEntity, const ComponentOrderPayload& payload) noexcept;
    [[nodiscard]] bool ColliderPropertyVisible(Keire::ColliderShape shape, std::string_view property) noexcept;
} // namespace KeireEditor
