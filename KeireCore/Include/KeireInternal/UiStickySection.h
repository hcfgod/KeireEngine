#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace Keire::Detail
{
    [[nodiscard]] inline std::optional<float> StickySectionTop(const float start, const float end,
                                                               const float viewportTop, const float height) noexcept
    {
        if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(viewportTop) || !std::isfinite(height) ||
            height <= 0.0F || start >= viewportTop || end <= viewportTop || end <= start)
            return std::nullopt;
        return std::min(viewportTop, end - height);
    }
} // namespace Keire::Detail
