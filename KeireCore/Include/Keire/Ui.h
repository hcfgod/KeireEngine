#pragma once

#include "Keire/Api.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"
#include "Keire/UiWorkspace.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    class RenderSurface;

    enum class UiMode : std::uint8_t
    {
        Disabled,
        Headless,
        Rendered
    };

    enum class UiPresentMode : std::uint8_t
    {
        VSync,
        Mailbox,
        Immediate
    };

    enum class UiFontRole : std::uint8_t
    {
        Body,
        Heading,
        Monospace,
        Icons
    };

    enum class UiStyleColorRole : std::uint8_t
    {
        Text,
        WindowBackground,
        ChildBackground,
        PopupBackground,
        Border,
        FrameBackground,
        FrameBackgroundHovered,
        FrameBackgroundActive,
        Button,
        ButtonHovered,
        ButtonActive,
        Header,
        HeaderHovered,
        HeaderActive
    };

    enum class UiStyleVariable : std::uint8_t
    {
        WindowPadding,
        WindowRounding,
        WindowBorderSize,
        ChildRounding,
        ChildBorderSize,
        FramePadding,
        FrameRounding,
        ItemSpacing
    };

    struct UiFontSpecification
    {
        UiFontRole Role = UiFontRole::Body;
        std::filesystem::path Path;
        float SizePixels = 16.0F;
    };

    struct UiSpecification
    {
        UiMode Mode = UiMode::Disabled;
        UiPresentMode PresentMode = UiPresentMode::VSync;
        UiTheme Theme = UiTheme::Dark;
        UiColor ClearColor;
        std::filesystem::path LayoutPath;
        UiWorkspaceSpecification Workspace;
        bool EnableDocking = true;
        bool EnableKeyboardNavigation = true;
        bool EnableGpuValidation = false;
        std::vector<UiFontSpecification> Fonts;
    };

    struct UiCaptureState
    {
        bool Pointer = false;
        bool Keyboard = false;
        bool TextInput = false;
    };

    enum class UiAxis : std::uint8_t
    {
        Horizontal,
        Vertical
    };

    enum class UiKey : std::uint8_t
    {
        Enter,
        Escape,
        Tab,
        Delete,
        F2,
        A,
        B,
        Backspace,
        C,
        D,
        Down,
        E,
        F,
        Left,
        Q,
        R,
        Right,
        S,
        Up,
        V,
        W,
        X,
        Y,
        Z
    };

    struct UiShortcut
    {
        UiKey Key = UiKey::Enter;
        bool Control = false;
        bool Shift = false;
        bool Alt = false;
        bool Primary = false;
        bool Global = false;
    };

    struct UiItemState
    {
        bool Hovered = false;
        bool Active = false;
        bool Activated = false;
        bool Edited = false;
        bool DeactivatedAfterEdit = false;
        bool DoubleClicked = false;
    };

    struct UiItemRect
    {
        UiPosition Minimum;
        UiPosition Maximum;

        [[nodiscard]] UiSize Size() const noexcept { return {Maximum.X - Minimum.X, Maximum.Y - Minimum.Y}; }

        [[nodiscard]] bool Contains(const UiPosition point) const noexcept
        {
            return point.X >= Minimum.X && point.Y >= Minimum.Y && point.X <= Maximum.X && point.Y <= Maximum.Y;
        }
    };

    struct UiPointerState
    {
        UiPosition Position;
        UiPosition Delta;
        float Wheel = 0.0F;
        bool LeftDown = false;
        bool MiddleDown = false;
        bool RightDown = false;
        bool LeftPressed = false;
        bool MiddlePressed = false;
        bool RightPressed = false;
        bool LeftReleased = false;
        bool MiddleReleased = false;
        bool RightReleased = false;
    };

    struct UiTooltipOptions
    {
        bool Delayed = false;
    };

    enum class UiIcon : std::uint8_t
    {
        Play,
        Stop,
        Pause,
        Step,
        View,
        Translate,
        Rotate,
        Scale,
        Local,
        Global,
        Snap,
        Settings,
        Camera,
        Perspective,
        Orthographic,
        AxisX,
        AxisY,
        AxisZ,
        Create,
        Search,
        Filter,
        Lock,
        Unlock,
        Physics,
        ColliderEdit,
        Folder,
        Refresh,
        List,
        Grid,
        Warning,
        Information,
        Close,
        Minimize,
        Maximize,
        Restore,
        Home,
        Documentation,
        LightMode,
        DarkMode,
        Notifications,
        Download,
        More,
        OpenExternal,
        Favorite,
        License,
        Learn,
        Build,
        Package,
        Bug,
        Link,
        Copy,
        Description
    };

    struct UiOverlayIconButtonSpecification
    {
        UiPosition Position;
        UiSize Size{30.0F, 28.0F};
        std::string_view Tooltip;
        bool Selected = false;
        bool Enabled = true;
    };

    enum class UiTableSizing : std::uint8_t
    {
        Proportional,
        Equal
    };

    enum class UiTableColumnSizing : std::uint8_t
    {
        Automatic,
        Fixed,
        Stretch
    };

    struct UiTableOptions
    {
        UiTableSizing Sizing = UiTableSizing::Proportional;
        bool Borders = true;
        bool Resizable = true;
        bool RowBackground = true;
        bool PersistSettings = true;
    };

    struct UiWindowOptions
    {
        bool MenuBar = false;
        bool NoTitleBar = false;
        bool NoResize = false;
        bool NoMove = false;
        bool NoCollapse = false;
        bool NoSavedSettings = false;
    };

    class KEIRE_API UiError final : public std::runtime_error
    {
      public:
        UiError(std::string operation, std::string diagnostic);

        [[nodiscard]] const std::string& Operation() const noexcept { return m_Operation; }
        [[nodiscard]] const std::string& Diagnostic() const noexcept { return m_Diagnostic; }

      private:
        std::string m_Operation;
        std::string m_Diagnostic;
    };

    class UiFrame;

    class KEIRE_API UiImage final : public RefCounted
    {
      public:
        class Impl;
        ~UiImage() override;
        [[nodiscard]] std::uint32_t Width() const noexcept;
        [[nodiscard]] std::uint32_t Height() const noexcept;

      private:
        friend class UiFrame;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);
        explicit UiImage(std::unique_ptr<Impl> implementation);
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API UiScope
    {
      public:
        UiScope(const UiScope&) = delete;
        UiScope& operator=(const UiScope&) = delete;
        UiScope(UiScope&& other) noexcept;
        UiScope& operator=(UiScope&& other) noexcept;
        ~UiScope();

        [[nodiscard]] explicit operator bool() const noexcept { return m_Visible; }
        [[nodiscard]] bool Visible() const noexcept { return m_Visible; }

      protected:
        enum class Kind : std::uint8_t
        {
            Window,
            Child,
            MenuBar,
            Menu,
            TabBar,
            TabItem,
            TreeNode,
            Disabled,
            Id,
            MainMenuBar,
            Combo,
            Popup,
            Table,
            DragSource,
            DragTarget,
            Clip,
            Font,
            StyleColor,
            StyleVariable
        };

        UiScope(UiFrame& frame, Kind kind, bool visible, bool closeRequired) noexcept;

      private:
        friend class UiFrame;
        void Reset() noexcept;

        UiFrame* m_Frame = nullptr;
        std::weak_ptr<void> m_Lifetime;
        std::uint64_t m_Generation = 0;
        Kind m_Kind = Kind::Window;
        bool m_Visible = false;
        bool m_CloseRequired = false;
    };

    class KEIRE_API UiWindowScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiWindowScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::Window, visible, true) {}
    };

    class KEIRE_API UiChildScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiChildScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::Child, visible, true) {}
    };

    class KEIRE_API UiMenuBarScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiMenuBarScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::MenuBar, visible, visible) {}
    };

    class KEIRE_API UiMenuScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiMenuScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::Menu, visible, visible) {}
    };

    class KEIRE_API UiTabBarScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiTabBarScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::TabBar, visible, visible) {}
    };

    class KEIRE_API UiTabItemScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiTabItemScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::TabItem, visible, visible) {}
    };

    class KEIRE_API UiTreeNodeScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiTreeNodeScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::TreeNode, visible, visible) {}
    };

    class KEIRE_API UiDisabledScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiDisabledScope(UiFrame& frame) noexcept : UiScope(frame, Kind::Disabled, true, true) {}
    };

    class KEIRE_API UiIdScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiIdScope(UiFrame& frame) noexcept : UiScope(frame, Kind::Id, true, true) {}
    };

    class KEIRE_API UiMainMenuBarScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiMainMenuBarScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::MainMenuBar, visible, visible)
        {
        }
    };

    class KEIRE_API UiComboScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiComboScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::Combo, visible, visible) {}
    };

    class KEIRE_API UiPopupScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiPopupScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::Popup, visible, visible) {}
    };

    class KEIRE_API UiTableScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiTableScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::Table, visible, visible) {}
    };

    class KEIRE_API UiDragSourceScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiDragSourceScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::DragSource, visible, visible) {}
    };

    class KEIRE_API UiDragTargetScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiDragTargetScope(UiFrame& frame, bool visible) noexcept : UiScope(frame, Kind::DragTarget, visible, visible) {}
    };

    class KEIRE_API UiClipScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiClipScope(UiFrame& frame) noexcept : UiScope(frame, Kind::Clip, true, true) {}
    };

    class KEIRE_API UiFontScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiFontScope(UiFrame& frame, bool available) noexcept : UiScope(frame, Kind::Font, available, available) {}
    };

    class KEIRE_API UiStyleColorScope final : public UiScope
    {
      private:
        friend class UiFrame;
        explicit UiStyleColorScope(UiFrame& frame) noexcept : UiScope(frame, Kind::StyleColor, true, true) {}
    };

    class KEIRE_API UiStyleVariableScope final : public UiScope
    {
      private:
        friend class UiFrame;
        explicit UiStyleVariableScope(UiFrame& frame) noexcept : UiScope(frame, Kind::StyleVariable, true, true) {}
    };

    class KEIRE_API UiPanelScope final : public UiScope
    {
      private:
        friend class UiFrame;
        UiPanelScope(UiFrame& frame, bool visible, bool submitted) noexcept
            : UiScope(frame, Kind::Window, visible, submitted)
        {
        }
    };

    class KEIRE_API UiFrame final
    {
      public:
        UiFrame(const UiFrame&) = delete;
        UiFrame& operator=(const UiFrame&) = delete;
        UiFrame(UiFrame&&) = delete;
        UiFrame& operator=(UiFrame&&) = delete;
        ~UiFrame();

        [[nodiscard]] UiWindowScope BeginWindow(std::string_view title, bool* open = nullptr,
                                                UiWindowOptions options = {});
        [[nodiscard]] UiChildScope BeginChild(std::string_view id, UiSize size = {}, bool border = false);
        [[nodiscard]] UiMenuBarScope BeginMenuBar();
        [[nodiscard]] UiMenuScope BeginMenu(std::string_view label, bool enabled = true);
        [[nodiscard]] UiTabBarScope BeginTabBar(std::string_view id);
        [[nodiscard]] UiTabItemScope BeginTabItem(std::string_view label, bool* open = nullptr);
        [[nodiscard]] UiTreeNodeScope BeginTreeNode(std::string_view label, bool selected = false);
        [[nodiscard]] UiDisabledScope BeginDisabled(bool disabled = true);
        [[nodiscard]] UiIdScope PushId(std::string_view id);
        [[nodiscard]] UiMainMenuBarScope BeginMainMenuBar();
        [[nodiscard]] UiWindowScope BeginMainToolbar(std::string_view id = "Main Toolbar", float height = 34.0F);
        [[nodiscard]] UiWindowScope BeginMainStatusBar(std::string_view id = "Main Status Bar", float height = 24.0F);
        [[nodiscard]] UiComboScope BeginCombo(std::string_view label, std::string_view preview);
        [[nodiscard]] UiPopupScope BeginPopupModal(std::string_view id, bool* open = nullptr,
                                                   UiWindowOptions options = {}, bool autoResize = true);
        [[nodiscard]] UiPopupScope BeginPopup(std::string_view id);
        [[nodiscard]] UiPopupScope BeginItemContextMenu(std::string_view id = {});
        [[nodiscard]] UiPopupScope BeginWindowContextMenu(std::string_view id = {});
        [[nodiscard]] UiTableScope BeginTable(std::string_view id, std::size_t columns, UiTableOptions options = {});
        [[nodiscard]] UiDragSourceScope BeginDragSource();
        [[nodiscard]] UiDragTargetScope BeginDragTarget();
        [[nodiscard]] UiDragTargetScope BeginDragTarget(UiItemRect area, std::string_view id);
        [[nodiscard]] UiClipScope PushClipRect(UiItemRect rectangle);
        [[nodiscard]] UiFontScope PushFont(UiFontRole role);
        [[nodiscard]] UiStyleColorScope PushStyleColor(UiStyleColorRole role, UiColor color);
        [[nodiscard]] UiStyleVariableScope PushStyleVariable(UiStyleVariable variable, float value);
        [[nodiscard]] UiStyleVariableScope PushStyleVariable(UiStyleVariable variable, UiSize value);
        [[nodiscard]] UiPanelScope BeginPanel(UiPanelRegistration& panel, UiWindowOptions options = {});
        void OpenPopup(std::string_view id);
        void CloseCurrentPopup();
        void TableNextRow();
        [[nodiscard]] bool TableNextColumn();
        void TableSetupColumn(std::string_view label, UiTableColumnSizing sizing = UiTableColumnSizing::Automatic,
                              float widthOrWeight = 0.0F);
        void TableHeaderRow();
        void SetDragPayload(std::string_view type, std::span<const std::byte> bytes);
        [[nodiscard]] bool AcceptDragPayload(std::string_view type, std::vector<std::byte>& bytes);

        void Text(std::string_view text);
        void TextColored(UiColor color, std::string_view text);
        void TextWrapped(std::string_view text);
        void TextColoredWrapped(UiColor color, std::string_view text);
        void Separator();
        void SameLine();
        void Spacing();
        void ProgressBar(float fraction, UiSize size = {}, std::string_view overlay = {});
        [[nodiscard]] bool Splitter(UiAxis axis, std::string_view id, float& leadingSize, float& trailingSize,
                                    float minimumLeading = 80.0F, float minimumTrailing = 80.0F,
                                    float thickness = 4.0F);
        [[nodiscard]] bool Shortcut(UiShortcut shortcut);
        [[nodiscard]] UiItemState LastItemState() const;
        [[nodiscard]] UiItemRect LastItemRect() const;
        [[nodiscard]] bool Button(std::string_view label, UiSize size = {});
        [[nodiscard]] bool InvisibleButton(std::string_view id, UiSize size = {});
        [[nodiscard]] bool IconButton(std::string_view id, UiIcon icon, bool selected = false,
                                      UiSize size = {30.0F, 0.0F});
        [[nodiscard]] bool OverlayIconButton(std::string_view id, UiIcon icon,
                                             UiOverlayIconButtonSpecification specification);
        [[nodiscard]] bool Checkbox(std::string_view label, bool& value);
        [[nodiscard]] bool DragInteger(std::string_view label, std::int64_t& value, double speed = 1.0,
                                       std::optional<std::int64_t> minimum = {},
                                       std::optional<std::int64_t> maximum = {});
        [[nodiscard]] bool DragUnsignedInteger(std::string_view label, std::uint64_t& value, double speed = 1.0,
                                               std::optional<std::uint64_t> minimum = {},
                                               std::optional<std::uint64_t> maximum = {});
        [[nodiscard]] bool DragScalar(std::string_view label, double& value, double speed = 0.1,
                                      std::optional<double> minimum = {}, std::optional<double> maximum = {});
        [[nodiscard]] bool DragVector2(std::string_view label, Vector2& value, float speed = 0.1F);
        [[nodiscard]] bool DragVector3(std::string_view label, Vector3& value, float speed = 0.1F);
        [[nodiscard]] bool DragVector4(std::string_view label, Vector4& value, float speed = 0.1F);
        [[nodiscard]] bool DragQuaternion(std::string_view label, Quaternion& value, float speed = 0.01F);
        [[nodiscard]] bool SliderFloat(std::string_view label, float& value, float minimum, float maximum);
        [[nodiscard]] bool SliderInt(std::string_view label, int& value, int minimum, int maximum);
        [[nodiscard]] bool SliderInteger(std::string_view label, std::int64_t& value, std::int64_t minimum,
                                         std::int64_t maximum);
        [[nodiscard]] bool SliderUnsignedInteger(std::string_view label, std::uint64_t& value, std::uint64_t minimum,
                                                 std::uint64_t maximum);
        [[nodiscard]] bool SliderScalar(std::string_view label, double& value, double minimum, double maximum);
        [[nodiscard]] bool InputText(std::string_view label, std::string& value, bool selectAllOnFocus = false);
        [[nodiscard]] bool InputTextMultiline(std::string_view label, std::string& value,
                                              std::uint32_t visibleLines = 4);
        [[nodiscard]] bool InputPassword(std::string_view label, std::string& value);
        [[nodiscard]] bool InputTextWithHint(std::string_view label, std::string_view hint, std::string& value);
        [[nodiscard]] bool Selectable(std::string_view label, bool selected = false);
        [[nodiscard]] bool MenuItem(std::string_view label, bool selected = false, bool enabled = true);
        [[nodiscard]] bool ColorEdit(std::string_view label, UiColor& color);
        [[nodiscard]] Ref<UiImage> CreateImage(std::uint32_t width, std::uint32_t height,
                                               std::span<const std::byte> rgbaPixels);
        void Image(const Ref<UiImage>& image, UiSize size = {});
        void Image(const Ref<RenderSurface>& surface, UiSize size = {});
        void DrawImage(const Ref<RenderSurface>& surface, UiItemRect rectangle);
        [[nodiscard]] bool ImageButton(std::string_view id, const Ref<UiImage>& image, UiSize size = {});
        void DrawLine(UiPosition start, UiPosition end, UiColor color, float thickness = 1.0F);
        void DrawCircle(UiPosition center, float radius, UiColor color, float thickness = 1.0F);
        void DrawFilledCircle(UiPosition center, float radius, UiColor color);
        void DrawRectangle(UiItemRect rectangle, UiColor color, float thickness = 1.0F, float rounding = 0.0F);
        void DrawFilledRectangle(UiItemRect rectangle, UiColor color, float rounding = 0.0F);
        void DrawTriangle(UiPosition first, UiPosition second, UiPosition third, UiColor color, float thickness = 1.0F);
        void DrawFilledTriangle(UiPosition first, UiPosition second, UiPosition third, UiColor color);
        [[nodiscard]] UiSize MeasureText(std::string_view text, float fontSize = 0.0F) const;
        void DrawOverlayText(UiPosition position, UiColor color, std::string_view text, float fontSize = 0.0F,
                             std::optional<UiItemRect> clip = {});
        void DrawOverlayIcon(UiIcon icon, UiPosition position, UiColor color);
        [[nodiscard]] UiItemRect ContentRect() const;
        [[nodiscard]] UiSize ContentAvailable() const;
        [[nodiscard]] UiPosition CursorPosition() const;
        [[nodiscard]] UiPosition CursorScreenPosition() const;
        [[nodiscard]] bool WindowFocused() const;
        [[nodiscard]] UiPointerState PointerState() const;
        /// Claims the vertical pointer wheel for the last item while it is hovered or active. Custom zoomable
        /// controls use this to prevent the containing panel from scrolling in the same frame.
        void CapturePointerWheel();
        [[nodiscard]] bool KeyDown(UiKey key) const;
        [[nodiscard]] bool KeyPressed(UiKey key) const;
        [[nodiscard]] std::string TextInput() const;
        [[nodiscard]] bool ControlDown() const;
        [[nodiscard]] bool ShiftDown() const;
        [[nodiscard]] bool AltDown() const;
        void SetTooltip(std::string_view text);
        void SetTooltip(std::string_view text, UiTooltipOptions options);
        void SetNextWindowSize(UiSize size, bool firstUseOnly = true);
        void SetNextWindowPosition(UiPosition position, bool firstUseOnly = true);
        void SetCursorPosition(UiPosition position);
        void SetCursorScreenPosition(UiPosition position);
        void SetNextItemWidth(float width);
        void RequestKeyboardFocus();
        void AlignNextItemGroup(float alignment, float width);

      private:
        friend class UiScope;
        friend class UiSystem;
        class Impl;

        UiFrame();
        void RequireActive(const char* operation) const;
        void OpenScope(UiScope::Kind kind);
        void CloseScope(UiScope::Kind kind, std::uint64_t generation) noexcept;
        [[nodiscard]] std::weak_ptr<void> Lifetime() const noexcept;
        [[nodiscard]] std::uint64_t Generation() const noexcept;

        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
