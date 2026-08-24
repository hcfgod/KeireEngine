#include "Keire/Replay/ReplaySystem.h"
#include "Keire/Time.h"

#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace
{
    class ReplayDirectory final
    {
      public:
        ReplayDirectory() : Path(std::filesystem::path("Build") / "ReplayTests")
        {
            std::filesystem::remove_all(Path);
            std::filesystem::create_directories(Path);
        }

        ~ReplayDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    [[nodiscard]] Keire::ReplaySerializerRegistration IntegerSerializer(int& value, const bool deterministic = true)
    {
        return {.Id = "test.integer",
                .Version = 1,
                .Deterministic = deterministic,
                .Capture =
                    [&value]
                {
                    std::vector<std::byte> bytes(sizeof(value));
                    std::memcpy(bytes.data(), &value, sizeof(value));
                    return bytes;
                },
                .Restore =
                    [&value](const std::span<const std::byte> bytes)
                {
                    REQUIRE(bytes.size() == sizeof(value));
                    std::memcpy(&value, bytes.data(), sizeof(value));
                }};
    }

    [[nodiscard]] std::uint32_t ReadLittleU32(const std::vector<char>& bytes, const std::size_t offset)
    {
        std::uint32_t result = 0;
        for (std::size_t index = 0; index < sizeof(result); ++index)
            result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + index])) << (index * 8U);
        return result;
    }

    [[nodiscard]] std::uint64_t ReadLittleU64(const std::vector<char>& bytes, const std::size_t offset)
    {
        std::uint64_t result = 0;
        for (std::size_t index = 0; index < sizeof(result); ++index)
            result |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset + index])) << (index * 8U);
        return result;
    }

    void WriteLittleU64(std::vector<char>& bytes, const std::size_t offset, const std::uint64_t value)
    {
        for (std::size_t index = 0; index < sizeof(value); ++index)
            bytes[offset + index] = static_cast<char>(value >> (index * 8U));
    }

    [[nodiscard]] std::size_t FindChunk(const std::vector<char>& bytes, const std::uint32_t wantedType)
    {
        constexpr std::size_t fileHeaderBytes = 12U + sizeof(std::uint32_t);
        constexpr std::size_t chunkHeaderBytes = sizeof(std::uint32_t) * 2U + sizeof(std::uint64_t) * 3U + 32U;
        std::size_t offset = fileHeaderBytes;
        while (offset <= bytes.size() && chunkHeaderBytes <= bytes.size() - offset)
        {
            const auto type = ReadLittleU32(bytes, offset);
            if (type == wantedType)
                return offset;
            const auto storedBytes = ReadLittleU64(bytes, offset + 24U);
            if (storedBytes > bytes.size() - offset - chunkHeaderBytes)
                break;
            offset += chunkHeaderBytes + static_cast<std::size_t>(storedBytes);
        }
        return bytes.size();
    }
} // namespace

