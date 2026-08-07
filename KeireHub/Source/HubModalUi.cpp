#include "KeireHub/HubModalUi.h"

#include <algorithm>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] Keire::UiColor Brighter(Keire::UiColor color, const float amount) noexcept
        {
            color.Red = std::min(1.0F, color.Red + amount);
            color.Green = std::min(1.0F, color.Green + amount);
            color.Blue = std::min(1.0F, color.Blue + amount);
            return color;
        }

        [[nodiscard]] bool StyledButton(Keire::UiFrame& ui, const Keire::UiColor color, const std::string_view label,
                                        const Keire::UiSize size)
        {
            [[maybe_unused]] const auto background = ui.PushStyleColor(Keire::UiStyleColorRole::Button, color);
            [[maybe_unused]] const auto hovered =
                ui.PushStyleColor(Keire::UiStyleColorRole::ButtonHovered, Brighter(color, 0.06F));
            [[maybe_unused]] const auto active =
                ui.PushStyleColor(Keire::UiStyleColorRole::ButtonActive, Brighter(color, 0.02F));
            return ui.Button(label, size);
        }
    } // namespace

    HubModalStyleScope::HubModalStyleScope(Keire::UiFrame& ui, const HubDesignTokens& tokens)
        : m_Background(ui.PushStyleColor(Keire::UiStyleColorRole::PopupBackground, tokens.Surface)),
          m_BorderColor(ui.PushStyleColor(Keire::UiStyleColorRole::Border, tokens.Border)),
          m_Padding(ui.PushStyleVariable(Keire::UiStyleVariable::WindowPadding, {24.0F, 22.0F})),
          m_Rounding(ui.PushStyleVariable(Keire::UiStyleVariable::WindowRounding, HubDesignTokens::RadiusLarge)),
          m_Border(ui.PushStyleVariable(Keire::UiStyleVariable::WindowBorderSize, HubDesignTokens::BorderWidth)),
          m_FramePadding(ui.PushStyleVariable(Keire::UiStyleVariable::FramePadding, {12.0F, 8.0F})),
          m_FrameRounding(ui.PushStyleVariable(Keire::UiStyleVariable::FrameRounding, HubDesignTokens::RadiusMedium)),
          m_ItemSpacing(ui.PushStyleVariable(Keire::UiStyleVariable::ItemSpacing, {8.0F, 10.0F}))
    {
    }

    Keire::UiWindowOptions HubModalWindowOptions() noexcept
    {
        Keire::UiWindowOptions options;
        options.NoTitleBar = true;
        options.NoResize = true;
        options.NoMove = true;
        options.NoCollapse = true;
        options.NoSavedSettings = true;
        return options;
    }

    void PrepareHubModal(Keire::UiFrame& ui, const Keire::UiSize size) { ui.SetNextWindowSize(size, false); }

    void DrawHubModalHeader(Keire::UiFrame& ui, const HubDesignTokens& tokens, const std::string_view title,
                            const std::string_view subtitle, const std::string_view eyebrow)
    {
        if (!eyebrow.empty())
            ui.TextColored(tokens.Accent, eyebrow);
        {
            [[maybe_unused]] const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
            ui.TextColored(tokens.PrimaryText, title);
        }
        if (!subtitle.empty())
            ui.TextColoredWrapped(tokens.SecondaryText, subtitle);
        ui.Spacing();
        ui.Separator();
        ui.Spacing();
    }

    bool HubPrimaryButton(Keire::UiFrame& ui, const HubDesignTokens& tokens, const std::string_view label,
                          const Keire::UiSize size)
    {
        return StyledButton(ui, tokens.Accent, label, size);
    }

    bool HubSecondaryButton(Keire::UiFrame& ui, const HubDesignTokens& tokens, const std::string_view label,
                            const Keire::UiSize size)
    {
        return StyledButton(ui, tokens.Elevated, label, size);
    }

    bool HubDangerButton(Keire::UiFrame& ui, const HubDesignTokens& tokens, const std::string_view label,
                         const Keire::UiSize size)
    {
        return StyledButton(ui, tokens.Danger, label, size);
    }
} // namespace KeireHub
