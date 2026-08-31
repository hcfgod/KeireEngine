#include "KeireInternal/Ui/RuntimeUiStyleInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <ranges>
#include <stdexcept>

namespace Keire
{
    bool RuntimeUiRect::Contains(const float x, const float y) const noexcept
    {
        return !Empty() && x >= X && y >= Y && x <= X + Width && y <= Y + Height;
    }

    RuntimeUiRect RuntimeUiRect::Intersect(const RuntimeUiRect other) const noexcept
    {
        const auto left = std::max(X, other.X);
        const auto top = std::max(Y, other.Y);
        const auto right = std::min(X + Width, other.X + other.Width);
        const auto bottom = std::min(Y + Height, other.Y + other.Height);
        return {left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
    }
} // namespace Keire

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] bool Finite(const float value) noexcept { return std::isfinite(value); }

        void ValidateGradient(const RuntimeUiGradient& gradient)
        {
            if (!Finite(gradient.LinearAngleDegrees) || !Math::IsFinite(gradient.RadialCenter) ||
                gradient.RadialCenter.X < 0.0F || gradient.RadialCenter.X > 1.0F || gradient.RadialCenter.Y < 0.0F ||
                gradient.RadialCenter.Y > 1.0F || !Finite(gradient.RadialRadius) || gradient.RadialRadius <= 0.0F ||
                gradient.StopCount > 8)
                throw std::invalid_argument("Runtime UI gradient geometry is invalid.");
            if (gradient.Kind == RuntimeUiGradientKind::None)
            {
                if (gradient.StopCount != 0)
                    throw std::invalid_argument("Runtime UI solid backgrounds cannot contain gradient stops.");
                return;
            }
            if (gradient.Kind != RuntimeUiGradientKind::Linear && gradient.Kind != RuntimeUiGradientKind::Radial)
                throw std::invalid_argument("Runtime UI gradient kind is invalid.");
            if (gradient.StopCount < 2)
                throw std::invalid_argument("Runtime UI gradients require between two and eight stops.");
            float previous = -1.0F;
            for (std::size_t index = 0; index < gradient.StopCount; ++index)
            {
                const auto& stop = gradient.Stops[index];
                if (!Finite(stop.Offset) || stop.Offset < 0.0F || stop.Offset > 1.0F || stop.Offset < previous ||
                    !Math::IsFinite(stop.ColorValue))
                    throw std::invalid_argument("Runtime UI gradient stops must be finite, normalized, and sorted.");
                previous = stop.Offset;
            }
        }

        constexpr std::array TransitionProperties{RuntimeUiTransitionProperty::BackgroundColor,
                                                  RuntimeUiTransitionProperty::ForegroundColor,
                                                  RuntimeUiTransitionProperty::BorderColor,
                                                  RuntimeUiTransitionProperty::Opacity,
                                                  RuntimeUiTransitionProperty::Left,
                                                  RuntimeUiTransitionProperty::Top,
                                                  RuntimeUiTransitionProperty::Width,
                                                  RuntimeUiTransitionProperty::Height,
                                                  RuntimeUiTransitionProperty::BorderRadius};

        void ValidateTransitions(const RuntimeUiStyle& style)
        {
            if (style.TransitionPropertyCount > style.TransitionProperties.size() ||
                style.TransitionDurationCount > style.TransitionDurations.size() ||
                style.TransitionDelayCount > style.TransitionDelays.size() ||
                style.TransitionEasingCount > style.TransitionEasings.size())
                throw std::invalid_argument("Runtime UI transition count exceeds the limit of eight.");
            for (std::size_t index = 0; index < style.TransitionPropertyCount; ++index)
            {
                const auto property = style.TransitionProperties[index];
                if (property > RuntimeUiTransitionProperty::BorderRadius ||
                    std::ranges::find(style.TransitionProperties.begin(),
                                      style.TransitionProperties.begin() + static_cast<std::ptrdiff_t>(index),
                                      property) !=
                        style.TransitionProperties.begin() + static_cast<std::ptrdiff_t>(index))
                    throw std::invalid_argument("Runtime UI transition properties must be valid and unique.");
            }
            for (std::size_t index = 0; index < style.TransitionDurationCount; ++index)
                if (!Finite(style.TransitionDurations[index]) || style.TransitionDurations[index] < 0.0F ||
                    style.TransitionDurations[index] > 60.0F)
                    throw std::invalid_argument("Runtime UI transition durations must be between zero and 60 seconds.");
            for (std::size_t index = 0; index < style.TransitionDelayCount; ++index)
                if (!Finite(style.TransitionDelays[index]) || style.TransitionDelays[index] < 0.0F ||
                    style.TransitionDelays[index] > 60.0F)
                    throw std::invalid_argument("Runtime UI transition delays must be between zero and 60 seconds.");
        }

        void ValidateShadows(const RuntimeUiStyle& style)
        {
            if (style.BoxShadowCount > style.BoxShadows.size() || style.TextShadowCount > style.TextShadows.size())
                throw std::invalid_argument("Runtime UI shadow count exceeds the bounded style capacity.");
            const auto validate = [](const RuntimeUiShadow& shadow)
            {
                if (!Math::IsFinite(shadow.Offset) || !Finite(shadow.BlurRadius) || shadow.BlurRadius < 0.0F ||
                    shadow.BlurRadius > 512.0F || !Finite(shadow.SpreadRadius) || shadow.SpreadRadius < -512.0F ||
                    shadow.SpreadRadius > 512.0F || !Math::IsFinite(shadow.ColorValue))
                    throw std::invalid_argument("Runtime UI shadow geometry or color is invalid.");
            };
            for (std::size_t index = 0; index < style.BoxShadowCount; ++index)
                validate(style.BoxShadows[index]);
            for (std::size_t index = 0; index < style.TextShadowCount; ++index)
                validate(style.TextShadows[index]);
        }
    } // namespace

    void ValidateRuntimeUiInsets(const RuntimeUiInsets& value)
    {
        if (!Finite(value.Left) || !Finite(value.Top) || !Finite(value.Right) || !Finite(value.Bottom) ||
            value.Left < 0.0F || value.Top < 0.0F || value.Right < 0.0F || value.Bottom < 0.0F)
            throw std::invalid_argument("Runtime UI insets must be finite and non-negative.");
    }

    void ValidateRuntimeUiCanvasSettings(const RuntimeUiCanvasSettings& settings)
    {
        if (!Finite(settings.ReferenceWidth) || !Finite(settings.ReferenceHeight) || settings.ReferenceWidth <= 0.0F ||
            settings.ReferenceHeight <= 0.0F || !Finite(settings.MatchWidthOrHeight) ||
            settings.MatchWidthOrHeight < 0.0F || settings.MatchWidthOrHeight > 1.0F ||
            !Finite(settings.AccessibilityScale) || settings.AccessibilityScale < 0.5F ||
            settings.AccessibilityScale > 3.0F)
            throw std::invalid_argument("Runtime UI canvas settings are invalid.");
    }

    void ValidateRuntimeUiControl(const RuntimeUiControlState& control)
    {
        if (!Finite(control.Minimum) || !Finite(control.Maximum) || !Finite(control.Value) ||
            !Finite(control.ContentSize.X) || !Finite(control.ContentSize.Y) || control.Minimum > control.Maximum ||
            control.Value < control.Minimum || control.Value > control.Maximum || control.ContentSize.X < 0.0F ||
            control.ContentSize.Y < 0.0F || !Finite(control.ScrollSensitivity) || control.ScrollSensitivity <= 0.0F)
            throw std::invalid_argument("Runtime UI control state is invalid.");
    }

    float ResolveRuntimeUiPercent(const float percentage, const float extent, const float fallback) noexcept
    {
        return percentage >= 0.0F ? percentage * extent : fallback;
    }

    float ResolveRuntimeUiScale(const float viewportWidth, const float viewportHeight,
                                const RuntimeUiCanvasSettings& settings) noexcept
    {
        float scale = 1.0F;
        if (settings.ScaleMode == RuntimeUiScaleMode::ScaleWithViewport)
        {
            const auto widthScale = viewportWidth / settings.ReferenceWidth;
            const auto heightScale = viewportHeight / settings.ReferenceHeight;
            scale = std::exp(std::log(widthScale) * (1.0F - settings.MatchWidthOrHeight) +
                             std::log(heightScale) * settings.MatchWidthOrHeight);
        }
        return scale * settings.AccessibilityScale;
    }

    RuntimeUiRect ResolveRuntimeUiRoot(RuntimeUiRect viewport, const RuntimeUiInsets safeArea,
                                       const RuntimeUiCanvasSettings& settings) noexcept
    {
        if (settings.RespectSafeArea)
        {
            viewport.X += safeArea.Left;
            viewport.Y += safeArea.Top;
            viewport.Width = std::max(0.0F, viewport.Width - safeArea.Left - safeArea.Right);
            viewport.Height = std::max(0.0F, viewport.Height - safeArea.Top - safeArea.Bottom);
        }
        return viewport;
    }

    bool RuntimeUiStyleContainsPoint(const RuntimeUiElementState& state, float x, float y) noexcept
    {
        const auto& style = state.Style;
        const float translationX = style.Translation.X * state.LayoutScale;
        const float translationY = style.Translation.Y * state.LayoutScale;
        const float originX = state.Rect.X + state.Rect.Width * style.TransformOrigin.X;
        const float originY = state.Rect.Y + state.Rect.Height * style.TransformOrigin.Y;
        x -= originX + translationX;
        y -= originY + translationY;
        const float radians = -style.RotationDegrees * std::numbers::pi_v<float> / 180.0F;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const float transformedX = (x * cosine - y * sine) / style.TransformScale.X + originX;
        const float transformedY = (x * sine + y * cosine) / style.TransformScale.Y + originY;
        return state.Rect.Contains(transformedX, transformedY);
    }

    void ValidateRuntimeUiStyle(const RuntimeUiStyle& style)
    {
        const float values[] = {
            style.X,
            style.Y,
            style.Width,
            style.Height,
            style.XPercent,
            style.YPercent,
            style.WidthPercent,
            style.HeightPercent,
            style.AnchorMinimum.X,
            style.AnchorMinimum.Y,
            style.AnchorMaximum.X,
            style.AnchorMaximum.Y,
            style.Pivot.X,
            style.Pivot.Y,
            style.AnchoredPosition.X,
            style.AnchoredPosition.Y,
            style.SizeDelta.X,
            style.SizeDelta.Y,
            style.LocalScale.X,
            style.LocalScale.Y,
            style.MinimumWidth,
            style.MinimumHeight,
            style.MaximumWidth,
            style.MaximumHeight,
            style.MinimumWidthPercent,
            style.MinimumHeightPercent,
            style.MaximumWidthPercent,
            style.MaximumHeightPercent,
            style.FlexGrow,
            style.FlexShrink,
            style.Gap,
            style.BorderWidth,
            style.CornerRadius,
            style.CornerRadii.TopLeft,
            style.CornerRadii.TopRight,
            style.CornerRadii.BottomRight,
            style.CornerRadii.BottomLeft,
            style.BackgroundPosition.X,
            style.BackgroundPosition.Y,
            style.Translation.X,
            style.Translation.Y,
            style.TransformScale.X,
            style.TransformScale.Y,
            style.RotationDegrees,
            style.TransformOrigin.X,
            style.TransformOrigin.Y,
            style.Opacity,
            style.FontSize,
            style.LineHeight,
            style.LetterSpacing,
            style.WordSpacing,
            style.GridCellSize.X,
            style.GridCellSize.Y,
            style.ContentOffset.X,
            style.ContentOffset.Y,
        };
        if (!std::ranges::all_of(values, Finite) || style.Width < 0.0F || style.Height < 0.0F ||
            style.AnchorMinimum.X < 0.0F || style.AnchorMinimum.Y < 0.0F || style.AnchorMaximum.X > 1.0F ||
            style.AnchorMaximum.Y > 1.0F || style.AnchorMinimum.X > style.AnchorMaximum.X ||
            style.AnchorMinimum.Y > style.AnchorMaximum.Y || style.Pivot.X < 0.0F || style.Pivot.X > 1.0F ||
            style.Pivot.Y < 0.0F || style.Pivot.Y > 1.0F || style.LocalScale.X <= 0.0F || style.LocalScale.Y <= 0.0F ||
            style.GridCellSize.X < 0.0F || style.GridCellSize.Y < 0.0F || style.MinimumWidth < 0.0F ||
            style.MinimumHeight < 0.0F || style.MaximumWidth < style.MinimumWidth ||
            style.MaximumHeight < style.MinimumHeight || style.XPercent < -1.0F || style.XPercent > 1.0F ||
            style.YPercent < -1.0F || style.YPercent > 1.0F || style.WidthPercent < -1.0F ||
            style.WidthPercent > 1.0F || style.HeightPercent < -1.0F || style.HeightPercent > 1.0F ||
            style.MinimumWidthPercent < -1.0F || style.MinimumWidthPercent > 1.0F ||
            style.MinimumHeightPercent < -1.0F || style.MinimumHeightPercent > 1.0F ||
            style.MaximumWidthPercent < -1.0F || style.MaximumWidthPercent > 1.0F ||
            style.MaximumHeightPercent < -1.0F || style.MaximumHeightPercent > 1.0F ||
            (style.MinimumWidthPercent >= 0.0F && style.MaximumWidthPercent >= 0.0F &&
             style.MaximumWidthPercent < style.MinimumWidthPercent) ||
            (style.MinimumHeightPercent >= 0.0F && style.MaximumHeightPercent >= 0.0F &&
             style.MaximumHeightPercent < style.MinimumHeightPercent) ||
            style.FlexGrow < 0.0F || style.FlexShrink < 0.0F || style.Gap < 0.0F || style.BorderWidth < 0.0F ||
            style.CornerRadius < 0.0F || style.Opacity < 0.0F || style.Opacity > 1.0F || style.FontSize <= 0.0F ||
            style.CornerRadii.TopLeft < 0.0F || style.CornerRadii.TopRight < 0.0F ||
            style.CornerRadii.BottomRight < 0.0F || style.CornerRadii.BottomLeft < 0.0F ||
            style.BackgroundPosition.X < 0.0F || style.BackgroundPosition.X > 1.0F ||
            style.BackgroundPosition.Y < 0.0F || style.BackgroundPosition.Y > 1.0F || style.TransformScale.X <= 0.0F ||
            style.TransformScale.Y <= 0.0F || style.TransformOrigin.X < 0.0F || style.TransformOrigin.X > 1.0F ||
            style.TransformOrigin.Y < 0.0F || style.TransformOrigin.Y > 1.0F || style.FontWeight < 1 ||
            style.FontWeight > 1000 || style.LineHeight < 0.0F || style.LineHeight > 4096.0F ||
            style.LetterSpacing < -1024.0F || style.LetterSpacing > 1024.0F || style.WordSpacing < -1024.0F ||
            style.WordSpacing > 1024.0F || style.ContentOffset.X < 0.0F || style.ContentOffset.Y < 0.0F ||
            style.NavigationOrder < 0)
            throw std::invalid_argument("Runtime UI style contains invalid dimensions.");
        ValidateRuntimeUiInsets(style.Margin);
        ValidateRuntimeUiInsets(style.Padding);
        ValidateRuntimeUiInsets(style.BorderWidths);
        ValidateRuntimeUiInsets(style.BackgroundSlice);
        if (!Math::IsFinite(style.BackgroundTint) || !Math::IsFinite(style.BorderColors.Left) ||
            !Math::IsFinite(style.BorderColors.Top) || !Math::IsFinite(style.BorderColors.Right) ||
            !Math::IsFinite(style.BorderColors.Bottom))
            throw std::invalid_argument("Runtime UI style contains an invalid color.");
        ValidateGradient(style.BackgroundGradient);
        ValidateShadows(style);
        ValidateTransitions(style);
    }

    std::span<const RuntimeUiTransitionProperty> RuntimeUiTransitionProperties() noexcept
    {
        return TransitionProperties;
    }

    Color ApplyRuntimeUiOpacity(Color color, const float opacity) noexcept
    {
        color.Alpha *= opacity;
        return color;
    }

    RuntimeUiGradient ApplyRuntimeUiOpacity(RuntimeUiGradient gradient, const float opacity) noexcept
    {
        for (std::size_t index = 0; index < gradient.StopCount; ++index)
            gradient.Stops[index].ColorValue.Alpha *= opacity;
        return gradient;
    }

    bool CanInterpolateRuntimeUiProperty(const RuntimeUiTransitionProperty property, const RuntimeUiStyle& from,
                                         const RuntimeUiStyle& to) noexcept
    {
        switch (property)
        {
        case RuntimeUiTransitionProperty::Left:
            return (from.XPercent >= 0.0F) == (to.XPercent >= 0.0F);
        case RuntimeUiTransitionProperty::Top:
            return (from.YPercent >= 0.0F) == (to.YPercent >= 0.0F);
        case RuntimeUiTransitionProperty::Width:
            return (from.WidthPercent >= 0.0F) == (to.WidthPercent >= 0.0F);
        case RuntimeUiTransitionProperty::Height:
            return (from.HeightPercent >= 0.0F) == (to.HeightPercent >= 0.0F);
        default:
            return property != RuntimeUiTransitionProperty::All;
        }
    }

    bool RuntimeUiPropertyEqual(const RuntimeUiTransitionProperty property, const RuntimeUiStyle& left,
                                const RuntimeUiStyle& right) noexcept
    {
        switch (property)
        {
        case RuntimeUiTransitionProperty::BackgroundColor:
            return left.Background == right.Background;
        case RuntimeUiTransitionProperty::ForegroundColor:
            return left.Foreground == right.Foreground;
        case RuntimeUiTransitionProperty::BorderColor:
            return left.Border == right.Border;
        case RuntimeUiTransitionProperty::Opacity:
            return left.Opacity == right.Opacity;
        case RuntimeUiTransitionProperty::Left:
            return left.X == right.X && left.XPercent == right.XPercent;
        case RuntimeUiTransitionProperty::Top:
            return left.Y == right.Y && left.YPercent == right.YPercent;
        case RuntimeUiTransitionProperty::Width:
            return left.Width == right.Width && left.WidthPercent == right.WidthPercent;
        case RuntimeUiTransitionProperty::Height:
            return left.Height == right.Height && left.HeightPercent == right.HeightPercent;
        case RuntimeUiTransitionProperty::BorderRadius:
            return left.CornerRadius == right.CornerRadius;
        case RuntimeUiTransitionProperty::All:
            return true;
        }
        return true;
    }

    void InterpolateRuntimeUiProperty(RuntimeUiStyle& result, const RuntimeUiStyle& from, const RuntimeUiStyle& to,
                                      const RuntimeUiTransitionProperty property, const float alpha) noexcept
    {
        const auto scalar = [alpha](const float left, const float right) { return left + (right - left) * alpha; };
        switch (property)
        {
        case RuntimeUiTransitionProperty::BackgroundColor:
            result.Background = Math::Lerp(from.Background, to.Background, alpha);
            break;
        case RuntimeUiTransitionProperty::ForegroundColor:
            result.Foreground = Math::Lerp(from.Foreground, to.Foreground, alpha);
            break;
        case RuntimeUiTransitionProperty::BorderColor:
            result.Border = Math::Lerp(from.Border, to.Border, alpha);
            break;
        case RuntimeUiTransitionProperty::Opacity:
            result.Opacity = scalar(from.Opacity, to.Opacity);
            break;
        case RuntimeUiTransitionProperty::Left:
            result.X = scalar(from.X, to.X);
            result.XPercent = scalar(from.XPercent, to.XPercent);
            break;
        case RuntimeUiTransitionProperty::Top:
            result.Y = scalar(from.Y, to.Y);
            result.YPercent = scalar(from.YPercent, to.YPercent);
            break;
        case RuntimeUiTransitionProperty::Width:
            result.Width = scalar(from.Width, to.Width);
            result.WidthPercent = scalar(from.WidthPercent, to.WidthPercent);
            break;
        case RuntimeUiTransitionProperty::Height:
            result.Height = scalar(from.Height, to.Height);
            result.HeightPercent = scalar(from.HeightPercent, to.HeightPercent);
            break;
        case RuntimeUiTransitionProperty::BorderRadius:
            result.CornerRadius = scalar(from.CornerRadius, to.CornerRadius);
            break;
        case RuntimeUiTransitionProperty::All:
            break;
        }
    }

    std::optional<float> RuntimeUiTransitionDuration(const RuntimeUiStyle& style,
                                                     const RuntimeUiTransitionProperty property) noexcept
    {
        if (style.TransitionDurationCount == 0)
            return std::nullopt;
        std::optional<float> result;
        for (std::size_t index = 0; index < style.TransitionPropertyCount; ++index)
        {
            if (style.TransitionProperties[index] != property &&
                style.TransitionProperties[index] != RuntimeUiTransitionProperty::All)
                continue;
            result = style.TransitionDurations[index % style.TransitionDurationCount];
        }
        return result;
    }

    float RuntimeUiTransitionDelay(const RuntimeUiStyle& style, const RuntimeUiTransitionProperty property) noexcept
    {
        if (style.TransitionDelayCount == 0)
            return 0.0F;
        float result = 0.0F;
        for (std::size_t index = 0; index < style.TransitionPropertyCount; ++index)
            if (style.TransitionProperties[index] == property ||
                style.TransitionProperties[index] == RuntimeUiTransitionProperty::All)
                result = style.TransitionDelays[index % style.TransitionDelayCount];
        return result;
    }

    RuntimeUiTransitionEasing RuntimeUiTransitionEasingFor(const RuntimeUiStyle& style,
                                                           const RuntimeUiTransitionProperty property) noexcept
    {
        if (style.TransitionEasingCount == 0)
            return RuntimeUiTransitionEasing::Linear;
        auto result = RuntimeUiTransitionEasing::Linear;
        for (std::size_t index = 0; index < style.TransitionPropertyCount; ++index)
            if (style.TransitionProperties[index] == property ||
                style.TransitionProperties[index] == RuntimeUiTransitionProperty::All)
                result = style.TransitionEasings[index % style.TransitionEasingCount];
        return result;
    }

    float ApplyRuntimeUiTransitionEasing(const RuntimeUiTransitionEasing easing, const float alpha) noexcept
    {
        const float value = std::clamp(alpha, 0.0F, 1.0F);
        switch (easing)
        {
        case RuntimeUiTransitionEasing::Linear:
            return value;
        case RuntimeUiTransitionEasing::Ease:
        case RuntimeUiTransitionEasing::EaseInOut:
            return value * value * (3.0F - 2.0F * value);
        case RuntimeUiTransitionEasing::EaseIn:
            return value * value;
        case RuntimeUiTransitionEasing::EaseOut:
            return 1.0F - (1.0F - value) * (1.0F - value);
        }
        return value;
    }
} // namespace Keire::Detail