TEST_CASE("replay records fixed input checkpoints and verifies canonical state")
{
    ReplayDirectory directory;
    const auto path = directory.Path / "verified.keirereplay";
    int state = 0;
    auto recorder = Keire::CreateRef<Keire::ReplaySystem>();
    recorder->RegisterSerializer(IntegerSerializer(state));
    recorder->BeginRecording(
        {path,
         Keire::ReplayProfile::StrictVerified,
         {.EngineBuild = "build", .Project = "project", .Modules = "modules", .Content = "content"}});
    for (std::uint64_t tick = 1; tick <= 3; ++tick)
    {
        Keire::FixedTickInputSnapshot input{.Tick = tick, .InputMapFingerprint = 77};
        input.Controls.push_back({Keire::InputDeviceId(1),
                                  "<Keyboard>/e",
                                  {Keire::InputValueType::Boolean, tick == 2 ? 1.0F : 0.0F, 0.0F},
                                  tick == 2,
                                  tick == 3});
        const auto recorded = recorder->BeginFixedTick(input);
        CHECK(recorded.Tick == tick);
        state = static_cast<int>(tick);
        recorder->EndFixedTick(tick);
    }
    recorder->Stop();
    CHECK(std::filesystem::file_size(path) > 64);

    state = 99;
    auto playback = Keire::CreateRef<Keire::ReplaySystem>();
    playback->RegisterSerializer(IntegerSerializer(state));
    playback->BeginPlayback(
        {path, {.EngineBuild = "build", .Project = "project", .Modules = "modules", .Content = "content"}, true});
    CHECK(state == 0);
    for (std::uint64_t tick = 1; tick <= 3; ++tick)
    {
        const auto input = playback->BeginFixedTick({.Tick = tick, .InputMapFingerprint = 77});
        CHECK(input.Tick == tick);
        REQUIRE(input.Controls.size() == 1);
        CHECK(input.Controls.front().Path == "<Keyboard>/e");
        CHECK(input.Controls.front().Value.AsBoolean() == (tick == 2));
        CHECK(input.Controls.front().Pressed == (tick == 2));
        CHECK(input.Controls.front().Released == (tick == 3));
        state = static_cast<int>(tick);
        playback->EndFixedTick(tick);
    }
    CHECK(playback->Status().State == Keire::ReplaySessionState::Completed);
    playback->Stop();
}

TEST_CASE("replay rejects nondeterministic strict serializers and corrupt chunks")
{
    ReplayDirectory directory;
    const auto path = directory.Path / "capture.keirereplay";
    int state = 0;
    auto recorder = Keire::CreateRef<Keire::ReplaySystem>();
    recorder->RegisterSerializer(IntegerSerializer(state, false));
    CHECK_THROWS_AS(recorder->BeginRecording({path}), std::logic_error);

    auto valid = Keire::CreateRef<Keire::ReplaySystem>();
    valid->RegisterSerializer(IntegerSerializer(state));
    valid->BeginRecording({path});
    (void)valid->BeginFixedTick({.Tick = 1, .InputMapFingerprint = 1});
    state = 1;
    valid->EndFixedTick(1);
    valid->Stop();

    std::ifstream input(path, std::ios::binary);
    std::vector<char> bytes(std::istreambuf_iterator<char>(input), {});
    input.close();
    REQUIRE(bytes.size() > 80);
    bytes[bytes.size() / 2] ^= 0x5a;
    const auto corrupt = directory.Path / "corrupt.keirereplay";
    std::ofstream output(corrupt, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();

    auto reader = Keire::CreateRef<Keire::ReplaySystem>();
    reader->RegisterSerializer(IntegerSerializer(state));
    CHECK_THROWS_AS(reader->BeginPlayback({corrupt}), std::runtime_error);
}

TEST_CASE("replay seek restores a checkpoint and deterministically advances to the requested tick")
{
    ReplayDirectory directory;
    const auto path = directory.Path / "seek.keirereplay";
    int state = 0;
    Keire::ReplaySystemSpecification specification;
    specification.CheckpointIntervalTicks = 2;
    specification.RewindBudgetBytes = 1024;
    auto recorder = Keire::CreateRef<Keire::ReplaySystem>(specification);
    recorder->RegisterSerializer(IntegerSerializer(state));
    recorder->BeginRecording({path});
    for (std::uint64_t tick = 1; tick <= 4; ++tick)
    {
        (void)recorder->BeginFixedTick({.Tick = tick});
        state = static_cast<int>(tick);
        recorder->EndFixedTick(tick);
    }
    recorder->Stop();

    state = 99;
    auto playback = Keire::CreateRef<Keire::ReplaySystem>(specification);
    playback->RegisterSerializer(IntegerSerializer(state));
    playback->BeginPlayback({path});
    playback->Pause(true);
    REQUIRE(playback->Seek(3));
    CHECK(state == 2);
    CHECK(playback->ShouldAdvanceFixedTick());
    const auto input = playback->BeginFixedTick({});
    CHECK(input.Tick == 3);
    state = 3;
    playback->EndFixedTick(101);
    CHECK(playback->Status().CurrentTick == 3);
    CHECK_FALSE(playback->ShouldAdvanceFixedTick());
}

TEST_CASE("paused replay discards host fixed steps and commits exactly one clock tick when stepped")
{
    ReplayDirectory directory;
    const auto path = directory.Path / "paused-clock.keirereplay";
    int state = 0;
    auto recorder = Keire::CreateRef<Keire::ReplaySystem>();
    recorder->RegisterSerializer(IntegerSerializer(state));
    recorder->BeginRecording({path});
    for (std::uint64_t tick = 1; tick <= 2; ++tick)
    {
        (void)recorder->BeginFixedTick({.Tick = tick});
        state = static_cast<int>(tick);
        recorder->EndFixedTick(tick);
    }
    recorder->Stop();

    auto playback = Keire::CreateRef<Keire::ReplaySystem>();
    playback->RegisterSerializer(IntegerSerializer(state));
    playback->BeginPlayback({path});
    CHECK_THROWS_AS(playback->Step(), std::logic_error);
    CHECK(playback->Status().State == Keire::ReplaySessionState::Playing);
    playback->Pause(true);

    Keire::Time time;
    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(100.0));
    REQUIRE_FALSE(playback->ShouldAdvanceFixedTick());
    CHECK(time.DiscardFixedSteps() == 6);
    CHECK(time.FixedTickCount() == 0);
    CHECK(time.FixedTime().Seconds() == 0.0);

    playback->Step();
    time.AdvanceFrame(Keire::TimeStep::FromMilliseconds(100.0));
    REQUIRE(playback->ShouldAdvanceFixedTick());
    REQUIRE(time.ConsumeFixedStep());
    const auto input = playback->BeginFixedTick({});
    CHECK(input.Tick == 1);
    state = 1;
    playback->EndFixedTick(1);
    REQUIRE_FALSE(playback->ShouldAdvanceFixedTick());
    CHECK(time.DiscardFixedSteps() == 5);
    CHECK(time.FixedTickCount() == 1);
    CHECK(time.FixedTime().Seconds() == doctest::Approx(1.0 / 60.0));
}

