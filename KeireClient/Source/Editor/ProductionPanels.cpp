#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/PlayerBuildService.h"
#include "KeireClient/Editor/PrefabAuthoring.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"

#include "KeireClient/Editor/ReplayPanelState.h"

#include "KeireInternal/Build/PlayerSupport.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>

namespace
{
    struct PlayerTargetChoice final
    {
        Keire::PlayerPlatform Platform;
        Keire::PlayerArchitecture Architecture;
        std::string_view Label;
    };

    constexpr std::array PlayerTargets{
        PlayerTargetChoice{Keire::PlayerPlatform::Windows, Keire::PlayerArchitecture::X86_64, "Windows x64"},
        PlayerTargetChoice{Keire::PlayerPlatform::Windows, Keire::PlayerArchitecture::Arm64, "Windows ARM64"},
        PlayerTargetChoice{Keire::PlayerPlatform::Linux, Keire::PlayerArchitecture::X86_64, "Linux x64"},
        PlayerTargetChoice{Keire::PlayerPlatform::Linux, Keire::PlayerArchitecture::Arm64, "Linux ARM64"},
        PlayerTargetChoice{Keire::PlayerPlatform::MacOS, Keire::PlayerArchitecture::X86_64, "macOS x64"},
        PlayerTargetChoice{Keire::PlayerPlatform::MacOS, Keire::PlayerArchitecture::Arm64, "macOS ARM64"}};

