#pragma once

#include "KeireHubRuntime/CatalogClient.h"
#include "KeireHubRuntime/NativeHttpTransport.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub::Detail
{
    struct ParsedHttpUrl final
    {
        std::string Scheme;
        std::string Host;
        std::optional<std::uint16_t> Port;
        std::string Target;
        bool Secure = true;
        bool Loopback = false;
    };

    struct DownloadResponseMetadata final
    {
        std::uint64_t AcceptedOffset = 0;
        std::uint64_t TotalBytes = 0;
        std::uint64_t BodyBytes = 0;
        std::string ETag;
    };

    [[nodiscard]] HubResult<ParsedHttpUrl> ParseHttpUrl(std::string_view url, bool allowInsecureLoopback,
                                                        bool allowRemoteHttp = false);
    [[nodiscard]] HubResult<ParsedHttpUrl> ParseProxyUrl(std::string_view url);
    [[nodiscard]] HubResult<std::string> ResolveHttpRedirect(std::string_view sourceUrl, std::string_view location,
                                                             bool allowInsecureLoopback);
    [[nodiscard]] HubStatus ValidateHttpRedirect(std::string_view sourceUrl, std::string_view targetUrl,
                                                 bool allowInsecureLoopback);
    [[nodiscard]] HubStatus ValidateHttpHeaders(std::span<const CatalogHttpHeader> headers, std::size_t maximumBytes);
    [[nodiscard]] HubStatus ValidateIdentityHttpEncoding(std::span<const CatalogHttpHeader> headers);
    [[nodiscard]] bool IsStrongHttpETag(std::string_view value) noexcept;
    [[nodiscard]] HubResult<std::optional<std::string>> FindSingleHttpHeader(std::span<const CatalogHttpHeader> headers,
                                                                             std::string_view name);
    [[nodiscard]] HubResult<DownloadResponseMetadata> ParseDownloadResponse(std::uint16_t statusCode,
                                                                            std::span<const CatalogHttpHeader> headers,
                                                                            std::uint64_t requestedOffset);
    [[nodiscard]] bool IsHttpRedirectStatus(std::uint16_t statusCode) noexcept;
    [[nodiscard]] HubError HttpCatalogError(std::string_view details, bool retryable = true);
    [[nodiscard]] HubError HttpDownloadError(HubErrorCode code, std::string_view details, bool retryable = false);
} // namespace KeireHub::Detail