TEST_CASE("replay checkpoint restoration rolls every serializer back when a later restore fails")
{
    ReplayDirectory directory;
    const auto path = directory.Path / "transaction.keirereplay";
    int first = 1;
    int second = 2;
    auto recorder = Keire::CreateRef<Keire::ReplaySystem>();
    auto firstSerializer = IntegerSerializer(first);
    firstSerializer.Id = "test.first";
    auto secondSerializer = IntegerSerializer(second);
    secondSerializer.Id = "test.second";
    recorder->RegisterSerializer(std::move(firstSerializer));
    recorder->RegisterSerializer(std::move(secondSerializer));
    recorder->BeginRecording({path});
    (void)recorder->BeginFixedTick({.Tick = 1});
    recorder->EndFixedTick(1);
    recorder->Stop();

    first = 10;
    second = 20;
    auto playback = Keire::CreateRef<Keire::ReplaySystem>();
    firstSerializer = IntegerSerializer(first);
    firstSerializer.Id = "test.first";
    secondSerializer = IntegerSerializer(second);
    secondSerializer.Id = "test.second";
    secondSerializer.Restore = [&second](const std::span<const std::byte> bytes)
    {
        REQUIRE(bytes.size() == sizeof(second));
        std::memcpy(&second, bytes.data(), sizeof(second));
        if (second == 2)
            throw std::runtime_error("intentional checkpoint restore failure");
    };
    playback->RegisterSerializer(std::move(firstSerializer));
    playback->RegisterSerializer(std::move(secondSerializer));

    CHECK_THROWS_WITH_AS(playback->BeginPlayback({path}), "intentional checkpoint restore failure", std::runtime_error);
    CHECK(first == 10);
    CHECK(second == 20);
    CHECK(playback->Status().State == Keire::ReplaySessionState::Failed);
    CHECK_FALSE(playback->Status().Diagnostic.empty());
}

