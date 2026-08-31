#pragma once

#include "Keire/Ui/RuntimeUi.h"

#include <optional>
#include <span>

namespace Keire::Detail
{
    void ValidateRuntimeUiInsets(const RuntimeUiInsets& value);
    void ValidateRuntimeUiStyle(const RuntimeUiStyle& style);
    void ValidateRuntimeUiCanvasSettings(const RuntimeUiCanvasSettings& settings);
    void ValidateRuntimeUiControl(const RuntimeUiControlState& control);
    [[nodiscard]] float ResolveRuntimeUiPercent(float percentage, float extent, float fallback) noexcept;
    [[nodiscard]] float ResolveRuntimeUiScale(float viewportWidth, float viewportHeight,
                                              const RuntimeUiCanvasSettings& settings) noexcept;
    [[nodiscard]] RuntimeUiRect ResolveRuntimeUiRoot(RuntimeUiRect viewport, RuntimeUiInsets safeArea,
                                                     const RuntimeUiCanvasSettings& settings) noexcept;
    [[nodiscard]] bool RuntimeUiStyleContainsPoint(const RuntimeUiElementState& state, float x, float y) noexcept;
    [[nodiscard]] Color ApplyRuntimeUiOpacity(Color color, float opacity) noexcept;
    [[nodiscard]] RuntimeUiGradient ApplyRuntimeUiOpacity(RuntimeUiGradient gradient, float opacity) noexcept;
    [[nodiscard]] std::span<const RuntimeUiTransitionProperty> RuntimeUiTransitionProperties() noexcept;
    [[nodiscard]] bool CanInterpolateRuntimeUiProperty(RuntimeUiTransitionProperty property, const RuntimeUiStyle& from,
                                                       const RuntimeUiStyle& to) noexcept;
    [[nodiscard]] bool RuntimeUiPropertyEqual(RuntimeUiTransitionProperty property, const RuntimeUiStyle& left,
                                              const RuntimeUiStyle& right) noexcept;
    void InterpolateRuntimeUiProperty(RuntimeUiStyle& result, const RuntimeUiStyle& from, const RuntimeUiStyle& to,
                                      RuntimeUiTransitionProperty property, float alpha) noexcept;
    [[nodiscard]] std::optional<float> RuntimeUiTransitionDuration(const RuntimeUiStyle& style,
                                                                   RuntimeUiTransitionProperty property) noexcept;
    [[nodiscard]] float RuntimeUiTransitionDelay(const RuntimeUiStyle& style,
                                                 RuntimeUiTransitionProperty property) noexcept;
    [[nodiscard]] RuntimeUiTransitionEasing RuntimeUiTransitionEasingFor(const RuntimeUiStyle& style,
                                                                         RuntimeUiTransitionProperty property) noexcept;
    [[nodiscard]] float ApplyRuntimeUiTransitionEasing(RuntimeUiTransitionEasing easing, float alpha) noexcept;
} // namespace Keire::Detail
