#pragma once

#include "Keire/Ui/RuntimeUi.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace Keire::Detail
{
    class RuntimeUiDiagnostics final
    {
      public:
        explicit RuntimeUiDiagnostics(std::size_t maximumEvents);
        ~RuntimeUiDiagnostics();

        RuntimeUiDiagnostics(const RuntimeUiDiagnostics&) = delete;
        RuntimeUiDiagnostics& operator=(const RuntimeUiDiagnostics&) = delete;

        void MarkDirty(RuntimeUiElementId element, RuntimeUiDirtyReason reason, bool resetReason);
        void Forget(RuntimeUiElementId element) noexcept;
        [[nodiscard]] RuntimeUiDirtyReason DirtyReasons(RuntimeUiElementId element) const noexcept;
        void RecordEvent(const RuntimeUiEvent& event, std::span<const RuntimeUiElementId> targetToRoot);
        [[nodiscard]] std::vector<RuntimeUiEventRouteEntry> EventRouteHistory() const;
        void ReportStylePass(float milliseconds);
        void ReportRepaintPass(float milliseconds);
        void Populate(RuntimeUiStatistics& statistics) const noexcept;
        void Clear() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire::Detail
