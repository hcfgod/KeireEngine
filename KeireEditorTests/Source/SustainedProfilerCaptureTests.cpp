#include "KeireClient/Editor/SustainedProfilerCapture.h"
#include <doctest/doctest.h>
#include <limits>

namespace
{
    Keire::ProfileFrameSummary Frame(std::uint64_t sequence, double start, double body = 500)
    {
        Keire::ProfileFrameSummary result;
        result.Sequence = sequence;
        result.StartMicroseconds = start;
        result.DurationMicroseconds = body;
        return result;
    }
} // namespace
TEST_CASE("Sustained profiler records start intervals including admission waits")
{
    KeireEditor::SustainedProfilerCapture capture;
    capture.Start(10, Frame(1, 0), 0);
    for (std::uint64_t i = 2; i <= 5002; ++i)
        capture.Observe(Frame(i, (i - 1) * 2000.0));
    CHECK(capture.Status() == KeireEditor::SustainedProfilerCapture::State::Complete);
    CHECK(capture.ElapsedMicroseconds() == 10000000);
    CHECK(capture.Frames().size() == 5001);
    CHECK(capture.Csv().find("average_application_fps=500") != std::string::npos);
    CHECK(capture.Csv().find("p99_us=2000") != std::string::npos);
}
TEST_CASE("Sustained profiler bounds warmup duration gaps and failure lifecycle")
{
    using Capture = KeireEditor::SustainedProfilerCapture;
    Capture capture(40000);
    capture.Start(30, Frame(1, 0), 2);
    CHECK(capture.Csv().empty());
    capture.Observe(Frame(1, 0));
    for (std::uint64_t i = 2; i <= 32002; ++i)
        capture.Observe(Frame(i, (i - 1) * 1000.0));
    CHECK(capture.Status() == Capture::State::Complete);
    CHECK(capture.Frames().front().StartMicroseconds == 2001000);
    CHECK(capture.ElapsedMicroseconds() == 30000000);
    SUBCASE("sequence gap fails closed")
    {
        capture.Start(10, Frame(1, 0), 0);
        capture.Observe(Frame(3, 2000));
        CHECK(capture.Status() == Capture::State::Gap);
    }
    SUBCASE("clock regression fails closed")
    {
        capture.Start(10, Frame(1, 1000), 0);
        capture.Observe(Frame(2, 500));
        CHECK(capture.Status() == Capture::State::InvalidClock);
    }
    SUBCASE("nonfinite fails closed")
    {
        capture.Start(10, Frame(1, 0), 0);
        capture.Observe(Frame(2, std::numeric_limits<double>::infinity()));
        CHECK(capture.Status() == Capture::State::InvalidClock);
    }
    SUBCASE("capacity is never silently truncated")
    {
        Capture small(1);
        small.Start(10, Frame(1, 0), 0);
        small.Observe(Frame(2, 1000));
        small.Observe(Frame(3, 2000));
        CHECK(small.Status() == Capture::State::Overflow);
        CHECK(small.Frames().size() == 1);
    }
    SUBCASE("stop and restart")
    {
        capture.Start(10, Frame(1, 0));
        capture.Stop();
        capture.Stop();
        CHECK(capture.Status() == Capture::State::Stopped);
        capture.Start(10, Frame(1, 0));
        CHECK(capture.Frames().empty());
    }
    CHECK_THROWS_AS(capture.Start(11, Frame(1, 0)), std::invalid_argument);
}

TEST_CASE("Sustained profiler preserves exact interval thresholds and rejects partial evidence")
{
    using Capture = KeireEditor::SustainedProfilerCapture;
    Capture capture;
    capture.Start(10, Frame(1, 0), 0);
    capture.Observe(Frame(2, 1000));
    capture.Observe(Frame(2, 1000));
    CHECK(capture.Frames().size() == 1);
    capture.Observe(Frame(3, 3000));
    capture.Observe(Frame(4, 5500));
    capture.Observe(Frame(5, 8501));
    capture.Stop();
    const auto csv = capture.Csv();
    CHECK(csv.find("status=Stopped: incomplete") != std::string::npos);
    CHECK(csv.find("frames_above_2ms=2") != std::string::npos);
    CHECK(csv.find("frames_above_2_5ms=1") != std::string::npos);
    CHECK(csv.find("p99_us=3001") != std::string::npos);
    capture.Start(10, Frame(1, 0), 0);
    capture.Observe(Frame(3, 2000));
    REQUIRE(capture.Status() == Capture::State::Gap);
    capture.Start(10, Frame(10, 20000), 0);
    CHECK(capture.Status() == Capture::State::Warming);
    CHECK(capture.Frames().empty());
    capture.Observe(Frame(11, 21000));
    CHECK(capture.Status() == Capture::State::Recording);
    CHECK_THROWS_AS(capture.Start(10, Frame(0, 0)), std::invalid_argument);
    CHECK(capture.Frames().size() == 1);
}

TEST_CASE("Sustained profiler rejects computed deadline overflow transactionally")
{
    KeireEditor::SustainedProfilerCapture capture;
    capture.Start(10, Frame(1, 0), 0);
    capture.Observe(Frame(2, 1000));
    CHECK_THROWS_AS(capture.Start(10, Frame(3, 2000), std::numeric_limits<double>::max()), std::invalid_argument);
    CHECK(capture.Status() == KeireEditor::SustainedProfilerCapture::State::Recording);
    REQUIRE(capture.Frames().size() == 1);
    CHECK(capture.Frames().front().Sequence == 2);
    capture.Stop();
    CHECK(capture.Status() == KeireEditor::SustainedProfilerCapture::State::Stopped);
    CHECK(capture.Csv().find("Stopped: incomplete") != std::string::npos);
}

