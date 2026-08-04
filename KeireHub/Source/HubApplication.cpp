#include "Keire/Core.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"
#include "KeireProjectModules/SourceModulePack.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifndef KEIRE_EDITOR_TARGET
#define KEIRE_EDITOR_TARGET "KeireClient"
#endif

namespace
{
    constexpr std::array HubOptions{
        Keire::ApplicationCommandLineOption{"--smoke-ui", "Render several project hub frames and exit."},
    };

    [[nodiscard]] std::string Utf8Path(const std::filesystem::path& path)
    {
        const auto value = path.generic_u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

    [[nodiscard]] std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const char character)
                               { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
        return value;
    }

    [[nodiscard]] const char* StatusLabel(const Keire::ProjectStatus status) noexcept
    {
        switch (status)
        {
        case Keire::ProjectStatus::Ready:
            return "Ready";
        case Keire::ProjectStatus::UpgradeAvailable:
            return "Upgrade available";
        case Keire::ProjectStatus::RecoveryRequired:
            return "Recovery required";
        case Keire::ProjectStatus::Missing:
            return "Missing";
        case Keire::ProjectStatus::Invalid:
            return "Invalid";
        case Keire::ProjectStatus::RequiresNewerEngine:
            return "Requires newer engine";
        case Keire::ProjectStatus::InUse:
            return "Open in another editor";
        }
        return "Unknown";
    }

    [[nodiscard]] Keire::UiColor StatusColor(const Keire::ProjectStatus status) noexcept
    {
        switch (status)
        {
        case Keire::ProjectStatus::Ready:
            return {0.32F, 0.84F, 0.58F, 1.0F};
        case Keire::ProjectStatus::InUse:
        case Keire::ProjectStatus::UpgradeAvailable:
            return {0.96F, 0.72F, 0.28F, 1.0F};
        case Keire::ProjectStatus::RecoveryRequired:
            return {0.96F, 0.50F, 0.25F, 1.0F};
        case Keire::ProjectStatus::Missing:
        case Keire::ProjectStatus::Invalid:
        case Keire::ProjectStatus::RequiresNewerEngine:
            return {0.96F, 0.38F, 0.42F, 1.0F};
        }
        return {0.62F, 0.66F, 0.74F, 1.0F};
    }

    [[nodiscard]] bool CanOpenOrUpgrade(const Keire::ProjectStatus status) noexcept
    {
        return status == Keire::ProjectStatus::Ready || status == Keire::ProjectStatus::UpgradeAvailable ||
               status == Keire::ProjectStatus::RecoveryRequired;
    }

    [[nodiscard]] std::string FormatLastOpened(const std::int64_t seconds)
    {
        if (seconds <= 0)
            return "Never";
        const std::time_t time = static_cast<std::time_t>(seconds);
        std::tm local{};
#if defined(_WIN32)
        if (localtime_s(&local, &time) != 0)
            return "Unknown";
#else
        if (!localtime_r(&time, &local))
            return "Unknown";
#endif
        std::ostringstream stream;
        stream << std::put_time(&local, "%b %d, %Y");
        return stream.str();
    }

    class HubLayer final : public Keire::Layer
    {
      public:
        HubLayer(std::filesystem::path executable, const bool smoke)
            : Keire::Layer("ProjectHub"), m_Executable(std::move(executable)), m_Smoke(smoke),
              m_CreateLocation(Utf8Path(std::filesystem::current_path()))
        {
        }

      protected:
        void OnAttach() override
        {
            if (!m_Smoke)
            {
                try
                {
                    Keire::SystemTraySpecification tray;
                    tray.Tooltip = "Kéire Project Hub";
                    tray.Actions = {{"Show Hub", [this] { ShowHub(); }}, {"Quit", [this] { Owner().RequestExit(); }}};
                    m_Tray = Owner().Windows()->CreateSystemTray(std::move(tray));
                    if (!m_Tray->IsAvailable())
                        m_Notice = "System tray unavailable; the Hub will minimize after launching an editor: " +
                                   m_Tray->Diagnostic();
                }
                catch (const std::exception& error)
                {
                    SetError(std::string("System tray unavailable: ") + error.what());
                }
                Listen<Keire::WindowMinimizedEvent>(
                    [this](const auto& event)
                    {
                        if (event.Header.Window != Owner().MainWindow()->Id() || !TrayAvailable())
                            return Keire::EventFlow::Continue;
                        Owner().MainWindow()->SetVisible(false);
                        return Keire::EventFlow::Handled;
                    });
            }
            if (!m_Smoke)
            {
                try
                {
                    m_Registry = Keire::CreateRef<Keire::ProjectRegistry>();
                    LoadPreferences();
                }
                catch (const std::exception& error)
                {
                    SetError(std::string("Project registry unavailable: ") + error.what());
                }
            }
        }

