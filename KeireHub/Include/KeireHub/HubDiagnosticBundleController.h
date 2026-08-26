#pragma once

#include "Keire/Log.h"
#include "Keire/Ref.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Window.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleProductInternal.h"
#include "KeireInternal/Diagnostics/DiagnosticBundleUiInternal.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KeireHub
{
    class HubController;
    struct HubProductSnapshot;

    struct HubDiagnosticBundleSummary final
    {
        std::size_t RecentProjects = 0;
        std::size_t PinnedProjects = 0;
        std::vector<Keire::Internal::DiagnosticBundlePackageVersion> Packages;
        std::vector<Keire::Internal::DiagnosticBundleFailureSummary> Failures;
    };

    [[nodiscard]] HubDiagnosticBundleSummary CreateHubDiagnosticBundleSummary(const HubProductSnapshot& snapshot,
                                                                              const HubController* controller,
                                                                              bool recentUiFailure);

    class HubDiagnosticBundleController final
    {
      public:
        HubDiagnosticBundleController() = default;

        HubDiagnosticBundleController(const HubDiagnosticBundleController&) = delete;
        HubDiagnosticBundleController& operator=(const HubDiagnosticBundleController&) = delete;
        HubDiagnosticBundleController(HubDiagnosticBundleController&&) = delete;
        HubDiagnosticBundleController& operator=(HubDiagnosticBundleController&&) = delete;

        void Open(HubDiagnosticBundleSummary summary, const Keire::Ref<Keire::RenderSystem>& renderer,
                  const Keire::LogConfig& logging);
        void Draw(Keire::UiFrame& ui, Keire::WindowSystem& windows, Keire::WindowId parent);
        void Shutdown() noexcept;

      private:
        Keire::Internal::DiagnosticBundleDialogController m_Dialog;
    };
} // namespace KeireHub
