#include "KeireHubInternal/HubNoticeUi.h"

#include "KeireHub/HubDesignTokens.h"

#include "Keire/Ui.h"

namespace KeireHub::Detail
{
    void DrawHubNotice(Keire::UiFrame& ui, const HubDesignTokens& tokens, std::string& notice, const bool noticeError,
                       std::string& observedNotice, std::chrono::steady_clock::time_point& noticeStarted)
    {
        const auto now = std::chrono::steady_clock::now();
        if (observedNotice != notice)
        {
            observedNotice = notice;
            noticeStarted = now;
        }
        if (!noticeError && !notice.empty() && now - noticeStarted >= std::chrono::seconds(5))
        {
            notice.clear();
            observedNotice.clear();
        }
        if (notice.empty())
            return;

        [[maybe_unused]] const auto bannerBackground =
            ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Elevated);
        if (auto banner = ui.BeginChild("HubGlobalNotice", {0.0F, 48.0F}, true); banner)
        {
            Keire::UiTableOptions layout;
            layout.Borders = false;
            layout.Resizable = false;
            layout.RowBackground = false;
            layout.PersistSettings = false;
            if (auto table = ui.BeginTable("HubGlobalNoticeLayout", 2, layout); table)
            {
                ui.TableSetupColumn("Message", Keire::UiTableColumnSizing::Stretch, 1.0F);
                ui.TableSetupColumn("Action", Keire::UiTableColumnSizing::Fixed, 76.0F);
                ui.TableNextRow();
                (void)ui.TableNextColumn();
                ui.TextColoredWrapped(noticeError ? tokens.Danger : tokens.Success, notice);
                (void)ui.TableNextColumn();
                if (ui.Button("Dismiss##HubNotice", {68.0F, 28.0F}))
                {
                    notice.clear();
                    observedNotice.clear();
                }
            }
        }
        ui.Spacing();
    }
} // namespace KeireHub::Detail