        void OnDetach() noexcept override
        {
            if (m_Tray)
                m_Tray->Close();
            m_Tray.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            if (m_Smoke && ++m_Frames >= 8)
                Owner().RequestExit();
            if (!m_FolderDialog)
                return;
            const auto status = m_FolderDialog->Status();
            if (status == Keire::FolderDialogStatus::Pending)
                return;
            if (status == Keire::FolderDialogStatus::Selected)
            {
                if (m_FolderTarget == FolderTarget::CreateLocation)
                    m_CreateLocation = Utf8Path(m_FolderDialog->SelectedPath());
                else
                    m_OpenPath = Utf8Path(m_FolderDialog->SelectedPath());
            }
            else if (status == Keire::FolderDialogStatus::Failed)
            {
                SetError("Folder dialog failed: " + m_FolderDialog->Diagnostic());
            }
            m_FolderDialog.Reset();
            m_FolderTarget = FolderTarget::None;
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            const auto size = Owner().MainWindow()->LogicalSize();
            ui.SetNextWindowPosition({0.0F, 0.0F}, false);
            ui.SetNextWindowSize({static_cast<float>(size.Width), static_cast<float>(size.Height)}, false);
            Keire::UiWindowOptions options;
            options.NoTitleBar = true;
            options.NoResize = true;
            options.NoMove = true;
            options.NoCollapse = true;
            options.NoSavedSettings = true;
            if (auto window = ui.BeginWindow("Kéire Project Hub", nullptr, options); window)
            {
                if (auto sidebar = ui.BeginChild("HubSidebar", {264.0F, 0.0F}, true); sidebar)
                    DrawSidebar(ui);
                ui.SameLine();
                if (auto workspace = ui.BeginChild("HubWorkspace", {}, false); workspace)
                {
                    ui.TextColored({0.95F, 0.97F, 1.0F, 1.0F}, "Welcome back");
                    ui.TextColored({0.53F, 0.58F, 0.68F, 1.0F}, "Open a recent workspace or create your next project.");
                    ui.Spacing();
                    if (ui.Button("+  New Project", {142.0F, 40.0F}))
                        m_RequestCreatePopup = true;
                    ui.SameLine();
                    if (ui.Button("Open Folder", {126.0F, 40.0F}))
                        m_RequestOpenPopup = true;
                    ui.SameLine();
                    if (ui.Button("Refresh", {88.0F, 40.0F}) && m_Registry)
                        Refresh();
                    ui.Spacing();
                    (void)ui.InputTextWithHint("##HubProjectSearch", "Search projects", m_Search);
                    ui.SameLine();
                    if (ui.IconButton("HubListView", Keire::UiIcon::List, m_View == ProjectView::List, {32.0F, 28.0F}))
                    {
                        m_View = ProjectView::List;
                        SavePreferences();
                    }
                    ui.SameLine();
                    if (ui.IconButton("HubCardView", Keire::UiIcon::Grid, m_View == ProjectView::Cards, {32.0F, 28.0F}))
                    {
                        m_View = ProjectView::Cards;
                        SavePreferences();
                    }
                    ui.SameLine();
                    constexpr std::array sortLabels{std::string_view("Last Opened"), std::string_view("Name"),
                                                    std::string_view("Status")};
                    if (auto sort = ui.BeginCombo("Sort", sortLabels[static_cast<std::size_t>(m_Sort)]); sort)
                    {
                        for (std::size_t index = 0; index < sortLabels.size(); ++index)
                        {
                            if (ui.Selectable(sortLabels[index], static_cast<std::size_t>(m_Sort) == index))
                            {
                                m_Sort = static_cast<ProjectSort>(index);
                                SavePreferences();
                            }
                        }
                    }

                    if (!m_Notice.empty())
                    {
                        ui.Spacing();
                        ui.TextColored(m_NoticeError ? Keire::UiColor{0.96F, 0.32F, 0.36F, 1.0F}
                                                     : Keire::UiColor{0.27F, 0.78F, 0.50F, 1.0F},
                                       m_Notice);
                    }
                    ui.Spacing();
                    DrawProjects(ui);
                }
                if (std::exchange(m_RequestCreatePopup, false))
                    ui.OpenPopup("Create Project");
                if (std::exchange(m_RequestOpenPopup, false))
                    ui.OpenPopup("Open Project");
                if (std::exchange(m_RequestUpgradePopup, false))
                    ui.OpenPopup("Project Upgrade");
                DrawCreateDialog(ui);
                DrawOpenDialog(ui);
                DrawUpgradeDialog(ui);
            }
        }

