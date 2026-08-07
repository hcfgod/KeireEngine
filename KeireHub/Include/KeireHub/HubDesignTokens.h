#pragma once

#include "KeireHubRuntime/HubSettingsStore.h"

#include "Keire/Ui.h"

namespace KeireHub
{
    struct HubDesignTokens final
    {
        Keire::UiColor PrimaryText;
        Keire::UiColor SecondaryText;
        Keire::UiColor MutedText;
        Keire::UiColor Accent;
        Keire::UiColor Success;
        Keire::UiColor Warning;
        Keire::UiColor Danger;
        Keire::UiColor Canvas;
        Keire::UiColor Surface;
        Keire::UiColor Elevated;
        Keire::UiColor Border;
        Keire::UiColor Shadow;

        static constexpr float TitleBarHeight = 40.0F;
        static constexpr float SidebarWidth = 224.0F;
        static constexpr float IconRailWidth = 72.0F;
        static constexpr float CompactBreakpoint = 1080.0F;
        static constexpr float ControlHeight = 38.0F;
        static constexpr float SmallControlHeight = 28.0F;
        static constexpr float Spacing2XSmall = 2.0F;
        static constexpr float SpacingXSmall = 4.0F;
        static constexpr float SpacingSmall = 8.0F;
        static constexpr float SpacingMedium = 12.0F;
        static constexpr float SpacingLarge = 16.0F;
        static constexpr float SpacingXLarge = 24.0F;
        static constexpr float Spacing2XLarge = 32.0F;
        static constexpr float BodyFontPixels = 15.0F;
        static constexpr float CaptionFontPixels = 13.0F;
        static constexpr float HeadingFontPixels = 20.0F;
        static constexpr float RadiusSmall = 4.0F;
        static constexpr float RadiusMedium = 6.0F;
        static constexpr float RadiusLarge = 10.0F;
        static constexpr float BorderWidth = 1.0F;
        static constexpr float ShadowOffset = 2.0F;
        static constexpr float IconSmall = 16.0F;
        static constexpr float IconMedium = 20.0F;
        static constexpr float IconLarge = 24.0F;
        static constexpr float HoverDurationSeconds = 0.08F;
        static constexpr float PanelDurationSeconds = 0.16F;
        static constexpr float ToastDurationSeconds = 0.18F;

        [[nodiscard]] static HubDesignTokens For(HubAppearance appearance, bool systemPrefersDark = true) noexcept;
    };

    [[nodiscard]] bool HubSystemPrefersDark() noexcept;
    [[nodiscard]] Keire::UiTheme ResolveHubUiTheme(HubAppearance appearance) noexcept;
} // namespace KeireHub