    [[nodiscard]] std::string Lowercase(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    template <typename Projection>
    [[nodiscard]] std::string UniqueProfileValue(const Keire::PlayerBuildProfiles& profiles, const std::string& base,
                                                 Projection projection)
    {
        for (std::uint32_t suffix = 1;; ++suffix)
        {
            const auto candidate = suffix == 1 ? base : base + ' ' + std::to_string(suffix);
            const auto lowered = Lowercase(candidate);
            if (std::ranges::none_of(profiles.Profiles, [&](const auto& profile)
                                     { return Lowercase(std::string(std::invoke(projection, profile))) == lowered; }))
                return candidate;
        }
    }

    [[nodiscard]] std::string JoinList(const std::vector<std::string>& values)
    {
        std::string result;
        for (const auto& value : values)
        {
            if (!result.empty())
                result += "; ";
            result += value;
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> SplitList(const std::string_view text)
    {
        std::vector<std::string> result;
        std::size_t begin = 0;
        while (begin <= text.size())
        {
            const auto end = text.find(';', begin);
            auto value =
                std::string(text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin));
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
                value.erase(value.begin());
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
                value.pop_back();
            if (!value.empty())
                result.push_back(std::move(value));
            if (end == std::string_view::npos)
                break;
            begin = end + 1;
        }
        return result;
    }

    [[nodiscard]] std::string ProfileCategoryName(const Keire::ProfileCategory category)
    {
        switch (category)
        {
        case Keire::ProfileCategory::Application:
            return "Application";
        case Keire::ProfileCategory::Assets:
            return "Assets";
        case Keire::ProfileCategory::Scripting:
            return "Scripting";
        case Keire::ProfileCategory::Physics:
            return "Physics";
        case Keire::ProfileCategory::Animation:
            return "Animation";
        case Keire::ProfileCategory::Audio:
            return "Audio";
        case Keire::ProfileCategory::Navigation:
            return "Navigation";
        case Keire::ProfileCategory::Rendering:
            return "Rendering";
        case Keire::ProfileCategory::User:
            return "User";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string ManagedBuildStateName(const Keire::ManagedBuildState state)
    {
        switch (state)
        {
        case Keire::ManagedBuildState::Idle:
            return "Idle";
        case Keire::ManagedBuildState::Generating:
            return "Generating";
        case Keire::ManagedBuildState::Compiling:
            return "Compiling";
        case Keire::ManagedBuildState::Publishing:
            return "Publishing";
        case Keire::ManagedBuildState::Succeeded:
            return "Succeeded";
        case Keire::ManagedBuildState::Failed:
            return "Failed";
        case Keire::ManagedBuildState::Cancelled:
            return "Cancelled";
        }
        return "Unknown";
    }

    [[nodiscard]] std::string FormatMicroseconds(const double microseconds)
    {
        std::ostringstream result;
        result << std::fixed << std::setprecision(microseconds >= 1000.0 ? 2 : 1);
        if (microseconds >= 1000.0)
            result << microseconds / 1000.0 << " ms";
        else
            result << microseconds << " us";
        return result.str();
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
            throw std::runtime_error("Could not open prefab source: " + path.string());
        const auto size = stream.tellg();
        if (size < 0 || size > static_cast<std::streamoff>(64U * 1024U * 1024U))
            throw std::runtime_error("Prefab source size is invalid.");
        std::vector<std::byte> result(static_cast<std::size_t>(size));
        stream.seekg(0);
        if (!result.empty() && !stream.read(reinterpret_cast<char*>(result.data()), size))
            throw std::runtime_error("Could not read prefab source: " + path.string());
        return result;
    }
} // namespace

void EditorWorkspaceLayer::DrawBuildSettings(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_BuildSettings); panel)
    {
        if (!m_PlayerBuildSettingsLoaded)
        {
            ui.TextColored(m_Theme.Error, "Player build settings could not be loaded.");
            ui.TextColored(m_Theme.MutedText, "Correct Player.keiresettings, BuildProfiles.keiresettings, and "
                                              "BuildScenes.keiresettings in ProjectSettings, then reopen the project.");
            return;
        }

        bool profileChanged = false;
        bool settingsChanged = false;
        bool scenesChanged = false;

        ui.TextColored(m_Theme.Accent, "SCENES IN BUILD");
        ui.TextColored(m_Theme.MutedText,
                       "The first enabled scene is the startup scene. Enabled scenes keep this order as their "
                       "runtime build index.");
        ui.TextColored(m_Theme.MutedText, "Drag scene rows to reorder them, or use the move controls below.");
        std::size_t enabledBuildIndex = 0;
        std::optional<std::pair<Keire::AssetId, Keire::AssetId>> requestedSceneReorder;
        for (auto& entry : m_PlayerBuildScenes.Scenes)
        {
            const auto record = std::ranges::find(m_AssetRecords, entry.Scene, &Keire::AssetSourceRecord::Id);
            const bool valid = record != m_AssetRecords.end() && record->Type == Keire::SceneAsset::StaticType();
            const auto indexLabel = entry.Enabled ? std::to_string(enabledBuildIndex++) : std::string("-");
            const auto sceneLabel = valid ? record->RelativePath.generic_string()
                                          : std::string("Missing scene: ").append(entry.Scene.ToString());
            auto id = ui.PushId(entry.Scene.ToString());
            if (ui.Checkbox("##BuildSceneEnabled", entry.Enabled))
                scenesChanged = true;
            ui.SameLine();
            const auto color = !valid ? m_Theme.Error : entry.Enabled ? m_Theme.Text : m_Theme.MutedText;
            auto textColor = ui.PushStyleColor(Keire::UiStyleColorRole::Text, color);
            auto displayLabel = indexLabel;
            displayLabel.append("  ").append(sceneLabel);
            if (ui.Selectable(displayLabel, m_SelectedPlayerBuildScene == entry.Scene))
                m_SelectedPlayerBuildScene = entry.Scene;
            const auto payloadText = entry.Scene.ToString();
            ui.SetDragPayload("KEIRE_BUILD_SCENE", std::as_bytes(std::span(payloadText.data(), payloadText.size())));
            std::vector<std::byte> payload;
            if (ui.AcceptDragPayload("KEIRE_BUILD_SCENE", payload))
            {
                try
                {
                    const std::string dragged(reinterpret_cast<const char*>(payload.data()), payload.size());
                    const auto source = Keire::AssetId::Parse(dragged);
                    if (source != entry.Scene)
                        requestedSceneReorder = std::pair{source, entry.Scene};
                }
                catch (...)
                {
                }
            }
        }
        if (requestedSceneReorder)
        {
            const auto source = std::ranges::find(m_PlayerBuildScenes.Scenes, requestedSceneReorder->first,
                                                  &Keire::PlayerBuildScene::Scene);
            const auto target = std::ranges::find(m_PlayerBuildScenes.Scenes, requestedSceneReorder->second,
                                                  &Keire::PlayerBuildScene::Scene);
            if (source != m_PlayerBuildScenes.Scenes.end() && target != m_PlayerBuildScenes.Scenes.end())
            {
                if (source < target)
                    std::rotate(source, source + 1, target + 1);
                else
                    std::rotate(target, source, source + 1);
                scenesChanged = true;
            }
        }
        if (m_PlayerBuildScenes.Scenes.empty())
            ui.TextColored(m_Theme.Warning, "No scenes are in this build. Add a scene before building the player.");

        const auto selectedBuildScene =
            std::ranges::find(m_PlayerBuildScenes.Scenes, m_SelectedPlayerBuildScene, &Keire::PlayerBuildScene::Scene);
        const auto selectedBuildSceneIndex =
            selectedBuildScene == m_PlayerBuildScenes.Scenes.end()
                ? m_PlayerBuildScenes.Scenes.size()
                : static_cast<std::size_t>(selectedBuildScene - m_PlayerBuildScenes.Scenes.begin());
        const auto openScene = m_SceneDocument ? m_SceneDocument->Asset() : Keire::AssetId{};
        const bool openSceneAlreadyAdded =
            openScene && std::ranges::find(m_PlayerBuildScenes.Scenes, openScene, &Keire::PlayerBuildScene::Scene) !=
                             m_PlayerBuildScenes.Scenes.end();
        if (auto disabled = ui.BeginDisabled(!openScene || openSceneAlreadyAdded); disabled)
        {
            if (ui.Button("Add Open Scene"))
            {
                m_PlayerBuildScenes.Scenes.push_back({openScene, true});
                m_SelectedPlayerBuildScene = openScene;
                scenesChanged = true;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(selectedBuildSceneIndex >= m_PlayerBuildScenes.Scenes.size()); disabled)
        {
            if (ui.Button("Remove"))
            {
                m_PlayerBuildScenes.Scenes.erase(m_PlayerBuildScenes.Scenes.begin() +
                                                 static_cast<std::ptrdiff_t>(selectedBuildSceneIndex));
                m_SelectedPlayerBuildScene = {};
                scenesChanged = true;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(selectedBuildSceneIndex == 0 ||
                                             selectedBuildSceneIndex >= m_PlayerBuildScenes.Scenes.size());
            disabled)
        {
            if (ui.Button("Move Up"))
            {
                std::swap(m_PlayerBuildScenes.Scenes[selectedBuildSceneIndex],
                          m_PlayerBuildScenes.Scenes[selectedBuildSceneIndex - 1]);
                scenesChanged = true;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(selectedBuildSceneIndex + 1 >= m_PlayerBuildScenes.Scenes.size());
            disabled)
        {
            if (ui.Button("Move Down"))
            {
                std::swap(m_PlayerBuildScenes.Scenes[selectedBuildSceneIndex],
                          m_PlayerBuildScenes.Scenes[selectedBuildSceneIndex + 1]);
                scenesChanged = true;
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(selectedBuildSceneIndex >= m_PlayerBuildScenes.Scenes.size()); disabled)
        {
            if (ui.Button("Set as Startup"))
            {
                auto selected = m_PlayerBuildScenes.Scenes[selectedBuildSceneIndex];
                selected.Enabled = true;
                m_PlayerBuildScenes.Scenes.erase(m_PlayerBuildScenes.Scenes.begin() +
                                                 static_cast<std::ptrdiff_t>(selectedBuildSceneIndex));
                m_PlayerBuildScenes.Scenes.insert(m_PlayerBuildScenes.Scenes.begin(), selected);
                scenesChanged = true;
            }
        }

        KeireEditor::AssetPickerOptions sceneOptions;
        sceneOptions.Label = "Scene Asset";
        sceneOptions.EmptyLabel = "Choose a scene to add";
        sceneOptions.ExpectedType = Keire::SceneAsset::StaticType();
        sceneOptions.AllowNone = true;
        (void)m_PlayerBuildScenePicker.Draw(ui, m_AssetRecords, m_PlayerBuildSceneCandidate, sceneOptions);
        const bool candidateAlreadyAdded =
            m_PlayerBuildSceneCandidate &&
            std::ranges::find(m_PlayerBuildScenes.Scenes, m_PlayerBuildSceneCandidate,
                              &Keire::PlayerBuildScene::Scene) != m_PlayerBuildScenes.Scenes.end();
        if (auto disabled = ui.BeginDisabled(!m_PlayerBuildSceneCandidate || candidateAlreadyAdded); disabled)
        {
            if (ui.Button("Add Selected Scene"))
            {
                m_PlayerBuildScenes.Scenes.push_back({m_PlayerBuildSceneCandidate, true});
                m_SelectedPlayerBuildScene = m_PlayerBuildSceneCandidate;
                m_PlayerBuildSceneCandidate = {};
                m_PlayerBuildScenePicker.Clear();
                scenesChanged = true;
            }
        }
        if (candidateAlreadyAdded)
            ui.TextColored(m_Theme.Warning, "That scene is already in the build.");
        if (!m_PlayerBuildScenePicker.Diagnostic().empty())
            ui.TextColored(m_Theme.Warning, m_PlayerBuildScenePicker.Diagnostic());

        const auto enabledScenes = Keire::EnabledPlayerBuildScenes(m_PlayerBuildScenes);
        if (!enabledScenes.empty())
        {
            const auto startup =
                std::ranges::find(m_AssetRecords, enabledScenes.front(), &Keire::AssetSourceRecord::Id);
            ui.TextColored(m_Theme.Success,
                           "Startup Scene: " + (startup != m_AssetRecords.end() ? startup->RelativePath.generic_string()
                                                                                : enabledScenes.front().ToString()));
        }
        else
        {
            ui.TextColored(m_Theme.Error, "Player builds are disabled until at least one scene is enabled.");
        }
        ui.Separator();

        auto active = std::ranges::find(m_PlayerBuildProfiles.Profiles, m_PlayerBuildProfiles.ActiveProfile,
                                        &Keire::PlayerBuildProfile::Id);
        if (active == m_PlayerBuildProfiles.Profiles.end())
        {
            ui.TextColored(m_Theme.Error, "The active player build profile is missing.");
            return;
        }

        if (auto combo = ui.BeginCombo("Build Profile", active->Name); combo)
        {
            for (const auto& profile : m_PlayerBuildProfiles.Profiles)
            {
                const auto selected = profile.Id == m_PlayerBuildProfiles.ActiveProfile;
                if (ui.Selectable(profile.Name + "###PlayerProfile" + profile.Id.ToString(), selected))
                {
                    m_PlayerBuildProfiles.ActiveProfile = profile.Id;
                    m_PlayerSigningEditProfile = {};
                    profileChanged = true;
                }
            }
        }
        if (ui.Button("Create"))
        {
            auto profile = Keire::DefaultPlayerBuildProfiles().Profiles.front();
            profile.Id = Keire::AssetId::Generate();
            profile.Name =
                UniqueProfileValue(m_PlayerBuildProfiles, "Desktop Development", &Keire::PlayerBuildProfile::Name);
            profile.OutputSlug = UniqueProfileValue(m_PlayerBuildProfiles, "Desktop-Development",
                                                    &Keire::PlayerBuildProfile::OutputSlug);
            m_PlayerBuildProfiles.ActiveProfile = profile.Id;
            m_PlayerBuildProfiles.Profiles.push_back(std::move(profile));
            m_PlayerSigningEditProfile = {};
            profileChanged = true;
        }
        ui.SameLine();
        if (ui.Button("Duplicate"))
        {
            auto profile = *active;
            profile.Id = Keire::AssetId::Generate();
            profile.Name =
                UniqueProfileValue(m_PlayerBuildProfiles, profile.Name + " Copy", &Keire::PlayerBuildProfile::Name);
            profile.OutputSlug = UniqueProfileValue(m_PlayerBuildProfiles, profile.OutputSlug + "-Copy",
                                                    &Keire::PlayerBuildProfile::OutputSlug);
            m_PlayerBuildProfiles.ActiveProfile = profile.Id;
            m_PlayerBuildProfiles.Profiles.push_back(std::move(profile));
            m_PlayerSigningEditProfile = {};
            profileChanged = true;
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(m_PlayerBuildProfiles.Profiles.size() <= 1); disabled)
        {
            if (ui.Button("Delete"))
            {
                m_PlayerBuildProfiles.Profiles.erase(active);
                m_PlayerBuildProfiles.ActiveProfile = m_PlayerBuildProfiles.Profiles.front().Id;
                m_PlayerSigningEditProfile = {};
                profileChanged = true;
            }
        }

        active = std::ranges::find(m_PlayerBuildProfiles.Profiles, m_PlayerBuildProfiles.ActiveProfile,
                                   &Keire::PlayerBuildProfile::Id);
        if (active == m_PlayerBuildProfiles.Profiles.end())
            return;
        auto& profile = *active;
        ui.Separator();

        profileChanged |= ui.InputText("Profile Name", profile.Name);
        profileChanged |= ui.InputText("Output Folder", profile.OutputSlug);
        const auto target = std::ranges::find_if(
            PlayerTargets, [&](const auto& choice)
            { return choice.Platform == profile.Platform && choice.Architecture == profile.Architecture; });
        const auto targetLabel = target == PlayerTargets.end() ? std::string_view("Unknown") : target->Label;
        if (auto combo = ui.BeginCombo("Target", targetLabel); combo)
        {
            for (const auto& choice : PlayerTargets)
            {
                const bool selected =
                    choice.Platform == profile.Platform && choice.Architecture == profile.Architecture;
                if (ui.Selectable(choice.Label, selected))
                {
                    profile.Platform = choice.Platform;
                    profile.Architecture = choice.Architecture;
                    profileChanged = true;
                }
            }
        }
        constexpr std::array configurations{Keire::PlayerBuildConfiguration::Development,
                                            Keire::PlayerBuildConfiguration::Release,
                                            Keire::PlayerBuildConfiguration::Dist};
        if (auto combo = ui.BeginCombo("Configuration", Keire::ToString(profile.Configuration)); combo)
        {
            for (const auto configuration : configurations)
            {
                if (ui.Selectable(Keire::ToString(configuration), profile.Configuration == configuration))
                {
                    profile.Configuration = configuration;
                    if (configuration == Keire::PlayerBuildConfiguration::Development)
                        profile.IncludeSymbols = true;
                    else if (configuration == Keire::PlayerBuildConfiguration::Dist)
                        profile.IncludeSymbols = false;
                    profileChanged = true;
                }
            }
        }
        profileChanged |= ui.Checkbox("Include Symbols", profile.IncludeSymbols);

        std::string supportStatus;
        bool supportInstalled = false;
        try
        {
            const auto modules = Owner().Modules();
            const auto support = Keire::Detail::ResolvePlayerSupport(m_ExecutablePath, profile.Platform,
                                                                     profile.Architecture, profile.Configuration,
                                                                     modules ? modules->Fingerprint() : std::string{});
            supportInstalled = true;
            supportStatus = support.DevelopmentFallback ? "Available from this development build"
                                                        : "Installed: " + support.Manifest.Id;
        }
        catch (const std::exception& error)
        {
            supportStatus = error.what();
        }
        ui.TextColored(supportInstalled ? m_Theme.Success : m_Theme.Warning, supportStatus);
        if (!supportInstalled && ui.Button("Open Build Support"))
        {
            try
            {
                OpenBuildSupportHub(profile);
            }
            catch (const std::exception& error)
            {
                ReportError("Build Support", error.what());
            }
        }

        ui.Separator();
        if (auto branding = ui.BeginTreeNode("Player Identity & Branding", true); branding)
        {
            settingsChanged |= ui.InputText("Product Name", m_PlayerSettings.ProductName);
            settingsChanged |= ui.InputText("Semantic Version", m_PlayerSettings.Version);
            settingsChanged |= ui.InputText("Application Identifier", m_PlayerSettings.ApplicationIdentifier);
            settingsChanged |= ui.InputText("Window Title", m_PlayerSettings.WindowTitle);
            KeireEditor::AssetPickerOptions iconOptions;
            iconOptions.EmptyLabel = "Kéire default icon";
            iconOptions.ExpectedType = Keire::Texture2DAsset::StaticType();
            iconOptions.Label = "Windows Icon";
            settingsChanged |=
                m_WindowsPlayerIconPicker.Draw(ui, m_AssetRecords, m_PlayerSettings.WindowsIcon, iconOptions);
            if (!m_WindowsPlayerIconPicker.Diagnostic().empty())
                ui.TextColored(m_Theme.Warning, m_WindowsPlayerIconPicker.Diagnostic());
            iconOptions.Label = "Linux Icon";
            settingsChanged |=
                m_LinuxPlayerIconPicker.Draw(ui, m_AssetRecords, m_PlayerSettings.LinuxIcon, iconOptions);
            if (!m_LinuxPlayerIconPicker.Diagnostic().empty())
                ui.TextColored(m_Theme.Warning, m_LinuxPlayerIconPicker.Diagnostic());
            iconOptions.Label = "macOS Icon";
            settingsChanged |=
                m_MacOSPlayerIconPicker.Draw(ui, m_AssetRecords, m_PlayerSettings.MacOSIcon, iconOptions);
            if (!m_MacOSPlayerIconPicker.Diagnostic().empty())
                ui.TextColored(m_Theme.Warning, m_MacOSPlayerIconPicker.Diagnostic());
            ui.TextColored(m_Theme.MutedText,
                           "Choose an imported image asset. Kéire default icon uses the built-in player artwork.");
        }

        if (auto signing = ui.BeginTreeNode("Signing", false); signing)
        {
            constexpr std::array policies{Keire::PlayerSigningPolicy::Disabled,
                                          Keire::PlayerSigningPolicy::SignIfConfigured,
                                          Keire::PlayerSigningPolicy::Required};
            if (auto combo = ui.BeginCombo("Policy", Keire::ToString(profile.Signing.Policy)); combo)
            {
                for (const auto policy : policies)
                {
                    if (ui.Selectable(Keire::ToString(policy), profile.Signing.Policy == policy))
                    {
                        profile.Signing.Policy = policy;
                        profileChanged = true;
                    }
                }
            }
            auto command = Keire::Detail::PathToUtf8(profile.Signing.Command);
            if (ui.InputText("Hook Executable", command))
            {
                profile.Signing.Command = Keire::Detail::PathFromUtf8(command);
                profileChanged = true;
            }
            if (m_PlayerSigningEditProfile != profile.Id)
            {
                m_PlayerSigningEditProfile = profile.Id;
                m_PlayerSigningArgumentsText = JoinList(profile.Signing.Arguments);
                m_PlayerSigningEnvironmentText = JoinList(profile.Signing.RequiredEnvironment);
            }
            if (ui.InputText("Hook Arguments", m_PlayerSigningArgumentsText))
            {
                profile.Signing.Arguments = SplitList(m_PlayerSigningArgumentsText);
                profileChanged = true;
            }
            if (ui.InputText("Required Environment", m_PlayerSigningEnvironmentText))
            {
                profile.Signing.RequiredEnvironment = SplitList(m_PlayerSigningEnvironmentText);
                profileChanged = true;
            }
            auto timeout = static_cast<std::int64_t>(profile.Signing.TimeoutSeconds);
            if (ui.DragInteger("Timeout (seconds)", timeout, 1.0, 1, 3600))
            {
                profile.Signing.TimeoutSeconds = static_cast<std::uint32_t>(timeout);
                profileChanged = true;
            }
            ui.TextColored(m_Theme.MutedText, "Separate hook arguments and environment names with semicolons.");
        }

        if (profileChanged || settingsChanged || scenesChanged)
        {
            try
            {
                SavePlayerBuildConfiguration();
            }
            catch (const std::exception& error)
            {
                ReportError("Player Build Settings", error.what());
            }
        }

        ui.Separator();
        const auto playerStatus =
            m_PlayerBuildService ? m_PlayerBuildService->Status() : KeireEditor::PlayerBuildStatus{};
        if (m_PlayerBuildService && m_PlayerBuildService->Busy())
        {
            const auto overlay =
                playerStatus.Phase.empty() ? playerStatus.Message : playerStatus.Phase + ": " + playerStatus.Message;
            ui.ProgressBar(playerStatus.Progress, {0.0F, 18.0F}, overlay);
        }
        else if (playerStatus.State != KeireEditor::PlayerBuildState::Idle)
        {
            const auto color = playerStatus.State == KeireEditor::PlayerBuildState::Succeeded   ? m_Theme.Success
                               : playerStatus.State == KeireEditor::PlayerBuildState::Cancelled ? m_Theme.Warning
                                                                                                : m_Theme.Error;
            ui.TextColored(color, playerStatus.Message);
        }
        if (auto disabled = ui.BeginDisabled(!m_CommandRouter->Available(KeireEditor::EditorCommand::BuildPlayer));
            disabled)
        {
            if (ui.Button("Build"))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::BuildPlayer);
        }
        ui.SameLine();
        if (auto disabled =
                ui.BeginDisabled(!m_CommandRouter->Available(KeireEditor::EditorCommand::BuildAndRunPlayer));
            disabled)
        {
            if (ui.Button("Build & Run"))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::BuildAndRunPlayer);
        }
        ui.SameLine();
        if (auto disabled =
                ui.BeginDisabled(!m_CommandRouter->Available(KeireEditor::EditorCommand::CancelPlayerBuild));
            disabled)
        {
            if (ui.Button("Cancel"))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::CancelPlayerBuild);
        }
        ui.SameLine();
        if (auto disabled =
                ui.BeginDisabled(!m_CommandRouter->Available(KeireEditor::EditorCommand::RevealPlayerBuild));
            disabled)
        {
            if (ui.Button("Reveal Build"))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::RevealPlayerBuild);
        }
        if (profile.Platform != Keire::HostPlayerPlatform() || profile.Architecture != Keire::HostPlayerArchitecture())
        {
            ui.TextColored(m_Theme.MutedText,
                           "Foreign targets can be assembled here; Build & Run requires a matching editor host.");
        }

        ui.Separator();

        const auto scripts = Owner().Scripts();
        const auto scriptStatus = scripts ? scripts->BuildStatus() : Keire::ManagedBuildStatus{};
        ui.Text("Managed build: " + ManagedBuildStateName(scriptStatus.State));
        if (!scriptStatus.ActiveAssemblyDirectory.empty())
            ui.Text("Assemblies: " + scriptStatus.ActiveAssemblyDirectory.generic_string());
        for (const auto& diagnostic : scriptStatus.Diagnostics)
        {
            const auto color = diagnostic.Severity == Keire::ManagedDiagnosticSeverity::Error     ? m_Theme.Error
                               : diagnostic.Severity == Keire::ManagedDiagnosticSeverity::Warning ? m_Theme.Warning
                                                                                                  : m_Theme.MutedText;
            std::string message = diagnostic.Message;
            if (!diagnostic.Code.empty())
            {
                message.insert(0, ": ");
                message.insert(0, diagnostic.Code);
            }
            if (!diagnostic.Source.empty())
            {
                auto location = diagnostic.Source.generic_string();
                location += ':';
                location += std::to_string(diagnostic.Line);
                location += ' ';
                location += message;
                message = std::move(location);
            }
            ui.TextColored(color, message);
        }
        const bool managedBusy = scriptStatus.State == Keire::ManagedBuildState::Generating ||
                                 scriptStatus.State == Keire::ManagedBuildState::Compiling ||
                                 scriptStatus.State == Keire::ManagedBuildState::Publishing;
        if (auto disabled = ui.BeginDisabled(!scripts || managedBusy || !m_AssetDatabase || !Owner().GetProject());
            disabled)
        {
            if (ui.Button("Regenerate C# Project"))
            {
                try
                {
                    GenerateManagedIdeWorkspace();
                }
                catch (const std::exception& error)
                {
                    ReportError("Scripts", error.what());
                }
            }
            ui.SameLine();
            if (ui.Button("Build Scripts"))
                (void)m_CommandRouter->Execute(KeireEditor::EditorCommand::BuildScripts);
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!scripts || !managedBusy); disabled)
        {
            if (ui.Button("Cancel Managed Build"))
                scripts->CancelBuild(scriptStatus.Operation);
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!m_AssetDatabase || !m_AssetOperations || m_AssetOperations->Busy());
            disabled)
        {
            if (ui.Button("Cook Assets"))
                CookAssets();
        }
        ui.TextColored(m_Theme.MutedText,
                       "Standalone outputs are published atomically under <Project>/Build/<profile output folder>.");
    }
}

