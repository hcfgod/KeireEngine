#include "Keire/Core.h"

#include "KeireInternal/Process.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <memory>
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
                    m_Registry = Keire::CreateRef<Keire::ProjectRegistry>();
                }
                catch (const std::exception& error)
                {
                    m_Notice = std::string("Project registry unavailable: ") + error.what();
                }
            }
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
                m_Notice = "Folder dialog failed: " + m_FolderDialog->Diagnostic();
                m_NoticeError = true;
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
                ui.TextColored({0.30F, 0.55F, 1.0F, 1.0F}, "KÉIRE PROJECT HUB");
                ui.TextColored({0.61F, 0.65F, 0.72F, 1.0F}, "Create and open isolated engine projects.");
                ui.Separator();
                if (ui.Button("New Project"))
                    ui.OpenPopup("Create Project");
                ui.SameLine();
                if (ui.Button("Open Existing"))
                    ui.OpenPopup("Open Project");
                ui.SameLine();
                if (ui.Button("Refresh") && m_Registry)
                    Refresh();
                ui.SameLine();
                (void)ui.InputText("Search", m_Search);

                if (!m_Notice.empty())
                {
                    ui.Spacing();
                    ui.TextColored(m_NoticeError ? Keire::UiColor{0.96F, 0.32F, 0.36F, 1.0F}
                                                 : Keire::UiColor{0.27F, 0.78F, 0.50F, 1.0F},
                                   m_Notice);
                }
                ui.Spacing();
                DrawProjects(ui);
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
                m_Notice = error.what();
                m_NoticeError = true;
            }
        }

        [[nodiscard]] std::filesystem::path EditorExecutable() const
        {
            const auto hub = std::filesystem::absolute(m_Executable);
#if defined(_WIN32)
            constexpr std::string_view extension = ".exe";
#else
            constexpr std::string_view extension;
#endif
            const auto name = std::string(KEIRE_EDITOR_TARGET) + std::string(extension);
            const std::array candidates{hub.parent_path() / name,
                                        hub.parent_path().parent_path() / KEIRE_EDITOR_TARGET / name};
            const auto found = std::ranges::find_if(candidates, [](const auto& path)
                                                    { return std::filesystem::is_regular_file(path); });
            return found == candidates.end() ? candidates.front() : *found;
        }

        void Launch(const Keire::Ref<Keire::Project>& project)
        {
            if (!project)
                return;
            try
            {
                if (m_Registry)
                    m_Registry->RecordOpened(*project);
                const std::array arguments{std::string("--project"), Utf8Path(project->Root())};
                std::string diagnostic;
                if (!Keire::Detail::LaunchDetachedProcess(EditorExecutable(), arguments, project->Root(), diagnostic))
                    throw std::runtime_error("Could not launch editor: " + diagnostic);
                m_Notice = "Opened " + project->Descriptor().Name + ".";
                m_NoticeError = false;
            }
            catch (const std::exception& error)
            {
                m_Notice = error.what();
                m_NoticeError = true;
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
                m_Notice = error.what();
                m_NoticeError = true;
            }
        }

        void Reveal(const std::filesystem::path& path)
        {
            std::string diagnostic;
            if (!Keire::Detail::RevealInFileManager(path, diagnostic))
            {
                m_Notice = "Could not reveal project: " + diagnostic;
                m_NoticeError = true;
            }
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
                m_Notice = error.what();
                m_NoticeError = true;
            }
        }

        void DrawProjects(Keire::UiFrame& ui)
        {
            ui.TextColored({0.91F, 0.92F, 0.95F, 1.0F}, "RECENT PROJECTS");
            if (!m_Registry)
            {
                ui.Text("No project registry is available.");
                return;
            }
            const auto search = Lower(m_Search);
            const auto entries = m_Registry->Entries();
            bool visible = false;
            for (const auto& entry : entries)
            {
                if (!search.empty() && Lower(entry.Name).find(search) == std::string::npos &&
                    Lower(Utf8Path(entry.Root)).find(search) == std::string::npos)
                    continue;
                visible = true;
                auto id = ui.PushId(entry.Id.ToString());
                if (auto card = ui.BeginChild("ProjectCard", {0.0F, 92.0F}, true); card)
                {
                    ui.TextColored({0.30F, 0.55F, 1.0F, 1.0F}, entry.Name);
                    ui.TextColored({0.61F, 0.65F, 0.72F, 1.0F}, Utf8Path(entry.Root));
                    ui.Text(StatusLabel(entry.Status));
                    ui.SameLine();
                    if (ui.Button(entry.Pinned ? "Unpin" : "Pin"))
                    {
                        try
                        {
                            (void)m_Registry->SetPinned(entry.Id, !entry.Pinned);
                        }
                        catch (const std::exception& error)
                        {
                            m_Notice = error.what();
                            m_NoticeError = true;
                        }
                    }
                    ui.SameLine();
                    if (ui.Button("Remove"))
                    {
                        try
                        {
                            (void)m_Registry->Remove(entry.Id);
                        }
                        catch (const std::exception& error)
                        {
                            m_Notice = error.what();
                            m_NoticeError = true;
                        }
                    }
                    ui.SameLine();
                    if (ui.Button("Reveal"))
                        Reveal(entry.Root);
                    ui.SameLine();
                    if (auto disabled = ui.BeginDisabled(entry.Status != Keire::ProjectStatus::Ready); disabled)
                    {
                        if (ui.Button("Open"))
                            Open(entry.Root);
                    }
                }
                ui.Spacing();
            }
            if (!visible)
                ui.TextColored({0.61F, 0.65F, 0.72F, 1.0F}, "No recent projects match this search.");

            const std::array samples{std::filesystem::current_path() / "Samples" / "KeireSandbox",
                                     m_Executable.parent_path().parent_path() / "samples" / "KeireSandbox"};
            const auto sample = std::ranges::find_if(
                samples, [](const auto& path) { return Keire::Project::Inspect(path) == Keire::ProjectStatus::Ready; });
            if (sample != samples.end())
            {
                ui.Separator();
                ui.TextColored({0.91F, 0.92F, 0.95F, 1.0F}, "SAMPLES");
                if (ui.Button("Open Kéire Sandbox"))
                    Open(*sample);
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
                        m_Notice = error.what();
                        m_NoticeError = true;
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
        specification.TargetFrameRate = smoke ? 240 : 0;
        return std::make_unique<HubApplication>(std::move(specification), std::string(arguments.Executable()), smoke);
    }
} // namespace Keire
