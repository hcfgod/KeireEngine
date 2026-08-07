#include "Keire/Window.h"

#include "Keire/BuildInfo.h"

#include "KeireInternal/TrayIconInternal.h"
#include "KeireInternal/WindowChromeInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <SDL3/SDL.h>

#include <algorithm>
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
    namespace Detail
    {
        class FolderDialogState final : public RefCounted
        {
          public:
            mutable std::mutex Mutex;
            FolderDialogStatus Status = FolderDialogStatus::Pending;
            std::filesystem::path Path;
            std::string Error;
        };

        class OpenFileDialogState final : public RefCounted
        {
          public:
            mutable std::mutex Mutex;
            OpenFileDialogStatus Status = OpenFileDialogStatus::Pending;
            std::filesystem::path Path;
            std::string Error;
        };

        class SaveFileDialogState final : public RefCounted
        {
          public:
            mutable std::mutex Mutex;
            SaveFileDialogStatus Status = SaveFileDialogStatus::Pending;
            std::filesystem::path Path;
            std::string Error;
        };
    } // namespace Detail

    namespace
    {
        std::atomic<bool> HasActiveSystem = false;
        class TrayDispatchState final
        {
          public:
            explicit TrayDispatchState(std::vector<SystemTrayAction> actions)
            {
                m_Callbacks.reserve(actions.size());
                for (auto& action : actions)
                    m_Callbacks.push_back(std::move(action.Callback));
            }

            void Queue(const std::size_t index) noexcept
            {
                if (m_Active.load(std::memory_order_acquire) && index < m_Callbacks.size())
                    m_Pending.fetch_or(std::uint64_t{1} << index, std::memory_order_release);
            }

            void Drain() noexcept
            {
                const auto pending = m_Pending.exchange(0, std::memory_order_acq_rel);
                if (!m_Active.load(std::memory_order_acquire))
                    return;

                for (std::size_t index = 0; index < m_Callbacks.size(); ++index)
                {
                    if ((pending & (std::uint64_t{1} << index)) == 0)
                        continue;
                    try
                    {
                        m_Callbacks[index]();
                    }
                    catch (...)
                    {
                    }
                }
            }

            void Close() noexcept
            {
                m_Active.store(false, std::memory_order_release);
                m_Pending.store(0, std::memory_order_release);
            }

          private:
            std::vector<std::function<void()>> m_Callbacks;
            std::atomic<std::uint64_t> m_Pending{0};
            std::atomic<bool> m_Active{true};
        };

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

        [[nodiscard]] std::string Utf8PathString(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        std::string BuildWindowErrorMessage(const std::string& operation, const std::string& diagnostic)
        {
            return "Window operation '" + operation + "' failed: " + diagnostic;
        }

        struct FolderDialogRequest final
        {
            WeakRef<Detail::FolderDialogState> State;
        };

        struct OpenFileDialogRequest final
        {
            WeakRef<Detail::OpenFileDialogState> State;
        };

        struct SaveFileDialogRequest final
        {
            WeakRef<Detail::SaveFileDialogState> State;
        };

        void SDLCALL FolderDialogCompleted(void* userData, const char* const* files, int)
        {
            std::unique_ptr<FolderDialogRequest> request(static_cast<FolderDialogRequest*>(userData));
            const auto state = request->State.Lock();
            if (!state)
                return;
            std::scoped_lock lock(state->Mutex);
            if (!files)
            {
                state->Status = FolderDialogStatus::Failed;
                state->Error = LastSdlError();
            }
            else if (!files[0])
                state->Status = FolderDialogStatus::Cancelled;
            else
            {
                state->Path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
                state->Status = FolderDialogStatus::Selected;
            }
        }

        void SDLCALL OpenFileDialogCompleted(void* userData, const char* const* files, int)
        {
            std::unique_ptr<OpenFileDialogRequest> request(static_cast<OpenFileDialogRequest*>(userData));
            const auto state = request->State.Lock();
            if (!state)
                return;
            std::scoped_lock lock(state->Mutex);
            if (!files)
            {
                state->Status = OpenFileDialogStatus::Failed;
                state->Error = LastSdlError();
            }
            else if (!files[0])
                state->Status = OpenFileDialogStatus::Cancelled;
            else
            {
                state->Path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
                state->Status = OpenFileDialogStatus::Selected;
            }
        }

        void SDLCALL SaveFileDialogCompleted(void* userData, const char* const* files, int)
        {
            std::unique_ptr<SaveFileDialogRequest> request(static_cast<SaveFileDialogRequest*>(userData));
            const auto state = request->State.Lock();
            if (!state)
                return;
            std::scoped_lock lock(state->Mutex);
            if (!files)
            {
                state->Status = SaveFileDialogStatus::Failed;
                state->Error = LastSdlError();
            }
            else if (!files[0])
                state->Status = SaveFileDialogStatus::Cancelled;
            else
            {
                state->Path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
                state->Status = SaveFileDialogStatus::Selected;
            }
        }

    } // namespace

    WindowError::WindowError(std::string operation, std::string diagnostic)
        : std::runtime_error(BuildWindowErrorMessage(operation, diagnostic)), m_Operation(std::move(operation)),
          m_Diagnostic(std::move(diagnostic))
    {
    }

    FolderDialogOperation::FolderDialogOperation(Ref<Detail::FolderDialogState> state) : m_State(std::move(state)) {}

    FolderDialogOperation::~FolderDialogOperation() = default;

    FolderDialogStatus FolderDialogOperation::Status() const noexcept
    {
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Status;
    }

    std::filesystem::path FolderDialogOperation::SelectedPath() const
    {
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Path;
    }

    std::string FolderDialogOperation::Diagnostic() const
    {
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Error;
    }

    OpenFileDialogOperation::OpenFileDialogOperation(Ref<Detail::OpenFileDialogState> state) : m_State(std::move(state))
    {
    }

    OpenFileDialogOperation::~OpenFileDialogOperation() = default;

    OpenFileDialogStatus OpenFileDialogOperation::Status() const noexcept
    {
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Status;
    }

    std::filesystem::path OpenFileDialogOperation::SelectedPath() const
    {
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Path;
    }

    std::string OpenFileDialogOperation::Diagnostic() const
    {
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Error;
    }

    class SaveFileDialogOperation::Impl final
    {
      public:
        explicit Impl(Ref<Detail::SaveFileDialogState> state) : State(std::move(state)) {}
        Ref<Detail::SaveFileDialogState> State;
    };

    SaveFileDialogOperation::SaveFileDialogOperation(std::unique_ptr<Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }

    SaveFileDialogOperation::~SaveFileDialogOperation() = default;

    SaveFileDialogStatus SaveFileDialogOperation::Status() const noexcept
    {
        std::scoped_lock lock(m_Impl->State->Mutex);
        return m_Impl->State->Status;
    }

    std::filesystem::path SaveFileDialogOperation::SelectedPath() const
    {
        std::scoped_lock lock(m_Impl->State->Mutex);
        return m_Impl->State->Path;
    }

    std::string SaveFileDialogOperation::Diagnostic() const
    {
        std::scoped_lock lock(m_Impl->State->Mutex);
        return m_Impl->State->Error;
    }

    class SystemTray::Impl final
    {
      public:
        struct Action final
        {
            TrayDispatchState* Dispatch = nullptr;
            std::size_t Index = 0;
        };

        Impl(SystemTraySpecification specification, std::shared_ptr<TrayDispatchState> dispatch)
            : OwnerThread(std::this_thread::get_id()), Dispatch(std::move(dispatch))
        {
            const auto icon = Detail::LoadTrayIcon(specification.Icon);
            Tray = SDL_CreateTray(icon.get(), specification.Tooltip.empty() ? nullptr : specification.Tooltip.c_str());
            if (!Tray)
            {
                Error = LastSdlError();
                return;
            }
            SDL_TrayMenu* menu = SDL_CreateTrayMenu(Tray);
            if (!menu)
            {
                Error = LastSdlError();
                SDL_DestroyTray(Tray);
                Tray = nullptr;
                return;
            }
            Actions.reserve(specification.Actions.size());
            for (std::size_t index = 0; index < specification.Actions.size(); ++index)
            {
                Actions.push_back({Dispatch.get(), index});
                SDL_TrayEntry* entry =
                    SDL_InsertTrayEntryAt(menu, -1, specification.Actions[index].Label.c_str(), SDL_TRAYENTRY_BUTTON);
                if (!entry)
                {
                    Error = LastSdlError();
                    SDL_DestroyTray(Tray);
                    Tray = nullptr;
                    Actions.clear();
                    return;
                }
                SDL_SetTrayEntryCallback(entry, &InvokeAction, &Actions[index]);
            }
        }

        static void SDLCALL InvokeAction(void* context, SDL_TrayEntry*)
        {
            auto* action = static_cast<Action*>(context);
            if (action && action->Dispatch)
                action->Dispatch->Queue(action->Index);
        }

        void Close() noexcept
        {
            if (std::this_thread::get_id() != OwnerThread)
                return;
            if (Tray)
            {
                SDL_DestroyTray(Tray);
                Tray = nullptr;
            }
            Dispatch->Close();
            Actions.clear();
        }

        std::thread::id OwnerThread;
        SDL_Tray* Tray = nullptr;
        std::shared_ptr<TrayDispatchState> Dispatch;
        std::vector<Action> Actions;
        std::string Error;
    };

    SystemTray::SystemTray(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    SystemTray::~SystemTray() { Close(); }
    bool SystemTray::IsAvailable() const noexcept { return m_Impl && m_Impl->Tray; }
    std::string SystemTray::Diagnostic() const { return m_Impl ? m_Impl->Error : std::string{}; }
    void SystemTray::Close() noexcept
    {
        if (m_Impl)
            m_Impl->Close();
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

            void SetPosition(const WindowPosition position) override { m_Implementation->SetPosition(m_Id, position); }

            void SetVisible(const bool visible) override { m_Implementation->SetVisible(m_Id, visible); }

            void Minimize() override { m_Implementation->Minimize(m_Id); }

            void Maximize() override { m_Implementation->Maximize(m_Id); }

            void Restore() override { m_Implementation->Restore(m_Id); }

            void Raise() override { m_Implementation->Raise(m_Id); }

            void SetMode(const WindowMode mode) override { m_Implementation->SetMode(m_Id, mode); }

            void SetChromeLayout(const WindowChromeLayout& layout) override
            {
                m_Implementation->SetChromeLayout(m_Id, layout);
            }

            void Close() override { m_Implementation->Close(m_Id); }

          private:
            Ref<Impl> m_Implementation;
            WindowId m_Id;
        };

        explicit Impl(WindowSystemSpecification specification) : m_OwnerThread(std::this_thread::get_id())
        {
            bool expected = false;
            if (!HasActiveSystem.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                throw std::logic_error("Only one WindowSystem may be active in a process.");
            }

            try
            {
                const auto version =
                    specification.ApplicationVersion.empty() ? GetVersionString() : specification.ApplicationVersion;
                const auto applicationName = specification.ApplicationName.empty()
                                                 ? std::string(GetBuildInfo().ProjectName)
                                                 : specification.ApplicationName;
                const auto* identifier =
                    specification.ApplicationIdentifier.empty() ? nullptr : specification.ApplicationIdentifier.c_str();

                if (!SDL_SetAppMetadata(applicationName.c_str(), version.c_str(), identifier))
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
            Detail::ValidateWindowSpecification(specification);
            DrainDeferredDestruction();

            const auto flags = static_cast<SDL_WindowFlags>(Detail::NativeWindowFlags(specification));

            UniqueNativeWindow native(SDL_CreateWindow(specification.Title.c_str(),
                                                       static_cast<int>(specification.Width),
                                                       static_cast<int>(specification.Height), flags));
            if (!native)
            {
                throw WindowError("SDL_CreateWindow", LastSdlError());
            }
            Detail::ConfigureMinimumWindowSize(native.get(), specification);

            CachedWindow cached;
            cached.Native = native.get();
            cached.Specification = specification;
            if (specification.Decoration == WindowDecoration::Custom)
            {
                cached.Chrome = std::make_unique<Detail::WindowChromeHitTestCache>(specification.Resizable);
                if (!cached.Chrome->Attach(native.get()))
                    cached.Specification.Decoration = WindowDecoration::Native;
            }
            cached.LogicalSize = Detail::QueryLogicalSize(native.get());
            cached.PixelSize = Detail::QueryPixelSize(native.get());
            cached.Position = Detail::QueryWindowPosition(native.get());
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

        [[nodiscard]] std::vector<DisplayInformation> Displays() const
        {
            RequireOwner("Displays");
            int count = 0;
            SDL_DisplayID* nativeDisplays = SDL_GetDisplays(&count);
            if (!nativeDisplays)
                throw WindowError("SDL_GetDisplays", LastSdlError());
            const auto release = [](SDL_DisplayID* displays) { SDL_free(displays); };
            const std::unique_ptr<SDL_DisplayID, decltype(release)> owned(nativeDisplays, release);
            const auto primary = SDL_GetPrimaryDisplay();
            std::vector<DisplayInformation> result;
            result.reserve(static_cast<std::size_t>(std::max(count, 0)));
            for (int index = 0; index < count; ++index)
            {
                SDL_Rect bounds{};
                SDL_Rect usable{};
                if (!SDL_GetDisplayBounds(nativeDisplays[index], &bounds))
                    throw WindowError("SDL_GetDisplayBounds", LastSdlError());
                if (!SDL_GetDisplayUsableBounds(nativeDisplays[index], &usable))
                    throw WindowError("SDL_GetDisplayUsableBounds", LastSdlError());
                const auto name = SDL_GetDisplayName(nativeDisplays[index]);
                result.push_back({static_cast<std::uint32_t>(index),
                                  name ? name : "Display " + std::to_string(index + 1),
                                  {bounds.x, bounds.y, static_cast<std::uint32_t>(std::max(bounds.w, 0)),
                                   static_cast<std::uint32_t>(std::max(bounds.h, 0))},
                                  {usable.x, usable.y, static_cast<std::uint32_t>(std::max(usable.w, 0)),
                                   static_cast<std::uint32_t>(std::max(usable.h, 0))},
                                  SDL_GetDisplayContentScale(nativeDisplays[index]),
                                  nativeDisplays[index] == primary});
            }
            return result;
        }

        std::optional<WindowEvent> PollEvent()
        {
            RequireOwner("PollEvent");
            DrainDeferredDestruction();
            SDL_Event event{};
            while (true)
            {
                const bool eventAvailable = SDL_PollEvent(&event);
                if (!eventAvailable)
                {
                    // Native window events queued by a previous hide/minimize must settle before a tray action can
                    // restore the window. Running the action between those events can immediately hide it again.
                    DrainTrayActions();
                    break;
                }

                for (const auto& sink : m_EventSinks)
                {
                    if (sink.Callback)
                        sink.Callback(sink.Context, event);
                }

                if (event.type == SDL_EVENT_QUIT)
                {
                    return QuitEvent{{event.common.timestamp, {}}};
                }

                if (event.type == SDL_EVENT_DROP_BEGIN || event.type == SDL_EVENT_DROP_FILE ||
                    event.type == SDL_EVENT_DROP_POSITION || event.type == SDL_EVENT_DROP_COMPLETE)
                {
                    const auto nativeIterator = m_NativeToWindow.find(event.drop.windowID);
                    if (nativeIterator == m_NativeToWindow.end())
                        continue;
                    const WindowId id(nativeIterator->second);
                    auto& drop = m_FileDrops[event.drop.windowID];
                    if (event.type == SDL_EVENT_DROP_BEGIN)
                    {
                        drop = {};
                        drop.Timestamp = event.drop.timestamp;
                    }
                    else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data && *event.drop.data)
                    {
                        const auto* first = reinterpret_cast<const char8_t*>(event.drop.data);
                        drop.Paths.emplace_back(std::u8string(first, first + std::char_traits<char8_t>::length(first)));
                        drop.Position = {static_cast<std::int32_t>(event.drop.x),
                                         static_cast<std::int32_t>(event.drop.y)};
                        drop.Timestamp = event.drop.timestamp;
                    }
                    else if (event.type == SDL_EVENT_DROP_POSITION)
                    {
                        drop.Position = {static_cast<std::int32_t>(event.drop.x),
                                         static_cast<std::int32_t>(event.drop.y)};
                        drop.Timestamp = event.drop.timestamp;
                    }
                    else if (event.type == SDL_EVENT_DROP_COMPLETE)
                    {
                        auto completed = std::move(drop);
                        m_FileDrops.erase(event.drop.windowID);
                        if (!completed.Paths.empty())
                        {
                            return WindowFileDropEvent{
                                {event.drop.timestamp, id}, completed.Position, std::move(completed.Paths)};
                        }
                    }
                    continue;
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
                    window.LogicalSize = Detail::ToLogicalExtent(event.window.data1, event.window.data2);
                    window.Specification.Width = window.LogicalSize.Width;
                    window.Specification.Height = window.LogicalSize.Height;
                    return WindowResizedEvent{header, window.LogicalSize};
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    window.PixelSize = Detail::ToPixelExtent(event.window.data1, event.window.data2);
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
                    ApplyCursorMode(window, window.Cursor);
                    return WindowFocusGainedEvent{header};
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    window.Focused = false;
                    ApplyCursorMode(window, CursorMode::Normal);
                    return WindowFocusLostEvent{header};
                case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                    window.Position = Detail::QueryWindowPosition(window.Native);
                    window.LogicalSize = Detail::QueryLogicalSize(window.Native);
                    window.PixelSize = Detail::QueryPixelSize(window.Native);
                    window.DisplayScale = SDL_GetWindowDisplayScale(window.Native);
                    window.Specification.Width = window.LogicalSize.Width;
                    window.Specification.Height = window.LogicalSize.Height;
                    return WindowDisplayChangedEvent{header};
                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                    window.DisplayScale = SDL_GetWindowDisplayScale(window.Native);
                    window.PixelSize = Detail::QueryPixelSize(window.Native);
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

        void RegisterSystemTray(const Ref<SystemTray>& tray, const std::shared_ptr<TrayDispatchState>& dispatch)
        {
            RequireOwner("CreateSystemTray");
            m_SystemTrays.emplace_back(tray);
            m_TrayDispatch.push_back(dispatch);
        }

        void Shutdown()
        {
            if (!m_Active.load(std::memory_order_acquire))
                return;

            RequireOwner("Shutdown");
            DrainDeferredDestruction();

            std::vector<Ref<SystemTray>> trays;
            trays.reserve(m_SystemTrays.size());
            for (const auto& weak : m_SystemTrays)
                if (auto tray = weak.Lock())
                    trays.push_back(std::move(tray));
            m_SystemTrays.clear();
            m_TrayDispatch.clear();
            for (const auto& tray : trays)
                tray->Close();

            struct NativeToDestroy final
            {
                SDL_Window* Native = nullptr;
                Detail::WindowChromeHitTestCache* Chrome = nullptr;
            };
            std::vector<NativeToDestroy> natives;
            {
                std::scoped_lock lock(m_StateMutex);

                for (auto& [id, window] : m_Windows)
                {
                    (void)id;
                    if (window.Native)
                        natives.push_back({window.Native, window.Chrome.get()});
                    window.Native = nullptr;
                    window.Open = false;
                }

                m_NativeToWindow.clear();
            }

            for (const auto& native : natives)
            {
                if (native.Chrome)
                    native.Chrome->Detach();
                SDL_DestroyWindow(native.Native);
            }

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

        [[nodiscard]] WindowSystemInternalAccess::EventSinkToken
        AddEventSink(void* context, const WindowSystemInternalAccess::EventSink sink)
        {
            RequireOwner("AddEventSink");
            if (!context || !sink)
                throw std::invalid_argument("Window event sink requires a context and callback.");
            const auto token = m_NextEventSinkToken++;
            m_EventSinks.push_back({token, context, sink});
            return token;
        }

        void RemoveEventSink(const WindowSystemInternalAccess::EventSinkToken token) noexcept
        {
            if (!token || std::this_thread::get_id() != m_OwnerThread)
                return;
            const auto found = std::ranges::find(m_EventSinks, token, &EventSinkRecord::Token);
            if (found != m_EventSinks.end())
            {
                found->Context = nullptr;
                found->Callback = nullptr;
            }
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

            Detail::ValidateWindowSize(size, GetSpecification(id));

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

        void SetPosition(const WindowId id, const WindowPosition position)
        {
            if (!RequireActiveOwner("SetPosition"))
                return;

            SDL_Window* native = NativeFor(id);
            if (!native)
                return;

            if (!SDL_SetWindowPosition(native, position.X, position.Y))
                throw WindowError("SDL_SetWindowPosition", LastSdlError());

            std::scoped_lock lock(m_StateMutex);
            if (auto iterator = m_Windows.find(id.Value()); iterator != m_Windows.end())
                iterator->second.Position = position;
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
        void Raise(const WindowId id) { (void)MutateSimple(id, "SDL_RaiseWindow", SDL_RaiseWindow); }
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
        void SetChromeLayout(const WindowId id, const WindowChromeLayout& layout)
        {
            if (!RequireActiveOwner("SetChromeLayout"))
                return;

            std::scoped_lock lock(m_StateMutex);
            const auto iterator = m_Windows.find(id.Value());
            if (iterator == m_Windows.end() || !iterator->second.Open ||
                iterator->second.Specification.Decoration != WindowDecoration::Custom || !iterator->second.Chrome)
            {
                return;
            }
            iterator->second.Chrome->Store(layout);
        }
        void Close(const WindowId id)
        {
            if (!RequireActiveOwner("Close"))
                return;

            DestroyWindow(id, false);
        }

        void SetCursorMode(const WindowId id, const CursorMode mode)
        {
            RequireOwner("SetCursorMode");
            std::scoped_lock lock(m_StateMutex);
            const auto found = m_Windows.find(id.Value());
            if (found == m_Windows.end() || !found->second.Open)
                throw std::invalid_argument("Cannot set cursor mode for an unknown or closed window.");
            ApplyCursorMode(found->second, found->second.Focused ? mode : CursorMode::Normal);
            found->second.Cursor = mode;
        }

        [[nodiscard]] CursorMode GetCursorMode(const WindowId id) const
        {
            std::scoped_lock lock(m_StateMutex);
            const auto found = m_Windows.find(id.Value());
            return found == m_Windows.end() ? CursorMode::Normal : found->second.Cursor;
        }

        void WarpCursor(const WindowId id, const WindowPosition position)
        {
            RequireOwner("WarpCursor");
            SDL_Window* native = NativeFor(id);
            if (!native)
                throw std::invalid_argument("Cannot warp the cursor for an unknown or closed window.");
            SDL_WarpMouseInWindow(native, static_cast<float>(position.X), static_cast<float>(position.Y));
        }

        void SetClipboardText(const std::string_view text)
        {
            RequireOwner("SetClipboardText");
            const std::string value(text);
            if (!SDL_SetClipboardText(value.c_str()))
                throw std::runtime_error("SDL_SetClipboardText failed: " + LastSdlError());
        }

        [[nodiscard]] std::string ClipboardText() const
        {
            RequireOwner("ClipboardText");
            char* value = SDL_GetClipboardText();
            if (!value)
                throw std::runtime_error("SDL_GetClipboardText failed: " + LastSdlError());
            std::string result(value);
            SDL_free(value);
            return result;
        }

        void OpenUrl(const std::string_view url)
        {
            RequireOwner("OpenUrl");
            if (!url.starts_with("https://") && !url.starts_with("http://"))
                throw std::invalid_argument("External URLs must use HTTP or HTTPS.");
            const std::string value(url);
            if (!SDL_OpenURL(value.c_str()))
                throw std::runtime_error("SDL_OpenURL failed: " + LastSdlError());
        }

      private:
        struct FileDropState
        {
            std::uint64_t Timestamp = 0;
            WindowPosition Position;
            std::vector<std::filesystem::path> Paths;
        };

        struct CachedWindow
        {
            SDL_Window* Native = nullptr;
            SDL_WindowID NativeId = 0;
            WindowSpecification Specification;
            std::unique_ptr<Detail::WindowChromeHitTestCache> Chrome;
            LogicalExtent LogicalSize;
            PixelExtent PixelSize;
            WindowPosition Position;
            float DisplayScale = 1.0F;
            bool Focused = false;
            bool Visible = false;
            bool Minimized = false;
            bool Maximized = false;
            WindowMode Mode = WindowMode::Windowed;
            CursorMode Cursor = CursorMode::Normal;
            bool CloseRequested = false;
            bool Open = false;
        };

        static void ApplyCursorMode(CachedWindow& window, const CursorMode mode) noexcept
        {
            if (!window.Native)
                return;
            (void)SDL_SetWindowRelativeMouseMode(window.Native, mode == CursorMode::RelativeLocked);
            (void)SDL_SetWindowMouseGrab(window.Native,
                                         mode == CursorMode::Confined || mode == CursorMode::RelativeLocked);
            if (mode == CursorMode::Hidden || mode == CursorMode::RelativeLocked)
                (void)SDL_HideCursor();
            else
                (void)SDL_ShowCursor();
        }

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
            Detail::WindowChromeHitTestCache* chrome = nullptr;
            std::unique_ptr<Detail::WindowChromeHitTestCache> ownedChrome;
            {
                std::scoped_lock lock(m_StateMutex);
                const auto iterator = m_Windows.find(id.Value());
                if (iterator == m_Windows.end())
                    return;
                native = iterator->second.Native;
                chrome = iterator->second.Chrome.get();
                m_NativeToWindow.erase(iterator->second.NativeId);
                iterator->second.Native = nullptr;
                iterator->second.Open = false;
                if (erase)
                {
                    ownedChrome = std::move(iterator->second.Chrome);
                    chrome = ownedChrome.get();
                    m_Windows.erase(iterator);
                }
            }
            if (native)
            {
                if (chrome)
                    chrome->Detach();
                SDL_DestroyWindow(native);
            }
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

        void DrainTrayActions() noexcept
        {
            std::vector<std::shared_ptr<TrayDispatchState>> active;
            active.reserve(m_TrayDispatch.size());
            std::erase_if(m_TrayDispatch,
                          [&active](const std::weak_ptr<TrayDispatchState>& weak)
                          {
                              if (auto dispatch = weak.lock())
                              {
                                  active.push_back(std::move(dispatch));
                                  return false;
                              }
                              return true;
                          });
            for (const auto& dispatch : active)
                dispatch->Drain();
        }
        std::thread::id m_OwnerThread;
        std::atomic<bool> m_Active{false};
        mutable std::mutex m_StateMutex;
        std::unordered_map<std::uint32_t, CachedWindow> m_Windows;
        std::unordered_map<SDL_WindowID, std::uint32_t> m_NativeToWindow;
        std::unordered_map<SDL_WindowID, FileDropState> m_FileDrops;
        std::uint32_t m_NextWindowId = 1;
        std::mutex m_DeferredMutex;
        std::vector<WindowId> m_DeferredDestruction;
        struct EventSinkRecord
        {
            WindowSystemInternalAccess::EventSinkToken Token = 0;
            void* Context = nullptr;
            WindowSystemInternalAccess::EventSink Callback = nullptr;
        };
        std::vector<EventSinkRecord> m_EventSinks;
        WindowSystemInternalAccess::EventSinkToken m_NextEventSinkToken = 1;
        std::vector<WeakRef<SystemTray>> m_SystemTrays;
        std::vector<std::weak_ptr<TrayDispatchState>> m_TrayDispatch;
    };

    WindowSystem::WindowSystem(WindowSystemSpecification specification)
        : m_Impl(CreateRef<Impl>(std::move(specification)))
    {
    }

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

    std::vector<DisplayInformation> WindowSystem::Displays() const { return m_Impl->Displays(); }

    std::optional<WindowEvent> WindowSystem::PollEvent() { return m_Impl->PollEvent(); }

    Ref<FolderDialogOperation> WindowSystem::ShowFolderDialog(const WindowId parent,
                                                              const std::filesystem::path& defaultLocation)
    {
        auto state = CreateRef<Detail::FolderDialogState>();
        auto operation = CreateRef<FolderDialogOperation>(state);
        auto request = std::make_unique<FolderDialogRequest>();
        request->State = WeakRef<Detail::FolderDialogState>(state);
        const auto location = defaultLocation.empty() ? std::string{} : Utf8PathString(defaultLocation);
        SDL_ShowOpenFolderDialog(FolderDialogCompleted, request.release(), m_Impl->NativeHandle(parent),
                                 location.empty() ? nullptr : location.c_str(), false);
        return operation;
    }

    Ref<OpenFileDialogOperation> WindowSystem::ShowOpenFileDialog(const WindowId parent,
                                                                  const OpenFileDialogSpecification& specification)
    {
        if (specification.Title.empty() || specification.Title.size() > 256 || specification.FilterName.size() > 128 ||
            specification.Extension.size() > 32 || specification.Extension.find('*') != std::string::npos ||
            specification.Extension.find('.') != std::string::npos)
            throw std::invalid_argument("Open file dialog specification is invalid.");
        auto state = CreateRef<Detail::OpenFileDialogState>();
        auto operation = CreateRef<OpenFileDialogOperation>(state);
        auto request = std::make_unique<OpenFileDialogRequest>();
        request->State = state;
        const auto location =
            specification.DefaultLocation.empty() ? std::string{} : Utf8PathString(specification.DefaultLocation);
        SDL_DialogFileFilter filter{specification.FilterName.c_str(), specification.Extension.c_str()};
        const SDL_DialogFileFilter* filters = specification.Extension.empty() ? nullptr : &filter;
        const int filterCount = filters ? 1 : 0;
        SDL_ShowOpenFileDialog(OpenFileDialogCompleted, request.release(), m_Impl->NativeHandle(parent), filters,
                               filterCount, location.empty() ? nullptr : location.c_str(), false);
        return operation;
    }

    Ref<SaveFileDialogOperation> WindowSystem::ShowSaveFileDialog(const WindowId parent,
                                                                  const SaveFileDialogSpecification& specification)
    {
        if (specification.Title.empty() || specification.Title.size() > 256 || specification.DefaultName.size() > 256 ||
            specification.Extension.size() > 32 || specification.Extension.find('*') != std::string::npos ||
            specification.Extension.find('.') != std::string::npos)
            throw std::invalid_argument("Save file dialog specification is invalid.");
        auto state = CreateRef<Detail::SaveFileDialogState>();
        auto operation = CreateRef<SaveFileDialogOperation>(std::make_unique<SaveFileDialogOperation::Impl>(state));
        auto request = std::make_unique<SaveFileDialogRequest>();
        request->State = state;
        const auto location =
            specification.DefaultLocation.empty() ? std::string{} : Utf8PathString(specification.DefaultLocation);
        const auto defaultPath = specification.DefaultName.empty()
                                     ? location
                                     : Utf8PathString(specification.DefaultLocation / specification.DefaultName);
        SDL_DialogFileFilter filter{specification.FilterName.c_str(), specification.Extension.c_str()};
        const SDL_DialogFileFilter* filters = specification.Extension.empty() ? nullptr : &filter;
        const int filterCount = filters ? 1 : 0;
        SDL_ShowSaveFileDialog(SaveFileDialogCompleted, request.release(), m_Impl->NativeHandle(parent), filters,
                               filterCount, defaultPath.empty() ? nullptr : defaultPath.c_str());
        return operation;
    }

    void WindowSystem::SetClipboardText(const std::string_view text) { m_Impl->SetClipboardText(text); }

    std::string WindowSystem::ClipboardText() const { return m_Impl->ClipboardText(); }

    void WindowSystem::OpenUrl(const std::string_view url) { m_Impl->OpenUrl(url); }

    Ref<SystemTray> WindowSystem::CreateSystemTray(SystemTraySpecification specification)
    {
        if (specification.Tooltip.size() > 256 || specification.Actions.empty() || specification.Actions.size() > 32)
            throw std::invalid_argument("System tray specification is invalid.");
        for (const auto& action : specification.Actions)
            if (action.Label.empty() || action.Label.size() > 128 || !action.Callback)
                throw std::invalid_argument("System tray actions require a label and callback.");
        auto dispatch = std::make_shared<TrayDispatchState>(specification.Actions);
        auto tray =
            CreateRef<SystemTray>(std::make_unique<SystemTray::Impl>(std::move(specification), std::move(dispatch)));
        m_Impl->RegisterSystemTray(tray, tray->m_Impl->Dispatch);
        return tray;
    }

    void WindowSystem::SetCursorMode(const WindowId window, const CursorMode mode)
    {
        m_Impl->SetCursorMode(window, mode);
    }

    CursorMode WindowSystem::GetCursorMode(const WindowId window) const { return m_Impl->GetCursorMode(window); }

    void WindowSystem::WarpCursor(const WindowId window, const WindowPosition position)
    {
        m_Impl->WarpCursor(window, position);
    }

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

    WindowChromeRole WindowSystemInternalAccess::HitTestChromeLayout(const WindowChromeLayout& layout,
                                                                     const WindowPosition position) noexcept
    {
        return Detail::HitTestWindowChromeLayout(layout, position);
    }

    WindowSystemInternalAccess::EventSinkToken
    WindowSystemInternalAccess::AddEventSink(WindowSystem& system, void* context, const EventSink sink)
    {
        return system.m_Impl->AddEventSink(context, sink);
    }

    void WindowSystemInternalAccess::RemoveEventSink(WindowSystem& system, const EventSinkToken token) noexcept
    {
        system.m_Impl->RemoveEventSink(token);
    }
} // namespace Keire
