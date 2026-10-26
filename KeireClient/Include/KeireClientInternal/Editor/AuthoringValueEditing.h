#pragma once

#include "Keire/Math/Curves.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace KeireEditor::Detail
{
    template <typename Key>
    [[nodiscard]] std::optional<float> FindAuthoringKeyTime(const std::span<const Key> keys, const float minimum,
                                                            const float maximum, const std::size_t capacity)
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum)
        {
            throw std::invalid_argument("Authoring key time range must be finite and increasing.");
        }
        if (keys.size() >= capacity)
        {
            return std::nullopt;
        }
        if (keys.empty() || keys.front().Time > minimum)
        {
            return minimum;
        }
        if (keys.back().Time < maximum)
        {
            return maximum;
        }

        std::optional<float> selected;
        double widestGap = 0.0;
        for (std::size_t index = 1; index < keys.size(); ++index)
        {
            const float left = std::max(keys[index - 1].Time, minimum);
            const float right = std::min(keys[index].Time, maximum);
            const float middle = std::midpoint(left, right);
            const double gap = static_cast<double>(right) - left;
            if (middle > left && middle < right && gap > widestGap)
            {
                selected = middle;
                widestGap = gap;
            }
        }
        return selected;
    }

    [[nodiscard]] inline bool InsertAuthoringCurveKey(Keire::Curve1D& value, const float minimum = 0.0F,
                                                      const float maximum = 1.0F)
    {
        const auto time = FindAuthoringKeyTime(value.Keys(), minimum, maximum, Keire::Curve1D::MaximumKeys);
        if (!time)
        {
            return false;
        }
        const float sample = value.Evaluate(*time);
        if (!std::isfinite(sample))
        {
            return false;
        }
        std::vector<Keire::CurveKey> keys(value.Keys().begin(), value.Keys().end());
        const auto position = std::ranges::lower_bound(keys, *time, {}, &Keire::CurveKey::Time);
        keys.insert(position, {.Time = *time, .Value = sample});
        value.SetKeys(std::move(keys));
        return true;
    }

    [[nodiscard]] inline bool InsertAuthoringGradientKey(Keire::ColorGradient& value)
    {
        const auto time = FindAuthoringKeyTime(value.Keys(), 0.0F, 1.0F, Keire::ColorGradient::MaximumKeys);
        if (!time)
        {
            return false;
        }
        const auto sample = value.Evaluate(*time);
        if (!Keire::Math::IsFinite(sample))
        {
            return false;
        }
        std::vector<Keire::ColorGradientKey> keys(value.Keys().begin(), value.Keys().end());
        const auto position = std::ranges::lower_bound(keys, *time, {}, &Keire::ColorGradientKey::Time);
        keys.insert(position, {.Time = *time, .Value = sample});
        value.SetKeys(std::move(keys));
        return true;
    }
} // namespace KeireEditor::Detail
