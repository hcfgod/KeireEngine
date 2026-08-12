#include "KeireHub/HubProductUi.h"

#include "KeireHub/HubModalUi.h"
#include "KeireHubRuntime/PackageResolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace KeireHub
{
    namespace
    {
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

        [[nodiscard]] std::string Utf8Path(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        [[nodiscard]] std::filesystem::path PathFromUtf8(const std::string_view value)
        {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
        }

        [[nodiscard]] std::string_view ChannelLabel(const std::string_view channel) noexcept
        {
            if (channel == "stable")
                return "Stable";
            if (channel == "preview")
                return "Pre-release";
            return "Nightly";
        }

        [[nodiscard]] bool ContainsInsensitive(const std::string_view value, const std::string_view query)
        {
            if (query.empty())
                return true;
            const auto found = std::ranges::search(value, query, [](const unsigned char left, const unsigned char right)
                                                   { return std::tolower(left) == std::tolower(right); });
            return !found.empty();
        }

        [[nodiscard]] HubEditorInstallUiRequest
        BuildRequest(const HubAvailableEditorUiRecord& editor, const std::string& destination,
                     std::vector<HubEditorComponentSelectionUiRecord> components)
        {
            std::ranges::sort(
                components, [](const auto& left, const auto& right)
                { return std::tie(left.PackageId, left.Version) < std::tie(right.PackageId, right.Version); });
            return {.Destination = PathFromUtf8(destination).lexically_normal(),
                    .EditorPackageId = editor.PackageId,
                    .EditorVersion = editor.Version,
                    .Components = std::move(components)};
        }

        [[nodiscard]] std::string RequiredByLabel(const std::vector<std::string>& packageIds)
        {
            constexpr std::size_t MaximumVisibleParents = 3;
            std::string result;
            const auto visible = std::min(packageIds.size(), MaximumVisibleParents);
            for (std::size_t index = 0; index < visible; ++index)
            {
                if (!result.empty())
                    result += ", ";
                result += packageIds[index];
            }
            if (packageIds.size() > visible)
                result += " +" + std::to_string(packageIds.size() - visible);
            return result;
        }

        [[nodiscard]] bool DestinationUnavailable(const std::filesystem::path& destination,
                                                  const std::span<const HubTaskUiRecord> tasks)
        {
            if (std::ranges::any_of(tasks,
                                    [&](const HubTaskUiRecord& task)
                                    {
                                        return task.Active && !task.EditorDestination.empty() &&
                                               task.EditorDestination.lexically_normal() ==
                                                   destination.lexically_normal();
                                    }))
            {
                return true;
            }
            std::error_code error;
            const auto status = std::filesystem::symlink_status(destination, error);
            return error ? error != std::errc::no_such_file_or_directory
                         : status.type() != std::filesystem::file_type::not_found;
        }

        [[nodiscard]] std::filesystem::path NextEditorInstallDestination(const std::filesystem::path& preferred,
                                                                         const std::span<const HubTaskUiRecord> tasks)
        {
            if (!DestinationUnavailable(preferred, tasks))
                return preferred;
            for (std::size_t suffix = 2; suffix <= 1'000; ++suffix)
            {
                const auto candidate = preferred.parent_path() / PathFromUtf8(Utf8Path(preferred.filename()) + " (" +
                                                                              std::to_string(suffix) + ')');
                if (!DestinationUnavailable(candidate, tasks))
                    return candidate;
            }
            return preferred;
        }

        [[nodiscard]] std::string DestinationProblem(const std::filesystem::path& destination)
        {
            if (destination.empty())
                return "Choose an install location.";
            if (!destination.is_absolute() || destination == destination.root_path() || destination.filename().empty())
                return "Install location must be an absolute folder below a drive or filesystem root.";
            if (std::ranges::any_of(destination,
                                    [](const auto& component) { return component == "." || component == ".."; }))
                return "Install location cannot contain . or .. path components.";
            std::error_code error;
            const auto status = std::filesystem::symlink_status(destination, error);
            if (error && error.default_error_condition() != std::errc::no_such_file_or_directory)
                return "Install location could not be inspected: " + error.message();
            if (!error && status.type() != std::filesystem::file_type::not_found)
                return "Install location already exists. Choose a new folder name.";
            return {};
        }
    } // namespace

    bool HubProductUi::RequestEditorInstall(const std::string_view packageOrVersion, const HubProductSnapshot& snapshot)
    {
        if (!snapshot.AvailableEditors || packageOrVersion.empty())
            return false;
        const auto selectable = [](const HubAvailableEditorUiRecord& editor)
        { return editor.AvailabilityMessage.empty() && editor.InstalledInstallationIds.empty(); };
        auto candidate =
            std::ranges::find(*snapshot.AvailableEditors, packageOrVersion, &HubAvailableEditorUiRecord::PackageId);
        if (candidate != snapshot.AvailableEditors->end())
        {
            if (!selectable(*candidate))
                return false;
        }
        else
        {
            candidate = std::ranges::find_if(*snapshot.AvailableEditors, [&](const auto& editor)
                                             { return editor.Version == packageOrVersion && selectable(editor); });
            if (candidate == snapshot.AvailableEditors->end())
            {
                const auto requested = SemanticVersion::Parse(packageOrVersion);
                if (!requested)
                    return false;
                std::optional<SemanticVersion> selectedVersion;
                for (auto current = snapshot.AvailableEditors->begin(); current != snapshot.AvailableEditors->end();
                     ++current)
                {
                    auto version = SemanticVersion::Parse(current->Version);
                    if (!version || version.Value() < requested.Value() || !selectable(*current))
                        continue;
                    if (!selectedVersion || version.Value() < *selectedVersion)
                    {
                        candidate = current;
                        selectedVersion = std::move(version).Value();
                    }
                }
                if (candidate == snapshot.AvailableEditors->end())
                    return false;
            }
        }
        m_PendingEditorInstall = *candidate;
        m_EditorInstallDestination =
            Utf8Path(snapshot.Settings.DefaultEditorRoot / PathFromUtf8("Kéire Editor " + candidate->Version));
        m_EditorComponentSearch.clear();
        m_SelectedEditorComponents.clear();
        m_LastEditorInstallRequest.reset();
        m_ConfirmDuplicateEditorInstall =
            HasActiveEditorInstall(snapshot.Tasks, candidate->PackageId, candidate->Version);
        m_RequestEditorInstall = true;
        return true;
    }

    void HubProductUi::SetEditorInstallParent(const std::filesystem::path& parent)
    {
        if (parent.empty() || !m_PendingEditorInstall)
            return;
        auto leaf = PathFromUtf8(m_EditorInstallDestination).filename();
        if (leaf.empty())
            leaf = PathFromUtf8("Kéire Editor " + m_PendingEditorInstall->Version);
        m_EditorInstallDestination = Utf8Path((parent / leaf).lexically_normal());
        m_LastEditorInstallRequest.reset();
    }

    void HubProductUi::DrawAvailableEditors(Keire::UiFrame& ui, const HubProductSnapshot& snapshot)
    {
        if (std::exchange(m_RequestEditorCatalog, false))
            ui.OpenPopup("Install Editor Catalog");
        PrepareHubModal(ui, {840.0F, 650.0F});
        HubModalStyleScope catalogStyle(ui, m_Tokens);
        auto catalog = ui.BeginPopupModal("Install Editor Catalog", nullptr, HubModalWindowOptions(), false);
        if (!catalog)
            return;

        DrawHubModalHeader(ui, m_Tokens, "Install Kéire Editor",
                           "Choose a verified editor release, then configure its location and components.",
                           "INSTALLS  •  VERSION CATALOG");
        if (!snapshot.EditorCatalogMessage.empty())
            ui.TextColoredWrapped(m_Tokens.Warning, snapshot.EditorCatalogMessage);

        ui.SetNextItemWidth(std::max(ui.ContentAvailable().Width - 206.0F, 120.0F));
        (void)ui.InputTextWithHint("##EditorCatalogSearch", "Search versions, channels, or platforms",
                                   m_EditorCatalogSearch);
        ui.SameLine();
        const auto selectedChannel =
            m_EditorCatalogChannel.empty() ? std::string_view("All channels") : ChannelLabel(m_EditorCatalogChannel);
        ui.SetNextItemWidth(190.0F);
        if (auto channelPicker = ui.BeginCombo("##EditorCatalogChannel", selectedChannel); channelPicker)
        {
            if (ui.Selectable("All channels", m_EditorCatalogChannel.empty()))
                m_EditorCatalogChannel.clear();
            for (const auto& channel : snapshot.PopulatedEditorChannels)
            {
                if (ui.Selectable(ChannelLabel(channel), m_EditorCatalogChannel == channel))
                    m_EditorCatalogChannel = channel;
            }
        }
        if (!m_EditorCatalogChannel.empty() &&
            std::ranges::find(snapshot.PopulatedEditorChannels, m_EditorCatalogChannel) ==
                snapshot.PopulatedEditorChannels.end())
        {
            m_EditorCatalogChannel.clear();
        }

        if (auto list = ui.BeginChild("EditorVersionCatalog", {0.0F, 490.0F}, true); list)
        {
            std::size_t visibleEditors = 0;
            if (snapshot.AvailableEditors)
            {
                for (const auto& editor : *snapshot.AvailableEditors)
                {
                    if ((!m_EditorCatalogChannel.empty() && editor.Channel != m_EditorCatalogChannel) ||
                        (!ContainsInsensitive(editor.DisplayName, m_EditorCatalogSearch) &&
                         !ContainsInsensitive(editor.Version, m_EditorCatalogSearch) &&
                         !ContainsInsensitive(editor.Channel, m_EditorCatalogSearch) &&
                         !ContainsInsensitive(editor.Platform, m_EditorCatalogSearch) &&
                         !ContainsInsensitive(editor.Architecture, m_EditorCatalogSearch)))
                    {
                        continue;
                    }
                    ++visibleEditors;
                    auto id = ui.PushId(editor.PackageId + "@" + editor.Version);
                    if (auto card = ui.BeginChild("AvailableEditor", {0.0F, 126.0F}, true); card)
                    {
                        const bool activeInstall =
                            HasActiveEditorInstall(snapshot.Tasks, editor.PackageId, editor.Version);
                        ui.TextColored(m_Tokens.Accent, std::string(ChannelLabel(editor.Channel)));
                        ui.TextColored(m_Tokens.PrimaryText, editor.DisplayName.empty()
                                                                 ? "Kéire Editor " + editor.Version
                                                                 : editor.DisplayName);
                        ui.TextColored(m_Tokens.SecondaryText,
                                       editor.Version + "  •  " + editor.Platform + " / " + editor.Architecture);
                        ui.TextColored(m_Tokens.MutedText, "Download " + HumanBytes(editor.DownloadBytes) +
                                                               "  •  Installed " + HumanBytes(editor.InstalledBytes));
                        if (!editor.AvailabilityMessage.empty())
                            ui.TextColoredWrapped(m_Tokens.Danger, editor.AvailabilityMessage);
                        else if (!editor.InstalledInstallationIds.empty())
                            ui.TextColored(m_Tokens.Success, "Installed and registered");
                        else if (auto disabled = ui.BeginDisabled(snapshot.EditorManagementBusy); disabled)
                        {
                            if (activeInstall)
                                ui.TextColored(m_Tokens.Warning, "Installation already in progress");
                            if (HubPrimaryButton(ui, m_Tokens, activeInstall ? "Install another" : "Select version",
                                                 {128.0F, 30.0F}))
                            {
                                (void)RequestEditorInstall(editor.PackageId, snapshot);
                                ui.CloseCurrentPopup();
                            }
                        }
                    }
                    ui.Spacing();
                }
            }

            if (visibleEditors == 0)
                ui.TextColored(m_Tokens.SecondaryText, snapshot.EditorCatalogRefreshing
                                                           ? "Loading verified editor catalogs..."
                                                           : "No verified editor release matches these filters.");
        }
        ui.Spacing();
        if (HubSecondaryButton(ui, m_Tokens, "Close", {88.0F, 36.0F}))
            ui.CloseCurrentPopup();
    }

    void HubProductUi::DrawEditorInstallDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot,
                                               HubUiCommand& command)
    {
        if (std::exchange(m_RequestEditorInstall, false))
            ui.OpenPopup("Install Editor");
        PrepareHubModal(ui, {760.0F, 600.0F});
        HubModalStyleScope modalStyle(ui, m_Tokens);
        auto installation = ui.BeginPopupModal("Install Editor", nullptr, HubModalWindowOptions(), false);
        if (!installation)
            return;
        if (!m_PendingEditorInstall)
        {
            DrawHubModalHeader(ui, m_Tokens, "Editor version unavailable",
                               "The verified catalog changed before this install could be reviewed.", "INSTALLS");
            ui.TextColoredWrapped(m_Tokens.Warning,
                                  "Refresh Installs and choose an editor version that is currently available.");
            if (HubSecondaryButton(ui, m_Tokens, "Close", {88.0F, 36.0F}))
                ui.CloseCurrentPopup();
            return;
        }

        const auto& editor = *m_PendingEditorInstall;
        const auto editorName = editor.DisplayName.empty() ? std::string("Kéire Editor") : editor.DisplayName;
        if (m_ConfirmDuplicateEditorInstall)
        {
            DrawHubModalHeader(ui, m_Tokens, "Editor version already downloading",
                               editorName + " " + editor.Version + " already has an active download or installation.",
                               "INSTALLS  •  CONFIRM");
            ui.TextColoredWrapped(
                m_Tokens.Warning,
                "Downloading again will create another managed copy in a different location. Continue?");
            if (HubPrimaryButton(ui, m_Tokens, "Download again", {144.0F, 38.0F}))
            {
                m_EditorInstallDestination = Utf8Path(NextEditorInstallDestination(
                    PathFromUtf8(m_EditorInstallDestination).lexically_normal(), snapshot.Tasks));
                m_ConfirmDuplicateEditorInstall = false;
            }
            ui.SameLine();
            if (HubSecondaryButton(ui, m_Tokens, "Not now", {88.0F, 38.0F}))
            {
                m_PendingEditorInstall.reset();
                m_LastEditorInstallRequest.reset();
                m_EditorComponentSearch.clear();
                m_SelectedEditorComponents.clear();
                m_ConfirmDuplicateEditorInstall = false;
                ui.CloseCurrentPopup();
            }
            return;
        }
        auto request = BuildRequest(editor, m_EditorInstallDestination, m_SelectedEditorComponents);
        bool previewMatches = snapshot.EditorInstallPreview && snapshot.EditorInstallPreview->Request == request;
        DrawHubModalHeader(ui, m_Tokens, previewMatches ? "Review editor installation" : "Configure editor install",
                           "Choose a managed location and the compatible components you need.",
                           previewMatches ? "INSTALLS  •  REVIEW" : "INSTALLS  •  CONFIGURE");
        {
            [[maybe_unused]] const auto summaryBackground =
                ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, m_Tokens.Elevated);
            if (auto summary = ui.BeginChild("EditorInstallSummary", {0.0F, 84.0F}, true); summary)
            {
                ui.TextColored(m_Tokens.PrimaryText, editorName + " " + editor.Version);
                ui.TextColored(m_Tokens.SecondaryText, std::string(ChannelLabel(editor.Channel)) + "  •  " +
                                                           editor.Platform + " / " + editor.Architecture);
                ui.TextColored(m_Tokens.MutedText, "Download " + HumanBytes(editor.DownloadBytes) + "  •  Installed " +
                                                       HumanBytes(editor.InstalledBytes));
            }
        }
        ui.Spacing();
        ui.TextColored(m_Tokens.SecondaryText, "Install location");
        const float browseWidth = 92.0F;
        ui.SetNextItemWidth(std::max(ui.ContentAvailable().Width - browseWidth - 8.0F, 80.0F));
        (void)ui.InputText("##EditorInstallDestination", m_EditorInstallDestination);
        ui.SameLine();
        if (HubSecondaryButton(ui, m_Tokens, "Browse...", {browseWidth, 0.0F}))
        {
            command = {.Type = HubUiCommandType::BrowseEditorInstallLocation,
                       .Path = PathFromUtf8(m_EditorInstallDestination).parent_path()};
        }
        if (!editor.Components.empty())
        {
            ui.Spacing();
            ui.TextColored(m_Tokens.PrimaryText, "Compatible components");
            ui.SetNextItemWidth(ui.ContentAvailable().Width);
            (void)ui.InputTextWithHint("##EditorComponentSearch", "Filter components", m_EditorComponentSearch);
            constexpr std::size_t MaximumVisibleComponents = 24;
            constexpr std::size_t MaximumSelectedComponents = 64;
            std::size_t matchingComponents = 0;
            std::size_t visibleComponents = 0;
            for (const auto& component : editor.Components)
            {
                if (!ContainsInsensitive(component.DisplayName, m_EditorComponentSearch) &&
                    !ContainsInsensitive(component.PackageId, m_EditorComponentSearch))
                {
                    continue;
                }
                ++matchingComponents;
                if (visibleComponents >= MaximumVisibleComponents)
                    continue;
                ++visibleComponents;
                const auto selected = std::ranges::find_if(
                    m_SelectedEditorComponents, [&](const auto& item)
                    { return item.PackageId == component.PackageId && item.Version == component.Version; });
                bool enabled = component.Required || selected != m_SelectedEditorComponents.end();
                const bool selectionLimit = !enabled && m_SelectedEditorComponents.size() >= MaximumSelectedComponents;
                if (auto disabled = ui.BeginDisabled(component.Required || selectionLimit); disabled)
                {
                    if (ui.Checkbox(component.DisplayName + " " + component.Version + "##" + component.PackageId + '@' +
                                        component.Version,
                                    enabled) &&
                        !component.Required)
                    {
                        std::erase_if(m_SelectedEditorComponents,
                                      [&](const auto& item) { return item.PackageId == component.PackageId; });
                        if (enabled)
                            m_SelectedEditorComponents.push_back({component.PackageId, component.Version});
                    }
                }
                ui.SameLine();
                ui.TextColored(m_Tokens.MutedText, std::string(component.Required ? "Required  ·  " : "Optional  ·  ") +
                                                       HumanBytes(component.DownloadBytes));
            }
            if (matchingComponents > visibleComponents)
                ui.TextColored(m_Tokens.MutedText, "+" + std::to_string(matchingComponents - visibleComponents) +
                                                       " matching component(s); refine the filter to select them.");
            if (m_SelectedEditorComponents.size() >= MaximumSelectedComponents)
                ui.TextColored(m_Tokens.Warning, "The maximum of 64 optional components is selected.");
        }

        request = BuildRequest(editor, m_EditorInstallDestination, m_SelectedEditorComponents);
        previewMatches = snapshot.EditorInstallPreview && snapshot.EditorInstallPreview->Request == request;
        if (previewMatches)
        {
            const auto& preview = *snapshot.EditorInstallPreview;
            ui.Spacing();
            ui.TextColored(m_Tokens.PrimaryText, "Verified install plan");
            ui.TextColored(m_Tokens.Accent, HumanBytes(preview.DownloadBytes) + " download  •  " +
                                                HumanBytes(preview.RequiredDiskBytes) + " disk required");
            const auto shownSteps = std::min<std::size_t>(preview.Steps.size(), 12);
            for (std::size_t index = 0; index < shownSteps; ++index)
            {
                const auto& step = preview.Steps[index];
                auto label = step.DisplayName + " " + step.Version + "  ·  " + HumanBytes(step.DownloadBytes);
                if (!step.RequiredBy.empty())
                    label += "  ·  Required by " + RequiredByLabel(step.RequiredBy);
                ui.TextColored(m_Tokens.SecondaryText, label);
            }
            if (preview.Steps.size() > shownSteps)
                ui.TextColored(m_Tokens.MutedText,
                               "+" + std::to_string(preview.Steps.size() - shownSteps) + " more package(s)");
        }
        else if (m_LastEditorInstallRequest && *m_LastEditorInstallRequest == request &&
                 !snapshot.EditorInstallPreviewMessage.empty())
            ui.TextColoredWrapped(m_Tokens.Danger, snapshot.EditorInstallPreviewMessage);

        const auto destinationProblem = DestinationProblem(request.Destination);
        if (!destinationProblem.empty())
            ui.TextColoredWrapped(m_Tokens.Warning, destinationProblem);
        if (snapshot.EditorManagementBusy)
            ui.TextColored(m_Tokens.Warning, "Wait for the active editor installation check to finish.");
        if (auto disabled = ui.BeginDisabled(!destinationProblem.empty() || snapshot.EditorManagementBusy); disabled)
        {
            if (HubPrimaryButton(ui, m_Tokens, previewMatches ? "Install editor" : "Review install", {132.0F, 38.0F}))
            {
                m_LastEditorInstallRequest = request;
                command = {.Type = previewMatches ? HubUiCommandType::InstallEditor
                                                  : HubUiCommandType::PreviewEditorInstall,
                           .EditorInstall = std::move(request)};
                if (previewMatches)
                {
                    m_PendingEditorInstall.reset();
                    m_LastEditorInstallRequest.reset();
                    m_EditorComponentSearch.clear();
                    m_SelectedEditorComponents.clear();
                    m_ConfirmDuplicateEditorInstall = false;
                    ui.CloseCurrentPopup();
                }
            }
        }
        ui.SameLine();
        if (HubSecondaryButton(ui, m_Tokens, "Cancel", {88.0F, 38.0F}))
        {
            m_PendingEditorInstall.reset();
            m_LastEditorInstallRequest.reset();
            m_EditorComponentSearch.clear();
            m_SelectedEditorComponents.clear();
            m_ConfirmDuplicateEditorInstall = false;
            ui.CloseCurrentPopup();
        }
    }
} // namespace KeireHub
