#include "KeireInternal/Ui/RuntimeUiDiagnosticsInternal.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <stdexcept>

namespace Keire::Detail
{
    class RuntimeUiDiagnostics::Impl final
    {
      public:
        explicit Impl(const std::size_t maximumEvents)
            : MaximumRouteEntries(std::clamp<std::size_t>(maximumEvents * 8U, 64U, 16'384U))
        {
        }

        void Push(RuntimeUiEventRouteEntry entry)
        {
            if (Routes.size() == MaximumRouteEntries)
                Routes.pop_front();
            Routes.push_back(std::move(entry));
        }

        std::size_t MaximumRouteEntries = 0;
        std::map<std::uint64_t, RuntimeUiDirtyReason> Dirty;
        std::deque<RuntimeUiEventRouteEntry> Routes;
        std::uint64_t NextEventSequence = 0;
        std::uint64_t StylePasses = 0;
        std::uint64_t RepaintPasses = 0;
        float StyleMilliseconds = 0.0F;
        float RepaintMilliseconds = 0.0F;
    };

    RuntimeUiDiagnostics::RuntimeUiDiagnostics(const std::size_t maximumEvents)
        : m_Impl(std::make_unique<Impl>(maximumEvents))
    {
    }

    RuntimeUiDiagnostics::~RuntimeUiDiagnostics() = default;

    void RuntimeUiDiagnostics::MarkDirty(const RuntimeUiElementId element, const RuntimeUiDirtyReason reason,
                                         const bool resetReason)
    {
        auto& current = m_Impl->Dirty[element.Value()];
        current = resetReason ? reason : current | reason;
    }

    RuntimeUiDirtyReason RuntimeUiDiagnostics::DirtyReasons(const RuntimeUiElementId element) const noexcept
    {
        const auto found = m_Impl->Dirty.find(element.Value());
        return found == m_Impl->Dirty.end() ? RuntimeUiDirtyReason::None : found->second;
    }

    void RuntimeUiDiagnostics::Forget(const RuntimeUiElementId element) noexcept
    {
        m_Impl->Dirty.erase(element.Value());
    }

    void RuntimeUiDiagnostics::RecordEvent(const RuntimeUiEvent& event,
                                           const std::span<const RuntimeUiElementId> targetToRoot)
    {
        if (targetToRoot.empty())
            return;
        ++m_Impl->NextEventSequence;
        if (m_Impl->NextEventSequence == 0)
            ++m_Impl->NextEventSequence;
        const auto append = [&](const RuntimeUiElementId current, const RuntimeUiEventPhase phase)
        {
            m_Impl->Push({.Sequence = m_Impl->NextEventSequence,
                          .Type = event.Type,
                          .Phase = phase,
                          .Target = event.Target,
                          .CurrentTarget = current,
                          .PointerX = event.PointerX,
                          .PointerY = event.PointerY,
                          .Button = event.Button});
        };
        for (std::size_t index = targetToRoot.size(); index > 1; --index)
            append(targetToRoot[index - 1], RuntimeUiEventPhase::TrickleDown);
        append(targetToRoot.front(), RuntimeUiEventPhase::Target);
        for (std::size_t index = 1; index < targetToRoot.size(); ++index)
            append(targetToRoot[index], RuntimeUiEventPhase::BubbleUp);
    }

    std::vector<RuntimeUiEventRouteEntry> RuntimeUiDiagnostics::EventRouteHistory() const
    {
        return {m_Impl->Routes.begin(), m_Impl->Routes.end()};
    }

    void RuntimeUiDiagnostics::ReportStylePass(const float milliseconds)
    {
        if (!std::isfinite(milliseconds) || milliseconds < 0.0F)
            throw std::invalid_argument("Runtime UI style timing must be finite and non-negative.");
        ++m_Impl->StylePasses;
        m_Impl->StyleMilliseconds = milliseconds;
    }

    void RuntimeUiDiagnostics::ReportRepaintPass(const float milliseconds)
    {
        if (!std::isfinite(milliseconds) || milliseconds < 0.0F)
            throw std::invalid_argument("Runtime UI repaint timing must be finite and non-negative.");
        ++m_Impl->RepaintPasses;
        m_Impl->RepaintMilliseconds = milliseconds;
    }

    void RuntimeUiDiagnostics::Populate(RuntimeUiStatistics& statistics) const noexcept
    {
        statistics.StylePasses = m_Impl->StylePasses;
        statistics.RepaintPasses = m_Impl->RepaintPasses;
        statistics.StyleMilliseconds = m_Impl->StyleMilliseconds;
        statistics.RepaintMilliseconds = m_Impl->RepaintMilliseconds;
    }

    void RuntimeUiDiagnostics::Clear() noexcept
    {
        m_Impl->Dirty.clear();
        m_Impl->Routes.clear();
        m_Impl->StylePasses = 0;
        m_Impl->RepaintPasses = 0;
        m_Impl->StyleMilliseconds = 0.0F;
        m_Impl->RepaintMilliseconds = 0.0F;
    }
} // namespace Keire::Detail
