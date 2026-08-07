#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#if defined(CreateWindow)
#undef CreateWindow
#endif

namespace Keire
{
    namespace Detail
    {
        class FolderDialogState;
        class OpenFileDialogState;
    } // namespace Detail

    enum class WindowMode : std::uint8_t
    {
        Windowed,
        BorderlessFullscreen
    };

    enum class CursorMode : std::uint8_t
    {
        Normal,
        Hidden,
        Confined,
        RelativeLocked
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

    struct WindowSystemSpecification
    {
        std::string ApplicationName;
        std::string ApplicationVersion;
        std::string ApplicationIdentifier;
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

    struct DisplayBounds
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        auto operator<=>(const DisplayBounds&) const noexcept = default;
    };

    struct DisplayInformation
    {
        std::uint32_t Index = 0;
        std::string Name;
        DisplayBounds Bounds;
        DisplayBounds UsableBounds;
        float ContentScale = 1.0F;
        bool Primary = false;
    };

    struct WindowEventHeader
    {
        std::uint64_t TimestampNanoseconds = 0;
        WindowId Window;
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

    struct WindowFileDropEvent
    {
        WindowEventHeader Header;
        WindowPosition Position;
        std::vector<std::filesystem::path> Paths;
    };

    using WindowEvent =
        std::variant<QuitEvent, WindowCloseRequestedEvent, WindowShownEvent, WindowHiddenEvent, WindowMovedEvent,
                     WindowResizedEvent, WindowPixelSizeChangedEvent, WindowMinimizedEvent, WindowMaximizedEvent,
                     WindowRestoredEvent, WindowFocusGainedEvent, WindowFocusLostEvent, WindowDisplayChangedEvent,
                     WindowDisplayScaleChangedEvent, WindowEnteredFullscreenEvent, WindowLeftFullscreenEvent,
                     WindowFileDropEvent>;

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
        [[nodiscard]] virtual LogicalExtent LogicalSize() const = 0;
        [[nodiscard]] virtual PixelExtent PixelSize() const = 0;
        [[nodiscard]] virtual WindowPosition Position() const = 0;
        [[nodiscard]] virtual float DisplayScale() const = 0;
        [[nodiscard]] virtual std::string Title() const = 0;
        [[nodiscard]] virtual bool Focused() const = 0;
        [[nodiscard]] virtual bool Visible() const = 0;
        [[nodiscard]] virtual bool Minimized() const = 0;
        [[nodiscard]] virtual bool Maximized() const = 0;
        [[nodiscard]] virtual WindowMode Mode() const = 0;
        [[nodiscard]] virtual bool CloseRequested() const = 0;
        [[nodiscard]] virtual bool IsOpen() const = 0;

        virtual void SetTitle(std::string title) = 0;
        virtual void SetSize(LogicalExtent size) = 0;
        virtual void SetPosition(WindowPosition)
        {
            throw std::logic_error("This Window implementation does not support positioning.");
        }
        virtual void SetVisible(bool visible) = 0;
        virtual void Minimize() = 0;
        virtual void Maximize() = 0;
        virtual void Restore() = 0;
        virtual void Raise() = 0;
        virtual void SetMode(WindowMode mode) = 0;
        virtual void Close() = 0;
    };

    enum class FolderDialogStatus : std::uint8_t
    {
        Pending,
        Selected,
        Cancelled,
        Failed
    };

    class KEIRE_API FolderDialogOperation final : public RefCounted
    {
      public:
        ~FolderDialogOperation() override;
        [[nodiscard]] FolderDialogStatus Status() const noexcept;
        [[nodiscard]] std::filesystem::path SelectedPath() const;
        [[nodiscard]] std::string Diagnostic() const;

      private:
        friend class WindowSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit FolderDialogOperation(Ref<Detail::FolderDialogState> state);
        Ref<Detail::FolderDialogState> m_State;
    };

    enum class OpenFileDialogStatus : std::uint8_t
    {
        Pending,
        Selected,
        Cancelled,
        Failed
    };

    struct OpenFileDialogSpecification
    {
        std::string Title = "Open File";
        std::filesystem::path DefaultLocation;
        std::string FilterName;
        std::string Extension;
    };

    class KEIRE_API OpenFileDialogOperation final : public RefCounted
    {
      public:
        ~OpenFileDialogOperation() override;
        [[nodiscard]] OpenFileDialogStatus Status() const noexcept;
        [[nodiscard]] std::filesystem::path SelectedPath() const;
        [[nodiscard]] std::string Diagnostic() const;

      private:
        friend class WindowSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit OpenFileDialogOperation(Ref<Detail::OpenFileDialogState> state);
        Ref<Detail::OpenFileDialogState> m_State;
    };

    enum class SaveFileDialogStatus : std::uint8_t
    {
        Pending,
        Selected,
        Cancelled,
        Failed
    };

    struct SaveFileDialogSpecification
    {
        std::string Title = "Save File";
        std::filesystem::path DefaultLocation;
        std::string DefaultName;
        std::string FilterName;
        std::string Extension;
    };

    class KEIRE_API SaveFileDialogOperation final : public RefCounted
    {
      public:
        class Impl;
        ~SaveFileDialogOperation() override;
        [[nodiscard]] SaveFileDialogStatus Status() const noexcept;
        [[nodiscard]] std::filesystem::path SelectedPath() const;
        [[nodiscard]] std::string Diagnostic() const;

      private:
        friend class WindowSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit SaveFileDialogOperation(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    struct SystemTrayAction
    {
        std::string Label;
        std::function<void()> Callback;
    };

    struct SystemTraySpecification
    {
        std::filesystem::path Icon;
        std::string Tooltip = "Kéire";
        std::vector<SystemTrayAction> Actions;
    };

    class KEIRE_API SystemTray final : public RefCounted
    {
      public:
        class Impl;
        ~SystemTray() override;
        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] std::string Diagnostic() const;
        void Close() noexcept;

      private:
        friend class WindowSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit SystemTray(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API WindowSystem final : public RefCounted
    {
      public:
        explicit WindowSystem(WindowSystemSpecification specification = {});
        ~WindowSystem() override;

        [[nodiscard]] Ref<Window> CreateWindow(const WindowSpecification& specification = {});
        [[nodiscard]] std::vector<DisplayInformation> Displays() const;
        [[nodiscard]] std::optional<WindowEvent> PollEvent();
        [[nodiscard]] Ref<FolderDialogOperation> ShowFolderDialog(WindowId parent,
                                                                  const std::filesystem::path& defaultLocation = {});
        [[nodiscard]] Ref<OpenFileDialogOperation> ShowOpenFileDialog(WindowId parent,
                                                                      const OpenFileDialogSpecification& specification);
        [[nodiscard]] Ref<SaveFileDialogOperation> ShowSaveFileDialog(WindowId parent,
                                                                      const SaveFileDialogSpecification& specification);
        void SetClipboardText(std::string_view text);
        [[nodiscard]] std::string ClipboardText() const;
        void OpenUrl(std::string_view url);
        [[nodiscard]] Ref<SystemTray> CreateSystemTray(SystemTraySpecification specification);
        void SetCursorMode(WindowId window, CursorMode mode);
        [[nodiscard]] CursorMode GetCursorMode(WindowId window) const;
        void WarpCursor(WindowId window, WindowPosition position);
        void Shutdown();
        [[nodiscard]] bool IsActive() const noexcept;

      private:
        friend class WindowSystemInternalAccess;
        class Impl;
        Ref<Impl> m_Impl;
    };

} // namespace Keire