TEST_CASE("replay enforces rewind and decoded-size budgets before retaining data")
{
    ReplayDirectory directory;
    int state = 0;
    Keire::ReplaySystemSpecification tinyBudget;
    tinyBudget.RewindBudgetBytes = 1;
    auto recorder = Keire::CreateRef<Keire::ReplaySystem>(tinyBudget);
    recorder->RegisterSerializer(IntegerSerializer(state));
    CHECK_THROWS_WITH_AS(recorder->BeginRecording({directory.Path / "too-small.keirereplay"}),
                         "Replay checkpoints exceed the configured rewind-memory budget.", std::length_error);
    CHECK(recorder->Status().State == Keire::ReplaySessionState::Failed);
    CHECK_FALSE(recorder->Status().Diagnostic.empty());

    Keire::ReplaySystemSpecification fileBudget;
    fileBudget.CheckpointIntervalTicks = 1000;
    fileBudget.RewindBudgetBytes = 512;
    fileBudget.MaximumReplayBytes = 1024;
    auto boundedRecorder = Keire::CreateRef<Keire::ReplaySystem>(fileBudget);
    boundedRecorder->RegisterSerializer(IntegerSerializer(state));
    boundedRecorder->BeginRecording({directory.Path / "bounded-recording.keirereplay"});
    bool recordingLimitReached = false;
    for (std::uint64_t tick = 1; tick <= 32; ++tick)
    {
        (void)boundedRecorder->BeginFixedTick({.Tick = tick});
        state = static_cast<int>(tick);
        try
        {
            boundedRecorder->EndFixedTick(tick);
        }
        catch (const std::length_error&)
        {
            recordingLimitReached = true;
            break;
        }
    }
    CHECK(recordingLimitReached);
    CHECK(boundedRecorder->Status().State == Keire::ReplaySessionState::Failed);
    CHECK_FALSE(boundedRecorder->Status().Diagnostic.empty());

    const auto validPath = directory.Path / "bounded.keirereplay";
    auto valid = Keire::CreateRef<Keire::ReplaySystem>();
    valid->RegisterSerializer(IntegerSerializer(state));
    valid->BeginRecording({validPath});
    (void)valid->BeginFixedTick({.Tick = 1});
    state = 1;
    valid->EndFixedTick(1);
    valid->Stop();

    std::ifstream input(validPath, std::ios::binary);
    std::vector<char> bytes(std::istreambuf_iterator<char>(input), {});
    const auto checkpoint = FindChunk(bytes, 3);
    REQUIRE(checkpoint < bytes.size());
    WriteLittleU64(bytes, checkpoint + 16U, (std::numeric_limits<std::uint64_t>::max)());
    const auto oversizedPath = directory.Path / "oversized.keirereplay";
    std::ofstream output(oversizedPath, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();

    auto playback = Keire::CreateRef<Keire::ReplaySystem>();
    playback->RegisterSerializer(IntegerSerializer(state));
    CHECK_THROWS_WITH_AS(playback->BeginPlayback({oversizedPath}),
                         "Replay decoded data exceeds the configured size limit.", std::runtime_error);
    CHECK(playback->Status().State == Keire::ReplaySessionState::Failed);
}

TEST_CASE("replay close exposes recording publication failures without throwing")
{
    ReplayDirectory directory;
    int state = 0;
    auto recorder = Keire::CreateRef<Keire::ReplaySystem>();
    recorder->RegisterSerializer(IntegerSerializer(state));
    recorder->BeginRecording({directory.Path});
    (void)recorder->BeginFixedTick({.Tick = 1});
    state = 1;
    recorder->EndFixedTick(1);

    CHECK_NOTHROW(recorder->Close());
    CHECK_FALSE(recorder->IsOpen());
    CHECK(recorder->Status().State == Keire::ReplaySessionState::Failed);
    CHECK_FALSE(recorder->Status().Diagnostic.empty());
}
