#include "KeireHub/HubProductUi.h"
#include "KeireHub/HubChromeLayout.h"
#include "KeireHub/HubModalUi.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string Lower(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const char character)
                                   { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
            return value;
        }

        [[nodiscard]] std::string Utf8Path(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        [[nodiscard]] std::filesystem::path PathFromUtf8(const std::string_view value)
        {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
        }

        [[nodiscard]] std::string PageName(const HubPage page)
        {
            constexpr std::array names{"Home",  "Projects",  "Installs", "Templates",
                                       "Learn", "Resources", "Licenses", "Settings"};
            const auto index = static_cast<std::size_t>(page);
            return index < names.size() ? names[index] : "Hub";
        }

        [[nodiscard]] std::string HumanBytes(const std::uint64_t bytes)
        {
            constexpr std::array suffixes{"B", "KiB", "MiB", "GiB", "TiB"};
            double value = static_cast<double>(bytes);
            std::size_t suffix = 0;
            while (value >= 1024.0 && suffix + 1 < suffixes.size())
            {
                value /= 1024.0;
                ++suffix;
            }
            std::ostringstream stream;
            if (suffix == 0)
                stream << bytes;
            else
            {
                stream.setf(std::ios::fixed);
                stream.precision(value < 10.0 ? 1 : 0);
                stream << value;
            }
            stream << ' ' << suffixes[suffix];
            return stream.str();
        }

        [[nodiscard]] std::size_t WrappedLineCount(Keire::UiFrame& ui, const std::string_view text,
                                                   const float availableWidth)
        {
            if (text.empty())
                return 0;
            const auto width = std::max(availableWidth, 1.0F);
            return std::max<std::size_t>(
                1, static_cast<std::size_t>(std::ceil(ui.MeasureText(text).Width * 1.08F / width)));
        }

        [[nodiscard]] bool HasTaskActions(const HubTaskUiRecord& task) noexcept
        {
            return task.Pausable || task.Cancellable || task.Retryable || task.Dismissible;
        }

        [[nodiscard]] std::string TaskDetails(const HubTaskUiRecord& task)
        {
            std::string details;
            if (task.TotalBytes != 0)
            {
                details = HumanBytes(task.BytesTransferred) + " / " + HumanBytes(task.TotalBytes);
                if (task.BytesPerSecond != 0)
                    details += "  •  " + HumanBytes(task.BytesPerSecond) + "/s";
            }
            if (!task.CurrentPackage.empty())
            {
                if (!details.empty())
                    details += "  •  ";
                details += task.CurrentPackage;
            }
            if (task.RemainingComponents != 0)
                details += "  •  " + std::to_string(task.RemainingComponents) + " remaining";
            return details;
        }

        void DrawTaskActivityCard(Keire::UiFrame& ui, const HubDesignTokens& tokens, const HubTaskUiRecord& task,
                                  HubUiCommand& command)
        {
            const auto details = TaskDetails(task);
            const bool showMessage = !task.Message.empty() && task.Message != task.Phase;
            const auto contentWidth = std::max(ui.ContentAvailable().Width - 24.0F, 1.0F);
            const auto lineHeight = std::max(ui.MeasureText("Ag").Height, 16.0F);
            const auto titleLines = WrappedLineCount(ui, task.Title, contentWidth);
            const auto detailLines =
                WrappedLineCount(ui, showMessage ? task.Message : std::string_view{}, contentWidth) +
                WrappedLineCount(ui, details, contentWidth);
            const auto extraLines = (titleLines > 1 ? titleLines - 1 : 0) + (detailLines > 1 ? detailLines - 1 : 0);
            const float baseHeight = HasTaskActions(task) ? 146.0F : 112.0F;
            const float height = std::min(baseHeight + static_cast<float>(extraLines) * lineHeight, 238.0F);
            [[maybe_unused]] const auto cardBackground =
                ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Elevated);
            if (auto card = ui.BeginChild("TaskActivityCard", {0.0F, height}, true); card)
            {
                ui.TextColoredWrapped(task.Active ? tokens.Accent : tokens.PrimaryText, task.Title);
                const float progress = std::clamp(task.Progress, 0.0F, 1.0F);
                const auto progressRow = ui.CursorPosition();
                const float progressRowWidth = ui.ContentAvailable().Width;
                ui.TextColored(tokens.SecondaryText, task.Phase.empty() ? "Queued" : task.Phase);
                const auto percentage = std::to_string(static_cast<unsigned>(std::round(progress * 100.0F))) + '%';
                const float percentageWidth = ui.MeasureText(percentage).Width;
                ui.SameLine();
                ui.SetCursorPosition(
                    {progressRow.X + std::max(progressRowWidth - percentageWidth, 0.0F), progressRow.Y});
                ui.TextColored(tokens.PrimaryText, percentage);
                ui.ProgressBar(progress, {0.0F, 8.0F}, " ");
                if (showMessage)
                    ui.TextColoredWrapped(tokens.SecondaryText, task.Message);
                if (!details.empty())
                    ui.TextColoredWrapped(tokens.MutedText, details);
                else if (!showMessage && !task.Message.empty())
                    ui.TextColoredWrapped(tokens.MutedText, task.Message);

                bool actionDrawn = false;
                const auto beginAction = [&]()
                {
                    if (actionDrawn)
                        ui.SameLine();
                    else
                        ui.Spacing();
                    actionDrawn = true;
                };
                if (task.Pausable)
                {
                    beginAction();
                    if (HubSecondaryButton(ui, tokens, task.Paused ? "Resume" : "Pause", {82.0F, 30.0F}))
                    {
                        command = {.Type = task.Paused ? HubUiCommandType::ResumeTask : HubUiCommandType::PauseTask,
                                   .ItemId = task.Id};
                    }
                }
                if (task.Cancellable)
                {
                    beginAction();
                    if (HubSecondaryButton(ui, tokens, "Cancel", {82.0F, 30.0F}))
                        command = {.Type = HubUiCommandType::CancelTask, .ItemId = task.Id};
                }
                if (task.Retryable)
                {
                    beginAction();
                    if (HubPrimaryButton(ui, tokens, "Retry", {82.0F, 30.0F}))
                        command = {.Type = HubUiCommandType::RetryTask, .ItemId = task.Id};
                }
                if (task.Dismissible)
                {
                    beginAction();
                    if (HubSecondaryButton(ui, tokens, "Dismiss", {82.0F, 30.0F}))
                        command = {.Type = HubUiCommandType::DismissTask, .ItemId = task.Id};
                }
            }
        }

        void PageHeader(Keire::UiFrame& ui, const HubDesignTokens& tokens, const std::string_view title,
                        const std::string_view description)
        {
            {
                const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                ui.TextColored(tokens.PrimaryText, title);
            }
            ui.TextColoredWrapped(tokens.SecondaryText, description);
            ui.Spacing();
        }

        [[nodiscard]] HubUiCommand OpenContent(const HubContentUiRecord& item)
        {
            if (!item.LocalPath.empty())
                return {.Type = HubUiCommandType::OpenLocalContent, .ItemId = item.Id, .Path = item.LocalPath};
            return {.Type = HubUiCommandType::OpenUrl, .ItemId = item.Id, .Url = item.Url};
        }

        void DrawMetric(Keire::UiFrame& ui, const HubDesignTokens& tokens, const std::string_view id,
                        const std::string_view label, const std::string& value,
                        const std::optional<Keire::UiColor> color = std::nullopt)
        {
            auto metricId = ui.PushId(id);
            if (auto card = ui.BeginChild("Metric", {0.0F, 82.0F}, true); card)
            {
                ui.TextColored(color.value_or(tokens.Accent), value);
                ui.TextColored(tokens.SecondaryText, label);
            }
        }

        void DrawTemplateArtwork(Keire::UiFrame& ui, const HubDesignTokens& tokens, const std::string_view label,
                                 const bool available, const Keire::Ref<Keire::UiImage>& image,
                                 const Keire::UiSize size, const std::string_view id)
        {
            const auto origin = ui.CursorScreenPosition();
            const Keire::UiItemRect bounds{origin, {origin.X + size.Width, origin.Y + size.Height}};
            if (image)
            {
                ui.Image(image, size);
                ui.DrawRectangle(bounds, tokens.Border, 1.0F, 8.0F);
                return;
            }
            ui.DrawFilledRectangle(bounds, tokens.Surface, 8.0F);
            ui.DrawRectangle(bounds, tokens.Border, 1.0F, 8.0F);
            const auto center = Keire::UiPosition{(bounds.Minimum.X + bounds.Maximum.X) * 0.5F,
                                                  (bounds.Minimum.Y + bounds.Maximum.Y) * 0.5F};
            const auto scale = std::min(size.Width / 320.0F, size.Height / 180.0F);
            const auto point = [&](const float x, const float y)
            { return Keire::UiPosition{center.X + x * scale, center.Y + y * scale}; };

            if (!available)
            {
                ui.DrawLine(point(-26.0F, -26.0F), point(26.0F, 26.0F), tokens.Warning, 3.0F);
                ui.DrawLine(point(26.0F, -26.0F), point(-26.0F, 26.0F), tokens.Warning, 3.0F);
            }
            else
            {
                ui.DrawFilledCircle(center, 34.0F * scale, tokens.Elevated);
                const auto monogram = label.empty() ? std::string("?") : std::string(label.substr(0, 1));
                ui.DrawOverlayText(point(-5.0F, -8.0F), tokens.Accent, monogram);
            }
            (void)ui.InvisibleButton("##TemplateArtwork" + std::string(id), size);
        }
    } // namespace

    void HubProductUi::SetAppearance(const HubAppearance appearance, const bool systemPrefersDark) noexcept
    {
        m_Tokens = HubDesignTokens::For(appearance, systemPrefersDark);
    }

    Keire::Ref<Keire::UiImage> HubProductUi::ResolveTemplateArtwork(Keire::UiFrame& ui,
                                                                    const HubTemplateArtworkUiRecord& artwork)
    {
        constexpr std::size_t MaximumTextures = 16;
        if (!artwork.Available || !artwork.Image.IsValid())
            return {};
        const auto cached =
            std::ranges::find(m_TemplateArtworkTextures, artwork.ResolvedPath, &TemplateArtworkTexture::Path);
        if (cached != m_TemplateArtworkTextures.end())
        {
            if (cached->Pixels == artwork.Image.RgbaPixels)
                return cached->Image;
            m_TemplateArtworkTextures.erase(cached);
        }

        Keire::Ref<Keire::UiImage> image;
        try
        {
            image = ui.CreateImage(artwork.Image.Width, artwork.Image.Height,
                                   std::span<const std::byte>(*artwork.Image.RgbaPixels));
        }
        catch (...)
        {
            // A failed bounded GPU upload falls back to a labeled artwork placeholder.
        }
        if (m_TemplateArtworkTextures.size() == MaximumTextures)
            m_TemplateArtworkTextures.erase(m_TemplateArtworkTextures.begin());
        m_TemplateArtworkTextures.push_back(
            {.Path = artwork.ResolvedPath, .Pixels = artwork.Image.RgbaPixels, .Image = image});
        return image;
    }

    void HubProductUi::DrawTitleBar(Keire::UiFrame& ui, Keire::Window& window, const HubPage page,
                                    const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        const auto bounds = ui.ContentRect();
        auto surface = m_Tokens.Surface;
        auto accent = m_Tokens.Accent;
        auto secondary = m_Tokens.SecondaryText;
        if (!window.Focused())
        {
            surface.Alpha *= 0.88F;
            accent.Alpha *= 0.72F;
            secondary.Alpha *= 0.72F;
        }
        ui.DrawFilledRectangle(bounds, surface);
        auto buttonBackground = surface;
        buttonBackground.Alpha = 0.0F;
        [[maybe_unused]] const auto titleButtonBackground =
            ui.PushStyleColor(Keire::UiStyleColorRole::Button, buttonBackground);
#if defined(__APPLE__)
        ui.SetCursorScreenPosition({bounds.Minimum.X + 120.0F, bounds.Minimum.Y + 10.0F});
#else
        ui.SetCursorScreenPosition({bounds.Minimum.X + 12.0F, bounds.Minimum.Y + 10.0F});
#endif
        ui.TextColored(accent, "KÉIRE");
        ui.SameLine();
        ui.TextColored(secondary, "Hub  /  " + PageName(page));

        constexpr float buttonHeight = static_cast<float>(HubCaptionButtonHeight);
#if defined(__APPLE__)
        constexpr float controlsWidth = 0.0F;
#else
        constexpr float buttonWidth = static_cast<float>(HubCaptionButtonWidth);
        const bool drawCaptionControls = window.Specification().Decoration == Keire::WindowDecoration::Custom;
        const float controlsWidth = drawCaptionControls ? static_cast<float>(HubCaptionControlsWidth) : 0.0F;
#endif
        const float controlsX = bounds.Maximum.X - controlsWidth - static_cast<float>(HubCaptionRightInset);
        const float controlsY = bounds.Minimum.Y + 1.0F;
        ui.SetCursorScreenPosition({controlsX - static_cast<float>(HubProductControlsWidth), controlsY});
        const auto accountLabel = snapshot.AccountSignedIn
                                      ? (snapshot.AccountDisplayName.empty() ? "Account" : snapshot.AccountDisplayName)
                                      : "Sign in";
        if (ui.Button(accountLabel + "##HubAccount", {92.0F, buttonHeight}))
            m_RequestAccountDialog = true;
        ui.SetTooltip(snapshot.AccountSignedIn ? snapshot.AccountEmail : "Kéire account", {.Delayed = true});
        ui.SameLine();
        if (ui.IconButton("HubHelp", Keire::UiIcon::Information, false, {44.0F, buttonHeight}))
            ui.OpenPopup("HubHelpPopover");
        ui.SetTooltip("Help", {.Delayed = true});
        ui.SameLine();
        const auto appearance = snapshot.Settings.Appearance == HubAppearance::System ? "Auto"
                                : snapshot.Settings.Appearance == HubAppearance::Dark ? "Dark"
                                                                                      : "Light";
        const auto appearanceIcon = snapshot.Settings.Appearance == HubAppearance::System  ? Keire::UiIcon::Settings
                                    : snapshot.Settings.Appearance == HubAppearance::Light ? Keire::UiIcon::LightMode
                                                                                           : Keire::UiIcon::DarkMode;
        if (ui.IconButton("HubAppearance", appearanceIcon, false, {44.0F, buttonHeight}))
        {
            auto settings = snapshot.Settings;
            settings.Appearance = settings.Appearance == HubAppearance::System ? HubAppearance::Dark
                                  : settings.Appearance == HubAppearance::Dark ? HubAppearance::Light
                                                                               : HubAppearance::System;
            command = {.Type = HubUiCommandType::SaveSettings, .Settings = std::move(settings)};
        }
        ui.SetTooltip(std::string("Appearance: ") + appearance + " (click to cycle)", {.Delayed = true});
        ui.SameLine();
        const auto activeTasks = std::ranges::count_if(snapshot.Tasks, &HubTaskUiRecord::Active);
        if (ui.Button("Tasks " + std::to_string(activeTasks) + "##HubTasks", {76.0F, buttonHeight}))
        {
            m_TaskCenterOpen = true;
            m_NotificationCenterOpen = false;
            m_CloseNotificationCenter = true;
        }
        ui.SameLine();
        if (ui.Button("Notifications " + std::to_string(snapshot.UnreadNotifications) + "##HubNotifications",
                      {126.0F, buttonHeight}))
        {
            m_NotificationCenterOpen = true;
            m_TaskCenterOpen = false;
            m_CloseTaskCenter = true;
        }

        if (auto help = ui.BeginPopup("HubHelpPopover"); help)
        {
            if (ui.MenuItem("Collect Diagnostics..."))
                command.Type = HubUiCommandType::CopyDiagnostics;
            if (ui.MenuItem("Open Documentation"))
            {
                command = {.Type = HubUiCommandType::OpenUrl,
                           .Url = "https://github.com/hcfgod/KeireEngine/tree/master/Docs"};
            }
        }

#if !defined(__APPLE__)
        if (drawCaptionControls)
        {
            [[maybe_unused]] const auto captionSpacing = ui.PushStyleVariable(
                Keire::UiStyleVariable::ItemSpacing, {static_cast<float>(HubCaptionButtonSpacing), 0.0F});
            ui.SetCursorScreenPosition({controlsX, controlsY});
            if (ui.IconButton("HubMinimize", Keire::UiIcon::Minimize, false, {buttonWidth, buttonHeight}))
                window.Minimize();
            ui.SetTooltip("Minimize", {.Delayed = true});
            ui.SameLine();
            if (ui.IconButton(window.Maximized() ? "HubRestore" : "HubMaximize",
                              window.Maximized() ? Keire::UiIcon::Restore : Keire::UiIcon::Maximize, false,
                              {buttonWidth, buttonHeight}))
            {
                if (window.Maximized())
                    window.Restore();
                else
                    window.Maximize();
            }
            ui.SetTooltip(window.Maximized() ? "Restore" : "Maximize", {.Delayed = true});
            ui.SameLine();
            {
                const auto closeHover = ui.PushStyleColor(Keire::UiStyleColorRole::ButtonHovered, m_Tokens.Danger);
                const auto closeActive = ui.PushStyleColor(Keire::UiStyleColorRole::ButtonActive, m_Tokens.Danger);
                if (ui.IconButton("HubClose", Keire::UiIcon::Close, false, {buttonWidth, buttonHeight}))
                    window.Close();
            }
            ui.SetTooltip("Close", {.Delayed = true});
        }
#endif
    }

    void HubProductUi::DrawFatalTitleBar(Keire::UiFrame& ui, Keire::Window& window)
    {
        const auto bounds = ui.ContentRect();
        auto surface = m_Tokens.Surface;
        auto accent = m_Tokens.Accent;
        auto secondary = m_Tokens.SecondaryText;
        if (!window.Focused())
        {
            surface.Alpha *= 0.88F;
            accent.Alpha *= 0.72F;
            secondary.Alpha *= 0.72F;
        }
        ui.DrawFilledRectangle(bounds, surface);
        auto buttonBackground = surface;
        buttonBackground.Alpha = 0.0F;
        [[maybe_unused]] const auto titleButtonBackground =
            ui.PushStyleColor(Keire::UiStyleColorRole::Button, buttonBackground);
#if defined(__APPLE__)
        ui.SetCursorScreenPosition({bounds.Minimum.X + 120.0F, bounds.Minimum.Y + 10.0F});
#else
        ui.SetCursorScreenPosition({bounds.Minimum.X + 12.0F, bounds.Minimum.Y + 10.0F});
#endif
        ui.TextColored(accent, "KÉIRE");
        ui.SameLine();
        ui.TextColored(secondary, "Hub  /  Recovery");

#if !defined(__APPLE__)
        constexpr float buttonWidth = static_cast<float>(HubCaptionButtonWidth);
        constexpr float buttonHeight = static_cast<float>(HubCaptionButtonHeight);
        if (window.Specification().Decoration == Keire::WindowDecoration::Custom)
        {
            const float controlsX = bounds.Maximum.X - static_cast<float>(HubCaptionStripWidth);
            const float controlsY = bounds.Minimum.Y + 1.0F;
            [[maybe_unused]] const auto captionSpacing = ui.PushStyleVariable(
                Keire::UiStyleVariable::ItemSpacing, {static_cast<float>(HubCaptionButtonSpacing), 0.0F});
            ui.SetCursorScreenPosition({controlsX, controlsY});
            if (ui.IconButton("HubFatalMinimize", Keire::UiIcon::Minimize, false, {buttonWidth, buttonHeight}))
                window.Minimize();
            ui.SetTooltip("Minimize", {.Delayed = true});
            ui.SameLine();
            if (ui.IconButton(window.Maximized() ? "HubFatalRestore" : "HubFatalMaximize",
                              window.Maximized() ? Keire::UiIcon::Restore : Keire::UiIcon::Maximize, false,
                              {buttonWidth, buttonHeight}))
            {
                if (window.Maximized())
                    window.Restore();
                else
                    window.Maximize();
            }
            ui.SetTooltip(window.Maximized() ? "Restore" : "Maximize", {.Delayed = true});
            ui.SameLine();
            {
                const auto closeHover = ui.PushStyleColor(Keire::UiStyleColorRole::ButtonHovered, m_Tokens.Danger);
                const auto closeActive = ui.PushStyleColor(Keire::UiStyleColorRole::ButtonActive, m_Tokens.Danger);
                if (ui.IconButton("HubFatalClose", Keire::UiIcon::Close, false, {buttonWidth, buttonHeight}))
                    window.Close();
            }
            ui.SetTooltip("Close", {.Delayed = true});
        }
#endif
    }

    HubFatalUiAction HubProductUi::DrawFatalScreen(Keire::UiFrame& ui, const HubFatalUiState& state)
    {
        HubFatalUiAction action = HubFatalUiAction::None;
        ui.SetCursorPosition({48.0F, 56.0F});
        {
            const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
            ui.TextColored(m_Tokens.PrimaryText, "Kéire Hub could not start");
        }
        ui.TextColoredWrapped(m_Tokens.SecondaryText, state.Message);
        ui.Spacing();
        ui.TextColoredWrapped(
            m_Tokens.MutedText,
            "Project, editor, install, and settings actions are disabled so a partially initialized runtime cannot "
            "change local data.");
        ui.Spacing();
        if (auto disabled = ui.BeginDisabled(!state.LogsAvailable); disabled)
            if (ui.Button("Open logs", {108.0F, 34.0F}))
                action = HubFatalUiAction::OpenLogs;
        ui.SameLine();
        if (ui.Button("Copy diagnostics", {142.0F, 34.0F}))
            action = HubFatalUiAction::CopyDiagnostics;
        ui.SameLine();
        if (ui.Button("Close Hub", {108.0F, 34.0F}))
            action = HubFatalUiAction::Close;
        if (!state.LogsAvailable)
            ui.TextColored(m_Tokens.MutedText, "No Hub log directory is available on this computer.");
        if (!state.ActionMessage.empty())
        {
            ui.Spacing();
            ui.TextColoredWrapped(m_Tokens.SecondaryText, state.ActionMessage);
        }
        return action;
    }

    HubFatalUiAction HubProductUi::DrawFatalRecoveryWindow(Keire::UiFrame& ui, Keire::Window& window,
                                                           const HubFatalUiState& state)
    {
        const auto size = window.LogicalSize();
        ui.SetNextWindowPosition({0.0F, 0.0F}, false);
        ui.SetNextWindowSize({static_cast<float>(size.Width), static_cast<float>(size.Height)}, false);
        [[maybe_unused]] const auto windowPadding =
            ui.PushStyleVariable(Keire::UiStyleVariable::WindowPadding, Keire::UiSize{});
        [[maybe_unused]] const auto windowRounding = ui.PushStyleVariable(Keire::UiStyleVariable::WindowRounding, 0.0F);
        [[maybe_unused]] const auto windowBorder = ui.PushStyleVariable(Keire::UiStyleVariable::WindowBorderSize, 0.0F);
        [[maybe_unused]] const auto windowBackground =
            ui.PushStyleColor(Keire::UiStyleColorRole::WindowBackground, m_Tokens.Canvas);
        Keire::UiWindowOptions options;
        options.NoTitleBar = true;
        options.NoResize = true;
        options.NoMove = true;
        options.NoCollapse = true;
        options.NoSavedSettings = true;
        if (auto recovery = ui.BeginWindow("Kéire Project Hub Recovery", nullptr, options); recovery)
        {
            if (auto titleBar = ui.BeginChild("HubFatalTitleBar", {0.0F, 40.0F}, false); titleBar)
                DrawFatalTitleBar(ui, window);
            if (auto workspace = ui.BeginChild("HubFatalWorkspace", {}, false); workspace)
                return DrawFatalScreen(ui, state);
        }
        return HubFatalUiAction::None;
    }

    void HubProductUi::DrawHome(Keire::UiFrame& ui, HubPage& page, const HubProductSnapshot& snapshot,
                                HubUiCommand& command)
    {
        PageHeader(ui, m_Tokens, "Good to see you",
                   "Continue a project, create from a real template, or review installation health.");
        if (ui.Button("New project", {122.0F, 38.0F}))
            command = {.Type = HubUiCommandType::CreateProjectFromTemplate, .ItemId = "keire.3d-starter"};
        ui.SameLine();
        if (ui.Button("Add project", {112.0F, 38.0F}))
            command.Type = HubUiCommandType::AddProject;
        ui.SameLine();
        if (ui.Button("View projects", {118.0F, 38.0F}))
            page = HubPage::Projects;
        ui.Spacing();

        Keire::UiTableOptions metrics;
        metrics.Sizing = Keire::UiTableSizing::Equal;
        metrics.Borders = false;
        metrics.Resizable = false;
        metrics.RowBackground = false;
        metrics.PersistSettings = false;
        const std::size_t columns = ui.ContentAvailable().Width >= 760.0F ? 4 : 2;
        if (auto table = ui.BeginTable("HomeMetrics", columns, metrics); table)
        {
            ui.TableNextRow();
            (void)ui.TableNextColumn();
            DrawMetric(ui, m_Tokens, "projects", "Recent projects", std::to_string(snapshot.RecentProjects));
            (void)ui.TableNextColumn();
            DrawMetric(ui, m_Tokens, "editors", "Editor installs", std::to_string(snapshot.Editors.size()),
                       snapshot.Editors.empty() ? m_Tokens.Warning : m_Tokens.Success);
            if (columns == 2)
                ui.TableNextRow();
            (void)ui.TableNextColumn();
            DrawMetric(ui, m_Tokens, "components",
                       snapshot.BuildSupportInventoryLoading ? "Checking components" : "Healthy components",
                       snapshot.BuildSupportInventoryLoading ? "..." : std::to_string(snapshot.HealthyComponents),
                       snapshot.BuildSupportInventoryLoading ? m_Tokens.MutedText : m_Tokens.Success);
            (void)ui.TableNextColumn();
            DrawMetric(ui, m_Tokens, "tasks", "Active tasks",
                       std::to_string(std::ranges::count_if(snapshot.Tasks, &HubTaskUiRecord::Active)),
                       snapshot.Tasks.empty() ? m_Tokens.MutedText : m_Tokens.Accent);
        }
        ui.Spacing();

        if (snapshot.Editors.empty())
        {
            ui.TextColored(m_Tokens.Warning, "No compatible editor installation is registered.");
            if (auto disabled = ui.BeginDisabled(snapshot.EditorManagementBusy); disabled)
            {
                if (ui.Button("Locate editor", {116.0F, 34.0F}))
                    command.Type = HubUiCommandType::LocateEditor;
            }
        }
        else if (const auto unhealthyEditors =
                     std::ranges::count_if(snapshot.Editors, [](const auto& editor) { return !editor.Healthy; });
                 unhealthyEditors > 0)
        {
            ui.TextColored(m_Tokens.Warning,
                           std::to_string(unhealthyEditors) + " editor installation(s) need verification or recovery.");
            if (ui.Button("Review installs", {124.0F, 34.0F}))
                page = HubPage::Installs;
        }
        else if (snapshot.BuildSupportInventoryLoading)
        {
            ui.TextColored(m_Tokens.SecondaryText, "Checking installed Build Support component health...");
        }
        else if (snapshot.BuildSupportBusy)
        {
            ui.TextColored(m_Tokens.Accent, "A Build Support operation is active.");
            if (ui.Button("View components", {136.0F, 34.0F}))
                page = HubPage::Installs;
        }
        else if (snapshot.UnhealthyComponents > 0)
        {
            ui.TextColored(m_Tokens.Warning, std::to_string(snapshot.UnhealthyComponents) +
                                                 " Build Support component(s) need attention.");
            if (ui.Button("Review components", {148.0F, 34.0F}))
                page = HubPage::Installs;
        }
        else
            ui.TextColored(m_Tokens.Success, "Installed editors and Build Support are healthy.");

        if (snapshot.HubUpdate)
        {
            ui.Spacing();
            ui.Separator();
            const auto& update = *snapshot.HubUpdate;
            ui.TextColored(update.Required ? m_Tokens.Warning : m_Tokens.Accent,
                           update.Required ? "Hub update required" : "Hub update available");
            ui.TextColoredWrapped(m_Tokens.SecondaryText, "Kéire Hub " + update.Version + " is available on the " +
                                                              update.Channel +
                                                              " channel through the verified distribution catalog.");
            if (update.DownloadBytes > 0)
                ui.TextColored(m_Tokens.MutedText, "Installer download: " + HumanBytes(update.DownloadBytes));
            if (!update.ActionMessage.empty())
                ui.TextColoredWrapped(update.DownloadFailed ? m_Tokens.Warning : m_Tokens.SecondaryText,
                                      update.ActionMessage);
            if (update.ReadyToInstall && update.NativeHandoffAvailable)
            {
                if (ui.Button("Install update…", {148.0F, 34.0F}))
                    command = {.Type = HubUiCommandType::InstallHubUpdate, .ItemId = update.PackageId};
            }
            else if (update.ReadyToInstall && !update.VerifiedInstallerPath.empty())
            {
                if (ui.Button("Reveal verified installer", {176.0F, 34.0F}))
                {
                    command = {.Type = HubUiCommandType::RevealPath,
                               .ItemId = update.PackageId,
                               .Path = update.VerifiedInstallerPath};
                }
            }
            else if (update.DownloadActive)
            {
                if (ui.Button(update.DownloadPaused ? "Open paused task" : "View download", {132.0F, 34.0F}))
                {
                    m_TaskCenterOpen = true;
                    m_CloseNotificationCenter = true;
                }
            }
            else if (update.CanDownload)
            {
                if (ui.Button("Download update", {132.0F, 34.0F}))
                    command = {.Type = HubUiCommandType::DownloadHubUpdate, .ItemId = update.PackageId};
            }
        }
        else if (!snapshot.HubUpdateMessage.empty())
        {
            ui.Spacing();
            ui.TextColoredWrapped(m_Tokens.Warning, snapshot.HubUpdateMessage);
        }

        const auto featured = std::ranges::find_if(snapshot.Learn, &HubContentUiRecord::Featured);
        if (featured != snapshot.Learn.end())
        {
            ui.Spacing();
            ui.Separator();
            ui.TextColored(m_Tokens.PrimaryText, "Featured learning");
            ui.Text(featured->Title);
            ui.TextColoredWrapped(m_Tokens.SecondaryText, featured->Summary);
            if (ui.Button("Open", {82.0F, 32.0F}))
                command = OpenContent(*featured);
        }
    }

    void HubProductUi::DrawInstalls(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        PageHeader(ui, m_Tokens, "Installs",
                   "Manage versioned editor installations and their compatible Build Support components.");
        if (HubPrimaryButton(ui, m_Tokens, "Install Editor", {126.0F, 36.0F}))
            m_RequestEditorCatalog = true;
        ui.SameLine();
        if (snapshot.EditorManagementRefreshing)
            ui.TextColored(m_Tokens.Accent, "Checking installed editor manifests and file inventories...");
        if (auto disabled = ui.BeginDisabled(snapshot.EditorManagementBusy); disabled)
        {
            if (HubSecondaryButton(ui, m_Tokens, "Locate existing", {142.0F, 36.0F}))
                command.Type = HubUiCommandType::LocateEditor;
        }
        ui.Spacing();
        if (snapshot.Editors.empty())
        {
            ui.TextColored(m_Tokens.SecondaryText, "No editor installation is currently registered.");
        }
        else
            ui.TextColored(m_Tokens.PrimaryText, "Installed");

        for (const auto& editor : snapshot.Editors)
        {
            auto id = ui.PushId(editor.Id);
            const auto shownIssues = std::min<std::size_t>(editor.HealthIssues.size(), 2);
            const float cardHeight = 160.0F + static_cast<float>(shownIssues) * 34.0F +
                                     (editor.RegistrationRefreshAvailable ? 34.0F : 0.0F) +
                                     (editor.Running || editor.HasActiveTask || editor.ManagementBusy ? 20.0F : 0.0F);
            const bool editorBusy =
                snapshot.EditorManagementBusy || editor.Running || editor.HasActiveTask || snapshot.BuildSupportBusy;
            if (auto card = ui.BeginChild("EditorInstall", {0.0F, cardHeight}, true); card)
            {
                ui.TextColored(m_Tokens.PrimaryText, "Kéire Editor " + editor.Version);
                ui.SameLine();
                const auto healthLabel = editor.Healthy ? std::string("Verified") : editor.HealthLabel;
                const auto healthColor = editor.Healthy             ? m_Tokens.Success
                                         : healthLabel == "Missing" ? m_Tokens.Danger
                                         : healthLabel == "Damaged" ? m_Tokens.Danger
                                                                    : m_Tokens.Warning;
                ui.TextColored(healthColor, healthLabel);
                ui.TextColored(m_Tokens.SecondaryText,
                               editor.Channel + "  •  " + editor.Platform + " / " + editor.Architecture);
                ui.TextColored(m_Tokens.MutedText, Utf8Path(editor.Root));
                const auto ownership = editor.Managed ? "Managed" : "External";
                ui.Text(std::string(ownership) + "  •  " + std::to_string(editor.ProjectCount) + " project(s)  •  " +
                        std::to_string(editor.ComponentCount) + " Build Support component(s)  •  " +
                        HumanBytes(editor.InstalledBytes));
                if (!editor.BundledDotnetSdk.empty())
                    ui.TextColored(m_Tokens.SecondaryText,
                                   "Bundled .NET SDK " + editor.BundledDotnetSdk + " (read-only)");
                for (std::size_t index = 0; index < shownIssues; ++index)
                    ui.TextColoredWrapped(healthColor, editor.HealthIssues[index]);
                if (editor.HealthIssues.size() > shownIssues)
                {
                    ui.TextColored(m_Tokens.MutedText, "+" + std::to_string(editor.HealthIssues.size() - shownIssues) +
                                                           " additional verification issue(s)");
                }
                if (editor.RegistrationRefreshAvailable)
                {
                    ui.TextColoredWrapped(
                        m_Tokens.Warning,
                        "This external package changed. Refresh its registration only if you intentionally rebuilt "
                        "or replaced it.");
                }
                if (editor.ManagementBusy)
                    ui.TextColored(m_Tokens.Accent, editor.ManagementStatus);
                else if (editor.Running)
                    ui.TextColored(m_Tokens.Warning, "Editor is running; installation changes are disabled.");
                else if (editor.HasActiveTask)
                    ui.TextColored(m_Tokens.Warning, "An installation task is active.");
                if (auto disabled = ui.BeginDisabled(editorBusy); disabled)
                {
                    const auto label = editor.ManagementBusy                 ? "Checking..."
                                       : editor.RegistrationRefreshAvailable ? "Refresh registration"
                                                                             : "Verify";
                    const auto width = editor.RegistrationRefreshAvailable ? 152.0F : 82.0F;
                    if (ui.Button(label, {width, 30.0F}))
                    {
                        command = {.Type = editor.RegistrationRefreshAvailable
                                               ? HubUiCommandType::RefreshExternalEditorRegistration
                                               : HubUiCommandType::VerifyEditor,
                                   .ItemId = editor.Id,
                                   .Path = editor.Root};
                    }
                }
                if (editor.RepairAvailable)
                {
                    ui.SameLine();
                    if (auto disabled = ui.BeginDisabled(editorBusy); disabled)
                    {
                        if (ui.Button("Repair", {82.0F, 30.0F}))
                        {
                            command = {.Type = HubUiCommandType::RepairManagedEditor,
                                       .ItemId = editor.Id,
                                       .Path = editor.Root};
                        }
                    }
                }
                ui.SameLine();
                if (auto disabled =
                        ui.BeginDisabled(!editor.Healthy || editor.Entrypoint.empty() ||
                                         editor.AssetToolEntrypoint.empty() || snapshot.EditorManagementBusy);
                    disabled)
                {
                    if (ui.Button("Create project", {112.0F, 30.0F}))
                    {
                        command = {.Type = HubUiCommandType::CreateProjectFromTemplate,
                                   .ItemId = "keire.3d-starter",
                                   .Text = editor.Id};
                    }
                }
                ui.SameLine();
                if (auto disabled =
                        ui.BeginDisabled(!editor.Healthy || editor.AssetToolEntrypoint.empty() || editorBusy);
                    disabled)
                {
                    if (ui.Button("Manage Components", {142.0F, 30.0F}))
                    {
                        command = {
                            .Type = HubUiCommandType::ManageBuildSupport, .ItemId = editor.Id, .Path = editor.Root};
                    }
                }
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(editor.Missing); disabled)
                {
                    if (ui.Button("Reveal", {82.0F, 30.0F}))
                        command = {.Type = HubUiCommandType::RevealPath, .ItemId = editor.Id, .Path = editor.Root};
                }
                ui.SameLine();
                if (editor.Managed)
                {
                    if (auto disabled = ui.BeginDisabled((!editor.Healthy && !editor.Missing) || editorBusy); disabled)
                    {
                        if (ui.Button(editor.Missing ? "Remove from Hub..." : "Uninstall...",
                                      {editor.Missing ? 142.0F : 102.0F, 30.0F}))
                        {
                            m_PendingEditorRemoval = editor;
                            m_ConfirmManagedEditorRemoval = false;
                            m_RequestEditorRemoval = true;
                        }
                    }
                    if (!editor.Healthy)
                    {
                        ui.TextColoredWrapped(
                            m_Tokens.MutedText,
                            editor.Missing
                                ? "The editor folder is already missing. Remove this stale Hub registration to "
                                  "reinstall the version; no files will be deleted."
                            : editor.RepairAvailable
                                ? "Repair restores the exact signed package set before uninstall is allowed."
                                : "This installation has no receipt-bound repair source. Repair or verify it before "
                                  "removing it.");
                    }
                }
                else if (auto disabled = ui.BeginDisabled(editorBusy); disabled)
                {
                    if (ui.Button("Remove from Hub...", {142.0F, 30.0F}))
                    {
                        m_PendingEditorRemoval = editor;
                        m_ConfirmManagedEditorRemoval = false;
                        m_RequestEditorRemoval = true;
                    }
                }
            }
            ui.Spacing();
        }

        DrawAvailableEditors(ui, snapshot);
        DrawEditorInstallDialog(ui, snapshot, command);

        if (std::exchange(m_RequestEditorRemoval, false))
            ui.OpenPopup("Remove Editor?");
        PrepareHubModal(ui, {600.0F, 390.0F});
        HubModalStyleScope removalStyle(ui, m_Tokens);
        if (auto confirmation = ui.BeginPopupModal("Remove Editor?", nullptr, HubModalWindowOptions(), false);
            confirmation)
        {
            const auto version = m_PendingEditorRemoval ? m_PendingEditorRemoval->Version : std::string("selected");
            const bool managed = m_PendingEditorRemoval && m_PendingEditorRemoval->Managed;
            const bool missingManaged = managed && m_PendingEditorRemoval->Missing;
            DrawHubModalHeader(ui, m_Tokens,
                               missingManaged ? "Remove missing Kéire Editor " + version + "?"
                               : managed      ? "Uninstall Kéire Editor " + version + "?"
                                              : "Remove Kéire Editor " + version + "?",
                               missingManaged
                                   ? "The folder is already absent; only its stale Hub registration will be removed."
                               : managed ? "Review the managed files that will be permanently removed."
                                         : "The editor files stay on disk; only this Hub registration is removed.",
                               missingManaged ? "MISSING EDITOR"
                               : managed      ? "DESTRUCTIVE ACTION"
                                              : "EXTERNAL EDITOR");
            if (missingManaged)
            {
                ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                      "This recovery removes the missing installation from the Hub so the same "
                                      "version can be installed again. It does not delete any files.");
                if (m_PendingEditorRemoval)
                    ui.TextColoredWrapped(m_Tokens.Warning, Utf8Path(m_PendingEditorRemoval->Root));
            }
            else if (managed)
            {
                ui.TextColoredWrapped(
                    m_Tokens.SecondaryText,
                    "This permanently removes the complete managed editor installation shown below. Projects, Hub "
                    "settings, downloads, and externally managed editors are not deleted.");
                if (m_PendingEditorRemoval)
                    ui.TextColoredWrapped(m_Tokens.Warning, Utf8Path(m_PendingEditorRemoval->Root));
                (void)ui.Checkbox("I understand that this editor folder will be permanently deleted",
                                  m_ConfirmManagedEditorRemoval);
            }
            else
            {
                ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                      "Only this external installation's Hub registration is removed. The editor "
                                      "folder and every file inside it remain untouched.");
            }
            if (m_PendingEditorRemoval && m_PendingEditorRemoval->Running)
                ui.TextColored(m_Tokens.Warning, "Close the running editor before continuing.");
            else if (m_PendingEditorRemoval && m_PendingEditorRemoval->HasActiveTask)
                ui.TextColored(m_Tokens.Warning, "Wait for the active installation task to finish first.");
            else if (snapshot.BuildSupportBusy)
                ui.TextColored(m_Tokens.Warning, "Wait for the active Build Support operation to finish first.");
            else if (snapshot.EditorManagementBusy)
                ui.TextColored(m_Tokens.Warning, "Wait for the active editor installation check to finish first.");
            const bool blocked =
                !m_PendingEditorRemoval || m_PendingEditorRemoval->Running || m_PendingEditorRemoval->HasActiveTask ||
                snapshot.BuildSupportBusy || snapshot.EditorManagementBusy ||
                (managed && !missingManaged && (!m_PendingEditorRemoval->Healthy || !m_ConfirmManagedEditorRemoval));
            if (auto disabled = ui.BeginDisabled(blocked); disabled)
            {
                const bool remove = missingManaged ? HubPrimaryButton(ui, m_Tokens, "Remove from Hub", {142.0F, 38.0F})
                                    : managed      ? HubDangerButton(ui, m_Tokens, "Uninstall editor", {142.0F, 38.0F})
                                                   : HubPrimaryButton(ui, m_Tokens, "Remove from Hub", {142.0F, 38.0F});
                if (remove)
                {
                    command = {.Type = missingManaged ? HubUiCommandType::RemoveMissingManagedEditor
                                       : managed      ? HubUiCommandType::RemoveManagedEditor
                                                      : HubUiCommandType::RemoveExternalEditor,
                               .ItemId = m_PendingEditorRemoval->Id,
                               .Path = m_PendingEditorRemoval->Root};
                    m_PendingEditorRemoval.reset();
                    m_ConfirmManagedEditorRemoval = false;
                    ui.CloseCurrentPopup();
                }
            }
            ui.SameLine();
            if (HubSecondaryButton(ui, m_Tokens, "Cancel", {88.0F, 38.0F}))
            {
                m_PendingEditorRemoval.reset();
                m_ConfirmManagedEditorRemoval = false;
                ui.CloseCurrentPopup();
            }
        }
    }

    void HubProductUi::DrawTemplates(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        PageHeader(ui, m_Tokens, "Templates",
                   "Browse the verified project starting points packaged with this Hub release.");
        if (snapshot.Templates.empty())
        {
            ui.TextColored(m_Tokens.SecondaryText, "No verified templates are installed.");
            return;
        }

        (void)ui.InputTextWithHint("##TemplateSearch", "Search templates, descriptions, or tags", m_TemplateSearch);
        ui.SameLine();
        const auto categoryName = [](const HubTemplateCategoryFilter category) -> std::string_view
        {
            switch (category)
            {
            case HubTemplateCategoryFilter::All:
                return "All categories";
            case HubTemplateCategoryFilter::Core:
                return "Core";
            case HubTemplateCategoryFilter::Sample:
                return "Sample";
            case HubTemplateCategoryFilter::Learning:
                return "Learning";
            }
            return "All categories";
        };
        if (auto categories = ui.BeginCombo("##TemplateCategory", categoryName(m_TemplateCategory)); categories)
        {
            if (ui.Selectable("All categories", m_TemplateCategory == HubTemplateCategoryFilter::All))
                m_TemplateCategory = HubTemplateCategoryFilter::All;
            constexpr std::array categoryOptions{std::pair{HubTemplateCategoryFilter::Core, "Core"},
                                                 std::pair{HubTemplateCategoryFilter::Sample, "Sample"},
                                                 std::pair{HubTemplateCategoryFilter::Learning, "Learning"}};
            for (const auto& [category, name] : categoryOptions)
            {
                if (std::ranges::none_of(snapshot.Templates,
                                         [&](const HubTemplateUiRecord& item) { return item.Category == name; }))
                    continue;
                if (ui.Selectable(name, m_TemplateCategory == category))
                    m_TemplateCategory = category;
            }
        }

        const auto visible =
            QueryTemplateIndices(snapshot.Templates, {.Search = m_TemplateSearch, .Category = m_TemplateCategory});
        if (visible.empty())
        {
            ui.Spacing();
            ui.TextColored(m_Tokens.SecondaryText, "No installed templates match this search and category.");
            return;
        }
        if (std::ranges::none_of(visible, [&](const std::size_t index)
                                 { return snapshot.Templates[index].Id == m_SelectedTemplateId; }))
            m_SelectedTemplateId = snapshot.Templates[visible.front()].Id;

        std::vector<HubTemplateEditorCompatibilityInput> editors;
        editors.reserve(snapshot.Editors.size());
        for (const auto& editor : snapshot.Editors)
        {
            editors.push_back({.Version = editor.Version,
                               .MinimumProjectSchema = editor.MinimumProjectSchema,
                               .MaximumProjectSchema = editor.MaximumProjectSchema,
                               .Healthy = editor.Healthy,
                               .HasEntrypoint = !editor.Entrypoint.empty(),
                               .HasAssetToolEntrypoint = !editor.AssetToolEntrypoint.empty()});
        }

        Keire::UiTableOptions layoutOptions;
        layoutOptions.Borders = false;
        layoutOptions.RowBackground = false;
        layoutOptions.PersistSettings = false;
        if (auto layout = ui.BeginTable("TemplateBrowserLayout", 2, layoutOptions); layout)
        {
            ui.TableSetupColumn("Installed templates", Keire::UiTableColumnSizing::Fixed, 356.0F);
            ui.TableSetupColumn("Template details", Keire::UiTableColumnSizing::Stretch, 1.0F);
            ui.TableNextRow();
            (void)ui.TableNextColumn();
            if (auto list = ui.BeginChild("TemplateBrowserList", {0.0F, 0.0F}, false); list)
            {
                for (const auto index : visible)
                {
                    const auto& item = snapshot.Templates[index];
                    auto id = ui.PushId(item.Id);
                    if (auto card = ui.BeginChild("TemplateCard", {0.0F, 246.0F}, true); card)
                    {
                        DrawTemplateArtwork(ui, m_Tokens, item.Name, item.Thumbnail.Available,
                                            ResolveTemplateArtwork(ui, item.Thumbnail),
                                            {std::max(ui.ContentAvailable().Width, 1.0F), 88.0F}, item.Id);
                        const auto selected = m_SelectedTemplateId == item.Id;
                        if (ui.Selectable(item.Name + "##TemplateSelection", selected))
                            m_SelectedTemplateId = item.Id;
                        ui.TextColored(m_Tokens.Accent, item.Category + "  •  Installed v" + item.Version +
                                                            (item.Featured ? "  •  Featured" : ""));
                        ui.TextColoredWrapped(m_Tokens.SecondaryText, item.Description);
                        const auto compatibleEditors = CountCompatibleEditors(item, editors);
                        ui.TextColored(compatibleEditors > 0 ? m_Tokens.Success : m_Tokens.Warning,
                                       compatibleEditors > 0
                                           ? "Compatible with " + std::to_string(compatibleEditors) +
                                                 (compatibleEditors == 1 ? " installed editor" : " installed editors")
                                           : "No compatible editor installed");
                    }
                    ui.Spacing();
                }
            }

            (void)ui.TableNextColumn();
            const auto selected = std::ranges::find(snapshot.Templates, m_SelectedTemplateId, &HubTemplateUiRecord::Id);
            if (selected == snapshot.Templates.end())
                return;
            if (auto details = ui.BeginChild("TemplateDetails", {0.0F, 0.0F}, true); details)
            {
                const auto artworkWidth = std::clamp(ui.ContentAvailable().Width, 1.0F, 640.0F);
                DrawTemplateArtwork(ui, m_Tokens, selected->Name, selected->Thumbnail.Available,
                                    ResolveTemplateArtwork(ui, selected->Thumbnail),
                                    {artworkWidth, std::min(220.0F, artworkWidth * 0.5625F)}, selected->Id);
                ui.Spacing();
                {
                    const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                    ui.TextColored(m_Tokens.PrimaryText, selected->Name);
                }
                ui.TextColored(m_Tokens.Accent, selected->Category + "  •  v" + selected->Version +
                                                    (selected->Featured ? "  •  Featured" : ""));
                ui.TextColoredWrapped(m_Tokens.SecondaryText, selected->Description);
                ui.Spacing();

                const auto compatibleEditors = CountCompatibleEditors(*selected, editors);
                ui.TextColored(compatibleEditors > 0 ? m_Tokens.Success : m_Tokens.Warning,
                               compatibleEditors > 0 ? "Compatible editor installations"
                                                     : "No compatible editor is installed");
                if (snapshot.Editors.empty())
                    ui.TextColored(m_Tokens.MutedText, "Install or locate an editor to create this project.");
                for (std::size_t index = 0; index < snapshot.Editors.size(); ++index)
                {
                    const auto compatibility = EvaluateTemplateCompatibility(*selected, editors[index]);
                    ui.TextColored(compatibility.Compatible() ? m_Tokens.Success : m_Tokens.MutedText,
                                   snapshot.Editors[index].Version + "  •  " + compatibility.Label);
                }

                ui.Spacing();
                ui.Separator();
                ui.TextColored(m_Tokens.PrimaryText, "Template details");
                ui.TextColored(m_Tokens.SecondaryText, "Editor versions: " + selected->CompatibleEditors +
                                                           "  •  Project schema " +
                                                           std::to_string(selected->ProjectSchema));
                ui.TextColored(m_Tokens.SecondaryText, "Target: " + selected->PlatformTarget +
                                                           "  •  Estimated project size " +
                                                           HumanBytes(selected->EstimatedBytes));
                if (!selected->Tags.empty())
                {
                    std::string tags = "Tags: ";
                    for (std::size_t index = 0; index < selected->Tags.size(); ++index)
                    {
                        if (index != 0)
                            tags += ", ";
                        tags += selected->Tags[index];
                    }
                    ui.TextColoredWrapped(m_Tokens.SecondaryText, tags);
                }
                const auto screenshots =
                    std::ranges::count_if(selected->Screenshots, &HubTemplateArtworkUiRecord::Available);
                if (screenshots == 0)
                    ui.TextColored(m_Tokens.SecondaryText, "No screenshots are packaged");
                else
                {
                    ui.Spacing();
                    ui.TextColored(m_Tokens.PrimaryText, "Screenshots");
                    std::size_t screenshotIndex = 0;
                    for (const auto& screenshot : selected->Screenshots)
                    {
                        if (!screenshot.Available)
                            continue;
                        const auto width = std::clamp(ui.ContentAvailable().Width, 1.0F, 480.0F);
                        DrawTemplateArtwork(ui, m_Tokens, selected->Name, true, ResolveTemplateArtwork(ui, screenshot),
                                            {width, width * 0.5625F},
                                            selected->Id + "-screenshot-" + std::to_string(screenshotIndex++));
                        ui.Spacing();
                    }
                }

                ui.Spacing();
                ui.TextColored(m_Tokens.PrimaryText, "Included starter content");
                if (selected->StarterContent.empty())
                    ui.TextColored(m_Tokens.MutedText, "No starter content");
                for (const auto& content : selected->StarterContent)
                    ui.TextColored(m_Tokens.SecondaryText, content);

                ui.Spacing();
                ui.TextColored(m_Tokens.PrimaryText, "Package requirements");
                if (selected->RequiredPackages.empty() && selected->RecommendedPackages.empty())
                    ui.TextColored(m_Tokens.MutedText, "No additional packages");
                for (const auto& package : selected->RequiredPackages)
                    ui.TextColored(m_Tokens.SecondaryText,
                                   "Required: " + package.PackageId + " " + package.VersionConstraint);
                for (const auto& package : selected->RecommendedPackages)
                    ui.TextColored(m_Tokens.SecondaryText,
                                   "Recommended: " + package.PackageId + " " + package.VersionConstraint);

                ui.Spacing();
                ui.TextColored(m_Tokens.PrimaryText, "Licensing");
                if (selected->LicenseReferences.empty())
                    ui.TextColored(m_Tokens.MutedText, "No additional template license references");
                for (const auto& license : selected->LicenseReferences)
                    ui.TextColored(m_Tokens.SecondaryText, license);

                ui.Spacing();
                if (auto disabled = ui.BeginDisabled(compatibleEditors == 0 || snapshot.ProjectCreationBusy); disabled)
                {
                    if (ui.Button("Create with this template", {184.0F, 34.0F}))
                        command = {.Type = HubUiCommandType::CreateProjectFromTemplate, .ItemId = selected->Id};
                }
                if (snapshot.ProjectCreationBusy)
                    ui.TextColored(m_Tokens.Accent, snapshot.ProjectCreationMessage);
            }
        }
    }

    void HubProductUi::SynchronizeSettings(const HubSettings& settings)
    {
        if (!m_EditedSettings)
            m_EditedSettings = settings;
    }

    void HubProductUi::DrawSettings(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        SynchronizeSettings(snapshot.Settings);
        auto& settings = *m_EditedSettings;
        PageHeader(ui, m_Tokens, "Settings", "Hub preferences are versioned, validated, and written atomically.");

        auto settingsBody = ui.BeginChild("SettingsBody", {0.0F, 0.0F}, false);
        if (!settingsBody)
            return;

        ui.TextColored(m_Tokens.PrimaryText, "General");
        constexpr std::array pageLabels{std::string_view("Home"),     std::string_view("Projects"),
                                        std::string_view("Installs"), std::string_view("Templates"),
                                        std::string_view("Learn"),    std::string_view("Resources"),
                                        std::string_view("Licenses"), std::string_view("Settings")};
        const auto currentPage = static_cast<std::size_t>(settings.StartupPage);
        if (auto combo = ui.BeginCombo("Startup page", pageLabels[currentPage]); combo)
        {
            for (std::size_t index = 0; index < pageLabels.size(); ++index)
                if (ui.Selectable(pageLabels[index], index == currentPage))
                    settings.StartupPage = static_cast<HubPage>(index);
        }
        constexpr std::array appearanceLabels{std::string_view("System"), std::string_view("Dark"),
                                              std::string_view("Light")};
        const auto appearance = static_cast<std::size_t>(settings.Appearance);
        if (auto combo = ui.BeginCombo("Appearance", appearanceLabels[appearance]); combo)
        {
            for (std::size_t index = 0; index < appearanceLabels.size(); ++index)
                if (ui.Selectable(appearanceLabels[index], index == appearance))
                    settings.Appearance = static_cast<HubAppearance>(index);
        }
        (void)ui.Checkbox("Keep Hub running after editor launch", settings.KeepRunningAfterEditorLaunch);
        (void)ui.Checkbox("Close to system tray", settings.CloseToTray);
        (void)ui.Checkbox("Reduce motion and animations", settings.ReducedMotion);
        (void)ui.Checkbox("Check for updates", settings.CheckForUpdates);

        ui.Spacing();
        ui.Separator();
        ui.TextColored(m_Tokens.PrimaryText, "Projects");
        auto projectRoot = Utf8Path(settings.DefaultProjectLocation);
        if (ui.InputTextWithHint("Default project location", "Choose a project root", projectRoot))
            settings.DefaultProjectLocation = PathFromUtf8(projectRoot);
        ui.TextColored(m_Tokens.MutedText,
                       "Discovery scans only the roots listed here; it never scans an entire disk.");
        std::optional<std::size_t> removedDiscoveryRoot;
        for (std::size_t index = 0; index < settings.ProjectDiscoveryRoots.size(); ++index)
        {
            auto id = ui.PushId(std::to_string(index));
            auto root = Utf8Path(settings.ProjectDiscoveryRoots[index]);
            if (ui.InputTextWithHint("Discovery root", "Additional project folder", root))
                settings.ProjectDiscoveryRoots[index] = PathFromUtf8(root);
            ui.SameLine();
            if (ui.Button("Remove##DiscoveryRoot", {76.0F, 0.0F}))
                removedDiscoveryRoot = index;
        }
        if (removedDiscoveryRoot)
            settings.ProjectDiscoveryRoots.erase(settings.ProjectDiscoveryRoots.begin() +
                                                 static_cast<std::ptrdiff_t>(*removedDiscoveryRoot));
        if (settings.ProjectDiscoveryRoots.size() < 32 && ui.Button("Add discovery root", {142.0F, 30.0F}))
            settings.ProjectDiscoveryRoots.push_back(settings.DefaultProjectLocation);
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(snapshot.FirstRunDiscoveryRunning); disabled)
        {
            if (ui.Button("Scan selected roots", {148.0F, 30.0F}))
            {
                command = {.Type = HubUiCommandType::BeginFirstRunDiscovery,
                           .ItemId = "settings-discovery",
                           .Settings = settings};
            }
        }
        if (snapshot.FirstRunDiscoveryRunning || !snapshot.FirstRunDiscoveryMessage.empty())
        {
            ui.TextColored(m_Tokens.MutedText, snapshot.FirstRunDiscoveryRunning
                                                   ? snapshot.FirstRunDiscoveryMessage
                                                   : snapshot.FirstRunDiscoveryMessage + " " +
                                                         std::to_string(snapshot.DiscoveredProjects) + " project(s), " +
                                                         std::to_string(snapshot.DiscoveredEditors) + " editor(s).");
        }
        (void)ui.Checkbox("Confirm before removing a project from the Hub", settings.ConfirmProjectRemoval);
        (void)ui.Checkbox("Remove missing projects automatically", settings.RemoveMissingProjectsAutomatically);

        ui.Spacing();
        ui.Separator();
        ui.TextColored(m_Tokens.PrimaryText, "Installs and downloads");
        auto editorRoot = Utf8Path(settings.DefaultEditorRoot);
        if (ui.InputTextWithHint("Default editor root", "Choose an editor root", editorRoot))
            settings.DefaultEditorRoot = PathFromUtf8(editorRoot);
        auto cacheRoot = Utf8Path(settings.CacheRoot);
        if (ui.InputTextWithHint("Verified cache root", "Package cache location", cacheRoot))
            settings.CacheRoot = PathFromUtf8(cacheRoot);
        auto temporaryRoot = Utf8Path(settings.TemporaryRoot);
        if (ui.InputTextWithHint("Temporary files root", "Operation staging location", temporaryRoot))
            settings.TemporaryRoot = PathFromUtf8(temporaryRoot);
        int downloads = static_cast<int>(settings.ConcurrentDownloads);
        if (ui.SliderInt("Concurrent downloads", downloads, 1, 8))
            settings.ConcurrentDownloads = static_cast<std::uint32_t>(downloads);
        (void)ui.Checkbox("Stable channel", settings.EnableStableChannel);
        (void)ui.Checkbox("Pre-release channel", settings.EnablePreReleaseChannel);
        (void)ui.Checkbox("Nightly channel", settings.EnableNightlyChannel);

        ui.Spacing();
        ui.Separator();
        ui.TextColored(m_Tokens.PrimaryText, "Network and advanced");
        (void)ui.Checkbox("Offline mode", settings.OfflineMode);
        constexpr std::array proxyLabels{std::string_view("System proxy"), std::string_view("Custom proxy")};
        const auto proxy = static_cast<std::size_t>(settings.NetworkProxyMode);
        if (auto combo = ui.BeginCombo("Proxy", proxyLabels[proxy]); combo)
        {
            for (std::size_t index = 0; index < proxyLabels.size(); ++index)
                if (ui.Selectable(proxyLabels[index], index == proxy))
                    settings.NetworkProxyMode = static_cast<ProxyMode>(index);
        }
        if (settings.NetworkProxyMode == ProxyMode::Custom)
            (void)ui.InputTextWithHint("Custom proxy URL", "https://proxy.example:8443", settings.CustomProxyUrl);
        int bandwidthMiB = static_cast<int>(
            std::min<std::uint64_t>(settings.BandwidthLimitBytesPerSecond / (1024ULL * 1024ULL), 1024ULL));
        if (ui.SliderInt("Bandwidth limit (MiB/s, 0 = unlimited)", bandwidthMiB, 0, 1024))
            settings.BandwidthLimitBytesPerSecond = static_cast<std::uint64_t>(bandwidthMiB) * 1024ULL * 1024ULL;
        ui.TextColored(m_Tokens.MutedText, "Log-level changes take effect after restart.");
        constexpr std::array logLevels{std::string_view("trace"), std::string_view("debug"), std::string_view("info"),
                                       std::string_view("warning"), std::string_view("error")};
        if (auto combo = ui.BeginCombo("Log level", settings.LogLevel); combo)
            for (const auto level : logLevels)
                if (ui.Selectable(level, settings.LogLevel == level))
                    settings.LogLevel = level;
