#pragma once

#include "Keire/Ui.h"
#include "Keire/UiWorkspace.h"

namespace KeireEditor
{
    class DiagnosticsPanel final
    {
      public:
        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme, std::uint64_t frame,
                  double deltaMilliseconds, Keire::UiSize windowSize, Keire::UiCaptureState capture);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        Keire::UiPanelRegistration m_Registration;
    };
} // namespace KeireEditor