      private:
        enum class FolderTarget : std::uint8_t
        {
            None,
            CreateLocation,
            OpenProject
        };

        enum class ProjectView : std::uint8_t
        {
            List,
            Cards
        };

        enum class ProjectSort : std::uint8_t
        {
            LastOpened,
            Name,
            Status
        };

        void LoadPreferences()
        {
            if (!m_Registry)
                return;
            m_PreferencesPath = m_Registry->Path().parent_path() / "HubUi.settings";
            if (!std::filesystem::is_regular_file(m_PreferencesPath))
                return;
            try
            {
                const auto settings = Keire::Detail::ReadTextFile(m_PreferencesPath, 4096);
                m_View = settings.find("view=cards") != std::string::npos ? ProjectView::Cards : ProjectView::List;
                if (settings.find("sort=name") != std::string::npos)
                    m_Sort = ProjectSort::Name;
                else if (settings.find("sort=status") != std::string::npos)
                    m_Sort = ProjectSort::Status;
                else
                    m_Sort = ProjectSort::LastOpened;
            }
            catch (const std::exception& error)
            {
                KEIRE_CLIENT_WARN("[Project Hub] Could not read UI preferences: {}", error.what());
            }
        }

        void SavePreferences() noexcept
        {
            if (m_PreferencesPath.empty())
                return;
            try
            {
                const std::string view = m_View == ProjectView::Cards ? "cards" : "list";
                const std::string sort = m_Sort == ProjectSort::Name     ? "name"
                                         : m_Sort == ProjectSort::Status ? "status"
                                                                         : "last-opened";
                Keire::Detail::WriteTextFileAtomically(m_PreferencesPath, "view=" + view + "\nsort=" + sort + "\n");
            }
            catch (const std::exception& error)
            {
                KEIRE_CLIENT_WARN("[Project Hub] Could not save UI preferences: {}", error.what());
            }
        }

        void DrawSidebar(Keire::UiFrame& ui)
        {
            ui.Spacing();
            ui.TextColored({0.37F, 0.66F, 1.0F, 1.0F}, "KÉIRE");
            ui.TextColored({0.55F, 0.61F, 0.72F, 1.0F}, "CREATE  •  BUILD  •  SHIP");
            ui.Spacing();
            ui.Separator();
            ui.Spacing();
            const auto width = std::max(ui.ContentAvailable().Width, 1.0F);
            (void)ui.Button("Projects", {width, 42.0F});
            if (ui.Button("New Project", {width, 42.0F}))
                m_RequestCreatePopup = true;
            if (ui.Button("Open Project", {width, 42.0F}))
                m_RequestOpenPopup = true;
            ui.Spacing();
            ui.Separator();
            ui.Spacing();
            ui.TextColored({0.58F, 0.63F, 0.72F, 1.0F}, "QUICK START");
            if (const auto sample = SampleProject())
                if (ui.Button("Kéire Sandbox", {width, 38.0F}))
                    Open(*sample);
            ui.Spacing();
            ui.TextColored({0.46F, 0.50F, 0.58F, 1.0F}, "Engine " + std::string(Keire::GetBuildInfo().Version));
        }

        [[nodiscard]] std::optional<std::filesystem::path> SampleProject() const
        {
            const std::array samples{std::filesystem::current_path() / "Samples" / "KeireSandbox",
                                     m_Executable.parent_path().parent_path() / "samples" / "KeireSandbox"};
            const auto sample = std::ranges::find_if(
                samples, [](const auto& path) { return Keire::Project::Inspect(path) == Keire::ProjectStatus::Ready; });
            return sample == samples.end() ? std::nullopt : std::optional<std::filesystem::path>(*sample);
        }

        [[nodiscard]] bool TrayAvailable() const noexcept { return m_Tray && m_Tray->IsAvailable(); }

        void SetError(std::string message) noexcept
        {
            try
            {
                m_Notice = message;
                m_NoticeError = true;
            }
            catch (...)
            {
            }
            try
            {
                KEIRE_CLIENT_ERROR("[Project Hub] {}", message);
            }
            catch (...)
            {
                std::fprintf(stderr, "[Project Hub] %s\n", message.c_str());
            }
        }

