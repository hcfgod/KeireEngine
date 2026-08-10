#include "Keire/Window.h"

#include "KeireInternal/TrayIconInternal.h"
#include "KeireInternal/WindowChromeInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{
    static_assert(!noexcept(std::declval<const Keire::Window&>().LogicalSize()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().PixelSize()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().Position()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().DisplayScale()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().Focused()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().Visible()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().Minimized()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().Maximized()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().Mode()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().CloseRequested()));
    static_assert(!noexcept(std::declval<const Keire::Window&>().IsOpen()));
    static_assert(std::is_trivially_copyable_v<Keire::WindowChromeLayout>);

    void UseDummyVideoDriver()
    {
#if defined(_WIN32)
        REQUIRE(_putenv_s("SDL_VIDEODRIVER", "dummy") == 0);
#else
        REQUIRE(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
#endif
        REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    }

    Keire::WindowSpecification HiddenSpecification(const char* title)
    {
        Keire::WindowSpecification specification;
        specification.Title = title;
        specification.Visible = false;
        specification.HighPixelDensity = true;
        return specification;
    }

    SDL_WindowID OnlyNativeWindowId()
    {
        int count = 0;
        SDL_Window** windows = SDL_GetWindows(&count);
        REQUIRE(windows != nullptr);
        REQUIRE(count == 1);
        const auto id = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
        return id;
    }

    template <typename T> std::optional<T> PollFor(const Keire::Ref<Keire::WindowSystem>& system)
    {
        for (int iteration = 0; iteration < 64; ++iteration)
        {
            auto event = system->PollEvent();
            if (!event)
                return std::nullopt;
            if (const auto* typed = std::get_if<T>(&*event))
                return *typed;
        }
        return std::nullopt;
    }
} // namespace

TEST_CASE("Window chrome layouts are bounded and later regions override earlier regions")
{
    Keire::WindowChromeLayout layout;
    REQUIRE(layout.Add({Keire::WindowChromeRole::Drag, {0, 0, 800, 40}}));
    REQUIRE(layout.Add({Keire::WindowChromeRole::ResizeTop, {0, 0, 800, 6}}));
    REQUIRE(layout.Add({Keire::WindowChromeRole::SystemMenu, {0, 0, 40, 40}}));
    REQUIRE(layout.Add({Keire::WindowChromeRole::Minimize, {656, 0, 48, 40}}));
    REQUIRE(layout.Add({Keire::WindowChromeRole::MaximizeRestore, {704, 0, 48, 40}}));
    REQUIRE(layout.Add({Keire::WindowChromeRole::Close, {752, 0, 48, 40}}));

    const auto hit = [&layout](const Keire::WindowPosition position)
    { return Keire::WindowSystemInternalAccess::HitTestChromeLayout(layout, position); };
    CHECK(hit({100, 20}) == Keire::WindowChromeRole::Drag);
    CHECK(hit({100, 2}) == Keire::WindowChromeRole::ResizeTop);
    CHECK(hit({5, 20}) == Keire::WindowChromeRole::SystemMenu);
    CHECK(hit({680, 20}) == Keire::WindowChromeRole::Minimize);
    CHECK(hit({720, 20}) == Keire::WindowChromeRole::MaximizeRestore);
    CHECK(hit({780, 20}) == Keire::WindowChromeRole::Close);
    CHECK(hit({100, 40}) == Keire::WindowChromeRole::Client);
    CHECK(hit({800, 20}) == Keire::WindowChromeRole::Client);
    CHECK_FALSE(layout.Add({Keire::WindowChromeRole::Drag, {0, 0, 0, 40}}));
    CHECK_FALSE(layout.Add({static_cast<Keire::WindowChromeRole>(255), {0, 0, 1, 1}}));

    Keire::WindowChromeLayout full;
    for (std::size_t index = 0; index < Keire::WindowChromeLayout::MaximumRegions; ++index)
        REQUIRE(full.Add({Keire::WindowChromeRole::Client, {static_cast<std::int32_t>(index), 0, 1, 1}}));
    CHECK_FALSE(full.Add({Keire::WindowChromeRole::Client, {0, 0, 1, 1}}));
}