void EditorWorkspaceLayer::StartManagedBuild()
{
    const auto scripts = Owner().Scripts();
    if (!scripts || !m_AssetDatabase)
        return;
    const auto sdk = ProjectManagedSdk();
    scripts->ConfigureManagedSdk(sdk.Selection, sdk.CustomExecutable);
    Keire::ManagedBuildRequest request;
    const auto projectRoot = Owner().GetProject()->Root();
    for (const auto& record : m_AssetDatabase->Records())
    {
        if (record.Type != Keire::ManagedAssemblyAsset::StaticType())
            continue;
        const auto assembly =
            Keire::ManagedAssemblyAsset::Decode(ReadBytes(projectRoot / "Assets" / record.RelativePath));
        request.Assemblies.push_back({record.Id, assembly->Definition()});
    }
    if (request.Assemblies.empty())
        throw std::runtime_error("The project contains no .keireasm assembly definitions.");
    request.Configuration = "Debug";
    if (m_PlayerBuildSettingsLoaded)
    {
        const auto& profile = Keire::FindPlayerBuildProfile(m_PlayerBuildProfiles, m_PlayerBuildProfiles.ActiveProfile);
        if (profile.Configuration != Keire::PlayerBuildConfiguration::Development)
            request.Configuration = "Release";
    }
    (void)scripts->StartBuild(std::move(request));
}

