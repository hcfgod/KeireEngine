#pragma once

#include "Keire/Api.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    struct UiColor
    {
        float Red = 0.08F;
        float Green = 0.09F;
        float Blue = 0.11F;
        float Alpha = 1.0F;
        auto operator<=>(const UiColor&) const noexcept = default;
    };

    struct UiSize
    {
        float Width = 0.0F;
        float Height = 0.0F;
        auto operator<=>(const UiSize&) const noexcept = default;
    };

    struct UiPosition
    {
        float X = 0.0F;
        float Y = 0.0F;
        auto operator<=>(const UiPosition&) const noexcept = default;
    };

    enum class UiTheme : std::uint8_t
    {
        KeireDark,
        KeireLight,
        Classic,
        Dark = KeireDark,
        Light = KeireLight
    };

    struct UiThemeDefinition
    {
        UiColor Canvas{0.075F, 0.078F, 0.086F, 1.0F};
        UiColor Panel{0.105F, 0.11F, 0.12F, 1.0F};
        UiColor RaisedPanel{0.145F, 0.15F, 0.165F, 1.0F};
        UiColor Border{0.225F, 0.23F, 0.25F, 1.0F};
        UiColor Text{0.91F, 0.92F, 0.95F, 1.0F};
        UiColor MutedText{0.61F, 0.65F, 0.72F, 1.0F};
        UiColor Accent{0.30F, 0.55F, 1.0F, 1.0F};
        UiColor AccentHovered{0.42F, 0.63F, 1.0F, 1.0F};
        UiColor AccentActive{0.20F, 0.47F, 0.96F, 1.0F};
        UiColor Selection{0.30F, 0.55F, 1.0F, 0.35F};
        UiColor Success{0.27F, 0.78F, 0.50F, 1.0F};
        UiColor Warning{1.0F, 0.69F, 0.25F, 1.0F};
        UiColor Error{0.96F, 0.32F, 0.36F, 1.0F};
        UiSize WindowPadding{7.0F, 6.0F};
        UiSize FramePadding{7.0F, 4.0F};
        UiSize ItemSpacing{6.0F, 4.0F};
        float WindowRounding = 3.0F;
        float FrameRounding = 3.0F;
        float TabRounding = 2.0F;
        float ScrollbarRounding = 3.0F;
        float WindowBorderSize = 1.0F;
        float FrameBorderSize = 0.0F;
        auto operator<=>(const UiThemeDefinition&) const noexcept = default;
    };

    class UiLayoutId final
    {
      public:
        constexpr UiLayoutId() noexcept = default;
        explicit constexpr UiLayoutId(const std::uint64_t value) noexcept : m_Value(value) {}
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return m_Value; }
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] auto operator<=>(const UiLayoutId&) const noexcept = default;

      private:
        std::uint64_t m_Value = 0;
    };

    class UiThemeId final
    {
      public:
        constexpr UiThemeId() noexcept = default;
        explicit constexpr UiThemeId(const std::uint64_t value) noexcept : m_Value(value) {}
        [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return m_Value; }
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_Value != 0; }
        [[nodiscard]] auto operator<=>(const UiThemeId&) const noexcept = default;

      private:
        std::uint64_t m_Value = 0;
    };

    struct UiLayoutInfo
    {
        UiLayoutId Id;
        std::string Name;
        bool BuiltIn = false;
        bool Active = false;
        bool Modified = false;
    };

    struct UiThemeInfo
    {
        UiThemeId Id;
        std::string Name;
        bool BuiltIn = false;
        bool Active = false;
    };

    enum class UiDockDirection : std::uint8_t
    {
        Left,
        Right,
        Up,
        Down
    };

    class UiDockRegion final
    {
      public:
        constexpr UiDockRegion() noexcept = default;
        [[nodiscard]] explicit constexpr operator bool() const noexcept { return m_Value != 0; }

      private:
        friend class UiLayoutBuilder;
        explicit constexpr UiDockRegion(const std::uint32_t value) noexcept : m_Value(value) {}
        std::uint32_t m_Value = 0;
    };

    struct UiDockSplit
    {
        UiDockRegion Near;
        UiDockRegion Far;
    };

    class KEIRE_API UiLayoutBuilder final
    {
      public:
        UiLayoutBuilder(const UiLayoutBuilder&) = delete;
        UiLayoutBuilder& operator=(const UiLayoutBuilder&) = delete;
        UiLayoutBuilder(UiLayoutBuilder&&) = delete;
        UiLayoutBuilder& operator=(UiLayoutBuilder&&) = delete;
        ~UiLayoutBuilder();

        [[nodiscard]] UiDockRegion Root() const noexcept;
        [[nodiscard]] UiDockSplit Split(UiDockRegion region, UiDockDirection direction, float ratio);
        void Dock(std::string_view panelId, UiDockRegion region);

      private:
        friend class UiWorkspace;
        class Impl;
        UiLayoutBuilder();
        std::unique_ptr<Impl> m_Impl;
    };

    using UiFactoryLayoutCallback = std::function<void(UiLayoutBuilder&)>;

    struct UiWorkspaceSpecification
    {
        bool Enabled = false;
        bool Ephemeral = false;
        std::filesystem::path DirectoryOverride;
        UiFactoryLayoutCallback BuildFactoryLayout;
    };

    struct UiPanelSpecification
    {
        std::string Id;
        std::string Title;
        bool DefaultVisible = true;
    };

    class KEIRE_API UiPanelRegistration final
    {
      public:
        UiPanelRegistration() noexcept;
        UiPanelRegistration(const UiPanelRegistration&) = delete;
        UiPanelRegistration& operator=(const UiPanelRegistration&) = delete;
        UiPanelRegistration(UiPanelRegistration&& other) noexcept;
        UiPanelRegistration& operator=(UiPanelRegistration&& other) noexcept;
        ~UiPanelRegistration();

        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] std::string_view Id() const noexcept;
        [[nodiscard]] std::string_view Title() const noexcept;
        [[nodiscard]] bool Visible() const noexcept;
        [[nodiscard]] bool Locked() const noexcept;
        void SetVisible(bool visible);
        void SetLocked(bool locked);
        void RequestFocus();

      private:
        friend class UiFrame;
        friend class UiWorkspace;
        class Impl;
        explicit UiPanelRegistration(std::unique_ptr<Impl> implementation) noexcept;
        [[nodiscard]] const std::string& SubmittedName() const;
        [[nodiscard]] bool* VisibilityAddress();
        [[nodiscard]] bool ConsumeFocusRequest();
        void NotifyVisibilityChanged(bool previous);
        std::unique_ptr<Impl> m_Impl;
    };

    enum class UiWorkspaceNoticeSeverity : std::uint8_t
    {
        Information,
        Warning,
        Error
    };

    struct UiWorkspaceNotice
    {
        UiWorkspaceNoticeSeverity Severity = UiWorkspaceNoticeSeverity::Information;
        std::string Message;
    };

    class Window;
    class WindowSystem;

    class KEIRE_API UiWorkspace final
    {
      public:
        UiWorkspace(const UiWorkspace&) = delete;
        UiWorkspace& operator=(const UiWorkspace&) = delete;
        UiWorkspace(UiWorkspace&&) = delete;
        UiWorkspace& operator=(UiWorkspace&&) = delete;
        ~UiWorkspace();

        [[nodiscard]] std::vector<UiLayoutInfo> Layouts() const;
        [[nodiscard]] std::vector<UiThemeInfo> Themes() const;
        [[nodiscard]] UiLayoutId ActiveLayout() const noexcept;
        [[nodiscard]] UiThemeId ActiveTheme() const noexcept;
        [[nodiscard]] UiThemeDefinition ThemeDefinition(UiThemeId id) const;

        [[nodiscard]] UiPanelRegistration RegisterPanel(UiPanelSpecification specification);

        void LoadLayout(UiLayoutId id);
        void SaveLayoutAs(std::string name);
        void RenameLayout(UiLayoutId id, std::string name);
        void DeleteLayout(UiLayoutId id);
        void ResetFactoryLayout();
        void ImportLayout(const std::filesystem::path& path);
        void ExportLayout(UiLayoutId id, const std::filesystem::path& path);
        void ShowImportLayoutDialog();
        void ShowExportLayoutDialog(UiLayoutId id);

        void ApplyTheme(UiThemeId id);
        void PreviewTheme(UiThemeDefinition definition);
        void CancelThemePreview();
        [[nodiscard]] UiThemeId SaveThemeAs(std::string name, UiThemeDefinition definition);
        void UpdateTheme(UiThemeId id, UiThemeDefinition definition);
        void RenameTheme(UiThemeId id, std::string name);
        void DeleteTheme(UiThemeId id);
        void ImportTheme(const std::filesystem::path& path);
        void ExportTheme(UiThemeId id, const std::filesystem::path& path);
        void ShowImportThemeDialog();
        void ShowExportThemeDialog(UiThemeId id);

        [[nodiscard]] std::optional<UiWorkspaceNotice> ConsumeNotice();

      private:
        friend class UiPanelRegistration;
        friend class UiSystem;
        class Impl;
        UiWorkspace(UiWorkspaceSpecification specification, WindowSystem& windows, Window& window,
                    bool nativeDialogsEnabled);
        void BeforeNewFrame();
        void AfterNewFrame(UiSize viewportSize);
        void AfterFrame();
        void Shutdown() noexcept;
        [[nodiscard]] std::uint32_t DockspaceId() const noexcept;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