TEST_CASE("Sustained profiler renderer observations remain optional asynchronous and bounded")
{
    KeireEditor::SustainedProfilerCapture capture(3);
    capture.Start(10, Frame(1, 0), 0);
    CHECK(capture.Observe(Frame(2, 1000)));
    CHECK_FALSE(capture.RendererTelemetryEnabled());
    CHECK(capture.RendererObservations().empty());
    CHECK_THROWS_AS(capture.ObserveRenderer(2, nullptr), std::logic_error);
    capture.Start(10, Frame(1, 0), 0, true);
    CHECK_FALSE(capture.Observe(Frame(1, 0)));
    CHECK(capture.RendererObservations().empty());
    CHECK(capture.Observe(Frame(2, 1000)));
    Keire::RenderStatistics statistics;
    statistics.Frame = 71;
    statistics.GpuTimingFrame = 68;
    statistics.GpuTimingSupported = true;
    statistics.GpuFrameMilliseconds = 1.5F;
    statistics.FrameAdmissionWaitMilliseconds = 2.0F;
    statistics.CpuPreparationMilliseconds = 3.5F;
    CHECK_THROWS_AS(capture.ObserveRenderer(3, &statistics), std::logic_error);
    capture.ObserveRenderer(2, &statistics);
    CHECK_THROWS_AS(capture.ObserveRenderer(2, &statistics), std::logic_error);
    CHECK(capture.RendererObservations()[0].Frame == 71);
    CHECK(capture.RendererObservations()[0].GpuTimingFrame == 68);
    CHECK(capture.RendererObservations()[0].Admission == 2.0F);
    CHECK(capture.RendererObservations()[0].CpuPreparation == 3.5F);
    CHECK(capture.Csv().empty());
    CHECK(capture.Observe(Frame(3, 2000)));
    capture.ObserveRenderer(3, nullptr);
    CHECK_FALSE(capture.RendererObservations()[1].Available);
    CHECK(capture.Observe(Frame(4, 3000)));
    statistics.GpuTimingSupported = false;
    capture.ObserveRenderer(4, &statistics);
    CHECK_FALSE(capture.Observe(Frame(5, 4000)));
    CHECK(capture.Status() == KeireEditor::SustainedProfilerCapture::State::Overflow);
    CHECK(capture.RendererObservations().size() == capture.Frames().size());
    CHECK(capture.Csv().find("asynchronous_observation_not_application_frame_join") != std::string::npos);
    CHECK(capture.Csv().find("observed_renderer_frame,observed_gpu_timing_frame") != std::string::npos);
    capture.Start(10, Frame(10, 10000), 0, false);
    CHECK(capture.RendererObservations().empty());
    CHECK_FALSE(capture.RendererTelemetryEnabled());
}
TEST_CASE("Sustained profiler renderer warmup stop and failed restart preserve observation ownership")
{
    KeireEditor::SustainedProfilerCapture capture;
    capture.Start(10, Frame(1, 0), 2, true);
    CHECK_FALSE(capture.Observe(Frame(2, 1000)));
    CHECK(capture.RendererObservations().empty());
    CHECK_THROWS_AS(capture.Start(11, Frame(2, 1000), 0, false), std::invalid_argument);
    CHECK(capture.RendererTelemetryEnabled());
    CHECK(capture.Observe(Frame(3, 2000500)));
    capture.Stop();
    CHECK_THROWS_AS(capture.ObserveRenderer(3, nullptr), std::logic_error);
    CHECK_FALSE(capture.RendererObservations()[0].Available);
    CHECK_FALSE(capture.Observe(Frame(4, 2001500)));
    CHECK(capture.RendererObservations().size() == 1);
}

TEST_CASE("Sustained profiler rejects renderer attachment after terminal observation failures")
{
    using Capture = KeireEditor::SustainedProfilerCapture;
    for (int failure = 0; failure < 3; ++failure)
    {
        Capture capture(1);
        capture.Start(10, Frame(1, 0), 0, true);
        REQUIRE(capture.Observe(Frame(2, 1000)));
        const auto next = failure == 0 ? Frame(4, 2000) : failure == 1 ? Frame(3, 0) : Frame(3, 2000);
        CHECK_FALSE(capture.Observe(next));
        CHECK_THROWS_AS(capture.ObserveRenderer(2, nullptr), std::logic_error);
        CHECK_FALSE(capture.RendererObservations()[0].Available);
    }
    Capture capture;
    capture.Start(10, Frame(1, 0), 0, true);
    REQUIRE(capture.Observe(Frame(2, 1000)));
    Keire::RenderStatistics statistics;
    statistics.Frame = 9;
    statistics.GpuTimingSupported = true;
    statistics.GpuTimingFrame = 0;
    statistics.GpuFrameMilliseconds = 123;
    capture.ObserveRenderer(2, &statistics);
    CHECK(capture.RendererObservations()[0].Available);
    CHECK(capture.RendererObservations()[0].GpuTimingSupported);
    capture.Stop();
    CHECK(capture.Csv().find(",9,0,1,0,0,0,0,,0,0") != std::string::npos);
}
