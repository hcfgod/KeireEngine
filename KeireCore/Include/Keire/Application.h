#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Audio/AudioSystem.h"
#include "Keire/Diagnostics/Diagnostic.h"
#include "Keire/Diagnostics/Profiler.h"
#include "Keire/Event.h"
#include "Keire/Input/Input.h"
#include "Keire/Jobs/JobSystem.h"
#include "Keire/Layer.h"
#include "Keire/Log.h"
#include "Keire/Memory/MemorySystem.h"
#include "Keire/Modules/EngineModule.h"
#include "Keire/Navigation/NavigationSystem.h"
#include "Keire/Physics/PhysicsSystem.h"
#include "Keire/Project/Project.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Replay/ReplaySystem.h"
#include "Keire/Scenes/SceneSystem.h"
#include "Keire/Scripting/ScriptSystem.h"
#include "Keire/Streaming/StreamingSystem.h"
#include "Keire/StringInterner.h"
#include "Keire/Time.h"
#include "Keire/Ui.h"
#include "Keire/Undo.h"
#include "Keire/Window.h"

#include <cstdint>
#include <memory>

namespace Keire
{
    struct ApplicationSpecification
    {
        WindowSpecification MainWindow;
        EventBusSpecification Events;
        ProfilerSpecification Profiling;
        DiagnosticSystemSpecification Diagnostics;
        MemorySystemSpecification Memory;
        JobSystemSpecification Jobs;
        ModuleRegistrySpecification Modules;
        AssetSystemSpecification Assets;
        StreamingBudgetSpecification Streaming;
        ReplaySystemSpecification Replay;
        ScriptSystemSpecification Scripting;
        PhysicsSystemSpecification Physics;
        AudioSystemSpecification Audio;
        NavigationSystemSpecification Navigation;
        ProjectSystemSpecification Projects;
        SceneSystemSpecification Scenes;
        InputSystemSpecification Input;
        TimeSpecification Timing;
        LogConfig Logging;
        RenderSpecification Render;
        UiSpecification Ui;
        UndoSpecification Undo;
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
        [[nodiscard]] Ref<Profiler> GetProfiler() const noexcept;
        [[nodiscard]] Ref<DiagnosticCatalog> DiagnosticDefinitions() const noexcept;
        [[nodiscard]] Ref<DiagnosticSink> DiagnosticReports() const noexcept;
        [[nodiscard]] Ref<MemorySystem> Memory() const noexcept;
        [[nodiscard]] Ref<StringInterner> Strings() const noexcept;
        [[nodiscard]] Ref<JobSystem> Jobs() const noexcept;
        [[nodiscard]] Ref<ModuleRegistry> Modules() const noexcept;
        [[nodiscard]] Ref<AssetSystem> Assets() const noexcept;
        [[nodiscard]] Ref<StreamingSystem> Streaming() const noexcept;
        [[nodiscard]] Ref<ReplaySystem> Replay() const noexcept;
        [[nodiscard]] Ref<ScriptSystem> Scripts() const noexcept;
        [[nodiscard]] Ref<PhysicsSystem> Physics() const noexcept;
        [[nodiscard]] Ref<AudioSystem> Audio() const noexcept;
        [[nodiscard]] Ref<NavigationSystem> Navigation() const noexcept;
        [[nodiscard]] Ref<Project> GetProject() const noexcept;
        [[nodiscard]] Ref<SceneSystem> Scenes() const noexcept;
        [[nodiscard]] Ref<InputSystem> Input() const noexcept;
        [[nodiscard]] Time& GetTime();
        [[nodiscard]] const Time& GetTime() const;
        [[nodiscard]] Ref<WindowSystem> Windows() const noexcept;
        [[nodiscard]] Ref<Window> MainWindow() const noexcept;
        [[nodiscard]] Ref<RenderSystem> Renderer() const noexcept;
        [[nodiscard]] Ref<UndoService> Undo() const noexcept;
        [[nodiscard]] const ApplicationSpecification& Specification() const noexcept;
        [[nodiscard]] bool UiEnabled() const noexcept;
        [[nodiscard]] UiCaptureState UiCapture() const noexcept;
        [[nodiscard]] UiWorkspace& GetUiWorkspace();
        [[nodiscard]] const UiWorkspace& GetUiWorkspace() const;

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
