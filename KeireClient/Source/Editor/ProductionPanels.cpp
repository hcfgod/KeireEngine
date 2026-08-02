#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/PrefabAuthoring.h"
#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ranges>
#include <set>
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
        case Keire::PrefabOverrideKind::SetObjectLayer:
            return "Set layer";
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
        if (m_PrefabEditingStage)
        {
            ui.Text("Prefab Mode");
            ui.Text(m_PrefabEditingStage->RelativePath.generic_string());
            ui.TextColored(m_Theme.MutedText,
                           "This isolated stage does not modify the open scene. Save publishes the prefab source; "
                           "Discard restores the previous scene document.");
            if (ui.Button("Save Prefab"))
            {
                try
                {
                    SavePrefabEditingStage();
                }
                catch (const std::exception& error)
                {
                    ReportError("Prefab", error.what());
                }
            }
            ui.SameLine();
            if (ui.Button("Save and Close"))
            {
                try
                {
                    SavePrefabEditingStage();
                    ClosePrefabEditingStage();
                }
                catch (const std::exception& error)
                {
                    ReportError("Prefab", error.what());
                }
            }
            ui.SameLine();
            if (ui.Button("Discard and Close"))
                ClosePrefabEditingStage();
            ui.Separator();
            ui.Text("Edit prefab objects in the Scene viewport and Inspector.");
            return;
        }

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
        if (ui.Button("Apply to Prefab"))
        {
            try
            {
                ApplySelectedPrefabOverrides();
            }
            catch (const std::exception& error)
            {
                ReportError("Prefab", error.what());
            }
        }
        ui.SameLine();
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
        ui.TextColored(
            m_Theme.MutedText,
            "Root placement is scene-owned. Nested roots require a variant so source ownership is preserved.");
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
                       "Native build, launch, packaging, and debugger-wait commands use Scripts/project launchers.");
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
    request.Configuration = m_BuildConfiguration == 0 ? "Debug" : "Release";
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
            presentation.StutterCount =
                std::ranges::count_if(frameTimes, [&](const double value) { return value > stutterThreshold; });
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
                ui.Text("Audio voices " + std::to_string(statistics.AudibleVoices) + " audible / " +
                        std::to_string(statistics.VirtualVoices) + " virtual / " + std::to_string(statistics.Voices) +
                        " allocated");
                ui.Text("Audio frames " + std::to_string(statistics.RenderedFrames));
                if (statistics.Underruns != 0)
                    ui.TextColored(m_Theme.Warning, std::to_string(statistics.Underruns) + " output underruns");
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
