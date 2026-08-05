#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/PlayerBuildService.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"

#include "KeireInternal/Build/PlayerSupport.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

void EditorWorkspaceLayer::BindPlayerBuildCommands()
{
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::BuildPlayer, [this] { RequestPlayerBuild(false); },
        [this] { return CanBuildPlayer(false); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::BuildAndRunPlayer, [this] { RequestPlayerBuild(true); },
        [this] { return CanBuildPlayer(true); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::CancelPlayerBuild,
        [this]
        {
            if (m_PlayerBuildService)
                m_PlayerBuildService->Cancel();
        },
        [this] { return m_PlayerBuildService && m_PlayerBuildService->Busy(); });
    m_CommandRouter->Bind(
        KeireEditor::EditorCommand::RevealPlayerBuild,
        [this]
        {
            try
            {
                RevealPlayerBuild();
            }
            catch (const std::exception& error)
            {
                ReportError("Player Build", error.what());
            }
        },
        [this] { return CanRevealPlayerBuild(); });
}

void EditorWorkspaceLayer::InitializePlayerBuild()
{
    const auto project = Owner().GetProject();
    if (!project)
        return;
    try
    {
        m_PlayerSettings = Keire::LoadPlayerSettings(project->Root(), project->Descriptor());
        m_PlayerBuildProfiles = Keire::LoadPlayerBuildProfiles(project->Root());
        m_PlayerBuildSettingsLoaded = true;
    }
    catch (const std::exception& error)
    {
        ReportError("Player Build", std::string("Could not load player build settings: ") + error.what());
    }
    try
    {
        m_PlayerBuildService = std::make_unique<KeireEditor::PlayerBuildService>(m_ExecutablePath, project->Root());
    }
    catch (const std::exception& error)
    {
        ReportError("Player Build", error.what());
    }
}

void EditorWorkspaceLayer::ShutdownPlayerBuild() noexcept
{
    if (m_PlayerBuildService)
        m_PlayerBuildService->Shutdown();
}

void EditorWorkspaceLayer::SavePlayerBuildConfiguration()
{
    const auto project = Owner().GetProject();
    if (!project || !m_PlayerBuildSettingsLoaded)
        throw std::logic_error("Player build settings are not available.");
    Keire::SavePlayerSettings(project->Root(), m_PlayerSettings);
    Keire::SavePlayerBuildProfiles(project->Root(), m_PlayerBuildProfiles);
}

bool EditorWorkspaceLayer::CanBuildPlayer(const bool runAfterBuild) const noexcept
{
    if (!m_PlayerBuildSettingsLoaded || !m_PlayerBuildService || m_PlayerBuildService->Busy())
        return false;
    try
    {
        const auto& profile = Keire::FindPlayerBuildProfile(m_PlayerBuildProfiles, m_PlayerBuildProfiles.ActiveProfile);
        return !runAfterBuild || (profile.Platform == Keire::HostPlayerPlatform() &&
                                  profile.Architecture == Keire::HostPlayerArchitecture());
    }
    catch (...)
    {
        return false;
    }
}

bool EditorWorkspaceLayer::CanRevealPlayerBuild() const noexcept
{
    if (!m_PlayerBuildSettingsLoaded)
        return false;
    try
    {
        const auto project = Owner().GetProject();
        const auto& profile = Keire::FindPlayerBuildProfile(m_PlayerBuildProfiles, m_PlayerBuildProfiles.ActiveProfile);
        return project && std::filesystem::is_directory(project->Root() / "Build" / profile.OutputSlug);
    }
    catch (...)
    {
        return false;
    }
}

void EditorWorkspaceLayer::RevealPlayerBuild()
{
    const auto project = Owner().GetProject();
    if (!project)
        return;
    const auto& profile = Keire::FindPlayerBuildProfile(m_PlayerBuildProfiles, m_PlayerBuildProfiles.ActiveProfile);
    std::string diagnostic;
    if (!Keire::Detail::RevealInFileManager(project->Root() / "Build" / profile.OutputSlug, diagnostic))
        throw std::runtime_error("Could not reveal the player build: " + diagnostic);
}

void EditorWorkspaceLayer::RequestPlayerBuild(const bool runAfterBuild)
{
    if (!CanBuildPlayer(runAfterBuild))
        return;
    m_PendingPlayerBuildRun = runAfterBuild;
    if ((m_SceneDocument && m_SceneDocument->Dirty()) ||
        (m_ProjectSettingsDocument && m_ProjectSettingsDocument->Dirty()))
    {
        OpenDialog(Dialog::DirtyPlayerBuild);
        return;
    }
    StartPlayerBuild(runAfterBuild);
}

void EditorWorkspaceLayer::StartPlayerBuild(const bool runAfterBuild)
{
    if (!m_PlayerBuildService)
        throw std::logic_error("Player build service is unavailable.");
    const auto& profile = Keire::FindPlayerBuildProfile(m_PlayerBuildProfiles, m_PlayerBuildProfiles.ActiveProfile);
    try
    {
        const auto modules = Owner().Modules();
        (void)Keire::Detail::ResolvePlayerSupport(m_ExecutablePath, profile.Platform, profile.Architecture,
                                                  profile.Configuration,
                                                  modules ? modules->Fingerprint() : std::string{});
    }
    catch (const std::exception& error)
    {
        OpenBuildSupportHub(profile);
        throw std::runtime_error(std::string(error.what()) + " The Hub Build Support page was opened.");
    }
    SavePlayerBuildConfiguration();
    m_PlayerBuildService->Start(m_PlayerBuildProfiles.ActiveProfile, runAfterBuild);
    m_PlayerBuildReported = false;
    m_PendingPlayerBuildRun = false;
    AddConsoleMessage("Player Build", "Standalone player build started.", m_Theme.Accent);
}

void EditorWorkspaceLayer::OpenBuildSupportHub(const Keire::PlayerBuildProfile& profile)
{
    const auto hub = Keire::Detail::ResolveCompanionExecutable(m_ExecutablePath, "KeireHub");
    const std::vector<std::string> arguments{"--build-support", std::string(Keire::ToString(profile.Platform)),
                                             std::string(Keire::ToString(profile.Architecture))};
    std::string diagnostic;
    if (!Keire::Detail::LaunchDetachedProcess(hub, arguments, hub.parent_path(), diagnostic))
        throw std::runtime_error("Could not open the Hub Build Support page: " + diagnostic);
}

void EditorWorkspaceLayer::UpdatePlayerBuild()
{
    if (!m_PlayerBuildService)
        return;
    m_PlayerBuildService->Update();
    const auto& status = m_PlayerBuildService->Status();
    if (m_PlayerBuildService->Busy() || m_PlayerBuildReported || status.State == KeireEditor::PlayerBuildState::Idle)
        return;
    m_PlayerBuildReported = true;
    if (status.State == KeireEditor::PlayerBuildState::Succeeded)
    {
        AddConsoleMessage("Player Build", "Built " + Keire::Detail::PathToUtf8(status.Output), m_Theme.Success);
    }
    else if (status.State == KeireEditor::PlayerBuildState::Cancelled)
    {
        AddConsoleMessage("Player Build", "Standalone player build cancelled.", m_Theme.Warning, Keire::LogLevel::Warn);
    }
    else
    {
        ReportError("Player Build", status.Message.empty() ? "Standalone player build failed." : status.Message);
    }
}