        void HideHub()
        {
            const auto window = Owner().MainWindow();
            if (TrayAvailable())
            {
                // Keep the cached minimized state so the hidden Hub uses the bounded background pump rate.
                window->Minimize();
                window->SetVisible(false);
            }
            else
            {
                window->Minimize();
            }
        }

        void ShowHub()
        {
            const auto window = Owner().MainWindow();
            window->SetVisible(true);
            window->Restore();
            window->Raise();
        }

        void Refresh()
        {
            try
            {
                m_Registry->Refresh();
                m_Notice = "Project list refreshed.";
                m_NoticeError = false;
            }
            catch (const std::exception& error)
            {
                SetError(error.what());
            }
        }

        [[nodiscard]] std::filesystem::path EditorExecutable() const
        {
            return Keire::Detail::ResolveCompanionExecutable(m_Executable, KEIRE_EDITOR_TARGET);
        }

        void Launch(const Keire::Ref<Keire::Project>& project)
        {
            if (!project)
                return;
            try
            {
                const std::array arguments{std::string("--project"), Utf8Path(project->Root())};
                std::string diagnostic;
                if (!Keire::Detail::LaunchDetachedProcess(EditorExecutable(), arguments, project->Root(), diagnostic))
                    throw std::runtime_error("Could not launch editor: " + diagnostic);
                m_Notice = "Opened " + project->Descriptor().Name + ".";
                m_NoticeError = false;
                HideHub();
                if (m_Registry)
                {
                    try
                    {
                        m_Registry->RecordOpened(*project);
                    }
                    catch (const std::exception& error)
                    {
                        KEIRE_CLIENT_WARN("[Project Hub] Editor launched, but the recent-project registry could not "
                                          "be updated: {}",
                                          error.what());
                    }
                }
            }
            catch (const std::exception& error)
            {
                SetError(error.what());
            }
        }

        void BrowseForFolder(const FolderTarget target, const std::filesystem::path& initialPath)
        {
            try
            {
                m_FolderDialog = Owner().Windows()->ShowFolderDialog(Owner().MainWindow()->Id(), initialPath);
                m_FolderTarget = target;
            }
            catch (const std::exception& error)
            {
                SetError(error.what());
            }
        }

        void Reveal(const std::filesystem::path& path)
        {
            std::string diagnostic;
            if (!Keire::Detail::RevealInFileManager(path, diagnostic))
                SetError("Could not reveal project: " + diagnostic);
        }

        void Open(const std::filesystem::path& path)
        {
            try
            {
                if (Keire::Project::IsLocked(path))
                    throw std::runtime_error("Project is already open in another editor.");
                const auto status = Keire::Project::Inspect(path);
                if (status == Keire::ProjectStatus::UpgradeAvailable ||
                    status == Keire::ProjectStatus::RecoveryRequired)
                {
                    m_UpgradeService = std::make_unique<Keire::ProjectUpgradeService>(
                        path, Owner().Modules() ? Owner().Modules()->ProjectUpgrades()
                                                : std::vector<Keire::ProjectUpgradeStep>{});
                    m_UpgradeInterrupted =
                        m_UpgradeService->State() == Keire::ProjectUpgradeTransactionState::Interrupted;
                    m_UpgradePlan = m_UpgradeInterrupted ? std::nullopt : std::optional(m_UpgradeService->Plan());
                    m_RequestUpgradePopup = true;
                    return;
                }
                Launch(Keire::Project::Open(path));
            }
            catch (const std::exception& error)
            {
                SetError(error.what());
            }
        }