TEST_CASE("Window chrome caption presses activate only on a matching release")
{
    Keire::WindowChromeLayout layout;
    REQUIRE(layout.Add({Keire::WindowChromeRole::Minimize, {656, 0, 48, 40}}));
    REQUIRE(layout.Add({Keire::WindowChromeRole::MaximizeRestore, {704, 0, 48, 40}}));
    REQUIRE(layout.Add({Keire::WindowChromeRole::Close, {752, 0, 48, 40}}));
    Keire::Detail::WindowChromeHitTestCache cache(true);
    cache.Store(layout);

    CHECK_FALSE(cache.BeginCaptionPress(Keire::WindowChromeRole::Drag));
    auto activated = Keire::WindowChromeRole::Close;
    CHECK_FALSE(cache.CompleteCaptionPress({720, 20}, activated));
    CHECK(activated == Keire::WindowChromeRole::Client);

    REQUIRE(cache.BeginCaptionPress(Keire::WindowChromeRole::MaximizeRestore));
    CHECK(cache.CompleteCaptionPress({720, 20}, activated));
    CHECK(activated == Keire::WindowChromeRole::MaximizeRestore);

    REQUIRE(cache.BeginCaptionPress(Keire::WindowChromeRole::Minimize));
    CHECK(cache.CompleteCaptionPress({780, 20}, activated));
    CHECK(activated == Keire::WindowChromeRole::Client);

    REQUIRE(cache.BeginCaptionPress(Keire::WindowChromeRole::Close));
    CHECK(cache.CancelCaptionPress());
    CHECK_FALSE(cache.CancelCaptionPress());
    CHECK_FALSE(cache.CompleteCaptionPress({780, 20}, activated));
    CHECK(activated == Keire::WindowChromeRole::Client);
}

TEST_CASE("WindowSystem initialization failures retain SDL diagnostics")
{
#if defined(_WIN32)
    REQUIRE(_putenv_s("SDL_VIDEODRIVER", "keire-invalid-driver") == 0);
#else
    REQUIRE(setenv("SDL_VIDEODRIVER", "keire-invalid-driver", 1) == 0);
#endif
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "keire-invalid-driver", SDL_HINT_OVERRIDE));
    try
    {
        (void)Keire::CreateRef<Keire::WindowSystem>();
        FAIL("expected WindowError");
    }
    catch (const Keire::WindowError& error)
    {
        CHECK(error.Operation() == "SDL_Init(SDL_INIT_VIDEO)");
        CHECK_FALSE(error.Diagnostic().empty());
    }
    UseDummyVideoDriver();
}

TEST_CASE("WindowSystem enforces one active instance and supports reinitialization")
{
    UseDummyVideoDriver();
    auto first = Keire::CreateRef<Keire::WindowSystem>();
    CHECK(first->IsActive());
    CHECK_THROWS_AS(Keire::CreateRef<Keire::WindowSystem>(), std::logic_error);
    first->Shutdown();
    CHECK_FALSE(first->IsActive());
    first.Reset();

    auto second = Keire::CreateRef<Keire::WindowSystem>();
    CHECK(Keire::GetSystemColorScheme() >= Keire::SystemColorScheme::Unknown);
    CHECK(Keire::GetSystemColorScheme() <= Keire::SystemColorScheme::Dark);
    CHECK(second->IsActive());
    second->Shutdown();
}

TEST_CASE("WindowSystem creates and routes multiple opaque window identities")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto first = system->CreateWindow(HiddenSpecification("first"));
    auto second = system->CreateWindow(HiddenSpecification("second"));
    CHECK(first->Id());
    CHECK(second->Id());
    CHECK(first->Id() != second->Id());
    CHECK(first->Title() == "first");
    CHECK((first->LogicalSize() == Keire::LogicalExtent{1280, 720}));
    CHECK(first->PixelSize().Width > 0);

    first->SetTitle("renamed");
    first->SetSize({640, 360});
    first->SetPosition({24, 48});
    CHECK(first->Title() == "renamed");
    CHECK((first->LogicalSize() == Keire::LogicalExtent{640, 360}));
    CHECK((first->Position() == Keire::WindowPosition{24, 48}));
    first->SetVisible(true);
    CHECK(first->Visible());
    first->SetVisible(false);
    CHECK_FALSE(first->Visible());
    first->SetMode(Keire::WindowMode::BorderlessFullscreen);
    CHECK(first->Mode() == Keire::WindowMode::BorderlessFullscreen);
    first->SetMode(Keire::WindowMode::Windowed);
    CHECK(first->Mode() == Keire::WindowMode::Windowed);

    first->Close();
    first->Close();
    CHECK_FALSE(first->IsOpen());
    CHECK(first->Title() == "renamed");
    second->Close();
    first.Reset();
    second.Reset();
    system->Shutdown();
}