#if !defined(KEIRE_DISTRIBUTION)
        ui.TextColored(m_Tokens.MutedText,
                       "Developer override requires both a service URL and a separately trusted public key.");
        auto developmentService = settings.DevelopmentServiceUrl.value_or("");
        if (ui.InputTextWithHint("Development service", "http://127.0.0.1:5080", developmentService))
            settings.DevelopmentServiceUrl =
                developmentService.empty() ? std::nullopt : std::optional(std::move(developmentService));
        auto developmentKey = settings.DevelopmentTrustedKey.value_or("");
        if (ui.InputTextWithHint("Development trusted key", "Ed25519 public-key document", developmentKey))
            settings.DevelopmentTrustedKey =
                developmentKey.empty() ? std::nullopt : std::optional(std::move(developmentKey));
#endif

        ui.Spacing();
        if (ui.Button("Save settings", {116.0F, 34.0F}))
            command = {.Type = HubUiCommandType::SaveSettings, .Settings = settings};
        ui.SameLine();
        if (ui.Button("Collect diagnostics...", {160.0F, 34.0F}))
            command.Type = HubUiCommandType::CopyDiagnostics;
        ui.SameLine();
        if (ui.Button("Open logs", {96.0F, 34.0F}))
            command.Type = HubUiCommandType::OpenLogs;
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(snapshot.VerifiedCacheClearRunning); disabled)
        {
            if (ui.Button(snapshot.VerifiedCacheClearRunning ? "Clearing cache..." : "Clear verified cache",
                          {150.0F, 34.0F}))
                m_RequestClearCache = true;
        }
        ui.SameLine();
        if (ui.Button("Reset settings", {116.0F, 34.0F}))
            m_RequestResetSettings = true;

        if (std::exchange(m_RequestClearCache, false))
            ui.OpenPopup("Clear verified cache?");
        {
            PrepareHubModal(ui, {540.0F, 280.0F});
            HubModalStyleScope clearCacheStyle(ui, m_Tokens);
            if (auto confirmation =
                    ui.BeginPopupModal("Clear verified cache?", nullptr, HubModalWindowOptions(), false);
                confirmation)
            {
                DrawHubModalHeader(ui, m_Tokens, "Clear verified package cache?",
                                   "Free disk space without changing installed products.", "STORAGE");
                ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                      "Downloaded packages will be removed. Installed editors, projects, settings, "
                                      "and signed catalog metadata are preserved.");
                if (auto disabled = ui.BeginDisabled(snapshot.VerifiedCacheClearRunning); disabled)
                {
                    if (HubDangerButton(ui, m_Tokens,
                                        snapshot.VerifiedCacheClearRunning ? "Clearing..." : "Clear cache",
                                        {118.0F, 38.0F}))
                    {
                        command.Type = HubUiCommandType::ClearVerifiedCache;
                        ui.CloseCurrentPopup();
                    }
                }
                ui.SameLine();
                if (HubSecondaryButton(ui, m_Tokens, "Cancel", {88.0F, 38.0F}))
                    ui.CloseCurrentPopup();
            }
        }
        if (std::exchange(m_RequestResetSettings, false))
            ui.OpenPopup("Reset Hub settings?");
        {
            PrepareHubModal(ui, {540.0F, 280.0F});
            HubModalStyleScope resetStyle(ui, m_Tokens);
            if (auto confirmation = ui.BeginPopupModal("Reset Hub settings?", nullptr, HubModalWindowOptions(), false);
                confirmation)
            {
                DrawHubModalHeader(ui, m_Tokens, "Reset Hub settings?",
                                   "Return the Hub to its first-run configuration.", "SETTINGS");
                ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                      "Hub preferences will return to defaults and first-run setup will appear "
                                      "again. Projects and editor installations are not removed.");
                if (HubDangerButton(ui, m_Tokens, "Reset settings", {132.0F, 38.0F}))
                {
                    command.Type = HubUiCommandType::ResetSettings;
                    ui.CloseCurrentPopup();
                }
                ui.SameLine();
                if (HubSecondaryButton(ui, m_Tokens, "Cancel", {88.0F, 38.0F}))
                    ui.CloseCurrentPopup();
            }
        }
    }

    void HubProductUi::DrawTaskCenter(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        const bool closeRequested = std::exchange(m_CloseTaskCenter, false);
        if (std::exchange(m_TaskCenterOpen, false))
            ui.OpenPopup("HubTaskPopover");
        const auto bounds = ui.ContentRect();
        constexpr float popoverWidth = 460.0F;
        ui.SetNextWindowPosition(
            {std::max(bounds.Maximum.X - popoverWidth - 12.0F, bounds.Minimum.X + 8.0F), bounds.Minimum.Y + 8.0F},
            false);
        ui.SetNextWindowSize({popoverWidth, 420.0F}, false);
        [[maybe_unused]] const auto popoverBackground =
            ui.PushStyleColor(Keire::UiStyleColorRole::PopupBackground, m_Tokens.Surface);
        if (auto center = ui.BeginPopup("HubTaskPopover"); center)
        {
            if (closeRequested)
            {
                ui.CloseCurrentPopup();
                return;
            }
            const auto headerRow = ui.CursorPosition();
            const auto headerWidth = ui.ContentAvailable().Width;
            {
                const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                ui.TextColored(m_Tokens.PrimaryText, "Tasks and downloads");
            }
            ui.SetCursorPosition({headerRow.X + std::max(headerWidth - 64.0F, 0.0F), headerRow.Y});
            if (ui.Button("Close##TaskCenter", {64.0F, 26.0F}))
                ui.CloseCurrentPopup();
            ui.TextColored(m_Tokens.SecondaryText, "Downloads, verification, installation, and recent results.");
            ui.Spacing();
            if (std::ranges::any_of(snapshot.Tasks, &HubTaskUiRecord::Dismissible) &&
                ui.Button("Clear finished##TaskCenter", {104.0F, 26.0F}))
            {
                command.Type = HubUiCommandType::ClearFinishedTasks;
            }
            if (snapshot.Tasks.empty())
            {
                ui.TextColored(m_Tokens.SecondaryText, "No current or recent tasks.");
                return;
            }
            for (const auto& task : snapshot.Tasks)
            {
                auto id = ui.PushId(task.Id);
                DrawTaskActivityCard(ui, m_Tokens, task, command);
                ui.Spacing();
            }
        }
    }

    void HubProductUi::DrawNotificationCenter(Keire::UiFrame& ui, const HubProductSnapshot& snapshot,
                                              HubUiCommand& command)
    {
        const bool closeRequested = std::exchange(m_CloseNotificationCenter, false);
        if (std::exchange(m_NotificationCenterOpen, false))
            ui.OpenPopup("HubNotificationPopover");
        const auto bounds = ui.ContentRect();
        constexpr float popoverWidth = 480.0F;
        ui.SetNextWindowPosition(
            {std::max(bounds.Maximum.X - popoverWidth - 12.0F, bounds.Minimum.X + 8.0F), bounds.Minimum.Y + 8.0F},
            false);
        ui.SetNextWindowSize({popoverWidth, 520.0F}, false);
        [[maybe_unused]] const auto popoverBackground =
            ui.PushStyleColor(Keire::UiStyleColorRole::PopupBackground, m_Tokens.Surface);
        if (auto center = ui.BeginPopup("HubNotificationPopover"); center)
        {
            if (closeRequested)
            {
                ui.CloseCurrentPopup();
                return;
            }
            const auto headerRow = ui.CursorPosition();
            const auto headerWidth = ui.ContentAvailable().Width;
            {
                const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                ui.TextColored(m_Tokens.PrimaryText, "Activity and notifications");
            }
            ui.SetCursorPosition({headerRow.X + std::max(headerWidth - 64.0F, 0.0F), headerRow.Y});
            if (ui.Button("Close##Notifications", {64.0F, 26.0F}))
                ui.CloseCurrentPopup();
            ui.TextColored(m_Tokens.SecondaryText, "Install progress, completed work, warnings, and recovery actions.");
            ui.Spacing();
            if (!snapshot.Notifications.empty() && ui.Button("Clear history##Notifications", {104.0F, 26.0F}))
                command.Type = HubUiCommandType::ClearNotifications;
            const auto activeTasks = std::ranges::count_if(snapshot.Tasks, &HubTaskUiRecord::Active);
            if (activeTasks != 0)
            {
                ui.Spacing();
                ui.TextColored(m_Tokens.PrimaryText, "Active installs and downloads");
                for (const auto& task : snapshot.Tasks)
                {
                    if (!task.Active)
                        continue;
                    auto id = ui.PushId("notification-" + task.Id);
                    DrawTaskActivityCard(ui, m_Tokens, task, command);
                    ui.Spacing();
                }
                ui.Separator();
            }
            if (snapshot.Notifications.empty() && activeTasks == 0)
            {
                ui.TextColored(m_Tokens.SecondaryText, "No current activity or notifications.");
                return;
            }
            if (!snapshot.Notifications.empty())
                ui.TextColored(m_Tokens.PrimaryText, "Notification history");
            for (const auto& notification : snapshot.Notifications)
            {
                auto id = ui.PushId(notification.Id);
                const auto color = notification.Severity == "Error"     ? m_Tokens.Danger
                                   : notification.Severity == "Warning" ? m_Tokens.Warning
                                   : notification.Severity == "Success" ? m_Tokens.Success
                                                                        : m_Tokens.Accent;
                [[maybe_unused]] const auto cardBackground =
                    ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, m_Tokens.Elevated);
                const auto title = (notification.Read ? "" : "• ") + notification.Severity + "  " + notification.Title;
                const auto contentWidth = std::max(ui.ContentAvailable().Width - 24.0F, 1.0F);
                const auto lineHeight = std::max(ui.MeasureText("Ag").Height, 16.0F);
                const auto textLines = WrappedLineCount(ui, title, contentWidth) +
                                       WrappedLineCount(ui, notification.Message, contentWidth);
                const auto cardHeight = std::clamp(68.0F + static_cast<float>(textLines) * lineHeight, 118.0F, 238.0F);
                if (auto card = ui.BeginChild("NotificationCard", {0.0F, cardHeight}, true); card)
                {
                    ui.TextColoredWrapped(color, title);
                    ui.TextColoredWrapped(m_Tokens.SecondaryText, notification.Message);
                    ui.Spacing();
                    if (!notification.Read && HubSecondaryButton(ui, m_Tokens, "Mark read", {92.0F, 28.0F}))
                        command = {.Type = HubUiCommandType::MarkNotificationRead, .ItemId = notification.Id};
                    if (notification.Read && HubSecondaryButton(ui, m_Tokens, "Dismiss", {92.0F, 28.0F}))
                        command = {.Type = HubUiCommandType::DismissNotification, .ItemId = notification.Id};
                }
                ui.Spacing();
            }
        }
    }

} // namespace KeireHub
