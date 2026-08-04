#pragma once

#include "Keire/Api.h"
#include "Keire/Input/Input.h"
#include "Keire/Ref.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    class DiagnosticSink;
    class MemorySystem;

    enum class ReplayProfile : std::uint8_t
    {
        StrictVerified,
        PerformanceCapture
    };

    enum class ReplaySessionState : std::uint8_t
    {
        Idle,
        Recording,
        Playing,
        Verifying,
        Paused,
        Completed,
        Diverged,
        Failed
    };

    struct ReplaySystemSpecification
    {
        ReplayProfile DefaultProfile = ReplayProfile::StrictVerified;
        std::uint32_t CheckpointIntervalTicks = 300;
        std::size_t RewindBudgetBytes = std::size_t{512} * 1024U * 1024U;
        std::size_t MaximumReplayBytes = std::size_t{4} * 1024U * 1024U * 1024U;
    };

    struct ReplayFingerprints
    {
        std::string EngineBuild;
        std::string Project;
        std::string Modules;
        std::string Content;
        std::string DeterministicConfiguration;
    };

    struct ReplayRecordRequest
    {
        std::filesystem::path Path;
        ReplayProfile Profile = ReplayProfile::StrictVerified;
        ReplayFingerprints Fingerprints;
    };

    struct ReplayPlaybackRequest
    {
        std::filesystem::path Path;
        ReplayFingerprints ExpectedFingerprints;
        bool Verify = false;
    };

    struct ReplaySerializerRegistration
    {
        std::string Id;
        std::uint32_t Version = 1;
        bool Deterministic = true;
        std::function<std::vector<std::byte>()> Capture;
        std::function<void(std::span<const std::byte>)> Restore;
    };

    struct ReplayDivergence
    {
        std::uint64_t Tick = 0;
        std::array<std::byte, 32> Expected{};
        std::array<std::byte, 32> Actual{};
        std::string Message;
    };

    struct ReplaySessionStatus
    {
        ReplaySessionState State = ReplaySessionState::Idle;
        ReplayProfile Profile = ReplayProfile::StrictVerified;
        std::uint64_t CurrentTick = 0;
        std::uint64_t TickCount = 0;
        std::uint64_t CheckpointCount = 0;
        std::optional<ReplayDivergence> Divergence;
        std::string Diagnostic;
    };

    class KEIRE_API ReplaySystem final : public RefCounted
    {
      public:
        explicit ReplaySystem(ReplaySystemSpecification specification = {}, Ref<DiagnosticSink> diagnostics = {},
                              Ref<MemorySystem> memory = {});
        ~ReplaySystem() override;

        ReplaySystem(const ReplaySystem&) = delete;
        ReplaySystem& operator=(const ReplaySystem&) = delete;

        void RegisterSerializer(ReplaySerializerRegistration serializer);
        void BeginRecording(ReplayRecordRequest request);
        void BeginPlayback(ReplayPlaybackRequest request);
        void Stop();
        void Pause(bool paused);
        void Step();
        [[nodiscard]] bool Seek(std::uint64_t tick);
        [[nodiscard]] FixedTickInputSnapshot BeginFixedTick(const FixedTickInputSnapshot& liveInput);
        void EndFixedTick(std::uint64_t tick);
        [[nodiscard]] ReplaySessionStatus Status() const;
        [[nodiscard]] bool ReplacesGameplayInput() const noexcept;
        [[nodiscard]] bool ShouldAdvanceFixedTick() const noexcept;
        [[nodiscard]] bool UsesStrictScheduling() const noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
