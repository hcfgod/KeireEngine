#include "KeireHubRuntime/DownloadManager.h"

#include <KeireHubRuntimeInternal/NativeHttpTransportPolicy.h>
#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <ranges>
#include <thread>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumResumeMetadataBytes = std::size_t{64U} * 1024U;
        constexpr std::size_t TransferBufferBytes = std::size_t{256U} * 1024U;
        constexpr auto MaximumThrottleWaitSlice = std::chrono::milliseconds(100);

        struct ResumeState final
        {
            std::uint64_t Bytes = 0;
            std::string ETag;
        };

        [[nodiscard]] bool ValidETag(const std::string_view value) noexcept
        {
            return !value.empty() && value.size() <= 512 &&
                   std::ranges::none_of(value, [](const unsigned char character)
                                        { return character < 0x20U || character == 0x7fU; });
        }

        [[nodiscard]] bool ValidNetworkUrl(const DownloadRequest& request)
        {
            return static_cast<bool>(Detail::ParseHttpUrl(request.Url, request.AllowInsecureLoopbackDevelopment));
        }

        [[nodiscard]] bool ValidUrl(const DownloadRequest& request)
        {
            if (request.Url.empty() || request.Url.size() > 2048)
                return false;
            if (request.Url.starts_with("file://"))
            {
                const auto path = std::string_view(request.Url).substr(7);
                return !path.empty() && path.front() == '/' && path.find_first_of("%?#") == std::string_view::npos &&
                       std::ranges::none_of(request.Url,
                                            [](const unsigned char value) { return value < 0x20U || value == 0x7fU; });
            }
            return ValidNetworkUrl(request);
        }

        [[nodiscard]] HubError DownloadError(const HubErrorCode code, const DownloadRequest& request,
                                             const std::string_view message, const std::string_view details = {},
                                             const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::string(message),
                    .Retryable = retryable,
                    .AffectedItem = request.PackageId,
                    .TechnicalDetails = std::string(details)};
        }

        [[nodiscard]] HubStatus RemoveFile(const std::filesystem::path& path, const DownloadRequest& request)
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            if (error)
            {
                return HubStatus::Failure(DownloadError(HubErrorCode::IoWrite, request,
                                                        "The Hub could not reset an incomplete download.",
                                                        error.message(), true));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ResetResume(const DownloadRequest& request)
        {
            if (auto status = RemoveFile(DownloadManager::PartialPath(request), request); !status)
                return status;
            return RemoveFile(DownloadManager::ResumeMetadataPath(request), request);
        }

        [[nodiscard]] HubStatus SaveResume(const DownloadRequest& request, const ResumeState& resume)
        {
            return Detail::WriteJsonFileAtomically(DownloadManager::ResumeMetadataPath(request),
                                                   {{"schemaVersion", 1},
                                                    {"packageId", request.PackageId},
                                                    {"url", request.Url},
                                                    {"sha256", request.Sha256},
                                                    {"sizeBytes", request.SizeBytes},
                                                    {"bytes", resume.Bytes},
                                                    {"etag", resume.ETag}});
        }

        [[nodiscard]] HubResult<ResumeState> LoadResume(const DownloadRequest& request)
        {
            const auto partial = DownloadManager::PartialPath(request);
            const auto metadata = DownloadManager::ResumeMetadataPath(request);
            std::error_code error;
            const bool partialExists = std::filesystem::exists(partial, error);
            if (error)
            {
                return HubResult<ResumeState>::Failure(
                    DownloadError(HubErrorCode::IoRead, request, "The Hub could not inspect an incomplete download.",
                                  error.message(), true));
            }
            const bool hasPartial =
                partialExists && std::filesystem::is_regular_file(std::filesystem::symlink_status(partial, error));
            if (error)
            {
                return HubResult<ResumeState>::Failure(
                    DownloadError(HubErrorCode::IoRead, request, "The Hub could not inspect an incomplete download.",
                                  error.message(), true));
            }
            const bool metadataExists = std::filesystem::exists(metadata, error);
            if (error)
            {
                return HubResult<ResumeState>::Failure(DownloadError(HubErrorCode::IoRead, request,
                                                                     "The Hub could not inspect download resume data.",
                                                                     error.message(), true));
            }
            const bool hasMetadata =
                metadataExists && std::filesystem::is_regular_file(std::filesystem::symlink_status(metadata, error));
            if (error)
            {
                return HubResult<ResumeState>::Failure(DownloadError(HubErrorCode::IoRead, request,
                                                                     "The Hub could not inspect download resume data.",
                                                                     error.message(), true));
            }
            if (!hasPartial || !hasMetadata)
            {
                if (auto status = ResetResume(request); !status)
                    return HubResult<ResumeState>::Failure(status.Error());
                return HubResult<ResumeState>::Success({});
            }

            auto document = Detail::ReadJsonFile(metadata, MaximumResumeMetadataBytes);
            bool valid = document.HasValue();
            ResumeState result;
            try
            {
                if (valid)
                {
                    const auto& value = document.Value();
                    valid = value.at("schemaVersion").get<std::uint32_t>() == 1U &&
                            value.at("packageId").get<std::string>() == request.PackageId &&
                            value.at("url").get<std::string>() == request.Url &&
                            value.at("sha256").get<std::string>() == request.Sha256 &&
                            value.at("sizeBytes").get<std::uint64_t>() == request.SizeBytes;
                    result.Bytes = value.at("bytes").get<std::uint64_t>();
                    result.ETag = value.at("etag").get<std::string>();
                    const auto actual = std::filesystem::file_size(partial, error);
                    valid = valid && !error && result.Bytes > 0 && result.Bytes <= request.SizeBytes &&
                            actual == result.Bytes && !result.ETag.empty() && result.ETag.size() <= 512;
                }
            }
            catch (const std::exception&)
            {
                valid = false;
            }
            if (!valid)
            {
                if (auto status = ResetResume(request); !status)
                    return HubResult<ResumeState>::Failure(status.Error());
                return HubResult<ResumeState>::Success({});
            }
            return HubResult<ResumeState>::Success(std::move(result));
        }

        [[nodiscard]] HubResult<bool> ValidateCache(const DownloadRequest& request)
        {
            const auto path = DownloadManager::CachePath(request);
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if (error)
            {
                return HubResult<bool>::Failure(DownloadError(HubErrorCode::IoRead, request,
                                                              "The Hub could not inspect the verified package cache.",
                                                              error.message(), true));
            }
            if (!exists)
                return HubResult<bool>::Success(false);
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) || error ||
                std::filesystem::file_size(path, error) != request.SizeBytes || error)
            {
                if (auto status = RemoveFile(path, request); !status)
                    return HubResult<bool>::Failure(status.Error());
                return HubResult<bool>::Success(false);
            }
            auto digest = Detail::Sha256File(path, DownloadManager::MaximumPackageBytes);
            if (digest && digest.Value() == request.Sha256)
                return HubResult<bool>::Success(true);
            if (auto status = RemoveFile(path, request); !status)
                return HubResult<bool>::Failure(status.Error());
            return HubResult<bool>::Success(false);
        }

        [[nodiscard]] DownloadControl Control(const DownloadCallbacks& callbacks)
        {
            return callbacks.Control ? callbacks.Control() : DownloadControl::Continue;
        }

        [[nodiscard]] std::chrono::steady_clock::time_point Now(const DownloadCallbacks& callbacks)
        {
            return callbacks.MonotonicNow ? callbacks.MonotonicNow() : std::chrono::steady_clock::now();
        }

        void WaitForThrottle(const DownloadCallbacks& callbacks, const std::chrono::milliseconds duration)
        {
            if (callbacks.WaitForThrottle)
                callbacks.WaitForThrottle(duration);
            else
                std::this_thread::sleep_for(duration);
        }

        [[nodiscard]] std::chrono::milliseconds RequiredTransferDuration(const std::uint64_t bytes,
                                                                         const std::uint64_t bytesPerSecond) noexcept
        {
            const auto wholeSeconds = bytes / bytesPerSecond;
            const auto remainder = bytes % bytesPerSecond;
            const auto remainderMilliseconds = (remainder * 1000ULL + bytesPerSecond - 1ULL) / bytesPerSecond;
            return std::chrono::milliseconds(
                static_cast<std::chrono::milliseconds::rep>(wholeSeconds * 1000ULL + remainderMilliseconds));
        }

        [[nodiscard]] DownloadControl Throttle(const DownloadRequest& request, const DownloadCallbacks& callbacks,
                                               const std::chrono::steady_clock::time_point started,
                                               const std::uint64_t initialBytes, const std::uint64_t currentBytes)
        {
            if (request.BandwidthLimitBytesPerSecond == 0 || currentBytes <= initialBytes)
                return Control(callbacks);

            const auto target =
                RequiredTransferDuration(currentBytes - initialBytes, request.BandwidthLimitBytesPerSecond);
            while (true)
            {
                if (const auto control = Control(callbacks); control != DownloadControl::Continue)
                    return control;
                const auto current = Now(callbacks);
                const auto elapsed = current <= started
                                         ? std::chrono::milliseconds::zero()
                                         : std::chrono::duration_cast<std::chrono::milliseconds>(current - started);
                if (elapsed >= target)
                    return DownloadControl::Continue;
                WaitForThrottle(callbacks, std::min(target - elapsed, MaximumThrottleWaitSlice));
            }
        }

        [[nodiscard]] std::size_t TransferCapacity(const DownloadRequest& request,
                                                   const std::uint64_t remainingBytes) noexcept
        {
            std::uint64_t capacity = TransferBufferBytes;
            if (request.BandwidthLimitBytesPerSecond != 0)
            {
                const auto tenthSecondAllowance =
                    std::max<std::uint64_t>(1ULL, request.BandwidthLimitBytesPerSecond / 10ULL);
                capacity = std::min(capacity, tenthSecondAllowance);
            }
            return static_cast<std::size_t>(std::min(capacity, remainingBytes));
        }

        void Report(const DownloadCallbacks& callbacks, const DownloadProgress& progress)
        {
            if (callbacks.Progress)
                callbacks.Progress(progress);
        }

        [[nodiscard]] HubResult<DownloadResult> StopResult(const DownloadControl control,
                                                           const std::filesystem::path& cachePath,
                                                           const std::uint32_t attempts,
                                                           const std::uint64_t bytesTransferred)
        {
            return HubResult<DownloadResult>::Success(
                {.Outcome = control == DownloadControl::Pause ? DownloadOutcome::Paused : DownloadOutcome::Cancelled,
                 .CachePath = cachePath,
                 .Attempts = attempts,
                 .BytesTransferred = bytesTransferred});
        }

        [[nodiscard]] HubStatus PublishCache(const DownloadRequest& request)
        {
            std::error_code error;
            std::filesystem::rename(DownloadManager::PartialPath(request), DownloadManager::CachePath(request), error);
            if (error)
            {
                return HubStatus::Failure(DownloadError(HubErrorCode::IoWrite, request,
                                                        "The verified package could not be published to the cache.",
                                                        error.message(), true));
            }
            return RemoveFile(DownloadManager::ResumeMetadataPath(request), request);
        }

        [[nodiscard]] std::uint64_t Speed(const std::chrono::steady_clock::time_point started,
                                          const std::uint64_t initialBytes, const std::uint64_t currentBytes,
                                          const std::chrono::steady_clock::time_point current) noexcept
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current - started);
            if (elapsed.count() <= 0 || currentBytes < initialBytes)
                return 0;
            const auto bytes = currentBytes - initialBytes;
            if (bytes > std::numeric_limits<std::uint64_t>::max() / 1000ULL)
                return bytes / static_cast<std::uint64_t>(elapsed.count()) * 1000ULL;
            return bytes * 1000ULL / static_cast<std::uint64_t>(elapsed.count());
        }
    } // namespace

    HubResult<DownloadResult> DownloadManager::Acquire(const DownloadRequest& request, DownloadTransport& transport,
                                                       const DownloadCallbacks& callbacks) const
    {
        if (auto status = Validate(request); !status)
            return HubResult<DownloadResult>::Failure(status.Error());
        try
        {
            std::filesystem::create_directories(CachePath(request).parent_path());
        }
        catch (const std::exception& error)
        {
            return HubResult<DownloadResult>::Failure(DownloadError(HubErrorCode::IoWrite, request,
                                                                    "The package cache directory could not be created.",
                                                                    error.what(), true));
        }

        auto cached = ValidateCache(request);
        if (!cached)
            return HubResult<DownloadResult>::Failure(cached.Error());
        if (cached.Value())
        {
            if (auto status = ResetResume(request); !status)
                return HubResult<DownloadResult>::Failure(status.Error());
            Report(callbacks,
                   {.BytesTransferred = request.SizeBytes, .TotalBytes = request.SizeBytes, .Phase = "Cached"});
            return HubResult<DownloadResult>::Success({.Outcome = DownloadOutcome::Completed,
                                                       .CachePath = CachePath(request),
                                                       .CacheHit = true,
                                                       .BytesTransferred = request.SizeBytes});
        }

        auto loadedResume = LoadResume(request);
        if (!loadedResume)
            return HubResult<DownloadResult>::Failure(loadedResume.Error());
        auto resume = std::move(loadedResume).Value();
        std::uint32_t progressAttempt = 0;
        HubError lastFailure = DownloadError(HubErrorCode::DownloadUnavailable, request,
                                             "The package download could not be completed.", {}, true);

        for (std::uint32_t attempt = 1; attempt <= request.Retry.MaximumAttempts; ++attempt)
        {
            if (const auto control = Control(callbacks); control != DownloadControl::Continue)
                return StopResult(control, CachePath(request), attempt - 1U, resume.Bytes);

            bool reopenFresh = false;
            do
            {
                reopenFresh = false;
                ++progressAttempt;
                auto opened = transport.Open({.Url = request.Url, .Offset = resume.Bytes, .IfRange = resume.ETag});
                if (!opened)
                {
                    lastFailure = opened.Error();
                    break;
                }
                auto response = std::move(opened).Value();
                if (!response.Body || !ValidETag(response.ETag) || response.TotalBytes != request.SizeBytes ||
                    response.AcceptedOffset > response.TotalBytes ||
                    (resume.Bytes == 0 && response.AcceptedOffset != 0))
                {
                    lastFailure = DownloadError(HubErrorCode::DownloadProtocolInvalid, request,
                                                "The download server returned an invalid package response.", {}, true);
                    break;
                }
                if (resume.Bytes > 0 && (response.AcceptedOffset != resume.Bytes || response.ETag != resume.ETag))
                {
                    if (auto status = ResetResume(request); !status)
                        return HubResult<DownloadResult>::Failure(status.Error());
                    resume = {};
                    reopenFresh = true;
                    continue;
                }

                resume.ETag = response.ETag;
                if (auto status = SaveResume(request, resume); !status)
                    return HubResult<DownloadResult>::Failure(status.Error());
                std::ofstream output(PartialPath(request), std::ios::binary | std::ios::app);
                if (!output)
                {
                    return HubResult<DownloadResult>::Failure(
                        DownloadError(HubErrorCode::IoWrite, request,
                                      "The Hub could not open the incomplete package file.", {}, true));
                }
                const auto started = Now(callbacks);
                const auto initialBytes = resume.Bytes;
                std::array<std::byte, TransferBufferBytes> buffer{};
                bool transferFailed = false;
                while (resume.Bytes < request.SizeBytes)
                {
                    if (const auto control = Control(callbacks); control != DownloadControl::Continue)
                    {
                        output.flush();
                        if (!output)
                        {
                            return HubResult<DownloadResult>::Failure(
                                DownloadError(HubErrorCode::IoWrite, request,
                                              "The incomplete package could not be saved.", {}, true));
                        }
                        return StopResult(control, CachePath(request), attempt, resume.Bytes);
                    }
                    const auto capacity = TransferCapacity(request, request.SizeBytes - resume.Bytes);
                    auto read = response.Body->Read(std::span(buffer).first(capacity));
                    if (!read)
                    {
                        lastFailure = read.Error();
                        transferFailed = true;
                        break;
                    }
                    if (read.Value() == 0)
                    {
                        lastFailure = DownloadError(HubErrorCode::DownloadUnavailable, request,
                                                    "The package download ended before all bytes arrived.", {}, true);
                        transferFailed = true;
                        break;
                    }
                    if (read.Value() > capacity)
                    {
                        lastFailure = DownloadError(HubErrorCode::DownloadProtocolInvalid, request,
                                                    "The download transport exceeded the requested buffer.");
                        transferFailed = true;
                        break;
                    }
                    output.write(reinterpret_cast<const char*>(buffer.data()),
                                 static_cast<std::streamsize>(read.Value()));
                    output.flush();
                    if (!output)
                    {
                        return HubResult<DownloadResult>::Failure(DownloadError(
                            HubErrorCode::IoWrite, request, "The incomplete package could not be saved.", {}, true));
                    }
                    resume.Bytes += read.Value();
                    if (auto status = SaveResume(request, resume); !status)
                        return HubResult<DownloadResult>::Failure(status.Error());
                    if (const auto control = Throttle(request, callbacks, started, initialBytes, resume.Bytes);
                        control != DownloadControl::Continue)
                    {
                        return StopResult(control, CachePath(request), attempt, resume.Bytes);
                    }
                    Report(callbacks, {.BytesTransferred = resume.Bytes,
                                       .TotalBytes = request.SizeBytes,
                                       .BytesPerSecond = Speed(started, initialBytes, resume.Bytes, Now(callbacks)),
                                       .Attempt = progressAttempt,
                                       .Resumed = initialBytes > 0,
                                       .Phase = "Downloading"});
                }
                output.close();
                if (transferFailed)
                    break;

                Report(callbacks, {.BytesTransferred = resume.Bytes,
                                   .TotalBytes = request.SizeBytes,
                                   .Attempt = progressAttempt,
                                   .Resumed = initialBytes > 0,
                                   .Phase = "Verifying"});
                auto digest = Detail::Sha256File(PartialPath(request), MaximumPackageBytes);
                if (!digest)
                {
                    return HubResult<DownloadResult>::Failure(
                        DownloadError(HubErrorCode::IoRead, request, "The downloaded package could not be verified.",
                                      digest.Error().TechnicalDetails, true));
                }
                if (digest.Value() != request.Sha256)
                {
                    if (auto status = ResetResume(request); !status)
                        return HubResult<DownloadResult>::Failure(status.Error());
                    return HubResult<DownloadResult>::Failure(DownloadError(
                        HubErrorCode::DownloadChecksumMismatch, request,
                        "The downloaded package failed its integrity check.", "SHA-256 digest mismatch.", true));
                }
                if (auto status = PublishCache(request); !status)
                    return HubResult<DownloadResult>::Failure(status.Error());
                Report(callbacks, {.BytesTransferred = request.SizeBytes,
                                   .TotalBytes = request.SizeBytes,
                                   .Attempt = progressAttempt,
                                   .Resumed = initialBytes > 0,
                                   .Phase = "Completed"});
                return HubResult<DownloadResult>::Success({.Outcome = DownloadOutcome::Completed,
                                                           .CachePath = CachePath(request),
                                                           .Attempts = attempt,
                                                           .BytesTransferred = request.SizeBytes});
            } while (reopenFresh);

            if (!lastFailure.Retryable || attempt == request.Retry.MaximumAttempts)
                break;
            const auto delay = RetryDelay(request, attempt);
            if (callbacks.WaitBeforeRetry)
                callbacks.WaitBeforeRetry(delay);
            else
            {
                auto remaining = delay;
                while (remaining.count() > 0)
                {
                    const auto slice = std::min(remaining, std::chrono::milliseconds(100));
                    std::this_thread::sleep_for(slice);
                    remaining -= slice;
                    if (const auto control = Control(callbacks); control != DownloadControl::Continue)
                        return StopResult(control, CachePath(request), attempt, resume.Bytes);
                }
            }
            if (const auto control = Control(callbacks); control != DownloadControl::Continue)
                return StopResult(control, CachePath(request), attempt, resume.Bytes);
        }
        if (!lastFailure.Retryable)
            return HubResult<DownloadResult>::Failure(std::move(lastFailure));
        return HubResult<DownloadResult>::Failure(
            DownloadError(HubErrorCode::DownloadUnavailable, request,
                          "The package download could not be completed after several attempts.",
                          std::string(ToString(lastFailure.Code)), true));
    }

    HubStatus DownloadManager::Validate(const DownloadRequest& request)
    {
        if (!Detail::IsBoundedIdentifier(request.PackageId) || !ValidUrl(request) ||
            !Detail::IsSha256(request.Sha256) || request.SizeBytes == 0 || request.SizeBytes > MaximumPackageBytes ||
            request.CacheRoot.empty() || !request.CacheRoot.is_absolute() || request.Retry.MaximumAttempts == 0 ||
            request.Retry.MaximumAttempts > 10 || request.Retry.BaseDelay.count() < 0 ||
            request.Retry.MaximumDelay < request.Retry.BaseDelay ||
            request.Retry.MaximumDelay > std::chrono::minutes(5) || request.Retry.JitterPermille > 1000 ||
            request.BandwidthLimitBytesPerSecond > MaximumBandwidthBytesPerSecond ||
            (request.CacheKind != DownloadCacheKind::Package && request.CacheKind != DownloadCacheKind::AssetPackage))
        {
            return HubStatus::Failure(
                DownloadError(HubErrorCode::InvalidArgument, request, "The package download request is invalid."));
        }
        if (request.CustomProxyUrl)
        {
            auto proxy = Detail::ParseProxyUrl(*request.CustomProxyUrl);
            if (!proxy)
                return HubStatus::Failure(proxy.Error());
        }
        return HubStatus::Success();
    }

    std::filesystem::path DownloadManager::CachePath(const DownloadRequest& request)
    {
        const auto extension = request.CacheKind == DownloadCacheKind::AssetPackage ? ".keireassetpackage" : ".package";
        return request.CacheRoot / "sha256" / request.Sha256.substr(0, 2) / (request.Sha256 + extension);
    }

    std::filesystem::path DownloadManager::PartialPath(const DownloadRequest& request)
    {
        auto result = CachePath(request);
        result.replace_extension(".partial");
        return result;
    }

    std::filesystem::path DownloadManager::ResumeMetadataPath(const DownloadRequest& request)
    {
        auto result = CachePath(request);
        result.replace_extension(".partial.json");
        return result;
    }

    std::chrono::milliseconds DownloadManager::RetryDelay(const DownloadRequest& request,
                                                          const std::uint32_t failedAttempt) noexcept
    {
        const auto exponent = std::min<std::uint32_t>(failedAttempt - std::min(failedAttempt, 1U), 20U);
        const auto multiplier = std::uint64_t{1} << exponent;
        const auto base = static_cast<std::uint64_t>(request.Retry.BaseDelay.count());
        const auto maximum = static_cast<std::uint64_t>(request.Retry.MaximumDelay.count());
        const auto bounded = std::min(maximum, base > maximum / multiplier ? maximum : base * multiplier);
        std::uint64_t seed = failedAttempt;
        for (const auto character : request.Sha256)
            seed = (seed ^ static_cast<unsigned char>(character)) * 1099511628211ULL;
        const auto span = bounded * request.Retry.JitterPermille / 1000ULL;
        const auto jitter = span == 0 ? 0 : seed % (span + 1ULL);
        return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(bounded - span / 2ULL + jitter));
    }
} // namespace KeireHub
