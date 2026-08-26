#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct ManagedRuntimeConsoleEntry final
    {
        Keire::ManagedDiagnosticSeverity Severity = Keire::ManagedDiagnosticSeverity::Error;
        std::uint64_t Generation = 0;
        std::string Message;
    };

    [[nodiscard]] std::string_view
    ManagedBehaviourCallbackDisplayName(Keire::ManagedBehaviourCallback callback) noexcept;

    class ManagedRuntimeDiagnosticsBridge final
    {
      public:
        [[nodiscard]] std::vector<ManagedRuntimeConsoleEntry>
        Collect(std::span<const Keire::ManagedRuntimeDiagnostic> diagnostics);
        void Reset() noexcept;
        [[nodiscard]] std::size_t ConsumedCount() const noexcept { return m_ConsumedCount; }

      private:
        // ScriptSystem retains diagnostics for its lifetime. A smaller snapshot is treated as a source reset and is
        // consumed from the beginning so a replacement service cannot silently lose its first diagnostics.
        std::size_t m_ConsumedCount = 0;
    };
} // namespace KeireEditor
