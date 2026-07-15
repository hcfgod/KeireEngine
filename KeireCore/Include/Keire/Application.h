#pragma once

#include "Keire/Api.h"
#include "Keire/Event.h"
#include "Keire/Layer.h"
#include "Keire/Log.h"
#include "Keire/Time.h"
#include "Keire/Window.h"

#include <cstdint>
#include <memory>

namespace Keire
{
    struct ApplicationSpecification
    {
        WindowSpecification MainWindow;
        EventBusSpecification Events;
        TimeSpecification Timing;
        LogConfig Logging;
        std::uint32_t TargetFrameRate = 0;
        std::uint32_t MinimizedPumpRate = 30;
        bool SuspendWhenMainWindowMinimized = true;
        bool ManageLogging = true;
    };

    class KEIRE_API Application
    {
      public:
        explicit Application(ApplicationSpecification specification = {});
        virtual ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        [[nodiscard]] int Run();
        void RequestExit(int exitCode = 0) noexcept;
        [[nodiscard]] bool ExitRequested() const noexcept;
        [[nodiscard]] bool IsRunning() const noexcept;

        [[nodiscard]] LayerId PushLayer(std::unique_ptr<Layer> layer);
        [[nodiscard]] LayerId PushOverlay(std::unique_ptr<Layer> overlay);
        [[nodiscard]] bool RemoveLayer(LayerId id);
        [[nodiscard]] LayerStack& Layers() noexcept;
        [[nodiscard]] const LayerStack& Layers() const noexcept;

        [[nodiscard]] Ref<EventBus> Events() const noexcept;
        [[nodiscard]] Time& GetTime();
        [[nodiscard]] const Time& GetTime() const;
        [[nodiscard]] Ref<WindowSystem> Windows() const noexcept;
        [[nodiscard]] Ref<Window> MainWindow() const noexcept;
        [[nodiscard]] const ApplicationSpecification& Specification() const noexcept;

      protected:
        virtual void OnInitialize() {}
        virtual void OnShutdown() noexcept {}

      private:
        friend class LayerStack;
        class Impl;

        void RequireOwnerThread(const char* operation) const;
        [[nodiscard]] bool CanModifyLayers() const noexcept;
        bool DispatchWindowEvent(const WindowEvent& event);
        void ShutdownRuntime(bool initialized) noexcept;

        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
