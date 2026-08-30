#pragma once

#include "Keire/Ui/RuntimeUi.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Keire::Detail
{
    struct RuntimeUiTreeNode final
    {
        struct ActiveTransition final
        {
            RuntimeUiTransitionProperty Property = RuntimeUiTransitionProperty::Opacity;
            float ElapsedSeconds = 0.0F;
            float DurationSeconds = 0.0F;
        };

        std::uint32_t Generation = 1;
        bool Alive = false;
        bool Dirty = true;
        bool HasStyle = false;
        RuntimeUiElementState State;
        RuntimeUiStyle TransitionStartStyle;
        RuntimeUiStyle TargetStyle;
        std::vector<ActiveTransition> ActiveTransitions;
        std::vector<RuntimeUiElementId> Children;
        std::optional<RuntimeUiCanvasSettings> RootCanvasSettings;
    };

    [[nodiscard]] Vector2 MeasureRuntimeUiIntrinsic(std::span<const RuntimeUiTreeNode> nodes, std::size_t index,
                                                    RuntimeUiRect available, float scale);
} // namespace Keire::Detail
