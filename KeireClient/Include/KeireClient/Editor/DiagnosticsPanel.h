#pragma once

#include "Keire/Diagnostics/Diagnostic.h"
#include "Keire/Ui.h"
#include "Keire/UiWorkspace.h"
#include "Keire/Window.h"

#include <string>

namespace KeireEditor
{
    class DiagnosticsPanel final
    {
      public:
        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme, std::uint64_t frame,
                  double deltaMilliseconds, Keire::UiSize windowSize, Keire::UiCaptureState capture);
        void Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme, std::uint64_t frame,
                  double deltaMilliseconds, Keire::UiSize windowSize, Keire::UiCaptureState capture,
                  Keire::Ref<Keire::DiagnosticCatalog> catalog, Keire::Ref<Keire::DiagnosticSink> reports,
                  Keire::Ref<Keire::WindowSystem> windows);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        Keire::UiPanelRegistration m_Registration;
        std::string m_Status;
    };
} // namespace KeireEditor
