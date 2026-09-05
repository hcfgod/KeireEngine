#include "KeireClient/Editor/AuthoringWidgets.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace KeireEditor
{
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
            throw std::invalid_argument("Curve editor time range must be finite and increasing.");
        ui.Text(label);
        std::vector<Keire::CurveKey> keys(value.Keys().begin(), value.Keys().end());
        bool changed = false;
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(index));
            double time = keys[index].Time;
            double keyValue = keys[index].Value;
            const float lower = index == 0 ? minimumTime : std::nextafter(keys[index - 1].Time, maximumTime);
            const float upper =
                index + 1 == keys.size() ? maximumTime : std::nextafter(keys[index + 1].Time, minimumTime);
            if (ui.DragScalar("Time", time, 0.01, lower, upper))
            {
                keys[index].Time = static_cast<float>(time);
                changed = true;
            }
            ui.SameLine();
            if (ui.DragScalar("Value", keyValue, 0.01))
            {
                keys[index].Value = static_cast<float>(keyValue);
                changed = true;
            }
        }
        if (changed)
            value.SetKeys(std::move(keys));
        return changed;
    }

    bool AuthoringValueEditors::Gradient(Keire::UiFrame& ui, const std::string_view label, Keire::ColorGradient& value)
    {
        ui.Text(label);
        std::vector<Keire::ColorGradientKey> keys(value.Keys().begin(), value.Keys().end());
        bool changed = false;
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(index));
            double time = keys[index].Time;
            const float lower = index == 0 ? 0.0F : std::nextafter(keys[index - 1].Time, 1.0F);
            const float upper = index + 1 == keys.size() ? 1.0F : std::nextafter(keys[index + 1].Time, 0.0F);
            if (ui.DragScalar("Time", time, 0.01, lower, upper))
            {
                keys[index].Time = static_cast<float>(time);
                changed = true;
            }
            Keire::UiColor color{keys[index].Value.Red, keys[index].Value.Green, keys[index].Value.Blue,
                                 keys[index].Value.Alpha};
            if (ui.ColorEdit("Color", color))
            {
                keys[index].Value = {color.Red, color.Green, color.Blue, color.Alpha};
                changed = true;
            }
        }
        if (changed)
            value.SetKeys(std::move(keys));
        return changed;
    }
} // namespace KeireEditor
