#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/PrefabAuthoring.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <string>

namespace
{
    [[nodiscard]] std::string PrefabOverrideName(const Keire::PrefabOverrideKind kind)
    {
        switch (kind)
        {
        case Keire::PrefabOverrideKind::RenameObject:
            return "Rename object";
        case Keire::PrefabOverrideKind::SetObjectActive:
            return "Set active";
        case Keire::PrefabOverrideKind::SetObjectTransform:
            return "Set transform";
        case Keire::PrefabOverrideKind::SetComponentProperty:
            return "Set component property";
        case Keire::PrefabOverrideKind::AddComponent:
            return "Add component";
        case Keire::PrefabOverrideKind::RemoveComponent:
            return "Remove component";
        case Keire::PrefabOverrideKind::AddObject:
            return "Add object";
        case Keire::PrefabOverrideKind::RemoveObject:
            return "Remove object";
        }
        return "Unknown override";
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

void EditorWorkspaceLayer::DrawPrefabOverrides(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_PrefabOverrides); panel)
    {
        const auto scene = m_SceneDocument ? m_SceneDocument->EditingScene() : Keire::Ref<Keire::Scene>{};
        if (!scene || !m_SceneDocument->Selection())
        {
            DrawEmptyState(ui, "No prefab instance selected", "Select an object in a prefab instance.",
                           "Instance mappings and overrides will appear here.");
            return;
        }

        auto definition = scene->Snapshot();
        const auto selection = m_SceneDocument->Selection();
        const auto instance = std::ranges::find_if(
            definition.PrefabInstances,
            [selection](const Keire::PrefabInstanceDefinition& candidate)
            {
                return candidate.Root == selection ||
                       std::ranges::any_of(candidate.Objects, [selection](const Keire::PrefabObjectMapping& mapping)
                                           { return mapping.Instance == selection; });
            });
        if (instance == definition.PrefabInstances.end())
        {
            DrawEmptyState(ui, "Selection is not a prefab instance", "Select an inherited prefab object.",
                           "Regular scene objects do not have prefab overrides.");
            return;
        }

        ui.Text("Prefab: " + instance->Prefab.ToString());
        ui.Text("Instance root: " + instance->Root.ToString());
        ui.Text(std::to_string(instance->Objects.size()) + " inherited objects");
        ui.Separator();
        ui.Text(std::to_string(instance->Overrides.size()) + " overrides");
        for (std::size_t index = 0; index < instance->Overrides.size(); ++index)
        {
            const auto& overrideValue = instance->Overrides[index];
            auto id = ui.PushId(std::to_string(index));
            std::string label = PrefabOverrideName(overrideValue.Kind);
            if (!overrideValue.Property.empty())
                label += "  " + overrideValue.Property;
            auto node = ui.BeginTreeNode(label);
            if (node)
            {
                ui.Text("Object: " + overrideValue.Object.ToString());
                if (overrideValue.Component)
                    ui.Text("Component: " + overrideValue.Component.ToString());
            }
        }

        ui.Separator();
        const auto instanceRoot = instance->Root;
        if (ui.Button("Revert All Overrides"))
        {
            try
            {
                RecordSceneUndo("Revert Prefab Overrides");
                auto replacement = scene->Snapshot();
                const auto found = std::ranges::find(replacement.PrefabInstances, instanceRoot,
                                                     &Keire::PrefabInstanceDefinition::Root);
                if (found == replacement.PrefabInstances.end())
                    throw std::runtime_error("Prefab instance changed before the operation completed.");
                if (!m_AssetDatabase || !Owner().GetProject())
                    throw std::runtime_error("The prefab asset database is unavailable.");
                const auto projectRoot = Owner().GetProject()->Root();
                const auto composed = Keire::ComposePrefab(
                    found->Prefab,
                    [&](const Keire::AssetId prefab)
                    {
                        const auto record = m_AssetDatabase->Find(prefab);
                        if (!record || record->Type != Keire::PrefabAsset::StaticType())
                            return Keire::Ref<Keire::PrefabAsset>{};
                        return Keire::PrefabAsset::Decode(ReadBytes(projectRoot / "Assets" / record->RelativePath));
                    });
                if (!KeireEditor::RevertPrefabInstance(replacement, instanceRoot, composed))
                    throw std::runtime_error("Prefab instance changed before the operation completed.");
                auto rebuilt =
                    Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
                rebuilt->MarkDirty();
                m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
                m_SceneDocument->SetStatus("Prefab instance overrides reverted.");
            }
            catch (const std::exception& error)
            {
                ReportError("Prefab", error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Unpack"))
        {
            try
            {
                RecordSceneUndo("Unpack Prefab");
                auto replacement = scene->Snapshot();
                if (!KeireEditor::UnpackPrefab(replacement, instanceRoot))
                    throw std::runtime_error("Prefab instance changed before the operation completed.");
                auto rebuilt =
                    Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
                rebuilt->MarkDirty();
                m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
                m_SceneDocument->SetStatus("Prefab instance unpacked.");
            }
            catch (const std::exception& error)
            {
                ReportError("Prefab", error.what());
            }
        }
        ui.SameLine();
        if (ui.Button("Unpack Completely"))
        {
            try
            {
                RecordSceneUndo("Unpack Prefab Completely");
                auto replacement = scene->Snapshot();
                if (!KeireEditor::UnpackPrefab(replacement, instanceRoot, true))
                    throw std::runtime_error("Prefab instance changed before the operation completed.");
                auto rebuilt =
                    Keire::CreateRef<Keire::Scene>(scene->Asset(), std::move(replacement), scene->Components());
                rebuilt->MarkDirty();
                m_SceneDocument->ReplaceEditingScene(std::move(rebuilt));
                m_SceneDocument->SetStatus("Prefab instance hierarchy unpacked.");
            }
            catch (const std::exception& error)
            {
                ReportError("Prefab", error.what());
            }
        }
        ui.TextColored(m_Theme.MutedText,
                       "Apply writes remain explicit: use prefab edit mode to modify the source asset.");
    }
}

void EditorWorkspaceLayer::DrawBuildSettings(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_BuildSettings); panel)
    {
        constexpr std::array Configurations{"Development", "Release", "Dist"};
        constexpr std::array Platforms{"Current desktop", "Windows x64", "Windows ARM64", "Linux x64",
                                       "Linux ARM64",     "macOS x64",   "macOS ARM64"};
        if (auto combo = ui.BeginCombo("Profile", Configurations[static_cast<std::size_t>(m_BuildConfiguration)]);
            combo)
        {
            for (std::size_t index = 0; index < Configurations.size(); ++index)
            {
                if (ui.Selectable(Configurations[index], m_BuildConfiguration == static_cast<int>(index)))
                    m_BuildConfiguration = static_cast<int>(index);
            }
        }
        if (auto combo = ui.BeginCombo("Platform", Platforms[static_cast<std::size_t>(m_BuildPlatform)]); combo)
        {
            for (std::size_t index = 0; index < Platforms.size(); ++index)
            {
                if (ui.Selectable(Platforms[index], m_BuildPlatform == static_cast<int>(index)))
                    m_BuildPlatform = static_cast<int>(index);
            }
        }
        (void)ui.Checkbox("Include symbols", m_BuildSymbols);
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
            std::string message =
                diagnostic.Code.empty() ? diagnostic.Message : diagnostic.Code + ": " + diagnostic.Message;
            if (!diagnostic.Source.empty())
                message = diagnostic.Source.generic_string() + ":" + std::to_string(diagnostic.Line) + " " + message;
            ui.TextColored(color, message);
        }
        const bool managedBusy = scriptStatus.State == Keire::ManagedBuildState::Generating ||
                                 scriptStatus.State == Keire::ManagedBuildState::Compiling ||
                                 scriptStatus.State == Keire::ManagedBuildState::Publishing;
        if (auto disabled = ui.BeginDisabled(!scripts || managedBusy || !m_AssetDatabase || !Owner().GetProject());
            disabled)
        {
            if (ui.Button("Build Managed"))
            {
                try
                {
                    Keire::ManagedBuildRequest request;
                    const auto projectRoot = Owner().GetProject()->Root();
                    for (const auto& record : m_AssetDatabase->Records())
                    {
                        if (record.Type != Keire::ManagedAssemblyAsset::StaticType())
                            continue;
                        const auto assembly = Keire::ManagedAssemblyAsset::Decode(
                            ReadBytes(projectRoot / "Assets" / record.RelativePath));
                        request.Assemblies.push_back({record.Id, assembly->Definition()});
                    }
                    if (request.Assemblies.empty())
                        throw std::runtime_error("The project contains no .keireasm assembly definitions.");
                    request.Configuration = m_BuildConfiguration == 0 ? "Debug" : "Release";
                    (void)scripts->StartBuild(std::move(request));
                }
                catch (const std::exception& error)
                {
                    ReportError("Managed Build", error.what());
                }
            }
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
                       "Native build, launch, packaging, and debugger-wait commands use Scripts/project launchers.");
    }
}