        void DrawProjects(Keire::UiFrame& ui)
        {
            ui.TextColored({0.82F, 0.85F, 0.91F, 1.0F}, "RECENT PROJECTS");
            if (!m_Registry)
            {
                ui.Text("No project registry is available.");
                return;
            }
            const auto search = Lower(m_Search);
            const auto entries = m_Registry->Entries();
            std::vector<const Keire::RecentProject*> visible;
            visible.reserve(entries.size());
            for (const auto& entry : entries)
            {
                if (!search.empty() && Lower(entry.Name).find(search) == std::string::npos &&
                    Lower(Utf8Path(entry.Root)).find(search) == std::string::npos)
                    continue;
                visible.push_back(&entry);
            }
            std::ranges::stable_sort(visible,
                                     [this](const auto* left, const auto* right)
                                     {
                                         if (left->Pinned != right->Pinned)
                                             return left->Pinned > right->Pinned;
                                         if (m_Sort == ProjectSort::Name)
                                             return Lower(left->Name) < Lower(right->Name);
                                         if (m_Sort == ProjectSort::Status)
                                             return std::tie(left->Status, left->Name) <
                                                    std::tie(right->Status, right->Name);
                                         return left->LastOpenedUnixSeconds > right->LastOpenedUnixSeconds;
                                     });
            if (visible.empty())
            {
                ui.Spacing();
                ui.TextColored({0.61F, 0.65F, 0.72F, 1.0F},
                               entries.empty() ? "No recent projects yet. Create or add one to get started."
                                               : "No recent projects match this search.");
                return;
            }

            ui.Spacing();
            if (m_View == ProjectView::List)
            {
                Keire::UiTableOptions options;
                options.Sizing = Keire::UiTableSizing::Proportional;
                options.Borders = true;
                options.Resizable = true;
                options.RowBackground = true;
                if (auto table = ui.BeginTable("RecentProjectList", 4, options); table)
                {
                    ui.TableNextRow();
                    (void)ui.TableNextColumn();
                    ui.TextColored({0.64F, 0.68F, 0.76F, 1.0F}, "PROJECT");
                    (void)ui.TableNextColumn();
                    ui.TextColored({0.64F, 0.68F, 0.76F, 1.0F}, "STATUS");
                    (void)ui.TableNextColumn();
                    ui.TextColored({0.64F, 0.68F, 0.76F, 1.0F}, "LAST OPENED");
                    (void)ui.TableNextColumn();
                    ui.TextColored({0.64F, 0.68F, 0.76F, 1.0F}, "PATH");
                    for (const auto* value : visible)
                    {
                        const auto& entry = *value;
                        ui.TableNextRow();
                        (void)ui.TableNextColumn();
                        auto id = ui.PushId(entry.Id.ToString());
                        const bool selected = m_SelectedProject == entry.Id;
                        if (ui.Selectable((entry.Pinned ? "*  " : "") + entry.Name, selected))
                            m_SelectedProject = entry.Id;
                        const auto state = ui.LastItemState();
                        if (state.DoubleClicked && CanOpenOrUpgrade(entry.Status))
                            Open(entry.Root);
                        if (auto context = ui.BeginItemContextMenu("ProjectActions"); context)
                        {
                            if (ui.MenuItem(entry.Status == Keire::ProjectStatus::Ready ? "Open" : "Review upgrade",
                                            false, CanOpenOrUpgrade(entry.Status)))
                                Open(entry.Root);
                            if (ui.MenuItem("Reveal"))
                                Reveal(entry.Root);
                            if (ui.MenuItem(entry.Pinned ? "Unpin" : "Pin"))
                                (void)m_Registry->SetPinned(entry.Id, !entry.Pinned);
                            if (ui.MenuItem("Remove"))
                                (void)m_Registry->Remove(entry.Id);
                        }
                        (void)ui.TableNextColumn();
                        ui.TextColored(StatusColor(entry.Status), StatusLabel(entry.Status));
                        (void)ui.TableNextColumn();
                        ui.Text(FormatLastOpened(entry.LastOpenedUnixSeconds));
                        (void)ui.TableNextColumn();
                        ui.TextColored({0.52F, 0.57F, 0.66F, 1.0F}, Utf8Path(entry.Root));
                    }
                }
                if (!visible.empty() &&
                    (ui.Shortcut({.Key = Keire::UiKey::Down}) || ui.Shortcut({.Key = Keire::UiKey::Up})))
                {
                    const auto selected = std::ranges::find_if(visible, [this](const auto* entry)
                                                               { return entry->Id == m_SelectedProject; });
                    const auto current = selected == visible.end()
                                             ? std::size_t{0}
                                             : static_cast<std::size_t>(selected - visible.begin());
                    const bool moveUp = ui.KeyDown(Keire::UiKey::Up);
                    const std::size_t next =
                        moveUp ? (current == 0 ? visible.size() - 1 : current - 1) : (current + 1) % visible.size();
                    m_SelectedProject = visible[next]->Id;
                }
                if (m_SelectedProject && ui.Shortcut({.Key = Keire::UiKey::Enter}))
                {
                    const auto selected = std::ranges::find_if(visible, [this](const auto* entry)
                                                               { return entry->Id == m_SelectedProject; });
                    if (selected != visible.end() && CanOpenOrUpgrade((*selected)->Status))
                        Open((*selected)->Root);
                }
                return;
            }
            const float contentWidth = ui.ContentAvailable().Width;
            const std::size_t columns = contentWidth >= 1120.0F ? 3 : contentWidth >= 700.0F ? 2 : 1;
            Keire::UiTableOptions tableOptions;
            tableOptions.Sizing = Keire::UiTableSizing::Equal;
            tableOptions.Borders = false;
            tableOptions.Resizable = false;
            tableOptions.RowBackground = false;
            tableOptions.PersistSettings = false;
            if (auto grid = ui.BeginTable("RecentProjectCards", columns, tableOptions); grid)
                for (std::size_t index = 0; index < visible.size(); ++index)
                {
                    const auto& entry = *visible[index];
                    if (index % columns == 0)
                        ui.TableNextRow();
                    (void)ui.TableNextColumn();
                    auto id = ui.PushId(entry.Id.ToString());
                    if (auto card = ui.BeginChild("ProjectCard", {0.0F, 150.0F}, true); card)
                    {
                        const float cardWidth = std::max(ui.ContentAvailable().Width, 1.0F);
                        const bool openCard = ui.InvisibleButton("OpenProjectCard", {cardWidth, 54.0F});
                        const auto header = ui.LastItemRect();
                        ui.DrawFilledRectangle(header, {0.115F, 0.145F, 0.205F, 1.0F}, 6.0F);
                        const Keire::UiItemRect badge{{header.Minimum.X + 9.0F, header.Minimum.Y + 8.0F},
                                                      {header.Minimum.X + 47.0F, header.Maximum.Y - 8.0F}};
                        ui.DrawFilledRectangle(badge, {0.24F, 0.48F, 0.88F, 1.0F}, 7.0F);
                        const std::string initial = entry.Name.empty() ? "K" : entry.Name.substr(0, 1);
                        ui.DrawOverlayText({badge.Minimum.X + 14.0F, badge.Minimum.Y + 9.0F},
                                           {0.97F, 0.98F, 1.0F, 1.0F}, initial);
                        ui.DrawOverlayText({header.Minimum.X + 58.0F, header.Minimum.Y + 9.0F},
                                           {0.92F, 0.95F, 1.0F, 1.0F}, entry.Name);
                        ui.DrawFilledCircle({header.Minimum.X + 63.0F, header.Minimum.Y + 37.0F}, 3.0F,
                                            StatusColor(entry.Status));
                        ui.DrawOverlayText({header.Minimum.X + 72.0F, header.Minimum.Y + 30.0F},
                                           {0.61F, 0.66F, 0.75F, 1.0F}, StatusLabel(entry.Status));
                        if (entry.Pinned)
                            ui.DrawOverlayText({header.Maximum.X - 54.0F, header.Minimum.Y + 9.0F},
                                               {0.98F, 0.75F, 0.30F, 1.0F}, "PINNED");
                        if (openCard && CanOpenOrUpgrade(entry.Status))
                            Open(entry.Root);
                        ui.TextColored({0.52F, 0.57F, 0.66F, 1.0F}, Utf8Path(entry.Root));
                        ui.Spacing();
                        if (auto disabled = ui.BeginDisabled(!CanOpenOrUpgrade(entry.Status)); disabled)
                            if (ui.Button(entry.Status == Keire::ProjectStatus::Ready ? "Open" : "Upgrade",
                                          {74.0F, 30.0F}))
                                Open(entry.Root);
                        ui.SameLine();
                        if (ui.Button("Reveal", {74.0F, 30.0F}))
                            Reveal(entry.Root);
                        ui.SameLine();
                        if (ui.Button(entry.Pinned ? "Unpin" : "Pin", {64.0F, 30.0F}))
                        {
                            try
                            {
                                (void)m_Registry->SetPinned(entry.Id, !entry.Pinned);
                            }
                            catch (const std::exception& error)
                            {
                                SetError(error.what());
                            }
                        }
                        ui.SameLine();
                        if (ui.Button("Remove", {74.0F, 30.0F}))
                        {
                            try
                            {
                                (void)m_Registry->Remove(entry.Id);
                            }
                            catch (const std::exception& error)
                            {
                                SetError(error.what());
                            }
                        }
                    }
                }
        }

