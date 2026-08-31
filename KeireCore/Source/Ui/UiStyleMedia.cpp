#include "Keire/Ui/UiToolkit.h"

#include <algorithm>
#include <cmath>

namespace Keire
{
    bool UiStyleMediaCondition::Empty() const noexcept
    {
        return !MinimumWidth && !MaximumWidth && !MinimumHeight && !MaximumHeight && !MinimumAspectRatio &&
               !MaximumAspectRatio && !MinimumDpi && !MaximumDpi && Orientation == UiStyleOrientation::Any &&
               Pointer == UiStylePointerPrecision::Any && Navigation == UiStyleNavigationMode::Any && !ReducedMotion;
    }

    bool MatchesUiStyleMediaCondition(const UiStyleMediaCondition& condition,
                                      const UiStyleEvaluationContext& context) noexcept
    {
        if (!std::isfinite(context.Width) || !std::isfinite(context.Height) || !std::isfinite(context.Dpi) ||
            context.Width <= 0.0F || context.Height <= 0.0F || context.Dpi <= 0.0F)
            return false;
        const auto below = [](const std::optional<float> limit, const float value)
        { return !limit || value >= *limit; };
        const auto above = [](const std::optional<float> limit, const float value)
        { return !limit || value <= *limit; };
        const float aspect = context.Width / context.Height;
        if (!below(condition.MinimumWidth, context.Width) || !above(condition.MaximumWidth, context.Width) ||
            !below(condition.MinimumHeight, context.Height) || !above(condition.MaximumHeight, context.Height) ||
            !below(condition.MinimumAspectRatio, aspect) || !above(condition.MaximumAspectRatio, aspect) ||
            !below(condition.MinimumDpi, context.Dpi) || !above(condition.MaximumDpi, context.Dpi))
            return false;
        if (condition.Orientation == UiStyleOrientation::Landscape && context.Width < context.Height)
            return false;
        if (condition.Orientation == UiStyleOrientation::Portrait && context.Width >= context.Height)
            return false;
        if (condition.Pointer != UiStylePointerPrecision::Any && condition.Pointer != context.Pointer)
            return false;
        if (condition.Navigation != UiStyleNavigationMode::Any && condition.Navigation != context.Navigation)
            return false;
        return !condition.ReducedMotion || *condition.ReducedMotion == context.ReducedMotion;
    }
} // namespace Keire
