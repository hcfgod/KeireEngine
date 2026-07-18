#include "Keire/Window.h"

#include "Keire/BuildInfo.h"

#include "WindowInternal.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        std::atomic<bool> HasActiveSystem = false;

        struct NativeWindowDeleter final
        {
            void operator()(SDL_Window* window) const noexcept
            {
                if (window)
                {
                    SDL_DestroyWindow(window);
                }
            }
        };

        using UniqueNativeWindow = std::unique_ptr<SDL_Window, NativeWindowDeleter>;

        std::string LastSdlError()
        {
            const char* error = SDL_GetError();
            return error && *error ? std::string(error) : std::string("SDL did not provide a diagnostic");
        }

        std::string BuildWindowErrorMessage(const std::string& operation, const std::string& diagnostic)
        {
            return "Window operation '" + operation + "' failed: " + diagnostic;
        }

        void ValidateSpecification(const WindowSpecification& specification)
        {
            if (specification.Title.empty() || specification.Title.size() > 1024)
            {
                throw std::invalid_argument("Window title must contain 1..1024 UTF-8 bytes.");
            }
            if (specification.Width < 1 || specification.Width > 16384 || specification.Height < 1 ||
                specification.Height > 16384)
            {
                throw std::invalid_argument("Window dimensions must be in the range 1..16384.");
            }
            if (specification.Maximized && specification.Mode == WindowMode::BorderlessFullscreen)
            {
                throw std::invalid_argument("A borderless fullscreen window cannot also start maximized.");
            }
        }
    } // namespace

    WindowError::WindowError(std::string operation, std::string diagnostic)
        : std::runtime_error(BuildWindowErrorMessage(operation, diagnostic)), m_Operation(std::move(operation)),
          m_Diagnostic(std::move(diagnostic))
    {
    }

    class WindowSystem::Impl final : public RefCounted
    {
        struct CachedWindow;

      public:
        class NativeWindow final : public Window
        {
          public:
            NativeWindow(Ref<Impl> implementation, const WindowId id)
                : m_Implementation(std::move(implementation)), m_Id(id)
            {
            }

            ~NativeWindow() override
            {
                if (m_Implementation)
                {
                    m_Implementation->ReleaseWindow(m_Id);
                }
            }

            [[nodiscard]] WindowId Id() const noexcept override { return m_Id; }

            [[nodiscard]] WindowSpecification Specification() const override
            {
                return m_Implementation->GetSpecification(m_Id);
            }

            [[nodiscard]] LogicalExtent LogicalSize() const override { return m_Implementation->GetLogicalSize(m_Id); }

            [[nodiscard]] PixelExtent PixelSize() const override { return m_Implementation->GetPixelSize(m_Id); }

            [[nodiscard]] WindowPosition Position() const override { return m_Implementation->GetPosition(m_Id); }

            [[nodiscard]] float DisplayScale() const override { return m_Implementation->GetDisplayScale(m_Id); }

            [[nodiscard]] std::string Title() const override { return m_Implementation->GetTitle(m_Id); }

            [[nodiscard]] bool Focused() const override
            {
                return m_Implementation->GetFlag(m_Id, &CachedWindow::Focused);
            }

            [[nodiscard]] bool Visible() const override
            {
                return m_Implementation->GetFlag(m_Id, &CachedWindow::Visible);
            }

            [[nodiscard]] bool Minimized() const override
            {
                return m_Implementation->GetFlag(m_Id, &CachedWindow::Minimized);
            }

            [[nodiscard]] bool Maximized() const override
            {
                return m_Implementation->GetFlag(m_Id, &CachedWindow::Maximized);
            }

            [[nodiscard]] WindowMode Mode() const override { return m_Implementation->GetMode(m_Id); }

            [[nodiscard]] bool CloseRequested() const override
            {
                return m_Implementation->GetFlag(m_Id, &CachedWindow::CloseRequested);
            }

            [[nodiscard]] bool IsOpen() const override { return m_Implementation->GetFlag(m_Id, &CachedWindow::Open); }

            void SetTitle(std::string title) override { m_Implementation->SetTitle(m_Id, std::move(title)); }

            void SetSize(const LogicalExtent size) override { m_Implementation->SetSize(m_Id, size); }

            void SetVisible(const bool visible) override { m_Implementation->SetVisible(m_Id, visible); }

            void Minimize() override { m_Implementation->Minimize(m_Id); }

            void Maximize() override { m_Implementation->Maximize(m_Id); }

            void Restore() override { m_Implementation->Restore(m_Id); }

            void SetMode(const WindowMode mode) override { m_Implementation->SetMode(m_Id, mode); }

            void Close() override { m_Implementation->Close(m_Id); }

          private:
            Ref<Impl> m_Implementation;
            WindowId m_Id;
        };

        Impl() : m_OwnerThread(std::this_thread::get_id())
        {
            bool expected = false;
            if (!HasActiveSystem.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                throw std::logic_error("Only one WindowSystem may be active in a process.");
            }

            try
            {
                const auto version = GetVersionString();
                const std::string applicationName(GetBuildInfo().ProjectName);

                if (!SDL_SetAppMetadata(applicationName.c_str(), version.c_str(), nullptr))
                {
                    throw WindowError("SDL_SetAppMetadata", LastSdlError());
                }

                if (!SDL_Init(SDL_INIT_VIDEO))
                {
                    throw WindowError("SDL_Init(SDL_INIT_VIDEO)", LastSdlError());
                }

                m_Active.store(true, std::memory_order_release);
            }
            catch (...)
            {
                HasActiveSystem.store(false, std::memory_order_release);
                throw;
            }
        }

        ~Impl() override
        {
            if (m_Active.load(std::memory_order_acquire) && std::this_thread::get_id() == m_OwnerThread)
            {
                try
                {
                    Shutdown();
                }
                catch (...)
                {
                }
            }
        }

        Ref<Window> CreateWindow(const Ref<Impl>& self, const WindowSpecification& specification)
        {
            RequireOwner("CreateWindow");
            ValidateSpecification(specification);
            DrainDeferredDestruction();

            SDL_WindowFlags flags = 0;
            if (specification.Resizable)
                flags |= SDL_WINDOW_RESIZABLE;
            if (specification.HighPixelDensity)
                flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
            if (!specification.Visible)
                flags |= SDL_WINDOW_HIDDEN;
            if (specification.Maximized)
                flags |= SDL_WINDOW_MAXIMIZED;
            if (specification.Mode == WindowMode::BorderlessFullscreen)
                flags |= SDL_WINDOW_FULLSCREEN;

            UniqueNativeWindow native(SDL_CreateWindow(specification.Title.c_str(),
                                                       static_cast<int>(specification.Width),
                                                       static_cast<int>(specification.Height), flags));
            if (!native)
            {
                throw WindowError("SDL_CreateWindow", LastSdlError());
            }

            CachedWindow cached;
            cached.Native = native.get();
            cached.Specification = specification;
            cached.LogicalSize = QueryLogicalSize(native.get());
            cached.PixelSize = QueryPixelSize(native.get());
            cached.Position = QueryPosition(native.get());
            cached.DisplayScale = SDL_GetWindowDisplayScale(native.get());
            const auto actualFlags = SDL_GetWindowFlags(native.get());
            cached.Focused = (actualFlags & SDL_WINDOW_INPUT_FOCUS) != 0;
            cached.Visible = (actualFlags & SDL_WINDOW_HIDDEN) == 0;
            cached.Minimized = (actualFlags & SDL_WINDOW_MINIMIZED) != 0;
            cached.Maximized = (actualFlags & SDL_WINDOW_MAXIMIZED) != 0;
            cached.Mode =
                (actualFlags & SDL_WINDOW_FULLSCREEN) != 0 ? WindowMode::BorderlessFullscreen : WindowMode::Windowed;
            cached.Open = true;

            const auto nativeId = SDL_GetWindowID(native.get());
            if (nativeId == 0)
            {
                throw WindowError("SDL_GetWindowID", LastSdlError());
            }

            cached.NativeId = nativeId;
            const WindowId id(m_NextWindowId++);
            auto handle = CreateRef<NativeWindow>(self, id);
            {
                std::scoped_lock lock(m_StateMutex);
                const auto [windowIterator, windowInserted] = m_Windows.emplace(id.Value(), std::move(cached));
                if (!windowInserted)
                {
                    throw std::logic_error("Window registration produced a duplicate opaque window ID.");
                }

                try
                {
                    const auto [nativeIterator, nativeInserted] = m_NativeToWindow.emplace(nativeId, id.Value());
                    (void)nativeIterator;
                    if (!nativeInserted)
                    {
                        throw std::logic_error("Window registration produced a duplicate SDL window ID.");
                    }
                }
                catch (...)
                {
                    m_Windows.erase(windowIterator);
                    throw;
                }
            }

            (void)native.release();
            return handle;
        }

        std::optional<WindowEvent> PollEvent()
        {
            RequireOwner("PollEvent");
            DrainDeferredDestruction();
            SDL_Event event{};
            while (SDL_PollEvent(&event))
            {
                if (m_EventSink)
                    m_EventSink(m_EventSinkContext, event);

                if (event.type == SDL_EVENT_QUIT)
                {
                    return QuitEvent{{event.common.timestamp, {}}};
                }

                if (event.type < SDL_EVENT_WINDOW_FIRST || event.type > SDL_EVENT_WINDOW_LAST)
                {
                    continue;
                }

                WindowId id;
                {
                    std::scoped_lock lock(m_StateMutex);
                    const auto nativeIterator = m_NativeToWindow.find(event.window.windowID);
                    if (nativeIterator == m_NativeToWindow.end())
                        continue;
                    id = WindowId(nativeIterator->second);
                }

                const WindowEventHeader header{event.window.timestamp, id};
                std::scoped_lock lock(m_StateMutex);
                const auto iterator = m_Windows.find(id.Value());

                if (iterator == m_Windows.end() || !iterator->second.Open)
                {
                    continue;
                }

                auto& window = iterator->second;
                switch (event.type)
                {
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    window.CloseRequested = true;
                    return WindowCloseRequestedEvent{header};
                case SDL_EVENT_WINDOW_SHOWN:
                    window.Visible = true;
                    window.Specification.Visible = true;
                    return WindowShownEvent{header};
                case SDL_EVENT_WINDOW_HIDDEN:
                    window.Visible = false;
                    window.Specification.Visible = false;
                    return WindowHiddenEvent{header};
                case SDL_EVENT_WINDOW_MOVED:
                    window.Position = {event.window.data1, event.window.data2};
                    return WindowMovedEvent{header, window.Position};
                case SDL_EVENT_WINDOW_RESIZED:
                    window.LogicalSize = ToLogicalExtent(event.window.data1, event.window.data2);
                    window.Specification.Width = window.LogicalSize.Width;
                    window.Specification.Height = window.LogicalSize.Height;
                    return WindowResizedEvent{header, window.LogicalSize};
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    window.PixelSize = ToPixelExtent(event.window.data1, event.window.data2);
                    return WindowPixelSizeChangedEvent{header, window.PixelSize};
                case SDL_EVENT_WINDOW_MINIMIZED:
                    window.Minimized = true;
                    window.Maximized = false;
                    window.Specification.Maximized = false;
                    return WindowMinimizedEvent{header};
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    window.Maximized = true;
                    window.Minimized = false;
                    window.Specification.Maximized = true;
                    return WindowMaximizedEvent{header};
                case SDL_EVENT_WINDOW_RESTORED:
                    window.Minimized = false;
                    window.Maximized = false;
                    window.Specification.Maximized = false;
                    return WindowRestoredEvent{header};
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    window.Focused = true;
                    return WindowFocusGainedEvent{header};
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    window.Focused = false;
                    return WindowFocusLostEvent{header};
                case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                    window.Position = QueryPosition(window.Native);
                    window.LogicalSize = QueryLogicalSize(window.Native);
                    window.PixelSize = QueryPixelSize(window.Native);
                    window.DisplayScale = SDL_GetWindowDisplayScale(window.Native);
                    window.Specification.Width = window.LogicalSize.Width;
                    window.Specification.Height = window.LogicalSize.Height;
                    return WindowDisplayChangedEvent{header};
                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                    window.DisplayScale = SDL_GetWindowDisplayScale(window.Native);
                    window.PixelSize = QueryPixelSize(window.Native);
                    return WindowDisplayScaleChangedEvent{header, window.DisplayScale};
                case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
                    window.Mode = WindowMode::BorderlessFullscreen;
                    window.Specification.Mode = window.Mode;
                    return WindowEnteredFullscreenEvent{header};
                case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
                    window.Mode = WindowMode::Windowed;
                    window.Specification.Mode = window.Mode;
                    return WindowLeftFullscreenEvent{header};
                default:
                    break;
                }
            }
            return std::nullopt;
        }

        void Shutdown()
        {
            if (!m_Active.load(std::memory_order_acquire))
                return;

            RequireOwner("Shutdown");
            DrainDeferredDestruction();

            std::vector<SDL_Window*> natives;
            {
                std::scoped_lock lock(m_StateMutex);

                for (auto& [id, window] : m_Windows)
                {
                    (void)id;
                    if (window.Native)
                        natives.push_back(window.Native);
                    window.Native = nullptr;
                    window.Open = false;
                }

                m_NativeToWindow.clear();
            }

            for (SDL_Window* native : natives)
                SDL_DestroyWindow(native);

            SDL_Quit();

            m_Active.store(false, std::memory_order_release);
            HasActiveSystem.store(false, std::memory_order_release);
        }

        [[nodiscard]] bool Active() const noexcept { return m_Active.load(std::memory_order_acquire); }

        [[nodiscard]] SDL_Window* NativeHandle(const WindowId id)
        {
            RequireOwner("NativeWindow");
            return NativeFor(id);
        }

        void SetEventSink(void* context, const WindowSystemInternalAccess::EventSink sink)
        {
            RequireOwner("SetEventSink");
            m_EventSinkContext = context;
            m_EventSink = sink;
        }

        void ReleaseWindow(const WindowId id) noexcept
        {
            try
            {
                if (!Active())
                    return;
                if (std::this_thread::get_id() == m_OwnerThread)
                {
                    DestroyWindow(id, true);
                    return;
                }

                std::scoped_lock lock(m_DeferredMutex);
                m_DeferredDestruction.push_back(id);
            }
            catch (...)
            {
                // WindowSystem retains the native window and reclaims it during shutdown.
            }
        }

        [[nodiscard]] WindowSpecification GetSpecification(const WindowId id) const
        {
            std::scoped_lock lock(m_StateMutex);
            if (const auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                return iterator->second.Specification;
            return {};
        }

        [[nodiscard]] LogicalExtent GetLogicalSize(const WindowId id) const
        {
            std::scoped_lock lock(m_StateMutex);
            if (const auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                return iterator->second.LogicalSize;
            return {};
        }

        [[nodiscard]] PixelExtent GetPixelSize(const WindowId id) const
        {
            std::scoped_lock lock(m_StateMutex);
            if (const auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                return iterator->second.PixelSize;
            return {};
        }

        [[nodiscard]] WindowPosition GetPosition(const WindowId id) const
        {
            std::scoped_lock lock(m_StateMutex);
            if (const auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                return iterator->second.Position;
            return {};
        }

        [[nodiscard]] float GetDisplayScale(const WindowId id) const
        {
            std::scoped_lock lock(m_StateMutex);
            if (const auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                return iterator->second.DisplayScale;
            return 1.0F;
        }

        [[nodiscard]] std::string GetTitle(const WindowId id) const
        {
            std::scoped_lock lock(m_StateMutex);
            if (const auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                return iterator->second.Specification.Title;
            return {};
        }

        [[nodiscard]] bool GetFlag(const WindowId id, const bool CachedWindow::* member) const
        {
            std::scoped_lock lock(m_StateMutex);
            if (const auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                return iterator->second.*member;
            return false;
        }

        [[nodiscard]] WindowMode GetMode(const WindowId id) const
        {
            std::scoped_lock lock(m_StateMutex);
            if (const auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                return iterator->second.Mode;
            return WindowMode::Windowed;
        }

        void SetTitle(const WindowId id, std::string title)
        {
            if (!RequireActiveOwner("SetTitle"))
                return;

            if (title.empty() || title.size() > 1024)
                throw std::invalid_argument("Window title must contain 1..1024 UTF-8 bytes.");

            SDL_Window* native = NativeFor(id);
            if (!native)
                return;

            if (!SDL_SetWindowTitle(native, title.c_str()))
                throw WindowError("SDL_SetWindowTitle", LastSdlError());

            std::scoped_lock lock(m_StateMutex);
            if (auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                iterator->second.Specification.Title = std::move(title);
        }

        void SetSize(const WindowId id, const LogicalExtent size)
        {
            if (!RequireActiveOwner("SetSize"))
                return;

            if (size.Width < 1 || size.Width > 16384 || size.Height < 1 || size.Height > 16384)
                throw std::invalid_argument("Window dimensions must be in the range 1..16384.");

            SDL_Window* native = NativeFor(id);
            if (!native)
                return;

            if (!SDL_SetWindowSize(native, static_cast<int>(size.Width), static_cast<int>(size.Height)))
                throw WindowError("SDL_SetWindowSize", LastSdlError());

            std::scoped_lock lock(m_StateMutex);
            if (auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
            {
                iterator->second.LogicalSize = size;
                iterator->second.Specification.Width = size.Width;
                iterator->second.Specification.Height = size.Height;
            }
        }

        void SetVisible(const WindowId id, const bool visible)
        {
            if (!RequireActiveOwner("SetVisible"))
                return;

            SDL_Window* native = NativeFor(id);
            if (!native)
                return;

            const bool result = visible ? SDL_ShowWindow(native) : SDL_HideWindow(native);
            if (!result)
                throw WindowError(visible ? "SDL_ShowWindow" : "SDL_HideWindow", LastSdlError());

            std::scoped_lock lock(m_StateMutex);
            if (auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
            {
                iterator->second.Visible = visible;
                iterator->second.Specification.Visible = visible;
            }
        }
        void Minimize(const WindowId id)
        {
            if (!MutateSimple(id, "SDL_MinimizeWindow", SDL_MinimizeWindow))
                return;

            std::scoped_lock lock(m_StateMutex);
            if (auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
            {
                iterator->second.Minimized = true;
                iterator->second.Maximized = false;
                iterator->second.Specification.Maximized = false;
            }
        }
        void Maximize(const WindowId id)
        {
            if (!MutateSimple(id, "SDL_MaximizeWindow", SDL_MaximizeWindow))
                return;

            std::scoped_lock lock(m_StateMutex);
            if (auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
            {
                iterator->second.Minimized = false;
                iterator->second.Maximized = true;
                iterator->second.Specification.Maximized = true;
            }
        }
        void Restore(const WindowId id)
        {
            if (!MutateSimple(id, "SDL_RestoreWindow", SDL_RestoreWindow))
                return;

            std::scoped_lock lock(m_StateMutex);
            if (auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
            {
                iterator->second.Minimized = false;
                iterator->second.Maximized = false;
                iterator->second.Specification.Maximized = false;
            }
        }
        void SetMode(const WindowId id, const WindowMode mode)
        {
            if (!RequireActiveOwner("SetMode"))
                return;

            SDL_Window* native = NativeFor(id);
            if (!native)
                return;

            if (!SDL_SetWindowFullscreen(native, mode == WindowMode::BorderlessFullscreen))
                throw WindowError("SDL_SetWindowFullscreen", LastSdlError());

            std::scoped_lock lock(m_StateMutex);
            if (auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
            {
                iterator->second.Mode = mode;
                iterator->second.Specification.Mode = mode;
            }
        }
        void Close(const WindowId id)
        {
            if (!RequireActiveOwner("Close"))
                return;

            DestroyWindow(id, false);
        }

      private:
        struct CachedWindow
        {
            SDL_Window* Native = nullptr;
            SDL_WindowID NativeId = 0;
            WindowSpecification Specification;
            LogicalExtent LogicalSize;
            PixelExtent PixelSize;
            WindowPosition Position;
            float DisplayScale = 1.0F;
            bool Focused = false;
            bool Visible = false;
            bool Minimized = false;
            bool Maximized = false;
            WindowMode Mode = WindowMode::Windowed;
            bool CloseRequested = false;
            bool Open = false;
        };

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != m_OwnerThread)
                throw std::logic_error(std::string("WindowSystem::") + operation +
                                       " must be called on its creating thread.");
            if (!Active())
                throw std::logic_error(std::string("WindowSystem::") + operation + " called after shutdown.");
        }
        bool RequireActiveOwner(const char* operation) const
        {
            if (!Active())
                return false;
            if (std::this_thread::get_id() != m_OwnerThread)
                throw std::logic_error(std::string("Window operation ") + operation +
                                       " must be called on the WindowSystem creating thread.");
            return true;
        }
        SDL_Window* NativeFor(const WindowId id) const
        {
            std::scoped_lock lock(m_StateMutex);
            if (const auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end() && iterator->second.Open)
                return iterator->second.Native;
            return nullptr;
        }
        bool MutateSimple(const WindowId id, const char* operation, bool (*function)(SDL_Window*))
        {
            if (!RequireActiveOwner(operation))
                return false;
            SDL_Window* native = NativeFor(id);
            if (!native)
                return false;
            if (!function(native))
                throw WindowError(operation, LastSdlError());
            return true;
        }
        void DestroyWindow(const WindowId id, const bool erase)
        {
            SDL_Window* native = nullptr;
            {
                std::scoped_lock lock(m_StateMutex);
                const auto iterator = m_Windows.find(id.Value());
                if (iterator == m_Windows.end())
                    return;
                native = iterator->second.Native;
                m_NativeToWindow.erase(iterator->second.NativeId);
                iterator->second.Native = nullptr;
                iterator->second.Open = false;
                if (erase)
                    m_Windows.erase(iterator);
            }
            if (native)
                SDL_DestroyWindow(native);
        }
        void DrainDeferredDestruction()
        {
            std::vector<WindowId> deferred;
            {
                std::scoped_lock lock(m_DeferredMutex);
                deferred.swap(m_DeferredDestruction);
            }
            for (const auto id : deferred)
                DestroyWindow(id, true);
        }
        static LogicalExtent ToLogicalExtent(const int width, const int height) noexcept
        {
            return {static_cast<std::uint32_t>(width > 0 ? width : 0),
                    static_cast<std::uint32_t>(height > 0 ? height : 0)};
        }
        static PixelExtent ToPixelExtent(const int width, const int height) noexcept
        {
            return {static_cast<std::uint32_t>(width > 0 ? width : 0),
                    static_cast<std::uint32_t>(height > 0 ? height : 0)};
        }
        static PixelExtent QueryPixelSize(SDL_Window* window)
        {
            int width = 0, height = 0;
            if (!SDL_GetWindowSizeInPixels(window, &width, &height))
                return {};
            return ToPixelExtent(width, height);
        }
        static LogicalExtent QueryLogicalSize(SDL_Window* window)
        {
            int width = 0, height = 0;
            if (!SDL_GetWindowSize(window, &width, &height))
                return {};
            return ToLogicalExtent(width, height);
        }
        static WindowPosition QueryPosition(SDL_Window* window)
        {
            int x = 0, y = 0;
            if (!SDL_GetWindowPosition(window, &x, &y))
                return {};
            return {x, y};
        }

        std::thread::id m_OwnerThread;
        std::atomic<bool> m_Active{false};
        mutable std::mutex m_StateMutex;
        std::unordered_map<std::uint32_t, CachedWindow> m_Windows;
        std::unordered_map<SDL_WindowID, std::uint32_t> m_NativeToWindow;
        std::uint32_t m_NextWindowId = 1;
        std::mutex m_DeferredMutex;
        std::vector<WindowId> m_DeferredDestruction;
        void* m_EventSinkContext = nullptr;
        WindowSystemInternalAccess::EventSink m_EventSink = nullptr;
    };

    WindowSystem::WindowSystem() : m_Impl(CreateRef<Impl>()) {}

    WindowSystem::~WindowSystem()
    {
        if (m_Impl && m_Impl->Active())
        {
            try
            {
                m_Impl->Shutdown();
            }
            catch (...)
            {
            }
        }
    }

    Ref<Window> WindowSystem::CreateWindow(const WindowSpecification& specification)
    {
        return m_Impl->CreateWindow(m_Impl, specification);
    }

    std::optional<WindowEvent> WindowSystem::PollEvent() { return m_Impl->PollEvent(); }

    void WindowSystem::Shutdown()
    {
        if (m_Impl)
            m_Impl->Shutdown();
    }

    bool WindowSystem::IsActive() const noexcept { return m_Impl && m_Impl->Active(); }

    SDL_Window* WindowSystemInternalAccess::NativeWindow(WindowSystem& system, const WindowId id)
    {
        return system.m_Impl->NativeHandle(id);
    }

    void WindowSystemInternalAccess::SetEventSink(WindowSystem& system, void* context, const EventSink sink)
    {
        system.m_Impl->SetEventSink(context, sink);
    }
} // namespace Keire
