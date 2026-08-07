#include "KeireHub/HubProductUi.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace KeireHub
{
    namespace
    {
        struct NavigationItem final
        {
            HubPage Page;
            std::string_view Label;
            Keire::UiIcon Icon;
        };

        [[nodiscard]] bool DrawNavigationItem(Keire::UiFrame& ui, const HubDesignTokens& tokens,
                                              const NavigationItem item, const bool selected, const bool compact,
                                              const float width)
        {
            const auto origin = ui.CursorScreenPosition();
            const auto id = "HubNavigation" + std::to_string(static_cast<int>(item.Page));
            const bool activated = ui.InvisibleButton(id, {width, HubDesignTokens::ControlHeight});
            const auto state = ui.LastItemState();
            const auto bounds = ui.LastItemRect();

            auto background = selected ? tokens.Elevated : tokens.Surface;
            if (!selected && !state.Hovered)
                background.Alpha = 0.0F;
            ui.DrawFilledRectangle(bounds, background, HubDesignTokens::RadiusSmall);
            if (selected)
            {
                ui.DrawFilledRectangle({bounds.Minimum, {bounds.Minimum.X + 3.0F, bounds.Maximum.Y}}, tokens.Accent,
                                       HubDesignTokens::RadiusSmall);
            }

            const float iconX = compact ? origin.X + (width - HubDesignTokens::IconMedium) * 0.5F : origin.X + 15.0F;
            ui.DrawOverlayIcon(item.Icon, {iconX, origin.Y + 9.0F}, selected ? tokens.Accent : tokens.SecondaryText);
            if (!compact)
            {
                ui.DrawOverlayText({origin.X + 48.0F, origin.Y + 10.0F},
                                   selected ? tokens.PrimaryText : tokens.SecondaryText, item.Label);
            }
            else
            {
                ui.SetTooltip(item.Label, {.Delayed = true});
            }
            return activated;
        }
    } // namespace

    void HubProductUi::DrawSidebar(Keire::UiFrame& ui, HubPage& page, const bool compact,
                                   const HubProductSnapshot& snapshot)
    {
        constexpr std::array items{NavigationItem{HubPage::Home, "Home", Keire::UiIcon::Home},
                                   NavigationItem{HubPage::Projects, "Projects", Keire::UiIcon::Folder},
                                   NavigationItem{HubPage::Installs, "Installs", Keire::UiIcon::Download},
                                   NavigationItem{HubPage::Templates, "Templates", Keire::UiIcon::Grid},
                                   NavigationItem{HubPage::Learn, "Learn", Keire::UiIcon::Learn},
                                   NavigationItem{HubPage::Resources, "Resources", Keire::UiIcon::Documentation},
                                   NavigationItem{HubPage::Licenses, "Licenses", Keire::UiIcon::License}};

        ui.TextColored(m_Tokens.Accent, compact ? "K" : "KÉIRE HUB");
        if (!compact)
            ui.TextColored(m_Tokens.MutedText, "CREATE  •  BUILD  •  SHIP");
        ui.Spacing();
        ui.Separator();
        ui.Spacing();
        const auto width = std::max(ui.ContentAvailable().Width, 1.0F);
        for (const auto& item : items)
        {
            if (DrawNavigationItem(ui, m_Tokens, item, page == item.Page, compact, width))
                page = item.Page;
        }

        const auto available = ui.ContentAvailable();
        if (available.Height > 120.0F)
            ui.SetCursorPosition({ui.CursorPosition().X, ui.CursorPosition().Y + available.Height - 112.0F});
        ui.Separator();
        if (DrawNavigationItem(ui, m_Tokens, {HubPage::Settings, "Settings", Keire::UiIcon::Settings},
                               page == HubPage::Settings, compact, width))
        {
            page = HubPage::Settings;
        }
        if (!compact)
        {
            const auto connection = snapshot.Settings.OfflineMode ? "Offline mode"
                                    : snapshot.Online             ? "Online"
                                                                  : "Unavailable";
            ui.TextColored(snapshot.Online ? m_Tokens.Success : m_Tokens.Warning, connection);
            ui.SameLine();
            ui.TextColored(m_Tokens.MutedText, "Hub " + snapshot.HubVersion);
        }
    }
} // namespace KeireHub
