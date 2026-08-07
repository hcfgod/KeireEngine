#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/PackageResolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
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
            Utf8Path(snapshot.Settings.DefaultEditorRoot / ("Kéire Editor " + candidate->Version));
        m_EditorComponentSearch.clear();
        m_SelectedEditorComponents.clear();
        m_LastEditorInstallRequest.reset();
        m_RequestEditorInstall = true;
        return true;
    }

    void HubProductUi::DrawAvailableEditors(Keire::UiFrame& ui, const HubProductSnapshot& snapshot)
    {
        if (snapshot.PopulatedEditorChannels.empty())
        {
            if (snapshot.EditorCatalogRefreshing)
                ui.TextColored(m_Tokens.SecondaryText, "Loading verified editor catalogs...");
            else if (!snapshot.EditorCatalogMessage.empty())
                ui.TextColoredWrapped(m_Tokens.Danger, snapshot.EditorCatalogMessage);
            else if (snapshot.Editors.empty())
                ui.TextColored(m_Tokens.MutedText, "No compatible editor versions are currently available.");
            return;
        }

        for (const auto& channel : snapshot.PopulatedEditorChannels)
        {
            ui.Spacing();
            ui.Separator();
            ui.TextColored(m_Tokens.PrimaryText, ChannelLabel(channel));
            if (!snapshot.AvailableEditors)
                continue;
            for (const auto& editor : *snapshot.AvailableEditors)
            {
                if (editor.Channel != channel)
                    continue;
                auto id = ui.PushId(editor.PackageId + "@" + editor.Version);
                if (auto card = ui.BeginChild("AvailableEditor", {0.0F, 142.0F}, true); card)
                {
                    ui.TextColored(m_Tokens.PrimaryText,
                                   editor.DisplayName.empty() ? "Kéire Editor " + editor.Version : editor.DisplayName);
                    ui.TextColored(m_Tokens.SecondaryText,
                                   editor.Version + "  ·  " + editor.Platform + " / " + editor.Architecture);
                    ui.TextColored(m_Tokens.MutedText, "Editor package " + HumanBytes(editor.DownloadBytes) +
                                                           "  ·  Installed " + HumanBytes(editor.InstalledBytes));
                    if (!editor.AvailabilityMessage.empty())
                        ui.TextColoredWrapped(m_Tokens.Danger, editor.AvailabilityMessage);
                    else if (!editor.InstalledInstallationIds.empty())
                        ui.TextColored(m_Tokens.Success, "Installed");
                    else if (auto disabled = ui.BeginDisabled(snapshot.EditorManagementBusy); disabled)
                    {
                        if (ui.Button("Install...", {96.0F, 30.0F}))
                            (void)RequestEditorInstall(editor.PackageId, snapshot);
                    }
                }
                ui.Spacing();
            }
        }
    }

    void HubProductUi::DrawEditorInstallDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot,
                                               HubUiCommand& command)
    {
        if (std::exchange(m_RequestEditorInstall, false))
            ui.OpenPopup("Install Editor");
        if (auto installation = ui.BeginPopupModal("Install Editor"); !installation)
            return;
        if (!m_PendingEditorInstall)
        {
            ui.TextColored(m_Tokens.Warning, "The selected editor version is no longer available.");
            if (ui.Button("Close", {76.0F, 32.0F}))
                ui.CloseCurrentPopup();
            return;
        }

        const auto& editor = *m_PendingEditorInstall;
        const auto editorName = editor.DisplayName.empty() ? std::string("Kéire Editor") : editor.DisplayName;
        ui.TextColored(m_Tokens.PrimaryText, "Install " + editorName + " " + editor.Version);
        ui.TextColored(m_Tokens.SecondaryText, editor.Platform + " / " + editor.Architecture);
        (void)ui.InputText("Destination", m_EditorInstallDestination);
        if (!editor.Components.empty())
        {
            ui.Spacing();
            ui.TextColored(m_Tokens.PrimaryText, "Compatible components");
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

        auto request = BuildRequest(editor, m_EditorInstallDestination, m_SelectedEditorComponents);
        const bool previewMatches = snapshot.EditorInstallPreview && snapshot.EditorInstallPreview->Request == request;
        if (previewMatches)
        {
            const auto& preview = *snapshot.EditorInstallPreview;
            ui.Spacing();
            ui.Separator();
            ui.TextColored(m_Tokens.PrimaryText, "Install plan");
            ui.Text("Download: " + HumanBytes(preview.DownloadBytes) +
                    "  ·  Disk required: " + HumanBytes(preview.RequiredDiskBytes));
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

        const bool destinationMissing = request.Destination.empty();
        if (snapshot.EditorManagementBusy)
            ui.TextColored(m_Tokens.Warning, "Wait for the active editor installation check to finish.");
        if (auto disabled = ui.BeginDisabled(destinationMissing || snapshot.EditorManagementBusy); disabled)
        {
            if (ui.Button(previewMatches ? "Install" : "Review install", {116.0F, 32.0F}))
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
                    ui.CloseCurrentPopup();
                }
            }
        }
        ui.SameLine();
        if (ui.Button("Cancel", {76.0F, 32.0F}))
        {
            m_PendingEditorInstall.reset();
            m_LastEditorInstallRequest.reset();
            m_EditorComponentSearch.clear();
            m_SelectedEditorComponents.clear();
            ui.CloseCurrentPopup();
        }
    }
} // namespace KeireHub
