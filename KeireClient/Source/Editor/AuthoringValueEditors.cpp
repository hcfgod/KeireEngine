#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClientInternal/Editor/AuthoringValueEditing.h"
#include "KeireClientInternal/Editor/InspectorFieldLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        template <typename Enum, std::size_t Size>
        [[nodiscard]] bool DrawInterpolation(Keire::UiFrame& ui, Enum& value,
                                             const std::array<std::string_view, Size>& names)
        {
            bool changed = false;
            if (auto combo = ui.BeginCombo("Interpolation", names.at(static_cast<std::size_t>(value))); combo)
            {
                for (std::size_t index = 0; index < names.size(); ++index)
                {
                    const auto candidate = static_cast<Enum>(index);
                    if (ui.Selectable(names[index], value == candidate) && value != candidate)
                    {
                        value = candidate;
                        changed = true;
                    }
                }
            }
            return changed;
        }

        [[nodiscard]] bool DrawFiniteValue(Keire::UiFrame& ui, const std::string_view label, float& value)
        {
            double edited = value;
            constexpr double maximum = std::numeric_limits<float>::max();
            if (!ui.DragScalar(label, edited, 0.01, -maximum, maximum) || !std::isfinite(edited))
            {
                return false;
            }
            const float candidate = static_cast<float>(std::clamp(edited, -maximum, maximum));
            if (candidate == value)
            {
                return false;
            }
            value = candidate;
            return true;
        }
    } // namespace

    void NodeMenuSelection::Open() noexcept
    {
        m_Selected.clear();
        m_FocusRequested = true;
    }

    bool NodeMenuSelection::ConsumeFocusRequest() noexcept { return std::exchange(m_FocusRequested, false); }

    void NodeMenuSelection::Synchronize(const std::span<const std::string_view> visibleIds)
    {
        if (visibleIds.empty())
        {
            m_Selected.clear();
            return;
        }
        if (std::ranges::find(visibleIds, m_Selected) == visibleIds.end())
            m_Selected = visibleIds.front();
    }

    void NodeMenuSelection::MovePrevious(const std::span<const std::string_view> visibleIds) { Move(visibleIds, -1); }

    void NodeMenuSelection::MoveNext(const std::span<const std::string_view> visibleIds) { Move(visibleIds, 1); }

    void NodeMenuSelection::Remember(const std::string_view id)
    {
        if (id.empty())
            return;
        const auto existing = std::ranges::find(m_Recent, id);
        if (existing != m_Recent.end())
            m_Recent.erase(existing);
        m_Recent.insert(m_Recent.begin(), std::string(id));
        if (m_Recent.size() > RecentCapacity)
            m_Recent.resize(RecentCapacity);
    }

    std::optional<std::string_view> NodeMenuSelection::Selected() const noexcept
    {
        return m_Selected.empty() ? std::nullopt : std::optional<std::string_view>(m_Selected);
    }

    bool NodeMenuSelection::IsSelected(const std::string_view id) const noexcept { return m_Selected == id; }

    void NodeMenuSelection::Move(const std::span<const std::string_view> visibleIds, const int direction)
    {
        Synchronize(visibleIds);
        if (visibleIds.empty())
            return;
        const auto selected = std::ranges::find(visibleIds, m_Selected);
        const auto index =
            selected == visibleIds.end() ? std::size_t{0} : static_cast<std::size_t>(selected - visibleIds.begin());
        const auto count = visibleIds.size();
        const auto next = direction < 0 ? (index + count - 1) % count : (index + 1) % count;
        m_Selected = visibleIds[next];
    }

    bool AuthoringValueEditors::Curve(Keire::UiFrame& ui, const std::string_view label, Keire::Curve1D& value,
                                      const float minimumTime, const float maximumTime)
    {
        if (!std::isfinite(minimumTime) || !std::isfinite(maximumTime) || maximumTime <= minimumTime)
        {
            throw std::invalid_argument("Curve editor time range must be finite and increasing.");
        }
        auto editorId = ui.PushId(label);
        ui.Text(Detail::InspectorVisibleLabel(label));
        std::vector<Keire::CurveKey> keys(value.Keys().begin(), value.Keys().end());
        std::optional<std::size_t> removed;
        bool changed = false;
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(index));
            double time = keys[index].Time;
            const float lower = index == 0 ? std::min(minimumTime, keys[index].Time)
                                           : std::nextafter(keys[index - 1].Time, keys[index].Time);
            const float upper = index + 1 == keys.size() ? std::max(maximumTime, keys[index].Time)
                                                         : std::nextafter(keys[index + 1].Time, keys[index].Time);
            if (ui.DragScalar("Time", time, 0.01, lower, upper) && std::isfinite(time))
            {
                const float candidate = static_cast<float>(std::clamp(time, double(lower), double(upper)));
                changed |= candidate != keys[index].Time;
                keys[index].Time = candidate;
            }
            changed |= DrawFiniteValue(ui, "Value", keys[index].Value);
            constexpr std::array names{std::string_view("Constant"), std::string_view("Linear"),
                                       std::string_view("Cubic")};
            changed |= DrawInterpolation(ui, keys[index].Interpolation, names);
            if (keys[index].Interpolation == Keire::CurveInterpolation::Cubic ||
                (index > 0 && keys[index - 1].Interpolation == Keire::CurveInterpolation::Cubic))
            {
                changed |= DrawFiniteValue(ui, "In Tangent", keys[index].InTangent);
                changed |= DrawFiniteValue(ui, "Out Tangent", keys[index].OutTangent);
            }
            if (ui.Button("Remove key"))
            {
                removed = index;
            }
        }
        if (removed)
        {
            keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(*removed));
            changed = true;
        }
        if (changed)
        {
            value.SetKeys(std::move(keys));
        }
        const auto insertion =
            Detail::FindAuthoringKeyTime(value.Keys(), minimumTime, maximumTime, Keire::Curve1D::MaximumKeys);
        auto disabled = ui.BeginDisabled(!insertion);
        if (ui.Button("Add key"))
        {
            changed |= Detail::InsertAuthoringCurveKey(value, minimumTime, maximumTime);
        }
        return changed;
    }

    bool AuthoringValueEditors::Gradient(Keire::UiFrame& ui, const std::string_view label, Keire::ColorGradient& value)
    {
        auto editorId = ui.PushId(label);
        ui.Text(Detail::InspectorVisibleLabel(label));
        std::vector<Keire::ColorGradientKey> keys(value.Keys().begin(), value.Keys().end());
        auto interpolation = value.Interpolation();
        constexpr std::array names{std::string_view("Constant"), std::string_view("Linear")};
        bool changed = DrawInterpolation(ui, interpolation, names);
        std::optional<std::size_t> removed;
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(index));
            double time = keys[index].Time;
            const float lower =
                index == 0 ? std::min(0.0F, keys[index].Time) : std::nextafter(keys[index - 1].Time, keys[index].Time);
            const float upper = index + 1 == keys.size() ? std::max(1.0F, keys[index].Time)
                                                         : std::nextafter(keys[index + 1].Time, keys[index].Time);
            if (ui.DragScalar("Time", time, 0.01, lower, upper) && std::isfinite(time))
            {
                const float candidate = static_cast<float>(std::clamp(time, double(lower), double(upper)));
                changed |= candidate != keys[index].Time;
                keys[index].Time = candidate;
            }
            Keire::UiColor color{keys[index].Value.Red, keys[index].Value.Green, keys[index].Value.Blue,
                                 keys[index].Value.Alpha};
            if (ui.ColorEdit("Color", color))
            {
                keys[index].Value = {color.Red, color.Green, color.Blue, color.Alpha};
                changed = true;
            }
            if (ui.Button("Remove key"))
            {
                removed = index;
            }
        }
        if (removed)
        {
            keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(*removed));
            changed = true;
        }
        if (changed)
        {
            value.SetKeys(std::move(keys));
            value.SetInterpolation(interpolation);
        }
        const auto insertion =
            Detail::FindAuthoringKeyTime(value.Keys(), 0.0F, 1.0F, Keire::ColorGradient::MaximumKeys);
        auto disabled = ui.BeginDisabled(!insertion);
        if (ui.Button("Add key"))
        {
            changed |= Detail::InsertAuthoringGradientKey(value);
        }
        return changed;
    }
} // namespace KeireEditor
