#pragma once

#include "Keire/Time.h"
#include "Keire/Ui.h"
#include "Keire/Window.h"

#include <memory>

namespace Keire
{
    class UiSystem final
    {
      public:
        UiSystem(const UiSpecification& specification, WindowSystem& windows, Window& window);
        ~UiSystem();

        UiSystem(const UiSystem&) = delete;
        UiSystem& operator=(const UiSystem&) = delete;
        UiSystem(UiSystem&&) = delete;
        UiSystem& operator=(UiSystem&&) = delete;

        void BeginFrame(TimeStep deltaTime, LogicalExtent displaySize);
        [[nodiscard]] UiFrame& Frame() noexcept;
        void EndFrame();
        void Shutdown() noexcept;
        [[nodiscard]] UiCaptureState Capture() const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
