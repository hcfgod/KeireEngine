#include "KeireHubRuntime/DownloadManager.h"

#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>

namespace KeireHub
{
    namespace
    {
        class FileByteStream final : public DownloadByteStream
        {
          public:
            FileByteStream(const std::filesystem::path& path, const std::uint64_t offset, const std::uint64_t size)
                : m_Stream(path, std::ios::binary), m_Remaining(size - offset)
            {
                if (m_Stream)
                    m_Stream.seekg(static_cast<std::streamoff>(offset));
            }

            [[nodiscard]] bool IsOpen() const noexcept { return static_cast<bool>(m_Stream); }

            HubResult<std::size_t> Read(const std::span<std::byte> destination) override
            {
                if (m_Remaining == 0 || destination.empty())
                    return HubResult<std::size_t>::Success(0);
                const auto requested =
                    static_cast<std::size_t>(std::min<std::uint64_t>(destination.size(), m_Remaining));
                m_Stream.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(requested));
                const auto count = m_Stream.gcount();
                if (count < 0 || (!m_Stream && !m_Stream.eof()))
                {
                    return HubResult<std::size_t>::Failure({.Code = HubErrorCode::IoRead,
                                                            .Message = "The offline package could not be read.",
                                                            .Retryable = true});
                }
                const auto bytes = static_cast<std::size_t>(count);
                m_Remaining -= bytes;
                return HubResult<std::size_t>::Success(bytes);
            }

          private:
            std::ifstream m_Stream;
            std::uint64_t m_Remaining = 0;
        };

        [[nodiscard]] HubResult<std::filesystem::path> DecodeFileUrl(const std::string_view url)
        {
            constexpr std::string_view prefix = "file://";
            if (!url.starts_with(prefix))
            {
                return HubResult<std::filesystem::path>::Failure(
                    {.Code = HubErrorCode::DownloadProtocolInvalid,
                     .Message = "This worker supports only offline file package sources.",
                     .AffectedItem = "download-transport"});
            }
            auto encoded = url.substr(prefix.size());
#if defined(_WIN32)
            if (encoded.size() >= 3 && encoded.front() == '/' && encoded[2] == ':')
                encoded.remove_prefix(1);
#endif
            if (encoded.empty() || encoded.find('%') != std::string_view::npos ||
                encoded.find('?') != std::string_view::npos || encoded.find('#') != std::string_view::npos)
            {
                return HubResult<std::filesystem::path>::Failure({.Code = HubErrorCode::DownloadProtocolInvalid,
                                                                  .Message = "The offline package URL is invalid.",
                                                                  .AffectedItem = "download-transport"});
            }
            auto path = Detail::PathFromUtf8(encoded);
            if (!path.is_absolute())
            {
                return HubResult<std::filesystem::path>::Failure(
                    {.Code = HubErrorCode::DownloadProtocolInvalid,
                     .Message = "The offline package URL must contain an absolute path.",
                     .AffectedItem = "download-transport"});
            }
            return HubResult<std::filesystem::path>::Success(std::move(path));
        }

        [[nodiscard]] std::string FileETag(const std::uint64_t size, const std::filesystem::file_time_type modified)
        {
            return "\"file-" + std::to_string(size) + '-' +
                   std::to_string(static_cast<std::int64_t>(modified.time_since_epoch().count())) + "\"";
        }
    } // namespace

    HubResult<DownloadTransportResponse> FileDownloadTransport::Open(const DownloadTransportRequest& request)
    {
        auto decoded = DecodeFileUrl(request.Url);
        if (!decoded)
            return HubResult<DownloadTransportResponse>::Failure(decoded.Error());
        const auto& path = decoded.Value();
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
        {
            return HubResult<DownloadTransportResponse>::Failure({.Code = HubErrorCode::DownloadUnavailable,
                                                                  .Message = "The offline package file is unavailable.",
                                                                  .Retryable = true,
                                                                  .AffectedItem = Detail::PathToUtf8(path.filename()),
                                                                  .TechnicalDetails = error.message()});
        }
        const auto size = std::filesystem::file_size(path, error);
        const auto modified = std::filesystem::last_write_time(path, error);
        if (error || size > DownloadManager::MaximumPackageBytes)
        {
            return HubResult<DownloadTransportResponse>::Failure({.Code = HubErrorCode::DownloadSizeMismatch,
                                                                  .Message = "The offline package has an invalid size.",
                                                                  .AffectedItem = Detail::PathToUtf8(path.filename()),
                                                                  .TechnicalDetails = error.message()});
        }
        const auto etag = FileETag(size, modified);
        const auto offset = request.Offset <= size && (request.IfRange.empty() || request.IfRange == etag)
                                ? request.Offset
                                : std::uint64_t{0};
        auto body = std::make_unique<FileByteStream>(path, offset, size);
        if (!body->IsOpen())
        {
            return HubResult<DownloadTransportResponse>::Failure({.Code = HubErrorCode::IoRead,
                                                                  .Message = "The offline package could not be opened.",
                                                                  .Retryable = true,
                                                                  .AffectedItem = Detail::PathToUtf8(path.filename())});
        }
        return HubResult<DownloadTransportResponse>::Success(
            {.AcceptedOffset = offset, .TotalBytes = size, .ETag = etag, .Body = std::move(body)});
    }
} // namespace KeireHub