void EditorWorkspaceLayer::UpdateManagedBuild(const Keire::Time& time)
{
    const auto scripts = Owner().Scripts();
    if (!scripts)
        return;
    if (m_ManagedBuildDebounceSeconds >= 0.0)
    {
        m_ManagedBuildDebounceSeconds -= time.UnscaledDeltaTime().Seconds();
        if (m_ManagedBuildDebounceSeconds <= 0.0)
        {
            m_ManagedBuildDebounceSeconds = -1.0;
            try
            {
                StartManagedBuild();
            }
            catch (const std::exception& error)
            {
                ReportError("Managed Build", error.what());
            }
        }
    }
    const auto status = scripts->BuildStatus();
    const bool terminal = status.State == Keire::ManagedBuildState::Succeeded ||
                          status.State == Keire::ManagedBuildState::Failed ||
                          status.State == Keire::ManagedBuildState::Cancelled;
    if (terminal && status.Operation && status.Operation != m_LastManagedBuildReport)
    {
        m_LastManagedBuildReport = status.Operation;
        for (const auto& diagnostic : status.Diagnostics)
        {
            std::string message;
            if (!diagnostic.Source.empty())
            {
                message = diagnostic.Source.generic_string();
                if (diagnostic.Line > 0)
                    message += ':' + std::to_string(diagnostic.Line);
                if (diagnostic.Column > 0)
                    message += ':' + std::to_string(diagnostic.Column);
                message += ": ";
            }
            if (!diagnostic.Code.empty())
                message += diagnostic.Code + ": ";
            message += diagnostic.Message;
            if (diagnostic.Severity == Keire::ManagedDiagnosticSeverity::Error)
                AddConsoleMessage("Managed Build", std::move(message), m_Theme.Error, Keire::LogLevel::Error);
            else if (diagnostic.Severity == Keire::ManagedDiagnosticSeverity::Warning)
                AddConsoleMessage("Managed Build", std::move(message), m_Theme.Warning, Keire::LogLevel::Warn);
            else
                AddConsoleMessage("Managed Build", std::move(message), m_Theme.MutedText);
        }
        if (status.State == Keire::ManagedBuildState::Succeeded)
        {
            AddConsoleMessage("Managed Build", "Scripts built in " + std::to_string(status.Elapsed.count()) + " ms.",
                              m_Theme.Success);
        }
        else if (status.State == Keire::ManagedBuildState::Cancelled)
        {
            AddConsoleMessage("Managed Build", "Script build cancelled.", m_Theme.Warning, Keire::LogLevel::Warn);
        }
        else if (status.Diagnostics.empty())
        {
            ReportError("Managed Build", "Script build failed without a compiler diagnostic.");
        }
    }
    if (status.State != Keire::ManagedBuildState::Succeeded || !status.Operation ||
        status.Operation == m_LastManagedReload)
    {
        return;
    }
    m_LastManagedReload = status.Operation;
    try
    {
        Keire::ManagedReloadRequest reload;
        reload.ManagedApiAssembly = status.ManagedApiAssembly;
        std::set<std::string, std::less<>> editorAssemblyFiles;
        const auto projectRoot = Owner().GetProject()->Root();
        for (const auto& record : m_AssetDatabase->Records())
        {
            if (record.Type != Keire::ManagedAssemblyAsset::StaticType())
                continue;
            const auto assembly =
                Keire::ManagedAssemblyAsset::Decode(ReadBytes(projectRoot / "Assets" / record.RelativePath));
            if (assembly->Definition().Classification != Keire::ManagedAssemblyClassification::Tests)
                editorAssemblyFiles.emplace(assembly->Definition().Name + ".dll");
        }
        for (const auto& entry : std::filesystem::directory_iterator(status.ActiveAssemblyDirectory))
        {
            if (entry.is_regular_file() && editorAssemblyFiles.contains(entry.path().filename().string()))
            {
                reload.Assemblies.push_back(entry.path());
            }
        }
        std::ranges::sort(reload.Assemblies);
        if (reload.Assemblies.empty())
            throw std::runtime_error("Managed build published no gameplay assemblies.");
        if (!scripts->PrepareReload(std::move(reload)))
            throw std::runtime_error(scripts->ReloadStatus().Diagnostic);
        scripts->CommitReload();
        const auto sharedComponents = Owner().Scenes()->Components();
        scripts->InstallManagedComponents(sharedComponents);
        if (const auto activeScene = m_SceneDocument->ActiveScene();
            activeScene && activeScene->Components() != sharedComponents)
        {
            scripts->InstallManagedComponents(activeScene->Components());
        }
        if (!m_SceneDocument->PlaySession())
        {
            if (const auto editingScene = m_SceneDocument->EditingScene())
            {
                const bool dirty = editingScene->Dirty();
                auto replacement = Keire::CreateRef<Keire::Scene>(editingScene->Asset(), editingScene->Snapshot(),
                                                                  editingScene->Components());
                if (dirty)
                    replacement->MarkDirty();
                else
                    replacement->MarkSaved();
                m_SceneDocument->ReplaceEditingScene(std::move(replacement));
            }
        }
        AddConsoleMessage("Managed", "Gameplay assemblies reloaded at a scene safe boundary.", m_Theme.Success);
        if (!m_PendingScriptAttachments.empty())
        {
            auto attachments = std::exchange(m_PendingScriptAttachments, {});
            m_ResolvingPendingScriptAttachments = true;
            for (const auto& [entity, script] : attachments)
            {
                try
                {
                    AddScriptToEntity(entity, script);
                }
                catch (const std::exception& error)
                {
                    ReportError("Scripts", error.what());
                }
            }
            m_ResolvingPendingScriptAttachments = false;
        }
    }
    catch (const std::exception& error)
    {
        ReportError("Managed Reload", error.what());
    }
}