        void DrawCreateDialog(Keire::UiFrame& ui)
        {
            ui.SetNextWindowSize({760.0F, 430.0F}, false);
            if (auto dialog = ui.BeginPopupModal("Create Project"); dialog)
            {
                Keire::UiTableOptions layoutOptions;
                layoutOptions.Borders = false;
                layoutOptions.Resizable = false;
                if (auto layout = ui.BeginTable("CreateProjectLayout", 2, layoutOptions); layout)
                {
                    ui.TableNextRow();
                    (void)ui.TableNextColumn();
                    ui.TextColored({0.58F, 0.68F, 0.88F, 1.0F}, "TEMPLATES");
                    if (ui.Selectable("Starter\nCamera, light, sample scene", m_Starter))
                        m_Starter = true;
                    if (ui.Selectable("Empty\nMinimal project structure", !m_Starter))
                        m_Starter = false;
                    (void)ui.TableNextColumn();
                    ui.TextColored({0.58F, 0.68F, 0.88F, 1.0F}, "PROJECT DETAILS");
                    (void)ui.InputTextWithHint("Name", "My Project", m_CreateName);
                    (void)ui.InputTextWithHint("Location", "Parent folder", m_CreateLocation);
                    ui.SameLine();
                    if (auto disabled = ui.BeginDisabled(static_cast<bool>(m_FolderDialog)); disabled)
                        if (ui.Button("Browse..."))
                            BrowseForFolder(FolderTarget::CreateLocation, m_CreateLocation);
                    const auto destination = std::filesystem::path(m_CreateLocation) / m_CreateName;
                    ui.TextColored({0.55F, 0.60F, 0.68F, 1.0F}, "Destination: " + Utf8Path(destination));
                    const bool validName =
                        !m_CreateName.empty() && m_CreateName.find_first_of("<>:\"/\\|?*") == std::string::npos;
                    const bool conflict = validName && std::filesystem::exists(destination);
                    if (!validName)
                        ui.TextColored({0.96F, 0.38F, 0.42F, 1.0F}, "Enter a valid project name.");
                    else if (conflict)
                        ui.TextColored({0.96F, 0.72F, 0.28F, 1.0F}, "The destination already exists.");
                    else
                        ui.TextColored({0.32F, 0.84F, 0.58F, 1.0F}, "Ready to create.");
                    if (auto disabled = ui.BeginDisabled(!validName || conflict || m_CreateLocation.empty()); disabled)
                        if (ui.Button("Create Project", {132.0F, 34.0F}))
                        {
                            try
                            {
                                const auto project = Keire::Project::Create(
                                    {m_CreateLocation, m_CreateName,
                                     m_Starter ? Keire::ProjectTemplate::Starter : Keire::ProjectTemplate::Empty});
                                ui.CloseCurrentPopup();
                                Launch(project);
                            }
                            catch (const std::exception& error)
                            {
                                SetError(error.what());
                            }
                        }
                }
                ui.SameLine();
                if (ui.Button("Cancel"))
                    ui.CloseCurrentPopup();
            }
        }

