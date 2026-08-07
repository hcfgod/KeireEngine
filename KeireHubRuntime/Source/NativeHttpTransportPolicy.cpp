#include "NativeHttpTransportPolicy.h"

#include "DistributionEncoding.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <ranges>

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::size_t MaximumUrlBytes = 4096U;
        constexpr std::size_t MaximumHeaderCount = 128U;
        constexpr std::size_t MaximumHeaderNameBytes = 128U;
        constexpr std::size_t MaximumHeaderValueBytes = 8192U;

        [[nodiscard]] HubError ConfigurationError(const std::string_view details)
        {
            return {.Code = HubErrorCode::DistributionConfigurationInvalid,
                    .Message = "The native HTTP transport configuration is invalid.",
                    .AffectedItem = "http-transport",
                    .TechnicalDetails = std::string(details)};
        }

        [[nodiscard]] bool IsAsciiUrlCharacter(const unsigned char character) noexcept
        {
            return character >= 0x21U && character <= 0x7eU && character != '\\';
        }

        [[nodiscard]] bool IsHostCharacter(const unsigned char character) noexcept
        {
            return std::isalnum(character) || character == '.' || character == '-';
        }

        [[nodiscard]] bool IsValidDnsHost(const std::string_view host) noexcept
        {
            if (host.empty() || !std::ranges::all_of(host, IsHostCharacter))
                return false;
            std::size_t offset = 0;
            while (offset < host.size())
            {
                const auto separator = host.find('.', offset);
                const auto label = host.substr(offset, separator == std::string_view::npos ? host.size() - offset
                                                                                           : separator - offset);
                if (label.empty() || label.size() > 63U || !std::isalnum(static_cast<unsigned char>(label.front())) ||
                    !std::isalnum(static_cast<unsigned char>(label.back())))
                    return false;
                if (separator == std::string_view::npos)
                    return true;
                offset = separator + 1U;
            }
            return false;
        }

        [[nodiscard]] bool IsLoopbackIpv4(const std::string_view host) noexcept
        {
            std::uint32_t components = 0;
            std::size_t offset = 0;
            while (offset < host.size())
            {
                const auto separator = host.find('.', offset);
                const auto component = host.substr(offset, separator == std::string_view::npos ? host.size() - offset
                                                                                               : separator - offset);
                unsigned int value = 0;
                const auto [end, error] = std::from_chars(component.data(), component.data() + component.size(), value);
                if (component.empty() || error != std::errc{} || end != component.data() + component.size() ||
                    value > 255U || (component.size() > 1U && component.front() == '0'))
                    return false;
                if (components == 0U && value != 127U)
                    return false;
                ++components;
                if (separator == std::string_view::npos)
                    break;
                offset = separator + 1U;
            }
            return components == 4U;
        }

        [[nodiscard]] bool IsLoopbackHost(const std::string_view host) noexcept
        {
            return host == "localhost" || host == "[::1]" || IsLoopbackIpv4(host);
        }

        [[nodiscard]] bool IsHeaderNameCharacter(const unsigned char character) noexcept
        {
            if (std::isalnum(character))
                return true;
            constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
            return punctuation.find(static_cast<char>(character)) != std::string_view::npos;
        }

        [[nodiscard]] HubResult<std::uint64_t> ParseUnsigned(const std::string_view value, const std::string_view field)
        {
            std::uint64_t result = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
            if (value.empty() || error != std::errc{} || end != value.data() + value.size())
            {
                return HubResult<std::uint64_t>::Failure(
                    HttpDownloadError(HubErrorCode::DownloadProtocolInvalid,
                                      std::string("Invalid ") + std::string(field) + " response header."));
            }
            return HubResult<std::uint64_t>::Success(result);
        }

        [[nodiscard]] HubResult<std::uint64_t> ContentLength(std::span<const CatalogHttpHeader> headers)
        {
            auto header = FindSingleHttpHeader(headers, "Content-Length");
            if (!header)
                return HubResult<std::uint64_t>::Failure(header.Error());
            if (!header.Value())
            {
                return HubResult<std::uint64_t>::Failure(HttpDownloadError(
                    HubErrorCode::DownloadProtocolInvalid, "The package response has no Content-Length header."));
            }
            return ParseUnsigned(*header.Value(), "Content-Length");
        }

        [[nodiscard]] bool ValidStrongETag(const std::string_view value) noexcept
        {
            if (value.size() < 2U || value.size() > 512U || value.starts_with("W/") || value.front() != '"' ||
                value.back() != '"')
                return false;
            return std::ranges::all_of(value.substr(1U, value.size() - 2U), [](const unsigned char character)
                                       { return character == 0x21U || (character >= 0x23U && character <= 0x7eU); });
        }

        struct ContentRange final
        {
            std::uint64_t First = 0;
            std::uint64_t Last = 0;
            std::uint64_t Total = 0;
        };

        [[nodiscard]] HubResult<ContentRange> ParseContentRange(const std::string_view value)
        {
            constexpr std::string_view prefix = "bytes ";
            if (!value.starts_with(prefix))
            {
                return HubResult<ContentRange>::Failure(HttpDownloadError(
                    HubErrorCode::DownloadProtocolInvalid, "The package Content-Range header is malformed."));
            }
            const auto dash = value.find('-', prefix.size());
            const auto slash = value.find('/', dash == std::string_view::npos ? prefix.size() : dash + 1U);
            if (dash == std::string_view::npos || slash == std::string_view::npos ||
                value.find_first_of("-/", slash + 1U) != std::string_view::npos)
            {
                return HubResult<ContentRange>::Failure(HttpDownloadError(
                    HubErrorCode::DownloadProtocolInvalid, "The package Content-Range header is malformed."));
            }
            auto first = ParseUnsigned(value.substr(prefix.size(), dash - prefix.size()), "Content-Range");
            auto last = ParseUnsigned(value.substr(dash + 1U, slash - dash - 1U), "Content-Range");
            auto total = ParseUnsigned(value.substr(slash + 1U), "Content-Range");
            if (!first)
                return HubResult<ContentRange>::Failure(first.Error());
            if (!last)
                return HubResult<ContentRange>::Failure(last.Error());
            if (!total)
                return HubResult<ContentRange>::Failure(total.Error());
            if (first.Value() > last.Value() || last.Value() >= total.Value())
            {
                return HubResult<ContentRange>::Failure(HttpDownloadError(
                    HubErrorCode::DownloadProtocolInvalid, "The package Content-Range bounds are invalid."));
            }
            return HubResult<ContentRange>::Success(
                {.First = first.Value(), .Last = last.Value(), .Total = total.Value()});
        }

        [[nodiscard]] std::string RemoveDotSegments(const std::string_view target)
        {
            const auto query = target.find('?');
            const auto path = target.substr(0U, query);
            std::vector<std::string_view> segments;
            std::size_t offset = path.starts_with('/') ? 1U : 0U;
            while (offset <= path.size())
            {
                const auto separator = path.find('/', offset);
                const auto segment = path.substr(offset, separator == std::string_view::npos ? path.size() - offset
                                                                                             : separator - offset);
                if (segment == "..")
                {
                    if (!segments.empty())
                        segments.pop_back();
                }
                else if (!segment.empty() && segment != ".")
                    segments.push_back(segment);
                if (separator == std::string_view::npos)
                    break;
                offset = separator + 1U;
            }
            std::string result = "/";
            for (std::size_t index = 0; index < segments.size(); ++index)
            {
                if (index != 0U)
                    result.push_back('/');
                result.append(segments[index]);
            }
            if (path.ends_with('/') && !result.ends_with('/'))
                result.push_back('/');
            if (query != std::string_view::npos)
                result.append(target.substr(query));
            return result;
        }
    } // namespace

    HubResult<ParsedHttpUrl> ParseHttpUrl(const std::string_view url, const bool allowInsecureLoopback,
                                          const bool allowRemoteHttp)
    {
        if (url.empty() || url.size() > MaximumUrlBytes ||
            !std::ranges::all_of(url, [](const unsigned char value) { return IsAsciiUrlCharacter(value); }) ||
            url.find('#') != std::string_view::npos)
        {
            return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL is malformed or exceeds its limit."));
        }

        const auto schemeEnd = url.find("://");
        if (schemeEnd == std::string_view::npos)
            return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL has no HTTP scheme."));
        std::string scheme(url.substr(0, schemeEnd));
        std::ranges::transform(scheme, scheme.begin(),
                               [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (scheme != "https" && scheme != "http")
            return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL scheme is not HTTP or HTTPS."));

        const auto authorityStart = schemeEnd + 3U;
        const auto targetStart = url.find_first_of("/?", authorityStart);
        const auto authority =
            url.substr(authorityStart, targetStart == std::string_view::npos ? url.size() - authorityStart
                                                                             : targetStart - authorityStart);
        if (authority.empty() || authority.find('@') != std::string_view::npos)
            return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL authority is invalid."));

        std::string host;
        std::optional<std::uint16_t> port;
        if (authority.front() == '[')
        {
            const auto bracket = authority.find(']');
            if (bracket == std::string_view::npos || bracket == 1U)
                return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL IPv6 host is invalid."));
            host = std::string(authority.substr(0, bracket + 1U));
            const auto interior = std::string_view(host).substr(1U, host.size() - 2U);
            if (!std::ranges::all_of(interior, [](const unsigned char value)
                                     { return std::isxdigit(value) || value == ':' || value == '.'; }))
            {
                return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL IPv6 host is invalid."));
            }
            if (bracket + 1U < authority.size())
            {
                if (authority[bracket + 1U] != ':')
                    return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL port is invalid."));
                const auto value = authority.substr(bracket + 2U);
                unsigned int number = 0;
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
                if (value.empty() || error != std::errc{} || end != value.data() + value.size() || number == 0U ||
                    number > 65535U)
                    return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL port is invalid."));
                port = static_cast<std::uint16_t>(number);
            }
        }
        else
        {
            const auto colon = authority.rfind(':');
            const auto hostValue = authority.substr(0, colon == std::string_view::npos ? authority.size() : colon);
            if (!IsValidDnsHost(hostValue))
            {
                return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL host is invalid."));
            }
            host = std::string(hostValue);
            if (colon != std::string_view::npos)
            {
                const auto value = authority.substr(colon + 1U);
                unsigned int number = 0;
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
                if (value.empty() || error != std::errc{} || end != value.data() + value.size() || number == 0U ||
                    number > 65535U)
                    return HubResult<ParsedHttpUrl>::Failure(ConfigurationError("The URL port is invalid."));
                port = static_cast<std::uint16_t>(number);
            }
        }
        std::ranges::transform(host, host.begin(),
                               [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        const auto loopback = IsLoopbackHost(host);
        if (scheme == "http" && !allowRemoteHttp && (!allowInsecureLoopback || !loopback))
        {
            return HubResult<ParsedHttpUrl>::Failure(
                ConfigurationError("Plain HTTP is allowed only for explicit loopback development."));
        }

        std::string target = targetStart == std::string_view::npos ? "/" : std::string(url.substr(targetStart));
        if (target.front() == '?')
            target.insert(target.begin(), '/');
        const auto secure = scheme == "https";
        return HubResult<ParsedHttpUrl>::Success({.Scheme = std::move(scheme),
                                                  .Host = std::move(host),
                                                  .Port = port,
                                                  .Target = std::move(target),
                                                  .Secure = secure,
                                                  .Loopback = loopback});
    }

    HubResult<ParsedHttpUrl> ParseProxyUrl(const std::string_view url)
    {
        auto result = ParseHttpUrl(url, true, true);
        if (!result)
            return result;
        if (result.Value().Target != "/")
        {
            return HubResult<ParsedHttpUrl>::Failure(
                ConfigurationError("A custom proxy URL cannot contain a path or query."));
        }
        return result;
    }

    HubResult<std::string> ResolveHttpRedirect(const std::string_view sourceUrl, const std::string_view location,
                                               const bool allowInsecureLoopback)
    {
        auto source = ParseHttpUrl(sourceUrl, allowInsecureLoopback);
        if (!source)
            return HubResult<std::string>::Failure(source.Error());
        if (location.empty() || location.size() > MaximumUrlBytes || location.find('#') != std::string_view::npos ||
            !std::ranges::all_of(location, [](const unsigned char value) { return IsAsciiUrlCharacter(value); }))
        {
            return HubResult<std::string>::Failure(
                HttpCatalogError("The redirect target is malformed or exceeds its limit.", false));
        }

        std::string target;
        if (location.find("://") != std::string_view::npos)
            target = std::string(location);
        else
        {
            const auto authority = source.Value().Host +
                                   (source.Value().Port ? ':' + std::to_string(*source.Value().Port) : std::string{});
            if (location.starts_with("//"))
                target = source.Value().Scheme + ':' + std::string(location);
            else if (location.starts_with('/'))
                target = source.Value().Scheme + "://" + authority + std::string(location);
            else if (location.starts_with('?'))
            {
                const auto query = source.Value().Target.find('?');
                const auto path = source.Value().Target.substr(0, query);
                target = source.Value().Scheme + "://" + authority + path + std::string(location);
            }
            else
            {
                const auto query = source.Value().Target.find('?');
                const auto path = source.Value().Target.substr(0, query);
                const auto slash = path.rfind('/');
                target = source.Value().Scheme + "://" + authority + path.substr(0, slash + 1U) + std::string(location);
            }
        }
        if (location.find("://") == std::string_view::npos && !location.starts_with("//"))
        {
            auto parsed = ParseHttpUrl(target, allowInsecureLoopback);
            if (!parsed)
                return HubResult<std::string>::Failure(parsed.Error());
            const auto authority = parsed.Value().Host +
                                   (parsed.Value().Port ? ':' + std::to_string(*parsed.Value().Port) : std::string{});
            target = parsed.Value().Scheme + "://" + authority + RemoveDotSegments(parsed.Value().Target);
        }
        if (auto status = ValidateHttpRedirect(sourceUrl, target, allowInsecureLoopback); !status)
            return HubResult<std::string>::Failure(status.Error());
        return HubResult<std::string>::Success(std::move(target));
    }

    HubStatus ValidateHttpRedirect(const std::string_view sourceUrl, const std::string_view targetUrl,
                                   const bool allowInsecureLoopback)
    {
        auto source = ParseHttpUrl(sourceUrl, allowInsecureLoopback);
        auto target = ParseHttpUrl(targetUrl, allowInsecureLoopback);
        if (!source)
            return HubStatus::Failure(source.Error());
        if (!target)
            return HubStatus::Failure(HttpCatalogError("The redirect target violates the transport policy.", false));
        if (source.Value().Secure && !target.Value().Secure)
        {
            return HubStatus::Failure(HttpCatalogError("HTTPS-to-HTTP redirects are not permitted.", false));
        }
        return HubStatus::Success();
    }

    HubStatus ValidateHttpHeaders(const std::span<const CatalogHttpHeader> headers, const std::size_t maximumBytes)
    {
        if (headers.size() > MaximumHeaderCount)
            return HubStatus::Failure(HttpCatalogError("The response contains too many headers.", false));
        std::size_t bytes = 0;
        for (const auto& header : headers)
        {
            if (header.Name.empty() || header.Name.size() > MaximumHeaderNameBytes ||
                !std::ranges::all_of(header.Name, IsHeaderNameCharacter) ||
                header.Value.size() > MaximumHeaderValueBytes ||
                !std::ranges::all_of(header.Value, [](const unsigned char value)
                                     { return value == '\t' || (value >= 0x20U && value <= 0x7eU); }))
            {
                return HubStatus::Failure(HttpCatalogError("The response contains a malformed header.", false));
            }
            if (header.Name.size() > std::numeric_limits<std::size_t>::max() - header.Value.size() - 4U)
                return HubStatus::Failure(HttpCatalogError("The response headers exceed their limit.", false));
            const auto next = header.Name.size() + header.Value.size() + 4U;
            if (bytes > maximumBytes || next > maximumBytes - bytes)
                return HubStatus::Failure(HttpCatalogError("The response headers exceed their limit.", false));
            bytes += next;
        }
        return HubStatus::Success();
    }

    HubStatus ValidateIdentityHttpEncoding(const std::span<const CatalogHttpHeader> headers)
    {
        auto encoding = FindSingleHttpHeader(headers, "Content-Encoding");
        if (!encoding)
            return HubStatus::Failure(HttpCatalogError(encoding.Error().TechnicalDetails, false));
        if (encoding.Value() && !EqualsCaseInsensitiveAscii(*encoding.Value(), "identity"))
            return HubStatus::Failure(HttpCatalogError("Encoded catalog responses are not permitted.", false));
        return HubStatus::Success();
    }

    bool IsStrongHttpETag(const std::string_view value) noexcept { return ValidStrongETag(value); }

    HubResult<std::optional<std::string>> FindSingleHttpHeader(const std::span<const CatalogHttpHeader> headers,
                                                               const std::string_view name)
    {
        const std::string* found = nullptr;
        for (const auto& header : headers)
        {
            if (!EqualsCaseInsensitiveAscii(header.Name, name))
                continue;
            if (found)
            {
                return HubResult<std::optional<std::string>>::Failure(
                    HttpDownloadError(HubErrorCode::DownloadProtocolInvalid,
                                      std::string("The response repeats the ") + std::string(name) + " header."));
            }
            found = &header.Value;
        }
        if (!found)
            return HubResult<std::optional<std::string>>::Success(std::nullopt);
        return HubResult<std::optional<std::string>>::Success(*found);
    }

    HubResult<DownloadResponseMetadata> ParseDownloadResponse(const std::uint16_t statusCode,
                                                              const std::span<const CatalogHttpHeader> headers,
                                                              const std::uint64_t requestedOffset)
    {
        if (statusCode != 200U && statusCode != 206U)
        {
            const auto retryable = statusCode == 408U || statusCode == 425U || statusCode == 429U || statusCode >= 500U;
            return HubResult<DownloadResponseMetadata>::Failure(
                HttpDownloadError(retryable ? HubErrorCode::DownloadUnavailable : HubErrorCode::DownloadProtocolInvalid,
                                  "The package server returned HTTP " + std::to_string(statusCode) + '.', retryable));
        }
        auto contentEncoding = FindSingleHttpHeader(headers, "Content-Encoding");
        if (!contentEncoding)
            return HubResult<DownloadResponseMetadata>::Failure(contentEncoding.Error());
        if (contentEncoding.Value() && !EqualsCaseInsensitiveAscii(*contentEncoding.Value(), "identity"))
        {
            return HubResult<DownloadResponseMetadata>::Failure(HttpDownloadError(
                HubErrorCode::DownloadProtocolInvalid, "Encoded package responses are not permitted."));
        }
        auto transferEncoding = FindSingleHttpHeader(headers, "Transfer-Encoding");
        if (!transferEncoding)
            return HubResult<DownloadResponseMetadata>::Failure(transferEncoding.Error());
        if (transferEncoding.Value())
        {
            return HubResult<DownloadResponseMetadata>::Failure(HttpDownloadError(
                HubErrorCode::DownloadProtocolInvalid, "Transfer-encoded package responses are not permitted."));
        }
        auto etag = FindSingleHttpHeader(headers, "ETag");
        if (!etag)
            return HubResult<DownloadResponseMetadata>::Failure(etag.Error());
        if (!etag.Value() || !ValidStrongETag(*etag.Value()))
        {
            return HubResult<DownloadResponseMetadata>::Failure(HttpDownloadError(
                HubErrorCode::DownloadProtocolInvalid, "The package response has no valid strong ETag."));
        }
        auto contentLength = ContentLength(headers);
        if (!contentLength)
            return HubResult<DownloadResponseMetadata>::Failure(contentLength.Error());

        if (statusCode == 200U)
        {
            auto unexpectedRange = FindSingleHttpHeader(headers, "Content-Range");
            if (!unexpectedRange)
                return HubResult<DownloadResponseMetadata>::Failure(unexpectedRange.Error());
            if (unexpectedRange.Value())
            {
                return HubResult<DownloadResponseMetadata>::Failure(HttpDownloadError(
                    HubErrorCode::DownloadProtocolInvalid, "A full package response contains Content-Range."));
            }
            if (contentLength.Value() > DownloadManager::MaximumPackageBytes)
            {
                return HubResult<DownloadResponseMetadata>::Failure(HttpDownloadError(
                    HubErrorCode::DownloadSizeMismatch, "The package response exceeds the supported size limit."));
            }
            return HubResult<DownloadResponseMetadata>::Success({.AcceptedOffset = 0,
                                                                 .TotalBytes = contentLength.Value(),
                                                                 .BodyBytes = contentLength.Value(),
                                                                 .ETag = std::move(*etag.Value())});
        }
        auto contentRangeHeader = FindSingleHttpHeader(headers, "Content-Range");
        if (!contentRangeHeader)
            return HubResult<DownloadResponseMetadata>::Failure(contentRangeHeader.Error());
        if (!contentRangeHeader.Value())
        {
            return HubResult<DownloadResponseMetadata>::Failure(HttpDownloadError(
                HubErrorCode::DownloadProtocolInvalid, "The partial package response has no Content-Range header."));
        }
        auto contentRange = ParseContentRange(*contentRangeHeader.Value());
        if (!contentRange)
            return HubResult<DownloadResponseMetadata>::Failure(contentRange.Error());
        if (contentRange.Value().Total > DownloadManager::MaximumPackageBytes)
        {
            return HubResult<DownloadResponseMetadata>::Failure(HttpDownloadError(
                HubErrorCode::DownloadSizeMismatch, "The package response exceeds the supported size limit."));
        }
        if (contentRange.Value().First != requestedOffset ||
            contentRange.Value().Last - contentRange.Value().First + 1U != contentLength.Value())
        {
            return HubResult<DownloadResponseMetadata>::Failure(HttpDownloadError(
                HubErrorCode::DownloadProtocolInvalid, "The package range response does not match the request."));
        }
        return HubResult<DownloadResponseMetadata>::Success({.AcceptedOffset = contentRange.Value().First,
                                                             .TotalBytes = contentRange.Value().Total,
                                                             .BodyBytes = contentLength.Value(),
                                                             .ETag = std::move(*etag.Value())});
    }

    bool IsHttpRedirectStatus(const std::uint16_t statusCode) noexcept
    {
        return statusCode == 301U || statusCode == 302U || statusCode == 303U || statusCode == 307U ||
               statusCode == 308U;
    }

    HubError HttpCatalogError(const std::string_view details, const bool retryable)
    {
        return {.Code = HubErrorCode::CatalogTransportFailed,
                .Message = "The distribution service could not be reached safely.",
                .Retryable = retryable,
                .AffectedItem = "distribution-service",
                .TechnicalDetails = std::string(details)};
    }

    HubError HttpDownloadError(const HubErrorCode code, const std::string_view details, const bool retryable)
    {
        return {.Code = code,
                .Message = "The package download could not be completed safely.",
                .Retryable = retryable,
                .AffectedItem = "package-download",
                .TechnicalDetails = std::string(details)};
    }
} // namespace KeireHub::Detail