TEST_CASE("WindowSystem reports explicit Linux folder dialog backend failures")
{
#if defined(__linux__)
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto window = system->CreateWindow(HiddenSpecification("folder dialog failure"));
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_FILE_DIALOG_DRIVER, "keire-invalid-driver", SDL_HINT_OVERRIDE));

    const auto operation = system->ShowFolderDialog(window->Id(), std::filesystem::current_path());
    CHECK(operation->Status() == Keire::FolderDialogStatus::Failed);
    CHECK_FALSE(operation->Diagnostic().empty());

    SDL_ResetHint(SDL_HINT_FILE_DIALOG_DRIVER);
    window->Close();
    window.Reset();
    system->Shutdown();
#else
    CHECK(true);
#endif
}

TEST_CASE("WindowSystem enforces logical minimums and safely falls back from unsupported custom chrome")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto specification = HiddenSpecification("custom chrome fallback");
    specification.Width = 800;
    specification.Height = 600;
    specification.MinimumWidth = 640;
    specification.MinimumHeight = 360;
    specification.Decoration = Keire::WindowDecoration::Custom;

    auto invalid = specification;
    invalid.MinimumWidth = invalid.Width + 1;
    CHECK_THROWS_AS((void)system->CreateWindow(invalid), std::invalid_argument);
    invalid = specification;
    invalid.MinimumHeight = 0;
    CHECK_THROWS_AS((void)system->CreateWindow(invalid), std::invalid_argument);
    invalid = specification;
    invalid.Decoration = static_cast<Keire::WindowDecoration>(255);
    CHECK_THROWS_AS((void)system->CreateWindow(invalid), std::invalid_argument);

    auto window = system->CreateWindow(specification);

    const auto effective = window->Specification();
    CHECK(effective.Decoration == Keire::WindowDecoration::Native);
    CHECK(effective.MinimumWidth == 640);
    CHECK(effective.MinimumHeight == 360);
    int minimumWidth = 0;
    int minimumHeight = 0;
    SDL_Window* native = Keire::WindowSystemInternalAccess::NativeWindow(*system, window->Id());
    REQUIRE(native != nullptr);
    CHECK(SDL_GetWindowMinimumSize(native, &minimumWidth, &minimumHeight));
    CHECK(minimumWidth == 640);
    CHECK(minimumHeight == 360);

    const auto original = window->LogicalSize();
    CHECK_THROWS_AS(window->SetSize({639, 360}), std::invalid_argument);
    CHECK(window->LogicalSize() == original);
    Keire::WindowChromeLayout layout;
    REQUIRE(layout.Add({Keire::WindowChromeRole::Drag, {0, 0, 800, 40}}));
    CHECK_NOTHROW(window->SetChromeLayout(layout));
    CHECK(window->Specification().Decoration == Keire::WindowDecoration::Native);

    window->Close();
    window.Reset();
    system->Shutdown();
}

TEST_CASE("WindowSystem rejects custom chrome updates from worker threads without changing state")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto window = system->CreateWindow(HiddenSpecification("chrome threading"));
    Keire::WindowChromeLayout layout;
    REQUIRE(layout.Add({Keire::WindowChromeRole::Drag, {0, 0, 100, 40}}));
    std::atomic<bool> rejected = false;
    std::thread worker(
        [&window, &layout, &rejected]
        {
            try
            {
                window->SetChromeLayout(layout);
            }
            catch (const std::logic_error&)
            {
                rejected.store(true, std::memory_order_release);
            }
        });
    worker.join();
    CHECK(rejected.load(std::memory_order_acquire));
    CHECK(window->Specification().Decoration == Keire::WindowDecoration::Native);

    window->Close();
    window.Reset();
    system->Shutdown();
}