        void DrawOpenDialog(Keire::UiFrame& ui)
        {
            if (auto dialog = ui.BeginPopupModal("Open Project"); dialog)
            {
                (void)ui.InputText("Project Folder", m_OpenPath);
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(static_cast<bool>(m_FolderDialog)); disabled)
                {
                    if (ui.Button("Browse..."))
                        BrowseForFolder(FolderTarget::OpenProject, m_OpenPath);
                }
                if (ui.Button("Open"))
                {
                    ui.CloseCurrentPopup();
                    Open(m_OpenPath);
                }
                ui.SameLine();
                if (ui.Button("Cancel"))
                    ui.CloseCurrentPopup();
            }
        }

        void DrawUpgradeDialog(Keire::UiFrame& ui)
        {
            ui.SetNextWindowSize({680.0F, 440.0F}, false);
            if (auto dialog = ui.BeginPopupModal("Project Upgrade"); dialog)
            {
                if (!m_UpgradeService)
                {
                    ui.Text("No project upgrade is pending.");
                }
                else if (m_UpgradeInterrupted)
                {
                    ui.TextColored({0.96F, 0.50F, 0.25F, 1.0F}, "An interrupted project upgrade was detected.");
                    ui.Text("Recover continues publication from the durable journal. Rollback restores before-images.");
                    if (ui.Button("Recover", {112.0F, 34.0F}))
                    {
                        try
                        {
                            const auto root = m_UpgradeService->Root();
                            m_UpgradeService->Recover();
                            m_UpgradeService.reset();
                            m_UpgradeInterrupted = false;
                            ui.CloseCurrentPopup();
                            Refresh();
                            Open(root);
                        }
                        catch (const std::exception& error)
                        {
                            SetError(error.what());
                        }
                    }
                    ui.SameLine();
                    if (ui.Button("Rollback", {112.0F, 34.0F}))
                    {
                        try
                        {
                            m_UpgradeService->Rollback();
                            m_UpgradeService.reset();
                            m_UpgradeInterrupted = false;
                            ui.CloseCurrentPopup();
                            Refresh();
                        }
                        catch (const std::exception& error)
                        {
                            SetError(error.what());
                        }
                    }
                }
                else if (m_UpgradePlan)
                {
                    const auto& plan = *m_UpgradePlan;
                    ui.TextColored({0.96F, 0.72F, 0.28F, 1.0F}, "Project schema " + std::to_string(plan.CurrentSchema) +
                                                                    " -> " + std::to_string(plan.TargetSchema));
                    ui.Text("Backup estimate: " + std::to_string(plan.EstimatedBackupBytes) + " bytes");
                    ui.Separator();
                    for (const auto& step : plan.Steps)
                    {
                        ui.Text(step.Id);
                        for (const auto& path : step.AffectedPaths)
                            ui.TextColored({0.55F, 0.60F, 0.68F, 1.0F}, "  " + Utf8Path(path));
                        if (!step.Warning.empty())
                            ui.TextColored({0.96F, 0.72F, 0.28F, 1.0F}, step.Warning);
                    }
                    ui.Separator();
                    ui.Text("The project is locked while staged files are validated and atomically published.");
                    if (ui.Button("Apply Upgrade", {136.0F, 34.0F}))
                    {
                        try
                        {
                            const auto root = plan.ProjectRoot;
                            m_UpgradeService->Apply(plan);
                            m_UpgradeService.reset();
                            m_UpgradePlan.reset();
                            ui.CloseCurrentPopup();
                            Refresh();
                            Open(root);
                        }
                        catch (const std::exception& error)
                        {
                            SetError(error.what());
                        }
                    }
                }
                ui.SameLine();
                if (ui.Button("Cancel"))
                {
                    m_UpgradeService.reset();
                    m_UpgradePlan.reset();
                    m_UpgradeInterrupted = false;
                    ui.CloseCurrentPopup();
                }
            }
        }

