#pragma once

#include "KeireHubRuntime/HubError.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace KeireHub
{
    struct DownloadRetryPolicy final
    {
        std::uint32_t MaximumAttempts = 3;
        std::chrono::milliseconds BaseDelay{250};
        std::chrono::milliseconds MaximumDelay{10'000};
        std::uint32_t JitterPermille = 250;
    };

    enum class DownloadCacheKind
    {
        Package,
        AssetPackage
    };

    struct DownloadRequest final
    {
        std::string PackageId;
        std::string Url;
        std::string Sha256;
        std::uint64_t SizeBytes = 0;
        std::filesystem::path CacheRoot;
        DownloadRetryPolicy Retry;
        bool AllowInsecureLoopbackDevelopment = false;
        std::optional<std::string> CustomProxyUrl;
        std::uint64_t BandwidthLimitBytesPerSecond = 0;
        DownloadCacheKind CacheKind = DownloadCacheKind::Package;
    };

    struct DownloadTransportRequest final
    {
        std::string Url;
        std::uint64_t Offset = 0;
        std::string IfRange;
    };

    class DownloadByteStream
    {
      public:
        virtual ~DownloadByteStream() = default;
        [[nodiscard]] virtual HubResult<std::size_t> Read(std::span<std::byte> destination) = 0;
    };

    struct DownloadTransportResponse final
    {
        std::uint64_t AcceptedOffset = 0;
        std::uint64_t TotalBytes = 0;
        std::string ETag;
        std::unique_ptr<DownloadByteStream> Body;
    };

    class DownloadTransport
    {
      public:
        virtual ~DownloadTransport() = default;
        [[nodiscard]] virtual HubResult<DownloadTransportResponse> Open(const DownloadTransportRequest& request) = 0;
    };

    enum class DownloadControl
    {
        Continue,
        Pause,
        Cancel
    };

    enum class DownloadOutcome
    {
        Completed,
        Paused,
        Cancelled,
        Failed
    };

    struct DownloadProgress final
    {
        std::uint64_t BytesTransferred = 0;
        std::uint64_t TotalBytes = 0;
        std::uint64_t BytesPerSecond = 0;
        std::uint32_t Attempt = 0;
        bool Resumed = false;
        std::string Phase;
    };

    struct DownloadCallbacks final
    {
        std::function<DownloadControl()> Control;
        std::function<void(const DownloadProgress&)> Progress;
        std::function<void(std::chrono::milliseconds)> WaitBeforeRetry;
        std::function<std::chrono::steady_clock::time_point()> MonotonicNow;
        std::function<void(std::chrono::milliseconds)> WaitForThrottle;
    };

    struct DownloadResult final
    {
        DownloadOutcome Outcome = DownloadOutcome::Completed;
        std::filesystem::path CachePath;
        bool CacheHit = false;
        std::uint32_t Attempts = 0;
        std::uint64_t BytesTransferred = 0;
    };

    class DownloadManager final
    {
      public:
        static constexpr std::uint64_t MaximumPackageBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
        static constexpr std::uint64_t MaximumBandwidthBytesPerSecond = 16ULL * 1024ULL * 1024ULL * 1024ULL;

        [[nodiscard]] HubResult<DownloadResult> Acquire(const DownloadRequest& request, DownloadTransport& transport,
                                                        const DownloadCallbacks& callbacks = {}) const;

        [[nodiscard]] static HubStatus Validate(const DownloadRequest& request);
        [[nodiscard]] static std::filesystem::path CachePath(const DownloadRequest& request);
        [[nodiscard]] static std::filesystem::path PartialPath(const DownloadRequest& request);
        [[nodiscard]] static std::filesystem::path ResumeMetadataPath(const DownloadRequest& request);
        [[nodiscard]] static std::chrono::milliseconds RetryDelay(const DownloadRequest& request,
                                                                  std::uint32_t failedAttempt) noexcept;
    };

    class FileDownloadTransport final : public DownloadTransport
    {
      public:
        [[nodiscard]] HubResult<DownloadTransportResponse> Open(const DownloadTransportRequest& request) override;
    };
} // namespace KeireHub
