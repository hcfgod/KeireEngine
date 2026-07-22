#include "Keire/Core.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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
            return {0.96F, 0.72F, 0.28F, 1.0F};
        case Keire::ProjectStatus::Missing:
        case Keire::ProjectStatus::Invalid:
        case Keire::ProjectStatus::RequiresNewerEngine:
            return {0.96F, 0.38F, 0.42F, 1.0F};
        }
        return {0.62F, 0.66F, 0.74F, 1.0F};
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
                if (auto sidebar = ui.BeginChild("HubSidebar", {244.0F, 0.0F}, true); sidebar)
                    DrawSidebar(ui);
                ui.SameLine();
                if (auto workspace = ui.BeginChild("HubWorkspace", {}, false); workspace)
                {
                    ui.TextColored({0.92F, 0.94F, 0.98F, 1.0F}, "PROJECTS");
                    ui.TextColored({0.56F, 0.61F, 0.70F, 1.0F}, "Continue a recent project or start something new.");
                    ui.Spacing();
                    if (ui.Button("New Project", {126.0F, 36.0F}))
                        m_RequestCreatePopup = true;
                    ui.SameLine();
                    if (ui.Button("Open Existing", {126.0F, 36.0F}))
                        m_RequestOpenPopup = true;
                    ui.SameLine();
                    if (ui.Button("Refresh", {86.0F, 36.0F}) && m_Registry)
                        Refresh();
                    ui.Spacing();
                    (void)ui.InputText("Search Projects", m_Search);

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
                DrawCreateDialog(ui);
                DrawOpenDialog(ui);
            }
        }

      private:
        enum class FolderTarget : std::uint8_t
        {
            None,
            CreateLocation,
            OpenProject
        };

        void DrawSidebar(Keire::UiFrame& ui)
        {
            ui.Spacing();
            ui.TextColored({0.34F, 0.61F, 1.0F, 1.0F}, "KÉIRE");
            ui.TextColored({0.58F, 0.63F, 0.72F, 1.0F}, "PROJECT HUB");
            ui.Spacing();
            ui.Separator();
            ui.Spacing();
            const auto width = std::max(ui.ContentAvailable().Width, 1.0F);
            (void)ui.Button("Projects", {width, 40.0F});
            if (ui.Button("Create New", {width, 40.0F}))
                m_RequestCreatePopup = true;
            if (ui.Button("Add Existing", {width, 40.0F}))
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
            if (visible.empty())
            {
                ui.Spacing();
                ui.TextColored({0.61F, 0.65F, 0.72F, 1.0F},
                               entries.empty() ? "No recent projects yet. Create or add one to get started."
                                               : "No recent projects match this search.");
                return;
            }

            ui.Spacing();
            const std::size_t columns = ui.ContentAvailable().Width >= 760.0F ? 2 : 1;
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
                    if (auto card = ui.BeginChild("ProjectCard", {0.0F, 126.0F}, true); card)
                    {
                        ui.TextColored({0.88F, 0.91F, 0.97F, 1.0F},
                                       entry.Pinned ? "PINNED  |  " + entry.Name : entry.Name);
                        ui.TextColored(StatusColor(entry.Status), StatusLabel(entry.Status));
                        ui.TextColored({0.55F, 0.59F, 0.67F, 1.0F}, Utf8Path(entry.Root));
                        ui.Spacing();
                        if (auto disabled = ui.BeginDisabled(entry.Status != Keire::ProjectStatus::Ready); disabled)
                            if (ui.Button("Open", {74.0F, 30.0F}))
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
            if (auto dialog = ui.BeginPopupModal("Create Project"); dialog)
            {
                (void)ui.InputText("Name", m_CreateName);
                (void)ui.InputText("Location", m_CreateLocation);
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(static_cast<bool>(m_FolderDialog)); disabled)
                {
                    if (ui.Button("Browse..."))
                        BrowseForFolder(FolderTarget::CreateLocation, m_CreateLocation);
                }
                if (auto combo = ui.BeginCombo("Template", m_Starter ? "Starter" : "Empty"); combo)
                {
                    if (ui.Selectable("Starter", m_Starter))
                        m_Starter = true;
                    if (ui.Selectable("Empty", !m_Starter))
                        m_Starter = false;
                }
                if (ui.Button("Create"))
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

        std::filesystem::path m_Executable;
        Keire::Ref<Keire::ProjectRegistry> m_Registry;
        Keire::Ref<Keire::SystemTray> m_Tray;
        Keire::Ref<Keire::FolderDialogOperation> m_FolderDialog;
        FolderTarget m_FolderTarget = FolderTarget::None;
        std::string m_Search;
        std::string m_CreateName = "NewProject";
        std::string m_CreateLocation;
        std::string m_OpenPath;
        std::string m_Notice;
        std::uint32_t m_Frames = 0;
        bool m_Starter = true;
        bool m_NoticeError = false;
        bool m_RequestCreatePopup = false;
        bool m_RequestOpenPopup = false;
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
