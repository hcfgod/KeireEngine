#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#if defined(CreateWindow)
#undef CreateWindow
#endif

namespace Keire
{
    enum class WindowMode : std::uint8_t
    {
        Windowed,
        BorderlessFullscreen
    };

    struct WindowSpecification
    {
        std::string Title = "Kéire";
        std::uint32_t Width = 1280;
        std::uint32_t Height = 720;
        bool Resizable = true;
        bool HighPixelDensity = true;
        bool Visible = true;
        bool Maximized = false;
        WindowMode Mode = WindowMode::Windowed;
    };

    class WindowId
    {
      public:
        constexpr WindowId() noexcept = default;
        explicit constexpr WindowId(const std::uint32_t value) noexcept : m_Value(value) {}

        [[nodiscard]] constexpr std::uint32_t Value() const noexcept { return m_Value; }
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] auto operator<=>(const WindowId&) const noexcept = default;

      private:
        std::uint32_t m_Value = 0;
    };

    struct LogicalExtent
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        auto operator<=>(const LogicalExtent&) const noexcept = default;
    };

    struct PixelExtent
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        auto operator<=>(const PixelExtent&) const noexcept = default;
    };

    struct WindowPosition
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;
        auto operator<=>(const WindowPosition&) const noexcept = default;
    };

    struct WindowEventHeader
    {
        std::uint64_t TimestampNanoseconds = 0;
        WindowId Window{};
    };

    struct QuitEvent
    {
        WindowEventHeader Header;
    };

    struct WindowCloseRequestedEvent
    {
        WindowEventHeader Header;
    };

    struct WindowShownEvent
    {
        WindowEventHeader Header;
    };

    struct WindowHiddenEvent
    {
        WindowEventHeader Header;
    };

    struct WindowMovedEvent
    {
        WindowEventHeader Header;
        WindowPosition Position;
    };

    struct WindowResizedEvent
    {
        WindowEventHeader Header;
        LogicalExtent Size;
    };

    struct WindowPixelSizeChangedEvent
    {
        WindowEventHeader Header;
        PixelExtent Size;
    };

    struct WindowMinimizedEvent
    {
        WindowEventHeader Header;
    };

    struct WindowMaximizedEvent
    {
        WindowEventHeader Header;
    };

    struct WindowRestoredEvent
    {
        WindowEventHeader Header;
    };

    struct WindowFocusGainedEvent
    {
        WindowEventHeader Header;
    };

    struct WindowFocusLostEvent
    {
        WindowEventHeader Header;
    };

    struct WindowDisplayChangedEvent
    {
        WindowEventHeader Header;
    };

    struct WindowDisplayScaleChangedEvent
    {
        WindowEventHeader Header;
        float Scale = 1.0F;
    };

    struct WindowEnteredFullscreenEvent
    {
        WindowEventHeader Header;
    };

    struct WindowLeftFullscreenEvent
    {
        WindowEventHeader Header;
    };

    using WindowEvent =
        std::variant<QuitEvent, WindowCloseRequestedEvent, WindowShownEvent, WindowHiddenEvent, WindowMovedEvent,
                     WindowResizedEvent, WindowPixelSizeChangedEvent, WindowMinimizedEvent, WindowMaximizedEvent,
                     WindowRestoredEvent, WindowFocusGainedEvent, WindowFocusLostEvent, WindowDisplayChangedEvent,
                     WindowDisplayScaleChangedEvent, WindowEnteredFullscreenEvent, WindowLeftFullscreenEvent>;

    class KEIRE_API WindowError final : public std::runtime_error
    {
      public:
        WindowError(std::string operation, std::string diagnostic);
        [[nodiscard]] const std::string& Operation() const noexcept { return m_Operation; }
        [[nodiscard]] const std::string& Diagnostic() const noexcept { return m_Diagnostic; }

      private:
        std::string m_Operation;
        std::string m_Diagnostic;
    };

    class KEIRE_API Window : public RefCounted
    {
      public:
        ~Window() override = default;

        [[nodiscard]] virtual WindowId Id() const noexcept = 0;
        [[nodiscard]] virtual WindowSpecification Specification() const = 0;
        [[nodiscard]] virtual LogicalExtent LogicalSize() const noexcept = 0;
        [[nodiscard]] virtual PixelExtent PixelSize() const noexcept = 0;
        [[nodiscard]] virtual WindowPosition Position() const noexcept = 0;
        [[nodiscard]] virtual float DisplayScale() const noexcept = 0;
        [[nodiscard]] virtual std::string Title() const = 0;
        [[nodiscard]] virtual bool Focused() const noexcept = 0;
        [[nodiscard]] virtual bool Visible() const noexcept = 0;
        [[nodiscard]] virtual bool Minimized() const noexcept = 0;
        [[nodiscard]] virtual bool Maximized() const noexcept = 0;
        [[nodiscard]] virtual WindowMode Mode() const noexcept = 0;
        [[nodiscard]] virtual bool CloseRequested() const noexcept = 0;
        [[nodiscard]] virtual bool IsOpen() const noexcept = 0;

        virtual void SetTitle(std::string title) = 0;
        virtual void SetSize(LogicalExtent size) = 0;
        virtual void SetVisible(bool visible) = 0;
        virtual void Minimize() = 0;
        virtual void Maximize() = 0;
        virtual void Restore() = 0;
        virtual void SetMode(WindowMode mode) = 0;
        virtual void Close() = 0;
    };

    class KEIRE_API WindowSystem final : public RefCounted
    {
      public:
        WindowSystem();
        ~WindowSystem() override;

        [[nodiscard]] Ref<Window> CreateWindow(const WindowSpecification& specification = {});
        [[nodiscard]] std::optional<WindowEvent> PollEvent();
        void Shutdown();
        [[nodiscard]] bool IsActive() const noexcept;

      private:
        class Impl;
        Ref<Impl> m_Impl;
    };

} // namespace Keire