        std::filesystem::path m_Executable;
        std::filesystem::path m_PreferencesPath;
        Keire::Ref<Keire::ProjectRegistry> m_Registry;
        Keire::Ref<Keire::SystemTray> m_Tray;
        Keire::Ref<Keire::FolderDialogOperation> m_FolderDialog;
        std::unique_ptr<Keire::ProjectUpgradeService> m_UpgradeService;
        std::optional<Keire::ProjectUpgradePlan> m_UpgradePlan;
        FolderTarget m_FolderTarget = FolderTarget::None;
        std::string m_Search;
        std::string m_CreateName = "NewProject";
        std::string m_CreateLocation;
        std::string m_OpenPath;
        std::string m_Notice;
        std::uint32_t m_Frames = 0;
        Keire::ProjectId m_SelectedProject;
        ProjectView m_View = ProjectView::List;
        ProjectSort m_Sort = ProjectSort::LastOpened;
        bool m_Starter = true;
        bool m_NoticeError = false;
        bool m_RequestCreatePopup = false;
        bool m_RequestOpenPopup = false;
        bool m_RequestUpgradePopup = false;
        bool m_UpgradeInterrupted = false;
        bool m_Smoke = false;
    };

    class HubApplication final : public Keire::Application
    {
      public:
        HubApplication(Keire::ApplicationSpecification specification, std::filesystem::path executable,
                       const bool smoke)
            : Keire::Application(std::move(specification)), m_Executable(std::move(executable)), m_Smoke(smoke)
        {
        }

      protected:
        void OnInitialize() override { (void)PushOverlay(std::make_unique<HubLayer>(m_Executable, m_Smoke)); }

      private:
        std::filesystem::path m_Executable;
        bool m_Smoke = false;
    };
} // namespace

namespace Keire
{
    ApplicationCommandLineDescription GetApplicationCommandLineDescription() noexcept
    {
        return {"[--smoke-ui]", HubOptions};
    }

    std::unique_ptr<Application> CreateApplication(const ApplicationCommandLineArguments& arguments)
    {
        bool smoke = false;
        for (std::size_t index = 1; index < arguments.Size(); ++index)
        {
            if (arguments[index] == "--smoke-ui")
                smoke = true;
            else
                throw CommandLineError("Unknown project hub option: " + std::string(arguments[index]));
        }
        ApplicationSpecification specification;
        specification.Modules.Modules = KeireProjectModules::CreateSourceModules();
        specification.MainWindow.Title = "Kéire Project Hub";
        specification.MainWindow.Width = 1120;
        specification.MainWindow.Height = 720;
        specification.Ui.Mode = UiMode::Rendered;
        specification.Ui.EnableDocking = false;
        specification.TargetFrameRate = smoke ? 240 : 30;
        return std::make_unique<HubApplication>(std::move(specification), Detail::PathFromUtf8(arguments.Executable()),
                                                smoke);
    }
} // namespace Keire
#include <sstream>
