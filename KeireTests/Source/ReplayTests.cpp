#include "Keire/Replay/ReplaySystem.h"

#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    specification.RewindBudgetBytes = 1;
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
    playback->EndFixedTick(3);
    CHECK(playback->Status().CurrentTick == 3);
    CHECK_FALSE(playback->ShouldAdvanceFixedTick());
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
    CHECK(playback->Status().State == Keire::ReplaySessionState::Idle);
}
