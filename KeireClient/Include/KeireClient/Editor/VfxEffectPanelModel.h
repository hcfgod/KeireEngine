#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AuthoringWidgets.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace KeireEditor
{
    namespace Detail
    {
        [[nodiscard]] std::string_view ModuleName(const Keire::VfxModulePayload& payload);
        [[nodiscard]] std::size_t BurstCount(const Keire::VfxEffectDefinition& definition);
        [[nodiscard]] StableNodeId PreferredCanvasId(Keire::AssetId id, std::uint64_t salt) noexcept;
        [[nodiscard]] Keire::UiColor ContextColor(Keire::VfxContextType context) noexcept;
        [[nodiscard]] Keire::UiColor NodeColor(const Keire::VfxGraphNode& node) noexcept;
        [[nodiscard]] Keire::UiColor PinColor(Keire::VfxValueType type) noexcept;
        [[nodiscard]] Keire::VfxGraphNode NewContextNode(Keire::VfxContextType context, Keire::Vector2 position);
        [[nodiscard]] Keire::VfxGraphNode NewParameterNode(const Keire::VfxBlackboardParameter& parameter,
                                                           Keire::Vector2 position);
        [[nodiscard]] Keire::VfxGraphNode NewCustomHlslNode(Keire::Vector2 position);
        [[nodiscard]] std::string NodeLabel(const Keire::VfxEffectDefinition& definition,
                                            const Keire::VfxGraphNode& node);
        [[nodiscard]] bool ModuleRunsInContext(const Keire::VfxModulePayload& payload, Keire::VfxContextType context);
    } // namespace Detail
} // namespace KeireEditor