void EditorWorkspaceLayer::UpdateManagedBuild()
{
    const auto scripts = Owner().Scripts();
    if (!scripts)
        return;
    const auto status = scripts->BuildStatus();
    if (status.State != Keire::ManagedBuildState::Succeeded || !status.Operation ||
        status.Operation == m_LastManagedReload)
    {
        return;
    }
    m_LastManagedReload = status.Operation;
    try
    {
        Keire::ManagedReloadRequest reload;
        for (const auto& entry : std::filesystem::directory_iterator(status.ActiveAssemblyDirectory))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".dll" &&
                entry.path().filename() != "Keire.Managed.dll")
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
        scripts->InstallManagedComponents(Owner().Scenes()->Components());
        AddConsoleMessage("Managed", "Gameplay assemblies reloaded at a scene safe boundary.", m_Theme.Success);
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

        const auto frame = profiler->LatestFrame();
        ui.Text("Capture " + std::to_string(frame.Sequence));
        if (frame.Truncated)
            ui.TextColored(m_Theme.Warning, "Capture was truncated by the configured limits.");
        ui.Separator();
        if (auto spans = ui.BeginTreeNode("CPU spans (" + std::to_string(frame.Spans.size()) + ")", true); spans)
        {
            auto ordered = frame.Spans;
            std::ranges::sort(ordered, std::greater{}, &Keire::ProfileSpan::DurationMicroseconds);
            for (const auto& span : ordered)
                ui.Text(ProfileCategoryName(span.Category) + " / " + span.Name + "  " +
                        FormatMicroseconds(span.DurationMicroseconds));
        }
        if (auto counters = ui.BeginTreeNode("Counters (" + std::to_string(frame.Counters.size()) + ")", true);
            counters)
        {
            for (const auto& counter : frame.Counters)
                ui.Text(ProfileCategoryName(counter.Category) + " / " + counter.Name + "  " +
                        std::to_string(counter.Value));
        }

        ui.Separator();
        if (const auto renderer = Owner().Renderer())
        {
            const auto statistics = renderer->Statistics();
            ui.Text("Rendering: " + std::to_string(statistics.DrawCalls) + " draws, " +
                    std::to_string(statistics.Triangles) + " triangles");
        }
        if (const auto assets = Owner().Assets())
        {
            const auto statistics = assets->Statistics();
            ui.Text("Assets: " + std::to_string(statistics.KnownAssets) + " known, " +
                    std::to_string(statistics.ResidentBytes) + " bytes");
        }
        if (const auto audio = Owner().Audio())
        {
            const auto statistics = audio->Statistics();
            ui.Text("Audio: " + std::to_string(statistics.AudibleVoices) + " audible / " +
                    std::to_string(statistics.Voices) + " voices");
            if (statistics.Underruns != 0)
                ui.TextColored(m_Theme.Warning, std::to_string(statistics.Underruns) + " audio underruns");
        }
    }
}