void EditorWorkspaceLayer::DrawProfiler(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_Profiler); panel)
    {
        const auto profiler = Owner().GetProfiler();
        if (!profiler || !profiler->IsOpen())
        {
            DrawEmptyState(ui, "Profiler is disabled", "Enable profiling in the application specification.",
                           "CPU spans and subsystem counters are retained per application frame.");
            return;
        }

        const auto latestSummary = profiler->LatestSummary();
        constexpr double refreshIntervalMicroseconds = 100'000.0;
        if (!m_ProfilerPaused &&
            (m_CachedProfileFrame.Sequence == 0 ||
             latestSummary.StartMicroseconds - m_CachedProfileFrame.StartMicroseconds >= refreshIntervalMicroseconds))
        {
            m_CachedProfileFrame = profiler->LatestFrame();
            m_CachedProfileHistory = profiler->RecentSummaries(240);
        }
        const auto& liveFrame = m_CachedProfileFrame;
        const auto& liveHistory = m_CachedProfileHistory;
        if (ui.Checkbox("Freeze capture", m_ProfilerPaused))
        {
            if (m_ProfilerPaused)
            {
                m_FrozenProfileFrame = liveFrame;
                m_FrozenProfileHistory = liveHistory;
            }
            else
            {
                m_FrozenProfileFrame = {};
                m_FrozenProfileHistory.clear();
            }
        }
        ui.SameLine();
        (void)ui.Checkbox("Viewport FPS overlay", m_ShowPerformanceOverlay);

        const auto& frame = m_ProfilerPaused ? m_FrozenProfileFrame : liveFrame;
        const auto& history = m_ProfilerPaused ? m_FrozenProfileHistory : liveHistory;
        if (frame.Sequence == 0)
        {
            DrawEmptyState(ui, "Waiting for capture", "The profiler has not completed an application frame yet.",
                           "Keep the editor active for one frame.");
            return;
        }

        if (m_ProfilerPresentation.FrameSequence != frame.Sequence)
        {
            auto presentation = ProfilerPresentationCache{};
            presentation.FrameSequence = frame.Sequence;
            std::vector<double> frameTimes;
            frameTimes.reserve(history.size());
            double totalFrameMicroseconds = 0.0;
            for (const auto& sample : history)
            {
                if (sample.DurationMicroseconds <= 0.0)
                    continue;
                frameTimes.push_back(sample.DurationMicroseconds);
                totalFrameMicroseconds += sample.DurationMicroseconds;
            }
            std::ranges::sort(frameTimes);
            const auto percentile = [&](const double fraction)
            {
                if (frameTimes.empty())
                    return 0.0;
                const auto index = static_cast<std::size_t>(std::clamp(fraction, 0.0, 1.0) *
                                                            static_cast<double>(frameTimes.size() - 1));
                return frameTimes[index];
            };
            presentation.AverageFrameMicroseconds =
                frameTimes.empty() ? 0.0 : totalFrameMicroseconds / static_cast<double>(frameTimes.size());
            presentation.P95FrameMicroseconds = percentile(0.95);
            presentation.P99FrameMicroseconds = percentile(0.99);
            presentation.MaximumFrameMicroseconds = frameTimes.empty() ? 0.0 : frameTimes.back();
            presentation.FramesPerSecond =
                frame.DurationMicroseconds > 0.0 ? 1'000'000.0 / frame.DurationMicroseconds : 0.0;
            presentation.AverageFramesPerSecond =
                presentation.AverageFrameMicroseconds > 0.0 ? 1'000'000.0 / presentation.AverageFrameMicroseconds : 0.0;
            presentation.OnePercentLow =
                presentation.P99FrameMicroseconds > 0.0 ? 1'000'000.0 / presentation.P99FrameMicroseconds : 0.0;
            const double stutterThreshold = std::max(33'333.0, presentation.AverageFrameMicroseconds * 1.5);
            presentation.StutterCount = static_cast<std::size_t>(
                std::ranges::count_if(frameTimes, [&](const double value) { return value > stutterThreshold; }));
            presentation.FrameLine =
                "Frame " + std::to_string(frame.Sequence) + "  |  " +
                std::to_string(static_cast<std::uint32_t>(std::lround(presentation.FramesPerSecond))) + " FPS  |  " +
                FormatMicroseconds(frame.DurationMicroseconds);
            presentation.HistoryLine =
                "Rolling " + std::to_string(history.size()) + " frames  |  Avg " +
                std::to_string(static_cast<std::uint32_t>(std::lround(presentation.AverageFramesPerSecond))) +
                " FPS  |  1% low " +
                std::to_string(static_cast<std::uint32_t>(std::lround(presentation.OnePercentLow))) + " FPS";
            presentation.TailLine = "P95 " + FormatMicroseconds(presentation.P95FrameMicroseconds) + "  |  P99 " +
                                    FormatMicroseconds(presentation.P99FrameMicroseconds) + "  |  Max " +
                                    FormatMicroseconds(presentation.MaximumFrameMicroseconds) + "  |  Stutters " +
                                    std::to_string(presentation.StutterCount);
            presentation.OrderedSpans = frame.Spans;
            std::ranges::sort(presentation.OrderedSpans, std::greater{}, &Keire::ProfileSpan::DurationMicroseconds);
            presentation.TimelineSpans = frame.Spans;
            std::ranges::sort(presentation.TimelineSpans, {}, &Keire::ProfileSpan::StartMicroseconds);
            presentation.SpanLines.reserve(presentation.OrderedSpans.size());
            for (const auto& span : presentation.OrderedSpans)
            {
                presentation.SpanLines.push_back(ProfileCategoryName(span.Category) + " / " + span.Name + "  " +
                                                 FormatMicroseconds(span.DurationMicroseconds) + "  thread " +
                                                 std::to_string(span.Thread));
            }
            presentation.TimelineLines.reserve(presentation.TimelineSpans.size());
            std::unordered_map<std::uint64_t, double> threadTotals;
            for (const auto& span : presentation.TimelineSpans)
            {
                presentation.TimelineLines.push_back(
                    "+" + FormatMicroseconds(span.StartMicroseconds - frame.StartMicroseconds) + "  " +
                    std::to_string(span.Thread) + "  " + ProfileCategoryName(span.Category) + " / " + span.Name + "  " +
                    FormatMicroseconds(span.DurationMicroseconds));
                threadTotals[span.Thread] += span.DurationMicroseconds;
            }
            presentation.ThreadLines.reserve(threadTotals.size());
            for (const auto& [thread, duration] : threadTotals)
                presentation.ThreadLines.push_back("Thread " + std::to_string(thread) + "  " +
                                                   FormatMicroseconds(duration));
            std::ranges::sort(presentation.ThreadLines);
            presentation.CounterLines.reserve(frame.Counters.size());
            for (const auto& counter : frame.Counters)
            {
                presentation.CounterLines.push_back(ProfileCategoryName(counter.Category) + " / " + counter.Name +
                                                    "  " + std::to_string(counter.Value));
            }
            if (const auto scripts = Owner().Scripts())
            {
                const auto callbackMetrics = scripts->CallbackMetrics();
                presentation.ManagedCallbacksTruncated = callbackMetrics.Truncated;
                presentation.ManagedCallbackLines.reserve(callbackMetrics.Entries.size());
                const auto callbackName = [](const Keire::ManagedBehaviourCallback callback) -> std::string_view
                {
                    switch (callback)
                    {
                    case Keire::ManagedBehaviourCallback::Awake:
                        return "Awake";
                    case Keire::ManagedBehaviourCallback::Enable:
                        return "OnEnable";
                    case Keire::ManagedBehaviourCallback::Start:
                        return "Start";
                    case Keire::ManagedBehaviourCallback::FixedUpdate:
                        return "FixedUpdate";
                    case Keire::ManagedBehaviourCallback::Update:
                        return "Update";
                    case Keire::ManagedBehaviourCallback::LateUpdate:
                        return "LateUpdate";
                    case Keire::ManagedBehaviourCallback::AnimationEvent:
                        return "OnAnimationEvent";
                    case Keire::ManagedBehaviourCallback::PhysicsContact:
                        return "Physics Contact";
                    case Keire::ManagedBehaviourCallback::Disable:
                        return "OnDisable";
                    case Keire::ManagedBehaviourCallback::Destroy:
                        return "OnDestroy";
                    case Keire::ManagedBehaviourCallback::BeforeReload:
                        return "OnBeforeReload";
                    case Keire::ManagedBehaviourCallback::AfterReload:
                        return "OnAfterReload";
                    case Keire::ManagedBehaviourCallback::AnimatorIk:
                        return "OnAnimatorIk";
                    case Keire::ManagedBehaviourCallback::ProceduralMotionEvent:
                        return "OnProceduralMotionEvent";
                    }
                    return "Unknown";
                };
                for (const auto& metric : callbackMetrics.Entries)
                {
                    const auto averageMicroseconds =
                        metric.Invocations == 0
                            ? 0.0
                            : metric.Milliseconds * 1'000.0 / static_cast<double>(metric.Invocations);
                    auto line = metric.TypeName + " / " + std::string(callbackName(metric.Callback)) + "  |  " +
                                std::to_string(metric.InstanceCount) + " instances  |  " +
                                std::to_string(metric.Invocations) + " calls  |  avg " +
                                FormatMicroseconds(averageMicroseconds) + "  |  max " +
                                FormatMicroseconds(metric.MaximumMilliseconds * 1'000.0);
                    if (metric.SkippedInvocations != 0)
                        line += "  |  " + std::to_string(metric.SkippedInvocations) + " skipped";
                    presentation.ManagedCallbackLines.push_back(std::move(line));
                }
            }
            m_ProfilerPresentation = std::move(presentation);
        }
        const auto& presentation = m_ProfilerPresentation;

        ui.TextColored(m_ProfilerPaused ? m_Theme.Warning : m_Theme.Accent,
                       m_ProfilerPaused ? "FROZEN PERFORMANCE CAPTURE" : "LIVE PERFORMANCE CAPTURE");
        ui.Text(presentation.FrameLine);
        ui.TextColored(m_Theme.MutedText, presentation.HistoryLine);
        ui.TextColored(presentation.StutterCount == 0 ? m_Theme.Success : m_Theme.Warning, presentation.TailLine);
        if (frame.Truncated)
            ui.TextColored(m_Theme.Warning, "Capture truncated: " + std::to_string(frame.DroppedSpans) + " spans and " +
                                                std::to_string(frame.DroppedCounters) + " counters dropped.");

        if (ui.Button("Copy Full Snapshot"))
        {
            std::ostringstream snapshot;
            snapshot << "Keire Profiler Capture " << frame.Sequence << '\n'
                     << "Frame: " << FormatMicroseconds(frame.DurationMicroseconds) << " ("
                     << presentation.FramesPerSecond << " FPS)\n"
                     << "Average: " << FormatMicroseconds(presentation.AverageFrameMicroseconds) << " ("
                     << presentation.AverageFramesPerSecond << " FPS)\n"
                     << "P95: " << FormatMicroseconds(presentation.P95FrameMicroseconds)
                     << "\nP99: " << FormatMicroseconds(presentation.P99FrameMicroseconds)
                     << "\n1% low: " << presentation.OnePercentLow << " FPS\n"
                     << "Stutters: " << presentation.StutterCount << "\nSpans: " << frame.Spans.size()
                     << "\nCounters: " << frame.Counters.size() << '\n';
            for (const auto& span : frame.Spans)
                snapshot << "SPAN," << ProfileCategoryName(span.Category) << ',' << span.Name << ',' << span.Thread
                         << ',' << span.StartMicroseconds << ',' << span.DurationMicroseconds << '\n';
            for (const auto& counter : frame.Counters)
                snapshot << "COUNTER," << ProfileCategoryName(counter.Category) << ',' << counter.Name << ','
                         << counter.Value << '\n';
            Owner().Windows()->SetClipboardText(snapshot.str());
        }
        ui.SameLine();
        if (ui.Button("Copy Perfetto Trace"))
            Owner().Windows()->SetClipboardText(profiler->LatestChromeTrace());
        ui.SameLine();
        if (ui.Button("Copy Frame CSV"))
        {
            std::ostringstream csv;
            csv << "sequence,start_us,duration_us,spans,counters,dropped_spans,dropped_counters,application_us,"
                   "assets_us,scripting_us,physics_us,animation_us,rendering_us,audio_us,navigation_us,user_us\n";
            for (const auto& sample : history)
                csv << sample.Sequence << ',' << sample.StartMicroseconds << ',' << sample.DurationMicroseconds << ','
                    << sample.SpanCount << ',' << sample.CounterCount << ',' << sample.DroppedSpans << ','
                    << sample.DroppedCounters << ',' << sample.ApplicationMicroseconds << ','
                    << sample.AssetsMicroseconds << ',' << sample.ScriptingMicroseconds << ','
                    << sample.PhysicsMicroseconds << ',' << sample.AnimationMicroseconds << ','
                    << sample.RenderingMicroseconds << ',' << sample.AudioMicroseconds << ','
                    << sample.NavigationMicroseconds << ',' << sample.UserMicroseconds << '\n';
            Owner().Windows()->SetClipboardText(csv.str());
        }

        ui.Separator();
        if (auto overview = ui.BeginTreeNode("Subsystem overview", true); overview)
        {
            ui.Text("Application  " + FormatMicroseconds(frame.ApplicationMicroseconds));
            ui.Text("Assets       " + FormatMicroseconds(frame.AssetsMicroseconds));
            ui.Text("Scripting    " + FormatMicroseconds(frame.ScriptingMicroseconds));
            ui.Text("Physics      " + FormatMicroseconds(frame.PhysicsMicroseconds));
            ui.Text("Animation    " + FormatMicroseconds(frame.AnimationMicroseconds));
            ui.Text("Rendering    " + FormatMicroseconds(frame.RenderingMicroseconds));
            ui.Text("Audio        " + FormatMicroseconds(frame.AudioMicroseconds));
            ui.Text("Navigation   " + FormatMicroseconds(frame.NavigationMicroseconds));
            ui.Text("Editor/User  " + FormatMicroseconds(frame.UserMicroseconds));
            if (const auto renderer = Owner().Renderer())
            {
                const auto statistics = renderer->Statistics();
                ui.Text("GPU submit   " + std::to_string(statistics.DrawCalls) + " draws / " +
                        std::to_string(statistics.Triangles) + " triangles");
                ui.Text("Visibility   " + std::to_string(statistics.VisibleSubmeshes) + " visible / " +
                        std::to_string(statistics.CulledSubmeshes) + " culled / " +
                        std::to_string(statistics.InstanceBatches) + " batches");
                ui.Text("Frame graph  " + std::to_string(statistics.ExecutedFrameGraphPasses) + " / " +
                        std::to_string(statistics.PlannedFrameGraphPasses) + " passes / " +
                        std::to_string(statistics.FrameGraphTransitions) + " transitions");
                ui.Text("Renderer CPU " + std::to_string(statistics.CpuPreparationMilliseconds) + " ms / P95 " +
                        std::to_string(statistics.CpuPreparationP95Milliseconds) + " ms / latency " +
                        std::to_string(statistics.RendererLatencyMilliseconds) + " ms");
                ui.Text("Command prep " + std::to_string(statistics.SkinningPreparationMilliseconds) + " ms skin / " +
                        std::to_string(statistics.VfxPreparationMilliseconds) + " ms VFX / " +
                        std::to_string(statistics.DrawPreparationMilliseconds) + " ms draws");
                ui.Text("Render passes " + std::to_string(statistics.ShadowRecordingMilliseconds) + " ms shadows / " +
                        std::to_string(statistics.ForwardPlusCullingMilliseconds) + " ms Forward+ / " +
                        std::to_string(statistics.ScenePassMilliseconds) + " ms scene / " +
                        std::to_string(statistics.DepthPassMilliseconds) + " ms depth / " +
                        std::to_string(statistics.ToneMapMilliseconds) + " ms tone map / " +
                        std::to_string(statistics.CommandRecordingUnattributedMilliseconds) + " ms other");
                ui.Text("Scheduling " + std::to_string(statistics.AllowedFramesInFlight) + " frames in flight / " +
                        std::to_string(statistics.GpuFenceWaitMilliseconds) + " ms fence wait / " +
                        std::to_string(statistics.SwapchainWaitMilliseconds) + " ms swapchain wait / " +
                        std::to_string(statistics.GpuCompletionLatencyMilliseconds) + " ms completion latency");
                ui.Text("Frame uploads " + std::to_string(statistics.FrameUploadMilliseconds) + " ms / " +
                        std::to_string(statistics.FrameUploadSubmissions) + " submissions / " +
                        std::to_string(statistics.ForwardPlusUploadBytes) + " Forward+ bytes / " +
                        std::to_string(statistics.ForwardPlusBufferReallocations) + " buffer reallocations / " +
                        std::to_string(statistics.ForwardPlusCacheHits) + " cache hits");
                ui.Text("GPU VFX " + std::to_string(statistics.VfxGpuWorlds) + " worlds / " +
                        std::to_string(statistics.VfxComputeDispatches) + " dispatches / " +
                        std::to_string(statistics.VfxComputeThreadGroups) + " thread groups / " +
                        std::to_string(statistics.VfxIndirectDraws) + " indirect draws / " +
                        std::to_string(statistics.VfxGpuParticleCapacity) + " particle slots / " +
                        std::to_string(statistics.VfxGpuBufferBytes) + " bytes / " +
                        std::to_string(statistics.VfxGpuCompletionLatencyMilliseconds) + " ms completion latency");
                if (statistics.VfxPipelineWarmupPending)
                    ui.TextColored(m_Theme.Warning, "GPU VFX pipelines are compiling asynchronously");
                else if (statistics.VfxPipelinesReady)
                    ui.TextColored(m_Theme.Success, "GPU VFX pipelines ready  " +
                                                        std::to_string(statistics.VfxPipelineWarmupMilliseconds) +
                                                        " ms warmup");
                ui.TextColored(
                    statistics.GpuTimingSupported ? m_Theme.Success : m_Theme.MutedText,
                    statistics.GpuTimingSupported
                        ? "GPU timestamps available  " + std::to_string(statistics.GpuFrameMilliseconds) + " ms"
                        : "GPU timestamps unavailable through the active SDL_GPU backend; pass timings are CPU.");
                if (statistics.OverflowedLightTiles != 0)
                    ui.TextColored(m_Theme.Warning,
                                   std::to_string(statistics.OverflowedLightTiles) + " overflowed light tiles");
            }
            if (const auto assets = Owner().Assets())
            {
                const auto statistics = assets->Statistics();
                ui.Text("Assets       " + std::to_string(statistics.KnownAssets) + " known / " +
                        std::to_string(statistics.ResidentBytes) + " resident bytes");
                ui.Text("Streaming    " + std::to_string(statistics.QueuedAssets) + " queued / " +
                        std::to_string(statistics.LoadingAssets) + " loading / high-water " +
                        std::to_string(statistics.QueueHighWaterMark));
                ui.Text("Asset health " + std::to_string(statistics.CompletedLoads) + " loaded / " +
                        std::to_string(statistics.FailedLoads) + " failed / " + std::to_string(statistics.Evictions) +
                        " evicted");
            }
            if (const auto audio = Owner().Audio())
            {
                const auto statistics = audio->Statistics();
                const auto specification = audio->Specification();
                ui.Text("Audio voices " + std::to_string(statistics.AudibleVoices) + " audible / " +
                        std::to_string(statistics.VirtualVoices) + " virtual / " + std::to_string(statistics.Voices) +
                        " allocated");
                const auto voiceUtilization = specification.MaximumVoices == 0
                                                  ? 0.0F
                                                  : static_cast<float>(statistics.AudibleVoices) /
                                                        static_cast<float>(specification.MaximumVoices);
                ui.ProgressBar(std::clamp(voiceUtilization, 0.0F, 1.0F), {0.0F, 12.0F},
                               std::to_string(statistics.AudibleVoices) + " / " +
                                   std::to_string(specification.MaximumVoices) + " audible voices");
                ui.Text("Audio output " + std::to_string(statistics.MixSampleRate) + " Hz / " +
                        std::to_string(statistics.OutputChannels) + " channels / " +
                        std::to_string(statistics.PeriodFrames) + " frame buffer");
                ui.Text("Audio device " + statistics.PlaybackDeviceName);
                if (statistics.PlaybackDeviceFallback)
                    ui.TextColored(m_Theme.Warning,
                                   "The selected playback device was unavailable; using the system default.");
                ui.Text("Audio routing " + std::to_string(statistics.MixerRoutings) + " mixers / " +
                        std::to_string(statistics.MixerBuses) + " buses / " + std::to_string(statistics.MixerEffects) +
                        " effects");
                if (const auto session =
                        m_SceneDocument ? m_SceneDocument->PlaySession() : Keire::Ref<Keire::SceneRuntimeSession>{};
                    session && session->Presentation())
                {
                    const auto sceneAudio = session->Presentation()->Statistics();
                    const auto listener = !sceneAudio.HasAudioListener            ? std::string("missing")
                                          : sceneAudio.UsingPrimaryCameraListener ? std::string("primary Camera")
                                                                                  : std::string("Audio Listener");
                    ui.Text("Scene audio " + std::to_string(sceneAudio.ActiveAudioSources) + " active sources / " +
                            std::to_string(sceneAudio.ActiveReverbZones) +
                            " active reverb zones / listener: " + listener);
                    if (!sceneAudio.HasAudioListener)
                        ui.TextColored(m_Theme.Warning,
                                       "No active primary Audio Listener or primary Camera is available.");
                    if (sceneAudio.PendingAudioAssets != 0)
                        ui.TextColored(m_Theme.Warning, std::to_string(sceneAudio.PendingAudioAssets) +
                                                            " audio assets are still loading or unavailable.");
                }
                ui.Text("Audio frames " + std::to_string(statistics.RenderedFrames) + " / meter readings " +
                        std::to_string(statistics.MeterReadings));
                if (statistics.Underruns != 0)
                    ui.TextColored(m_Theme.Warning, std::to_string(statistics.Underruns) + " output underruns");
                if (auto voices = ui.BeginTreeNode("Audio voice inspector", false); voices)
                {
                    const auto activeVoices = audio->Voices();
                    if (activeVoices.empty())
                        ui.TextColored(m_Theme.MutedText, "No active audio voices.");
                    for (const auto& voice : activeVoices)
                    {
                        const auto state = voice.Paused ? "paused" : voice.Virtualized ? "virtual" : "audible";
                        ui.Text(voice.Bus + "  |  " + state + "  |  priority " + std::to_string(voice.Priority) +
                                "  |  frame " + std::to_string(voice.Frame) + " / " +
                                std::to_string(voice.DurationFrames));
                    }
                }
                if (auto meters = ui.BeginTreeNode("Audio bus meters", false); meters)
                {
                    const auto snapshot = audio->LatestMeterSnapshot();
                    if (snapshot.Readings.empty())
                        ui.TextColored(m_Theme.MutedText, "No mixer meter data has been rendered yet.");
                    for (const auto& reading : snapshot.Readings)
                    {
                        const auto peak = Keire::LinearToDecibels(reading.Peak);
                        const auto rms = Keire::LinearToDecibels(reading.Rms);
                        ui.Text(reading.Bus.ToString() + "  |  peak " + std::to_string(peak) + " dB  |  RMS " +
                                std::to_string(rms) + " dB");
                        if (reading.Clipping)
                            ui.TextColored(m_Theme.Error, "Clipping detected on this bus.");
                    }
                    if (snapshot.DroppedReadings != 0)
                        ui.TextColored(m_Theme.Warning,
                                       std::to_string(snapshot.DroppedReadings) + " meter readings were dropped.");
                }
            }
        }

        if (!presentation.ManagedCallbackLines.empty())
        {
            if (auto callbacks =
                    ui.BeginTreeNode("Managed callbacks (" + std::to_string(presentation.ManagedCallbackLines.size()) +
                                         ")###ProfilerManagedCallbacks",
                                     true);
                callbacks)
            {
                constexpr std::size_t compactCallbackRows = 12;
                if (presentation.ManagedCallbackLines.size() > compactCallbackRows)
                    (void)ui.Checkbox("Show all callback rows###ProfilerShowAllCallbacks",
                                      m_ProfilerShowAllManagedCallbacks);
                const auto visibleRows = m_ProfilerShowAllManagedCallbacks
                                             ? presentation.ManagedCallbackLines.size()
                                             : std::min(compactCallbackRows, presentation.ManagedCallbackLines.size());
                ui.TextColored(m_Theme.MutedText, "Type / lifecycle  |  instances  |  calls  |  average  |  maximum");
                for (std::size_t index = 0; index < visibleRows; ++index)
                {
                    auto id = ui.PushId("managed-callback-" + std::to_string(index));
                    if (ui.Selectable(presentation.ManagedCallbackLines[index]))
                        Owner().Windows()->SetClipboardText(presentation.ManagedCallbackLines[index]);
                }
                if (presentation.ManagedCallbacksTruncated)
                    ui.TextColored(m_Theme.Warning, "Callback metrics were truncated to the 64-entry safety limit.");
            }
        }
        if (auto spans = ui.BeginTreeNode(
                "CPU hotspots (" + std::to_string(frame.Spans.size()) + ")###ProfilerCpuHotspots", true);
            spans)
        {
            ui.TextColored(m_Theme.MutedText, "Click any row to copy it.");
            constexpr std::size_t compactHotspotRows = 12;
            if (presentation.OrderedSpans.size() > compactHotspotRows)
                (void)ui.Checkbox("Show all hotspot rows###ProfilerShowAllHotspots", m_ProfilerShowAllHotspots);
            const auto visibleRows = m_ProfilerShowAllHotspots
                                         ? presentation.OrderedSpans.size()
                                         : std::min(compactHotspotRows, presentation.OrderedSpans.size());
            for (std::size_t index = 0; index < visibleRows; ++index)
            {
                const auto& line = presentation.SpanLines[index];
                auto id = ui.PushId(std::to_string(index));
                if (ui.Selectable(line))
                    Owner().Windows()->SetClipboardText(line);
            }
        }
        if (auto counters =
                ui.BeginTreeNode("Counters (" + std::to_string(frame.Counters.size()) + ")###ProfilerCounters", true);
            counters)
        {
            ui.TextColored(m_Theme.MutedText, "Click any row to copy it.");
            constexpr std::size_t compactCounterRows = 24;
            if (frame.Counters.size() > compactCounterRows)
                (void)ui.Checkbox("Show all counter rows###ProfilerShowAllCounters", m_ProfilerShowAllCounters);
            const auto visibleRows =
                m_ProfilerShowAllCounters ? frame.Counters.size() : std::min(compactCounterRows, frame.Counters.size());
            for (std::size_t index = 0; index < visibleRows; ++index)
            {
                const auto& line = presentation.CounterLines[index];
                auto id = ui.PushId("counter-" + std::to_string(index));
                if (ui.Selectable(line))
                    Owner().Windows()->SetClipboardText(line);
            }
        }
        if (auto timeline =
                ui.BeginTreeNode("Timeline (" + std::to_string(frame.Spans.size()) + ")###ProfilerTimeline", false);
            timeline)
        {
            ui.TextColored(m_Theme.MutedText, "Start offset  |  thread  |  category / span  |  duration");
            for (std::size_t index = 0; index < presentation.TimelineLines.size(); ++index)
            {
                auto id = ui.PushId("timeline-" + std::to_string(index));
                if (ui.Selectable(presentation.TimelineLines[index]))
                    Owner().Windows()->SetClipboardText(presentation.TimelineLines[index]);
            }
        }
        if (auto threads = ui.BeginTreeNode(
                "Thread lanes (" + std::to_string(presentation.ThreadLines.size()) + ")###ProfilerThreads", false);
            threads)
        {
            for (std::size_t index = 0; index < presentation.ThreadLines.size(); ++index)
            {
                auto id = ui.PushId("thread-" + std::to_string(index));
                if (ui.Selectable(presentation.ThreadLines[index]))
                    Owner().Windows()->SetClipboardText(presentation.ThreadLines[index]);
            }
        }
    }
}

void EditorWorkspaceLayer::DrawRenderGraph(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_RenderGraph); panel)
    {
        const auto renderer = Owner().Renderer();
        if (!renderer || !renderer->IsOpen())
        {
            DrawEmptyState(ui, "Render graph unavailable", "Enable the renderer to inspect its compiled graph.",
                           "The panel captures immutable data from the renderer's compiled frame graph.");
            return;
        }

        const auto snapshot = renderer->CaptureFrameGraph();
        ui.Text("Passes: " + std::to_string(snapshot.Passes.size()) +
                " | Resources: " + std::to_string(snapshot.Resources.size()));
        ui.Text("Transient: " + std::to_string(snapshot.ActiveTransientBytes) +
                " bytes | Unaliased: " + std::to_string(snapshot.TheoreticalUnaliasedBytes) +
                " | Saved: " + std::to_string(snapshot.SavedAliasingBytes));
        ui.Text("Fence-retired: " + std::to_string(snapshot.FenceRetiredBytes) + " bytes");

        if (ui.Button("Export JSON"))
        {
            try
            {
                const auto root = Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path(".");
                Keire::ExportFrameGraphJson(snapshot, root / "Library" / "Diagnostics" / "render-graph.json");
                m_RenderGraphStatus = "Exported Library/Diagnostics/render-graph.json";
            }
            catch (const std::exception& error)
            {
                ReportError("Render Graph", error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Export DOT"))
        {
            try
            {
                const auto root = Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path(".");
                Keire::ExportFrameGraphDot(snapshot, root / "Library" / "Diagnostics" / "render-graph.dot");
                m_RenderGraphStatus = "Exported Library/Diagnostics/render-graph.dot";
            }
            catch (const std::exception& error)
            {
                ReportError("Render Graph", error.what());
            }
        }

        if (!m_RenderGraphStatus.empty())
            ui.Text(m_RenderGraphStatus);

        ui.Separator();
        ui.Text("DETERMINISTIC PASS ORDER");
        for (const auto& pass : snapshot.Passes)
        {
            ui.Text(std::to_string(pass.Order) + ". " + pass.Name +
                    "  [transitions: " + std::to_string(pass.Transitions.size()) + "]");
        }

        ui.Separator();
        ui.Text("RESOURCE LIFETIMES AND ALIAS SLOTS");
        const auto passCount = std::max<std::size_t>(1, snapshot.Passes.size());
        for (const auto& resource : snapshot.Resources)
        {
            const auto label =
                resource.Name +
                (resource.Imported ? " [imported]"
                                   : " [transient slot " + std::to_string(resource.PhysicalAliasSlot) + "]") +
                "  " + std::to_string(resource.EstimatedBytes) + " bytes";
            ui.Text(label);
            const auto span = resource.Used ? resource.LastPass - resource.FirstPass + 1U : 0U;
            const auto fraction = static_cast<float>(span) / static_cast<float>(passCount);
            const auto interval = resource.Used
                                      ? std::to_string(resource.FirstPass) + ".." + std::to_string(resource.LastPass)
                                      : std::string("unused");
            ui.ProgressBar(fraction, {0.0F, 12.0F}, interval);
        }
    }
}
