#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KeireEditor
{
    struct EditorReplayState final
    {
        std::string Path;
        std::int64_t SeekTick = 0;
        bool PerformanceProfile = false;
        std::string ActionStatus;
    };

    struct EditorProfilerPresentation final
    {
        std::uint64_t FrameSequence = 0;
        double FramesPerSecond = 0.0;
        double AverageFrameMicroseconds = 0.0;
        double AverageFramesPerSecond = 0.0;
        double P95FrameMicroseconds = 0.0;
        double P99FrameMicroseconds = 0.0;
        double MaximumFrameMicroseconds = 0.0;
        double OnePercentLow = 0.0;
        std::size_t StutterCount = 0;
        std::string FrameLine;
        std::string HistoryLine;
        std::string TailLine;
        std::vector<Keire::ProfileSpan> OrderedSpans;
        std::vector<Keire::ProfileSpan> TimelineSpans;
        std::vector<std::string> SpanLines;
        std::vector<std::string> TimelineLines;
        std::vector<std::string> ThreadLines;
        std::vector<std::string> CounterLines;
        std::vector<std::string> ManagedCallbackLines;
        bool ManagedCallbacksTruncated = false;
    };

    struct EditorProfilerState final
    {
        bool Paused = false;
        bool ShowAllManagedCallbacks = false;
        bool ShowAllHotspots = false;
        bool ShowAllCounters = false;
        Keire::ProfileFrame CachedFrame;
        std::vector<Keire::ProfileFrameSummary> CachedHistory;
        Keire::ProfileFrame FrozenFrame;
        std::vector<Keire::ProfileFrameSummary> FrozenHistory;
        EditorProfilerPresentation Presentation;
    };

    class EditorReplayProfilingCoordinator final
    {
      public:
        EditorReplayProfilingCoordinator() = default;
        ~EditorReplayProfilingCoordinator() noexcept;

        EditorReplayProfilingCoordinator(const EditorReplayProfilingCoordinator&) = delete;
        EditorReplayProfilingCoordinator& operator=(const EditorReplayProfilingCoordinator&) = delete;
        EditorReplayProfilingCoordinator(EditorReplayProfilingCoordinator&&) = delete;
        EditorReplayProfilingCoordinator& operator=(EditorReplayProfilingCoordinator&&) = delete;

        [[nodiscard]] EditorReplayState& Replay();
        [[nodiscard]] EditorProfilerState& Profiler();
        [[nodiscard]] const EditorReplayState& Replay() const;
        [[nodiscard]] const EditorProfilerState& Profiler() const;
        void Shutdown() noexcept;

        [[nodiscard]] EditorWorkspaceCallbackToken CaptureCallbackToken() const;
        [[nodiscard]] bool ShutdownComplete() const;

      private:
        EditorCoordinatorLifetime m_Lifetime{"Editor replay/profiling coordinator"};
        EditorReplayState m_Replay;
        EditorProfilerState m_Profiler;
    };
} // namespace KeireEditor