TEST_CASE("WindowSystem preserves requested cursor modes across focus changes")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto window = system->CreateWindow(HiddenSpecification("cursor-modes"));
    CHECK(system->GetCursorMode(window->Id()) == Keire::CursorMode::Normal);
    system->SetCursorMode(window->Id(), Keire::CursorMode::Hidden);
    CHECK(system->GetCursorMode(window->Id()) == Keire::CursorMode::Hidden);

    SDL_Event lost{};
    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    lost.window.windowID = OnlyNativeWindowId();
    REQUIRE(SDL_PushEvent(&lost));
    while (system->PollEvent())
    {
    }
    CHECK(system->GetCursorMode(window->Id()) == Keire::CursorMode::Hidden);

    SDL_Event gained{};
    gained.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    gained.window.windowID = OnlyNativeWindowId();
    REQUIRE(SDL_PushEvent(&gained));
    while (system->PollEvent())
    {
    }
    CHECK(system->GetCursorMode(window->Id()) == Keire::CursorMode::Hidden);
    system->SetCursorMode(window->Id(), Keire::CursorMode::Normal);
    CHECK_NOTHROW(system->WarpCursor(window->Id(), {0, 0}));
    window->Close();
    window.Reset();
    system->Shutdown();
}

TEST_CASE("WindowSystem translates movement before exposing cached state")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto window = system->CreateWindow(HiddenSpecification("state-events"));
    while (system->PollEvent())
    {
    }
    const auto nativeId = OnlyNativeWindowId();
    const auto push = [nativeId](const std::uint32_t type, const int data1 = 0, const int data2 = 0)
    {
        SDL_Event event{};
        event.type = type;
        event.window.timestamp = 42;
        event.window.windowID = nativeId;
        event.window.data1 = data1;
        event.window.data2 = data2;
        REQUIRE(SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0) == 1);
    };

    push(SDL_EVENT_WINDOW_MOVED, -12, 34);
    REQUIRE(PollFor<Keire::WindowMovedEvent>(system).has_value());
    CHECK((window->Position() == Keire::WindowPosition{-12, 34}));
    window->Close();
    window.Reset();
    system->Shutdown();
}

TEST_CASE("Window events update cached state before delivery")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto window = system->CreateWindow(HiddenSpecification("events"));
    window->SetVisible(true);

    bool sawShown = false;
    for (int iteration = 0; iteration < 32; ++iteration)
    {
        const auto event = system->PollEvent();
        if (!event)
            break;
        if (const auto* shown = std::get_if<Keire::WindowShownEvent>(&*event);
            shown && shown->Header.Window == window->Id())
        {
            sawShown = true;
            CHECK(window->Visible());
        }
    }
    CHECK(sawShown);
    window->Close();
    window.Reset();
    system->Shutdown();
}

TEST_CASE("WindowSystem translates close and global quit events")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto window = system->CreateWindow(HiddenSpecification("routing"));
    while (system->PollEvent())
    {
    }

    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.timestamp = 123456789;
    close.window.windowID = OnlyNativeWindowId();
    REQUIRE(SDL_PushEvent(&close));

    const auto closeEvent = system->PollEvent();
    REQUIRE(closeEvent.has_value());
    const auto* translatedClose = std::get_if<Keire::WindowCloseRequestedEvent>(&*closeEvent);
    REQUIRE(translatedClose != nullptr);
    CHECK(translatedClose->Header.Window == window->Id());
    CHECK(translatedClose->Header.TimestampNanoseconds == 123456789);
    CHECK(window->CloseRequested());

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    quit.common.timestamp = 987654321;
    REQUIRE(SDL_PushEvent(&quit));
    std::optional<Keire::WindowEvent> quitEvent;
    for (int iteration = 0; iteration < 32 && !quitEvent; ++iteration)
    {
        auto candidate = system->PollEvent();
        if (candidate && std::holds_alternative<Keire::QuitEvent>(*candidate))
            quitEvent = std::move(candidate);
    }
    REQUIRE(quitEvent.has_value());
    const auto* translatedQuit = std::get_if<Keire::QuitEvent>(&*quitEvent);
    REQUIRE(translatedQuit != nullptr);
    CHECK_FALSE(translatedQuit->Header.Window);
    CHECK(translatedQuit->Header.TimestampNanoseconds == 987654321);

    window->Close();
    window.Reset();
    system->Shutdown();
}

