#include "KeireHub/HubProjectsUi.h"

#include "KeireHub/HubProjectUiSupport.h"

#include "KeireHubRuntime/EditorSelection.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iterator>
#include <memory>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <tuple>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::filesystem::path PathFromUtf8(const std::string_view value)
        {
            const auto* begin = reinterpret_cast<const char8_t*>(value.data());
            return std::filesystem::path(std::u8string(begin, begin + value.size()));
        }

        [[nodiscard]] bool CanMutateProject(const Keire::ProjectStatus status) noexcept
        {
            return status == Keire::ProjectStatus::Ready;
        }

        [[nodiscard]] EditorInstallation SelectionInstallation(const HubEditorUiRecord& editor)
        {
            auto relative = editor.Entrypoint.lexically_relative(editor.Root);
            std::vector<std::filesystem::path> entrypoints;
            if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                entrypoints.push_back(std::move(relative));
            return {.Id = editor.Id,
                    .Version = editor.Version,
                    .Root = editor.Root,
                    .Entrypoints = std::move(entrypoints),
                    .MinimumProjectSchema = editor.MinimumProjectSchema,
                    .MaximumProjectSchema = editor.MaximumProjectSchema,
                    .Health = editor.Healthy ? InstallationHealth::Healthy : InstallationHealth::Damaged};
        }

        [[nodiscard]] EditorSelectionRequest SelectionRequest(const std::uint32_t schema,
                                                              const std::string_view lastSaved,
                                                              const std::string_view minimum,
                                                              std::string preferred = {})
        {
            return {.PreferredInstallationId = std::move(preferred),
                    .LastSavedVersion = std::string(lastSaved),
                    .MinimumVersion = std::string(minimum),
                    .ProjectSchema = schema == 0 ? Keire::CurrentProjectSchemaVersion : schema};
        }

        [[nodiscard]] std::vector<EditorInstallation>
        SelectionInstallations(const std::vector<HubEditorUiRecord>& editors)
        {
            std::vector<EditorInstallation> result;
            result.reserve(editors.size());
            std::ranges::transform(editors, std::back_inserter(result), &SelectionInstallation);
            return result;
        }

        [[nodiscard]] bool HasInstalledEditor(const Keire::RecentProject& project,
                                              const std::vector<HubEditorUiRecord>& editors)
        {
            const auto installations = SelectionInstallations(editors);
            return static_cast<bool>(SelectCompatibleEditor(
                installations, SelectionRequest(project.ProjectSchemaVersion, project.LastSavedWithEngineVersion,
                                                project.MinimumEngineVersion, project.PreferredEditorInstallation)));
        }

        [[nodiscard]] std::string HumanBytes(const std::optional<std::uint64_t> bytes)
        {
            if (!bytes)
                return "Scanning";
            constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB"};
            double value = static_cast<double>(*bytes);
            std::size_t unit = 0;
            while (value >= 1024.0 && unit + 1 < units.size())
            {
                value /= 1024.0;
                ++unit;
            }
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << ' ' << units[unit];
            return stream.str();
        }

        [[nodiscard]] std::string ProjectMonogram(const std::string_view name)
        {
            if (name.empty())
                return "K";
            const auto first = static_cast<unsigned char>(name.front());
            const std::size_t bytes = first < 0x80U ? 1 : first < 0xe0U ? 2 : first < 0xf0U ? 3 : 4;
            return std::string(name.substr(0, std::min(bytes, name.size())));
        }
    } // namespace

    void HubProjectsUi::SetAppearance(const HubAppearance appearance, const bool systemPrefersDark) noexcept
    {
        m_Tokens = HubDesignTokens::For(appearance, systemPrefersDark);
    }

    void HubProjectsUi::SetPreferences(const HubProjectsView view, const HubProjectsSort sort) noexcept
    {
        m_View = view;
        m_Sort = sort;
    }

    void HubProjectsUi::SetDuplicateParent(const std::filesystem::path& parent)
    {
        m_DuplicateParent = Utf8Path(parent);
    }

    void HubProjectsUi::SetThumbnails(std::shared_ptr<const std::vector<ProjectThumbnail>> thumbnails) noexcept
    {
        if (m_Thumbnails == thumbnails)
            return;
        m_Thumbnails = std::move(thumbnails);
        std::erase_if(m_ThumbnailTextures,
                      [this](const auto& texture)
                      {
                          if (!m_Thumbnails)
                              return true;
                          const auto found =
                              std::ranges::find(*m_Thumbnails, texture.ProjectId, &ProjectThumbnail::ProjectId);
                          return found == m_Thumbnails->end() || found->Image.RgbaPixels != texture.Pixels;
                      });
    }

    Keire::Ref<Keire::UiImage> HubProjectsUi::ResolveThumbnail(Keire::UiFrame& ui, const std::string_view projectId)
    {
        constexpr std::size_t MaximumTextures = 64;
        if (!m_Thumbnails)
            return {};
        const auto source = std::ranges::find(*m_Thumbnails, projectId, &ProjectThumbnail::ProjectId);
        if (source == m_Thumbnails->end() || !source->Image.IsValid())
            return {};

        const auto cached = std::ranges::find(m_ThumbnailTextures, projectId, &ThumbnailTexture::ProjectId);
        if (cached != m_ThumbnailTextures.end())
        {
            auto texture = std::move(*cached);
            m_ThumbnailTextures.erase(cached);
            m_ThumbnailTextures.insert(m_ThumbnailTextures.begin(), std::move(texture));
            return m_ThumbnailTextures.front().Image;
        }

        Keire::Ref<Keire::UiImage> image;
        try
        {
            image = ui.CreateImage(source->Image.Width, source->Image.Height,
                                   std::span<const std::byte>(*source->Image.RgbaPixels));
        }
        catch (...)
        {
            // A failed GPU upload is represented by the normal monogram fallback and is not retried every frame.
        }
        if (m_ThumbnailTextures.size() == MaximumTextures)
            m_ThumbnailTextures.pop_back();
        m_ThumbnailTextures.insert(
            m_ThumbnailTextures.begin(),
            {.ProjectId = source->ProjectId, .Pixels = source->Image.RgbaPixels, .Image = image});
        return image;
    }

    void HubProjectsUi::SelectPending(const Keire::RecentProject& project)
    {
        m_Pending = HubProjectUiPendingSelection{.Id = project.Id.ToString(),
                                                 .Root = project.Root,
                                                 .Name = project.Name,
                                                 .Pinned = project.Pinned,
                                                 .Status = project.Status,
                                                 .SchemaVersion = project.ProjectSchemaVersion == 0
                                                                      ? Keire::CurrentProjectSchemaVersion
                                                                      : project.ProjectSchemaVersion,
                                                 .LastSavedVersion = project.LastSavedWithEngineVersion,
                                                 .MinimumVersion = project.MinimumEngineVersion};
    }

    void HubProjectsUi::DrawActionMenu(Keire::UiFrame& ui, const Keire::RecentProject& project,
                                       const bool confirmRemoval, HubProjectUiCommand& command)
    {
        if (ui.MenuItem(project.Status == Keire::ProjectStatus::Ready ? "Open" : "Review upgrade", false,
                        CanOpenOrUpgrade(project.Status)))
        {
            command = {.Type = HubProjectUiCommandType::Open, .ProjectId = project.Id.ToString(), .Path = project.Root};
        }
        if (ui.MenuItem("Open with another editor...", false, project.Status == Keire::ProjectStatus::Ready))
        {
            SelectPending(project);
            m_RequestOpenWithPopup = true;
        }
        if (ui.MenuItem("Find compatible editor...", false,
                        project.Status == Keire::ProjectStatus::RequiresNewerEngine ||
                            project.Status == Keire::ProjectStatus::UnsupportedSchema))
        {
            command = {.Type = HubProjectUiCommandType::FindCompatibleEditor,
                       .ProjectId = project.Id.ToString(),
                       .EditorVersion = project.LastSavedWithEngineVersion.empty() ? project.MinimumEngineVersion
                                                                                   : project.LastSavedWithEngineVersion,
                       .Path = project.Root};
        }
        if (ui.MenuItem("Reveal in file manager"))
            command = {.Type = HubProjectUiCommandType::Reveal, .Path = project.Root};
        if (ui.MenuItem("Copy path"))
            command = {.Type = HubProjectUiCommandType::CopyPath, .Path = project.Root};
        if (ui.MenuItem(project.Pinned ? "Unpin" : "Pin"))
            command = {.Type = HubProjectUiCommandType::SetPinned,
                       .ProjectId = project.Id.ToString(),
                       .Pinned = !project.Pinned};
        ui.Separator();
        if (ui.MenuItem("Duplicate...", false, CanMutateProject(project.Status)))
        {
            SelectPending(project);
            m_DuplicateName = project.Name + " Copy";
            m_DuplicateParent = Utf8Path(project.Root.parent_path());
            m_RequestDuplicatePopup = true;
        }
        if (ui.MenuItem("Rename display name...", false, CanMutateProject(project.Status)))
        {
            SelectPending(project);
            m_RenameName = project.Name;
            m_RequestRenamePopup = true;
        }
        if (ui.MenuItem("Locate moved project...", false, project.Status == Keire::ProjectStatus::Missing))
        {
            command = {.Type = HubProjectUiCommandType::BrowseLocateProject,
                       .ProjectId = project.Id.ToString(),
                       .Path = project.Root.parent_path()};
        }
        ui.Separator();
        if (ui.MenuItem("Remove from Hub..."))
        {
            if (confirmRemoval)
            {
                SelectPending(project);
                m_RequestRemovePopup = true;
            }
            else
            {
                command = {.Type = HubProjectUiCommandType::RemoveFromHub,
                           .ProjectId = project.Id.ToString(),
                           .Path = project.Root,
                           .DisplayName = project.Name};
            }
        }
    }

    void HubProjectsUi::DrawList(Keire::UiFrame& ui, const std::vector<const Keire::RecentProject*>& projects,
                                 const bool confirmRemoval, HubProjectUiCommand& command)
    {
        const bool wide = ui.ContentAvailable().Width >= 980.0F;
        const std::size_t columnCount = wide ? 8 : 5;
        Keire::UiTableOptions options;
        options.Sizing = Keire::UiTableSizing::Proportional;
        options.Borders = true;
        options.Resizable = true;
        options.RowBackground = true;
        if (auto table = ui.BeginTable("RecentProjectList", columnCount, options); table)
        {
            ui.TableNextRow();
            (void)ui.TableNextColumn();
            ui.TextColored(m_Tokens.SecondaryText, "PROJECT");
            (void)ui.TableNextColumn();
            ui.TextColored(m_Tokens.SecondaryText, "STATUS");
            (void)ui.TableNextColumn();
            ui.TextColored(m_Tokens.SecondaryText, "LAST OPENED");
            (void)ui.TableNextColumn();
            if (wide)
            {
                ui.TextColored(m_Tokens.SecondaryText, "CREATED");
                (void)ui.TableNextColumn();
                ui.TextColored(m_Tokens.SecondaryText, "MODIFIED");
                (void)ui.TableNextColumn();
            }
            ui.TextColored(m_Tokens.SecondaryText, "EDITOR");
            (void)ui.TableNextColumn();
            if (wide)
            {
                ui.TextColored(m_Tokens.SecondaryText, "SIZE");
                (void)ui.TableNextColumn();
            }
            ui.TextColored(m_Tokens.SecondaryText, "PATH");
            for (const auto* value : projects)
            {
                const auto& project = *value;
                ui.TableNextRow();
                (void)ui.TableNextColumn();
                auto id = ui.PushId(project.Id.ToString());
                const bool selected = m_SelectedProjectId == project.Id.ToString();
                if (ui.Selectable((project.Pinned ? "*  " : "") + project.Name, selected))
                    m_SelectedProjectId = project.Id.ToString();
                const auto state = ui.LastItemState();
                if (state.DoubleClicked && CanOpenOrUpgrade(project.Status))
                    command = {.Type = HubProjectUiCommandType::Open, .Path = project.Root};
                if (auto context = ui.BeginItemContextMenu("ProjectActions"); context)
                    DrawActionMenu(ui, project, confirmRemoval, command);
                (void)ui.TableNextColumn();
                ui.TextColored(StatusColor(project.Status), StatusLabel(project.Status));
                (void)ui.TableNextColumn();
                ui.Text(FormatLastOpened(project.LastOpenedUnixSeconds));
                (void)ui.TableNextColumn();
                if (wide)
                {
                    ui.Text(FormatLastOpened(project.CreatedUnixSeconds));
                    (void)ui.TableNextColumn();
                    ui.Text(FormatLastOpened(project.ModifiedUnixSeconds));
                    (void)ui.TableNextColumn();
                }
                ui.Text(project.LastSavedWithEngineVersion.empty() ? "Unknown" : project.LastSavedWithEngineVersion);
                (void)ui.TableNextColumn();
                if (wide)
                {
                    ui.Text(HumanBytes(project.SizeBytes));
                    (void)ui.TableNextColumn();
                }
                ui.TextColored(m_Tokens.MutedText, Utf8Path(project.Root));
            }
        }
        if (!projects.empty() && (ui.Shortcut({.Key = Keire::UiKey::Down}) || ui.Shortcut({.Key = Keire::UiKey::Up})))
        {
            const auto selected = std::ranges::find_if(projects, [this](const auto* project)
                                                       { return project->Id.ToString() == m_SelectedProjectId; });
            const auto current =
                selected == projects.end() ? std::size_t{0} : static_cast<std::size_t>(selected - projects.begin());
            const bool moveUp = ui.KeyDown(Keire::UiKey::Up);
            const std::size_t next =
                moveUp ? (current == 0 ? projects.size() - 1 : current - 1) : (current + 1) % projects.size();
            m_SelectedProjectId = projects[next]->Id.ToString();
        }
        if (!m_SelectedProjectId.empty() && ui.Shortcut({.Key = Keire::UiKey::Enter}))
        {
            const auto selected = std::ranges::find_if(projects, [this](const auto* project)
                                                       { return project->Id.ToString() == m_SelectedProjectId; });
            if (selected != projects.end() && CanOpenOrUpgrade((*selected)->Status))
                command = {.Type = HubProjectUiCommandType::Open, .Path = (*selected)->Root};
        }
    }

    void HubProjectsUi::DrawCards(Keire::UiFrame& ui, const std::vector<const Keire::RecentProject*>& projects,
                                  const bool confirmRemoval, HubProjectUiCommand& command)
    {
        const float contentWidth = ui.ContentAvailable().Width;
        const std::size_t columns = contentWidth >= 1120.0F ? 3 : contentWidth >= 700.0F ? 2 : 1;
        Keire::UiTableOptions options;
        options.Sizing = Keire::UiTableSizing::Equal;
        options.Borders = false;
        options.Resizable = false;
        options.RowBackground = false;
        options.PersistSettings = false;
        if (auto grid = ui.BeginTable("RecentProjectCards", columns, options); grid)
        {
            for (std::size_t index = 0; index < projects.size(); ++index)
            {
                const auto& project = *projects[index];
                if (index % columns == 0)
                    ui.TableNextRow();
                (void)ui.TableNextColumn();
                auto id = ui.PushId(project.Id.ToString());
                if (auto card = ui.BeginChild("ProjectCard", {0.0F, 150.0F}, true); card)
                {
                    const float cardWidth = std::max(ui.ContentAvailable().Width, 1.0F);
                    const auto thumbnail = ResolveThumbnail(ui, project.Id.ToString());
                    const bool openCard = ui.InvisibleButton("OpenProjectCard", {cardWidth, 54.0F});
                    const auto header = ui.LastItemRect();
                    if (auto context = ui.BeginItemContextMenu("ProjectCardContext"); context)
                        DrawActionMenu(ui, project, confirmRemoval, command);
                    ui.DrawFilledRectangle(header, m_Tokens.Elevated, 6.0F);
                    const Keire::UiItemRect preview{{header.Minimum.X + 9.0F, header.Minimum.Y + 7.0F},
                                                    {header.Minimum.X + 81.0F, header.Maximum.Y - 7.0F}};
                    if (thumbnail)
                    {
                        const auto restore = ui.CursorScreenPosition();
                        ui.SetCursorScreenPosition(preview.Minimum);
                        ui.Image(thumbnail,
                                 {preview.Maximum.X - preview.Minimum.X, preview.Maximum.Y - preview.Minimum.Y});
                        ui.SetCursorScreenPosition(restore);
                    }
                    else
                    {
                        ui.DrawFilledRectangle(preview, m_Tokens.Accent, 7.0F);
                        ui.DrawOverlayText({preview.Minimum.X + 29.0F, preview.Minimum.Y + 10.0F}, m_Tokens.PrimaryText,
                                           ProjectMonogram(project.Name));
                    }
                    ui.DrawOverlayText({header.Minimum.X + 92.0F, header.Minimum.Y + 9.0F}, m_Tokens.PrimaryText,
                                       project.Name);
                    ui.DrawFilledCircle({header.Minimum.X + 97.0F, header.Minimum.Y + 37.0F}, 3.0F,
                                        StatusColor(project.Status));
                    ui.DrawOverlayText({header.Minimum.X + 106.0F, header.Minimum.Y + 30.0F}, m_Tokens.SecondaryText,
                                       StatusLabel(project.Status));
                    if (project.Pinned)
                        ui.DrawOverlayText({header.Maximum.X - 54.0F, header.Minimum.Y + 9.0F}, m_Tokens.Warning,
                                           "PINNED");
                    if (openCard && CanOpenOrUpgrade(project.Status))
                        command = {.Type = HubProjectUiCommandType::Open, .Path = project.Root};
                    ui.TextColored(m_Tokens.MutedText, Utf8Path(project.Root));
                    ui.TextColored(m_Tokens.SecondaryText, (project.LastSavedWithEngineVersion.empty()
                                                                ? std::string("Editor unknown")
                                                                : "Editor " + project.LastSavedWithEngineVersion) +
                                                               "  •  " + HumanBytes(project.SizeBytes));
                    ui.Spacing();
                    const bool needsEditor = project.Status == Keire::ProjectStatus::RequiresNewerEngine ||
                                             project.Status == Keire::ProjectStatus::UnsupportedSchema;
                    if (auto disabled = ui.BeginDisabled(!CanOpenOrUpgrade(project.Status) && !needsEditor); disabled)
                    {
                        const auto label = needsEditor                                     ? "Find editor"
                                           : project.Status == Keire::ProjectStatus::Ready ? "Open"
                                                                                           : "Upgrade";
                        if (ui.Button(label, {needsEditor ? 88.0F : 64.0F, 30.0F}))
                        {
                            if (needsEditor)
                            {
                                command = {.Type = HubProjectUiCommandType::FindCompatibleEditor,
                                           .ProjectId = project.Id.ToString(),
                                           .EditorVersion = project.LastSavedWithEngineVersion.empty()
                                                                ? project.MinimumEngineVersion
                                                                : project.LastSavedWithEngineVersion,
                                           .Path = project.Root};
                            }
                            else
                                command = {.Type = HubProjectUiCommandType::Open, .Path = project.Root};
                        }
                    }
                    ui.SameLine();
                    if (ui.Button("Reveal", {66.0F, 30.0F}))
                        command = {.Type = HubProjectUiCommandType::Reveal, .Path = project.Root};
                    ui.SameLine();
                    if (ui.Button(project.Pinned ? "Unpin" : "Pin", {60.0F, 30.0F}))
                    {
                        command = {.Type = HubProjectUiCommandType::SetPinned,
                                   .ProjectId = project.Id.ToString(),
                                   .Pinned = !project.Pinned};
                    }
                    ui.SameLine();
                    if (ui.Button("Actions", {70.0F, 30.0F}))
                        ui.OpenPopup("ProjectCardActions");
                    if (auto popup = ui.BeginPopup("ProjectCardActions"); popup)
                        DrawActionMenu(ui, project, confirmRemoval, command);
                }
            }
        }
    }

    void HubProjectsUi::DrawDialogs(Keire::UiFrame& ui, const std::vector<HubEditorUiRecord>& editors,
                                    const bool folderDialogActive, HubProjectUiCommand& command)
    {
        if (std::exchange(m_RequestOpenWithPopup, false))
            ui.OpenPopup("Open with Editor");
        if (auto dialog = ui.BeginPopupModal("Open with Editor"); dialog)
        {
            ui.TextColored(m_Tokens.PrimaryText, "Choose an installed editor for this project.");
            ui.TextColoredWrapped(
                m_Tokens.SecondaryText,
                "Only healthy editors that satisfy the project schema, minimum version, and last-saved version are "
                "shown. Opening in a newer version can migrate project data.");
            bool found = false;
            if (m_Pending)
            {
                const auto pending = *m_Pending;
                const auto installations = SelectionInstallations(editors);
                const auto recommended = SelectCompatibleEditor(
                    installations,
                    SelectionRequest(pending.SchemaVersion, pending.LastSavedVersion, pending.MinimumVersion));
                for (const auto& editor : editors)
                {
                    const auto installation = SelectionInstallation(editor);
                    const std::array candidate{installation};
                    if (!SelectCompatibleEditor(candidate,
                                                SelectionRequest(pending.SchemaVersion, pending.LastSavedVersion,
                                                                 pending.MinimumVersion, editor.Id)))
                        continue;
                    found = true;
                    auto id = ui.PushId(editor.Id);
                    if (ui.Button(editor.Version + "  (" + editor.Channel + ")", {280.0F, 34.0F}))
                    {
                        command = TakeOpenWithEditorCommand(m_Pending, editor.Id);
                        ui.CloseCurrentPopup();
                    }
                    ui.SameLine();
                    std::string label = editor.Managed ? "Managed" : "External";
                    if (recommended && recommended.Value().Id == editor.Id)
                        label += "  |  Recommended";
                    if (!pending.LastSavedVersion.empty())
                    {
                        label += editor.Version == pending.LastSavedVersion ? "  |  Exact last-saved version"
                                                                            : "  |  Newer - migration risk";
                    }
                    ui.TextColored(m_Tokens.MutedText, label);
                }
            }
            if (!found)
            {
                ui.TextColored(m_Tokens.Warning, "No installed editor can safely open this project.");
                if (m_Pending && ui.Button("Find compatible editor", {164.0F, 32.0F}))
                {
                    command = {.Type = HubProjectUiCommandType::FindCompatibleEditor,
                               .ProjectId = m_Pending->Id,
                               .EditorVersion = m_Pending->LastSavedVersion.empty() ? m_Pending->MinimumVersion
                                                                                    : m_Pending->LastSavedVersion,
                               .Path = m_Pending->Root};
                    m_Pending.reset();
                    ui.CloseCurrentPopup();
                }
            }
            if (ui.Button("Cancel", {76.0F, 32.0F}))
            {
                m_Pending.reset();
                ui.CloseCurrentPopup();
            }
        }

        if (std::exchange(m_RequestDuplicatePopup, false))
            ui.OpenPopup("Duplicate Project");
        if (auto dialog = ui.BeginPopupModal("Duplicate Project"); dialog)
        {
            ui.TextColored(m_Tokens.PrimaryText, "Create a clean copy with a new project identity.");
            ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                  "Generated Library, Build, logs, and repository metadata are not copied. The source "
                                  "project is never modified.");
            (void)ui.InputText("Project name", m_DuplicateName);
            (void)ui.InputText("Parent folder", m_DuplicateParent);
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(folderDialogActive); disabled)
            {
                if (ui.Button("Browse..."))
                    command = {.Type = HubProjectUiCommandType::BrowseDuplicateLocation,
                               .Path = PathFromUtf8(m_DuplicateParent)};
            }
            const bool invalid =
                !m_Pending || m_DuplicateName.empty() || m_DuplicateParent.empty() || folderDialogActive;
            if (auto disabled = ui.BeginDisabled(invalid); disabled)
            {
                if (ui.Button("Duplicate", {96.0F, 32.0F}))
                {
                    command = {.Type = HubProjectUiCommandType::Duplicate,
                               .ProjectId = m_Pending->Id,
                               .Path = PathFromUtf8(m_DuplicateParent) / PathFromUtf8(m_DuplicateName),
                               .DisplayName = m_DuplicateName};
                    m_Pending.reset();
                    ui.CloseCurrentPopup();
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel", {76.0F, 32.0F}))
            {
                m_Pending.reset();
                ui.CloseCurrentPopup();
            }
        }

        if (std::exchange(m_RequestRenamePopup, false))
            ui.OpenPopup("Rename Project");
        if (auto dialog = ui.BeginPopupModal("Rename Project"); dialog)
        {
            ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                  "This changes the project display name only. The project folder is not renamed.");
            (void)ui.InputText("Display name", m_RenameName);
            if (auto disabled = ui.BeginDisabled(!m_Pending || m_RenameName.empty()); disabled)
            {
                if (ui.Button("Rename", {88.0F, 32.0F}))
                {
                    command = {.Type = HubProjectUiCommandType::Rename,
                               .ProjectId = m_Pending->Id,
                               .DisplayName = m_RenameName};
                    m_Pending.reset();
                    ui.CloseCurrentPopup();
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel", {76.0F, 32.0F}))
            {
                m_Pending.reset();
                ui.CloseCurrentPopup();
            }
        }

        if (std::exchange(m_RequestRemovePopup, false))
            ui.OpenPopup("Remove Project from Hub?");
        if (auto dialog = ui.BeginPopupModal("Remove Project from Hub?"); dialog)
        {
            const auto name = m_Pending ? m_Pending->Name : std::string("this project");
            ui.TextColored(m_Tokens.PrimaryText, "Remove " + name + " from the Hub?");
            ui.TextColoredWrapped(m_Tokens.SecondaryText,
                                  "Only the recent-project entry is removed. Project files remain untouched on disk.");
            if (auto disabled = ui.BeginDisabled(!m_Pending); disabled)
            {
                if (ui.Button("Remove from Hub", {132.0F, 32.0F}))
                {
                    command = {.Type = HubProjectUiCommandType::RemoveFromHub,
                               .ProjectId = m_Pending->Id,
                               .Path = m_Pending->Root,
                               .DisplayName = m_Pending->Name};
                    m_Pending.reset();
                    ui.CloseCurrentPopup();
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel", {76.0F, 32.0F}))
            {
                m_Pending.reset();
                ui.CloseCurrentPopup();
            }
        }
    }

    HubProjectUiCommand HubProjectsUi::Draw(Keire::UiFrame& ui, const std::vector<Keire::RecentProject>& entries,
                                            const std::vector<HubEditorUiRecord>& editors,
                                            const std::string_view notice, const bool noticeError,
                                            const bool confirmRemoval, const bool folderDialogActive)
    {
        HubProjectUiCommand command;
        ui.TextColored(m_Tokens.PrimaryText, "Projects");
        ui.TextColored(m_Tokens.SecondaryText, "Open, organize, and inspect local Kéire projects.");
        ui.Spacing();
        if (ui.Button("+  New Project", {142.0F, 40.0F}))
            command.Type = HubProjectUiCommandType::NewProject;
        ui.SameLine();
        if (ui.Button("Open Folder", {126.0F, 40.0F}))
            command.Type = HubProjectUiCommandType::AddProject;
        ui.SameLine();
        if (ui.Button("Refresh", {88.0F, 40.0F}))
            command.Type = HubProjectUiCommandType::Refresh;
        ui.Spacing();
        (void)ui.InputTextWithHint("##HubProjectSearch", "Search projects", m_Search);
        ui.SameLine();
        if (ui.IconButton("HubListView", Keire::UiIcon::List, m_View == HubProjectsView::List, {32.0F, 28.0F}))
        {
            m_View = HubProjectsView::List;
            command.Type = HubProjectUiCommandType::PreferencesChanged;
        }
        ui.SameLine();
        if (ui.IconButton("HubCardView", Keire::UiIcon::Grid, m_View == HubProjectsView::Cards, {32.0F, 28.0F}))
        {
            m_View = HubProjectsView::Cards;
            command.Type = HubProjectUiCommandType::PreferencesChanged;
        }
        ui.SameLine();
        constexpr std::array sortLabels{std::string_view("Last Opened"), std::string_view("Name"),
                                        std::string_view("Created"),     std::string_view("Modified"),
                                        std::string_view("Version"),     std::string_view("Status"),
                                        std::string_view("Size")};
        if (auto sort = ui.BeginCombo("Sort", sortLabels[static_cast<std::size_t>(m_Sort)]); sort)
        {
            for (std::size_t index = 0; index < sortLabels.size(); ++index)
            {
                if (ui.Selectable(sortLabels[index], static_cast<std::size_t>(m_Sort) == index))
                {
                    m_Sort = static_cast<HubProjectsSort>(index);
                    command.Type = HubProjectUiCommandType::PreferencesChanged;
                }
            }
        }
        constexpr std::array statusLabels{std::string_view("All statuses"),
                                          std::string_view("Ready"),
                                          std::string_view("Upgrade available"),
                                          std::string_view("Recovery required"),
                                          std::string_view("Missing"),
                                          std::string_view("Invalid"),
                                          std::string_view("Requires newer editor"),
                                          std::string_view("Open elsewhere"),
                                          std::string_view("Newer schema")};
        if (auto status = ui.BeginCombo("Status", statusLabels[m_StatusFilter]); status)
        {
            for (std::size_t index = 0; index < statusLabels.size(); ++index)
                if (ui.Selectable(statusLabels[index], m_StatusFilter == index))
                    m_StatusFilter = static_cast<std::uint8_t>(index);
        }
        ui.SameLine();
        constexpr std::array editorLabels{std::string_view("Any editor"), std::string_view("Editor installed"),
                                          std::string_view("Editor missing")};
        if (auto editor = ui.BeginCombo("Editor", editorLabels[m_EditorFilter]); editor)
        {
            for (std::size_t index = 0; index < editorLabels.size(); ++index)
                if (ui.Selectable(editorLabels[index], m_EditorFilter == index))
                    m_EditorFilter = static_cast<std::uint8_t>(index);
        }
        ui.SameLine();
        std::set<std::string, std::less<>> versions;
        for (const auto& entry : entries)
            if (!entry.LastSavedWithEngineVersion.empty())
                versions.insert(entry.LastSavedWithEngineVersion);
        if (auto version = ui.BeginCombo("Version", m_VersionFilter.empty() ? "All versions" : m_VersionFilter);
            version)
        {
            if (ui.Selectable("All versions", m_VersionFilter.empty()))
                m_VersionFilter.clear();
            for (const auto& candidate : versions)
                if (ui.Selectable(candidate, m_VersionFilter == candidate))
                    m_VersionFilter = candidate;
        }
        ui.SameLine();
        (void)ui.Checkbox("Favorites only", m_FavoritesOnly);
        if (!notice.empty())
        {
            ui.Spacing();
            ui.TextColored(noticeError ? m_Tokens.Danger : m_Tokens.Success, notice);
        }
        ui.Spacing();
        ui.TextColored(m_Tokens.PrimaryText, "RECENT PROJECTS");

        auto displayEntries = entries;
        for (auto& entry : displayEntries)
        {
            if (entry.Status == Keire::ProjectStatus::Ready ||
                entry.Status == Keire::ProjectStatus::RequiresNewerEngine ||
                entry.Status == Keire::ProjectStatus::UnsupportedSchema)
            {
                entry.Status = HasInstalledEditor(entry, editors) ? Keire::ProjectStatus::Ready
                                                                  : Keire::ProjectStatus::RequiresNewerEngine;
            }
        }

        const auto search = Lower(m_Search);
        std::vector<const Keire::RecentProject*> visible;
        visible.reserve(displayEntries.size());
        for (const auto& entry : displayEntries)
        {
            if (!search.empty() && Lower(entry.Name).find(search) == std::string::npos &&
                Lower(Utf8Path(entry.Root)).find(search) == std::string::npos)
                continue;
            if (m_FavoritesOnly && !entry.Pinned)
                continue;
            if (m_StatusFilter != 0 && static_cast<std::uint8_t>(entry.Status) + 1U != m_StatusFilter)
                continue;
            const bool editorInstalled = HasInstalledEditor(entry, editors);
            if ((m_EditorFilter == 1 && !editorInstalled) || (m_EditorFilter == 2 && editorInstalled))
                continue;
            if (!m_VersionFilter.empty() && entry.LastSavedWithEngineVersion != m_VersionFilter)
                continue;
            visible.push_back(&entry);
        }
        std::ranges::stable_sort(
            visible,
            [this](const auto* left, const auto* right)
            {
                if (left->Pinned != right->Pinned)
                    return left->Pinned > right->Pinned;
                if (m_Sort == HubProjectsSort::Name)
                    return Lower(left->Name) < Lower(right->Name);
                if (m_Sort == HubProjectsSort::Created)
                    return std::tie(left->CreatedUnixSeconds, left->Name) >
                           std::tie(right->CreatedUnixSeconds, right->Name);
                if (m_Sort == HubProjectsSort::Modified)
                    return std::tie(left->ModifiedUnixSeconds, left->Name) >
                           std::tie(right->ModifiedUnixSeconds, right->Name);
                if (m_Sort == HubProjectsSort::Version)
                    return std::tie(left->LastSavedWithEngineVersion, left->Name) <
                           std::tie(right->LastSavedWithEngineVersion, right->Name);
                if (m_Sort == HubProjectsSort::Status)
                    return std::tie(left->Status, left->Name) < std::tie(right->Status, right->Name);
                if (m_Sort == HubProjectsSort::Size)
                    return std::tie(left->SizeBytes, left->Name) > std::tie(right->SizeBytes, right->Name);
                return left->LastOpenedUnixSeconds > right->LastOpenedUnixSeconds;
            });
        if (visible.empty())
        {
            ui.Spacing();
            ui.TextColored(m_Tokens.SecondaryText, entries.empty()
                                                       ? "No recent projects yet. Create or add one to get started."
                                                       : "No recent projects match this search.");
        }
        else
        {
            ui.Spacing();
            if (m_View == HubProjectsView::List)
                DrawList(ui, visible, confirmRemoval, command);
            else
                DrawCards(ui, visible, confirmRemoval, command);
        }
        DrawDialogs(ui, editors, folderDialogActive, command);
        return command;
    }
} // namespace KeireHub
