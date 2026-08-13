#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/ReplayPanelState.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace
{
    [[nodiscard]] std::string FormatBytes(const std::uint64_t bytes)
    {
        constexpr std::uint64_t Kibibyte = 1024U;
        constexpr std::uint64_t Mebibyte = Kibibyte * 1024U;
        constexpr std::uint64_t Gibibyte = Mebibyte * 1024U;
        std::ostringstream result;
        result << std::fixed << std::setprecision(1);
        if (bytes >= Gibibyte)
            result << static_cast<double>(bytes) / static_cast<double>(Gibibyte) << " GiB";
        else if (bytes >= Mebibyte)
            result << static_cast<double>(bytes) / static_cast<double>(Mebibyte) << " MiB";
        else if (bytes >= Kibibyte)
            result << static_cast<double>(bytes) / static_cast<double>(Kibibyte) << " KiB";
        else
            result << bytes << " B";
        return result.str();
    }

    [[nodiscard]] std::string_view StreamingClassName(const Keire::StreamingClass value) noexcept
    {
        switch (value)
        {
        case Keire::StreamingClass::General:
            return "General";
        case Keire::StreamingClass::Texture:
            return "Textures";
        case Keire::StreamingClass::Mesh:
            return "Meshes";
        case Keire::StreamingClass::Audio:
            return "Audio";
        case Keire::StreamingClass::Animation:
            return "Animation";
        }
        return "Unknown";
    }
} // namespace