TEST_CASE("WindowSystem aggregates platform file-drop sessions without exposing SDL")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto window = system->CreateWindow(HiddenSpecification("file drop"));
    while (system->PollEvent())
    {
    }
    const auto native = OnlyNativeWindowId();
    const auto firstUtf8 = std::filesystem::absolute("First Asset.png").u8string();
    const auto secondPath = std::filesystem::absolute(std::filesystem::path(u8"Unicode-é.obj"));
    const auto secondUtf8 = secondPath.u8string();
    SDL_Event begin{};
    begin.type = SDL_EVENT_DROP_BEGIN;
    begin.drop.windowID = native;
    REQUIRE(SDL_PushEvent(&begin));
    SDL_Event file{};
    file.type = SDL_EVENT_DROP_FILE;
    file.drop.windowID = native;
    file.drop.x = 42.0F;
    file.drop.y = 84.0F;
    file.drop.data = reinterpret_cast<const char*>(firstUtf8.c_str());
    REQUIRE(SDL_PushEvent(&file));
    file.drop.data = reinterpret_cast<const char*>(secondUtf8.c_str());
    REQUIRE(SDL_PushEvent(&file));
    SDL_Event complete{};
    complete.type = SDL_EVENT_DROP_COMPLETE;
    complete.drop.windowID = native;
    REQUIRE(SDL_PushEvent(&complete));

    const auto event = PollFor<Keire::WindowFileDropEvent>(system);
    REQUIRE(event);
    CHECK(event->Header.Window == window->Id());
    CHECK(event->Position == Keire::WindowPosition{42, 84});
    REQUIRE(event->Paths.size() == 2);
    CHECK(event->Paths[0].filename() == std::filesystem::path("First Asset.png"));
    const bool unicodePathPreserved = event->Paths[1].filename() == std::filesystem::path(u8"Unicode-é.obj");
    CHECK(unicodePathPreserved);
    window->Close();
    window.Reset();
    system->Shutdown();
}

TEST_CASE("Window operations are owner-thread-affine and worker release is deferred")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto window = system->CreateWindow(HiddenSpecification("threading"));
    std::atomic<bool> rejected = false;
    std::atomic<bool> shutdownRejected = false;
    auto workerCopy = window;
    std::thread worker(
        [worker = std::move(workerCopy), &rejected]() mutable
        {
            try
            {
                worker->SetTitle("invalid");
            }
            catch (const std::logic_error&)
            {
                rejected.store(true, std::memory_order_release);
            }
            worker.Reset();
        });
    window.Reset();
    worker.join();
    CHECK(rejected.load(std::memory_order_acquire));
    std::thread shutdownWorker(
        [&system, &shutdownRejected]
        {
            try
            {
                system->Shutdown();
            }
            catch (const std::logic_error&)
            {
                shutdownRejected.store(true, std::memory_order_release);
            }
        });
    shutdownWorker.join();
    CHECK(shutdownRejected.load(std::memory_order_acquire));
    (void)system->PollEvent();
    system->Shutdown();
}

TEST_CASE("Shutdown makes surviving window handles inert")
{
    UseDummyVideoDriver();
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto window = system->CreateWindow(HiddenSpecification("inert"));
    system->Shutdown();
    CHECK_FALSE(window->IsOpen());
    CHECK(window->Title() == "inert");
    CHECK_NOTHROW(window->Close());
    CHECK_NOTHROW(window->SetVisible(false));
    window.Reset();
}

TEST_CASE("Window system shutdown closes surviving system tray handles")
{
    UseDummyVideoDriver();
    const auto iconPath = std::filesystem::current_path() / "Config/Branding/Keire.png";
    const auto trayIcon = Keire::Detail::LoadTrayIcon(iconPath);
    const auto windowIcon = Keire::Detail::LoadWindowIcon(iconPath);
    REQUIRE(trayIcon);
    REQUIRE(windowIcon);
    CHECK(trayIcon->w == 1024);
    CHECK(trayIcon->h == 1024);
    CHECK(windowIcon->w == 256);
    CHECK(windowIcon->h == 256);
    auto system = Keire::CreateRef<Keire::WindowSystem>();
    auto tray = system->CreateSystemTray(
        {.Icon = iconPath, .Tooltip = "Kéire tray shutdown test", .Actions = {{"Close", [] {}}}});
    system->Shutdown();
    CHECK_NOTHROW(tray->Close());
    CHECK_FALSE(tray->IsAvailable());
    tray.Reset();
}