void EditorWorkspaceLayer::DrawArchitectureDashboard(Keire::UiFrame& ui)
{
    if (auto panel = ui.BeginPanel(m_ArchitectureDashboard); panel)
    {
        if (const auto jobs = Owner().Jobs())
        {
            if (auto section = ui.BeginTreeNode("Jobs", true); section)
            {
                const auto statistics = jobs->Statistics();
                ui.Text(std::to_string(statistics.WorkerCount) + " compute workers / " +
                        std::to_string(statistics.BlockingWorkerCount) + " blocking workers");
                ui.Text(std::to_string(statistics.WaitingJobs) + " waiting / " + std::to_string(statistics.QueuedJobs) +
                        " queued / " + std::to_string(statistics.RunningJobs) + " running");
                ui.Text(std::to_string(statistics.SubmittedJobs) + " submitted / " +
                        std::to_string(statistics.CompletedJobs) + " completed / " +
                        std::to_string(statistics.FailedJobs) + " failed / " +
                        std::to_string(statistics.CancelledJobs) + " cancelled");
                ui.Text(std::to_string(statistics.StolenJobs) + " stolen / high-water " +
                        std::to_string(statistics.QueueHighWaterMark));
            }
        }

        if (const auto memory = Owner().Memory())
        {
            if (auto section = ui.BeginTreeNode("Memory domains", true); section)
            {
                const auto snapshot = memory->Snapshot();
                for (const auto& domain : snapshot.Domains)
                {
                    ui.Text(domain.Name + "  retained " + FormatBytes(domain.RetainedBytes) + " / peak " +
                            FormatBytes(domain.PeakBytes) + " / external " + FormatBytes(domain.ExternalBytes) +
                            " / allocations " + std::to_string(domain.AllocationCount));
                }
            }
        }

        if (const auto streaming = Owner().Streaming())
        {
            if (auto section = ui.BeginTreeNode("Streaming budgets", true); section)
            {
                for (const auto& statistics : streaming->Statistics())
                {
                    ui.Text(std::string(StreamingClassName(statistics.Class)) + "  resident CPU " +
                            FormatBytes(statistics.ResidentCpuBytes) + " / GPU " +
                            FormatBytes(statistics.ResidentGpuBytes));
                    ui.Text("  requested " + FormatBytes(statistics.RequestedBytes) + " / in flight " +
                            FormatBytes(statistics.InFlightBytes) + " / retired " +
                            FormatBytes(statistics.RetiredCpuBytes + statistics.RetiredGpuBytes));
                    ui.Text("  completed " + std::to_string(statistics.CompletedRequests) + " / cancelled " +
                            std::to_string(statistics.CancelledRequests) + " / misses " +
                            std::to_string(statistics.Misses));
                    ui.Text("  evictions " + std::to_string(statistics.Evictions) + " / failures " +
                            std::to_string(statistics.Failures) + " / successful latency " +
                            std::to_string(statistics.AverageLatencyMilliseconds) + " ms");
                    if (statistics.AudioUnderruns != 0)
                        ui.TextColored(m_Theme.Warning,
                                       "  audio underruns " + std::to_string(statistics.AudioUnderruns));
                }
            }
        }

        if (const auto renderer = Owner().Renderer())
        {
            if (auto section = ui.BeginTreeNode("Frame-safe resources", true); section)
            {
                const auto statistics = renderer->Statistics();
                ui.Text("Active transient  " + FormatBytes(statistics.ActiveTransientBytes));
                ui.Text("Theoretical unaliased  " + FormatBytes(statistics.TheoreticalUnaliasedBytes));
                ui.Text("Saved by aliasing  " + FormatBytes(statistics.SavedAliasingBytes));
                ui.Text("Fence-retired  " + FormatBytes(statistics.FenceRetiredBytes));
            }
        }

        if (const auto modules = Owner().Modules())
        {
            if (auto section = ui.BeginTreeNode("Source modules", false); section)
            {
                const auto catalog = modules->OrderedCatalog();
                if (catalog.empty())
                    ui.TextColored(m_Theme.MutedText, "No project source modules are linked.");
                for (const auto& module : catalog)
                    ui.Text(module.Id + " @ " + module.Version.ToString());
            }
        }

        if (const auto definitions = Owner().DiagnosticDefinitions())
        {
            if (auto section = ui.BeginTreeNode("Structured diagnostics", false); section)
            {
                const auto diagnostics = Owner().DiagnosticReports();
                ui.Text(std::to_string(definitions->Definitions().size()) + " definitions / " +
                        std::to_string(diagnostics ? diagnostics->Snapshot().size() : 0) + " retained reports / " +
                        std::to_string(diagnostics ? diagnostics->DroppedCount() : 0) + " dropped");
            }
        }

        if (const auto replay = Owner().Replay())
        {
            auto section = ui.BeginTreeNode("Replay and verification", true);
            if (!section)
                return;
            if (m_ReplayPath.empty())
            {
                const auto root = Owner().GetProject() ? Owner().GetProject()->Root() : std::filesystem::path(".");
                m_ReplayPath = (root / "Library" / "Replays" / "editor.keirereplay").generic_string();
            }
            const auto status = replay->Status();
            ui.Text(std::string(KeireEditor::Detail::ReplayStateName(status.State)) + "  tick " +
                    std::to_string(status.CurrentTick) + " / " + std::to_string(status.TickCount) + "  checkpoints " +
                    std::to_string(status.CheckpointCount));
            (void)ui.InputText("Replay file", m_ReplayPath);
            (void)ui.Checkbox("Performance capture profile", m_ReplayPerformanceProfile);

            const auto fingerprints = [&]
            {
                Keire::ReplayFingerprints result;
                const auto& build = Keire::GetBuildInfo();
                result.EngineBuild = std::string(build.Version) + '|' + std::string(build.Configuration) + '|' +
                                     std::string(build.Platform) + '|' + std::string(build.Architecture);
                if (const auto project = Owner().GetProject())
                {
                    result.Project = project->Descriptor().Id.ToString();
                    result.Content = project->Descriptor().StartupScene.ToString() + '|' +
                                     project->Descriptor().DefaultInput.ToString();
                }
                if (const auto modules = Owner().Modules())
                    result.Modules = modules->Fingerprint();
                result.DeterministicConfiguration = "fixed-input-v1|stable-simulation-v1";
                return result;
            };

            if (ui.Button("Record"))
            {
                try
                {
                    Keire::ReplayRecordRequest request;
                    request.Path = m_ReplayPath;
                    request.Profile = m_ReplayPerformanceProfile ? Keire::ReplayProfile::PerformanceCapture
                                                                 : Keire::ReplayProfile::StrictVerified;
                    request.Fingerprints = fingerprints();
                    replay->BeginRecording(std::move(request));
                    m_ReplayActionStatus = "Recording started.";
                }
                catch (const std::exception& error)
                {
                    m_ReplayActionStatus = error.what();
                }
            }
            ui.SameLine();
            if (ui.Button("Play"))
            {
                try
                {
                    replay->BeginPlayback({m_ReplayPath, fingerprints(), false});
                    m_ReplayActionStatus = "Playback started.";
                }
                catch (const std::exception& error)
                {
                    m_ReplayActionStatus = error.what();
                }
            }
            ui.SameLine();
            if (ui.Button("Verify"))
            {
                try
                {
                    replay->BeginPlayback({m_ReplayPath, fingerprints(), true});
                    m_ReplayActionStatus = "Verification started.";
                }
                catch (const std::exception& error)
                {
                    m_ReplayActionStatus = error.what();
                }
            }
            ui.SameLine();
            if (ui.Button("Stop"))
            {
                try
                {
                    replay->Stop();
                    m_ReplayActionStatus = "Replay session stopped.";
                }
                catch (const std::exception& error)
                {
                    m_ReplayActionStatus = error.what();
                }
            }
            const bool paused = status.State == Keire::ReplaySessionState::Paused;
            const auto runReplayAction = [this](auto&& action)
            {
                try
                {
                    m_ReplayActionStatus = action();
                }
                catch (const std::exception& error)
                {
                    m_ReplayActionStatus = error.what();
                }
            };
            const bool canTogglePause = KeireEditor::Detail::CanToggleReplayPause(status.State);
            if (auto disabled = ui.BeginDisabled(!canTogglePause); disabled)
            {
                if (ui.Button(paused ? "Resume" : "Pause"))
                {
                    runReplayAction(
                        [&]
                        {
                            replay->Pause(!paused);
                            return paused ? "Replay session resumed." : "Replay session paused.";
                        });
                }
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(!KeireEditor::Detail::CanStepReplay(status.State)); disabled)
            {
                if (ui.Button("Step"))
                {
                    runReplayAction(
                        [&]
                        {
                            replay->Step();
                            return "One replay tick requested.";
                        });
                }
            }
            (void)ui.DragInteger("Seek tick", m_ReplaySeekTick, 1.0, 0);
            ui.SameLine();
            const bool canSeek = KeireEditor::Detail::CanSeekReplay(status.State);
            if (auto disabled = ui.BeginDisabled(!canSeek); disabled)
            {
                if (ui.Button("Seek"))
                {
                    runReplayAction(
                        [&]
                        {
                            return replay->Seek(static_cast<std::uint64_t>(m_ReplaySeekTick))
                                       ? "Checkpoint restored."
                                       : "No checkpoint is available for that tick.";
                        });
                }
            }
            if (status.Divergence)
                ui.TextColored(m_Theme.Error, "Diverged at tick " + std::to_string(status.Divergence->Tick) + ": " +
                                                  status.Divergence->Message);
            if (!status.Diagnostic.empty())
                ui.TextColored(m_Theme.Warning, status.Diagnostic);
            if (!m_ReplayActionStatus.empty())
                ui.Text(m_ReplayActionStatus);
        }
    }
}
