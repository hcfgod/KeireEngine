#include "NativeHttpTransportPlatform.h"

#if defined(_WIN32)
#include "NativeHttpTransportPolicy.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::size_t MaximumCatalogBytes = 32U * 1024U * 1024U;

        struct HttpHandleCloser final
        {
            void operator()(void* handle) const noexcept
            {
                if (handle)
                    WinHttpCloseHandle(handle);
            }
        };

        using HttpHandle = std::unique_ptr<void, HttpHandleCloser>;

        struct WindowsResponse final
        {
            HttpHandle Session;
            HttpHandle Connection;
            HttpHandle Request;
            std::uint16_t StatusCode = 0;
            std::string EffectiveUrl;
            std::vector<CatalogHttpHeader> Headers;
        };

        [[nodiscard]] HubResult<std::wstring> ToWide(const std::string_view value)
        {
            if (value.empty())
                return HubResult<std::wstring>::Success({});
            if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return HubResult<std::wstring>::Failure(
                    HttpCatalogError("The request text exceeds the Windows transport limit.", false));
            }
            const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                      static_cast<int>(value.size()), nullptr, 0);
            if (required <= 0)
                return HubResult<std::wstring>::Failure(HttpCatalogError("The request is not valid UTF-8.", false));
            std::wstring result(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), required) != required)
            {
                return HubResult<std::wstring>::Failure(HttpCatalogError("The request is not valid UTF-8.", false));
            }
            return HubResult<std::wstring>::Success(std::move(result));
        }

        [[nodiscard]] HubResult<std::string> ToUtf8(const std::wstring_view value)
        {
            if (value.empty())
                return HubResult<std::string>::Success({});
            if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return HubResult<std::string>::Failure(HttpCatalogError("A response header is too large.", false));
            const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                                      static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (required <= 0)
                return HubResult<std::string>::Failure(
                    HttpCatalogError("A response header contains invalid text.", false));
            std::string result(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                    result.data(), required, nullptr, nullptr) != required)
            {
                return HubResult<std::string>::Failure(
                    HttpCatalogError("A response header contains invalid text.", false));
            }
            return HubResult<std::string>::Success(std::move(result));
        }

        [[nodiscard]] std::string WindowsFailure(const std::string_view operation)
        {
            return std::string(operation) + " failed (WinHTTP " + std::to_string(GetLastError()) + ").";
        }

        [[nodiscard]] HubResult<std::vector<CatalogHttpHeader>> QueryHeaders(void* request,
                                                                             const std::size_t maximumBytes)
        {
            DWORD required = 0;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                                &required, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(wchar_t) ||
                required > (maximumBytes + 1U) * sizeof(wchar_t))
            {
                return HubResult<std::vector<CatalogHttpHeader>>::Failure(
                    HttpCatalogError("The response headers are unavailable or exceed their limit.", false));
            }
            std::wstring raw(required / sizeof(wchar_t), L'\0');
            if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(),
                                     &required, WINHTTP_NO_HEADER_INDEX))
            {
                return HubResult<std::vector<CatalogHttpHeader>>::Failure(
                    HttpCatalogError(WindowsFailure("Reading response headers")));
            }
            raw.resize(required / sizeof(wchar_t));
            while (!raw.empty() && raw.back() == L'\0')
                raw.pop_back();

            std::vector<CatalogHttpHeader> result;
            std::size_t offset = 0;
            bool statusLine = true;
            while (offset < raw.size())
            {
                const auto end = raw.find(L"\r\n", offset);
                const auto line = std::wstring_view(raw).substr(offset, end == std::wstring::npos ? raw.size() - offset
                                                                                                  : end - offset);
                offset = end == std::wstring::npos ? raw.size() : end + 2U;
                if (line.empty())
                    continue;
                if (statusLine)
                {
                    statusLine = false;
                    continue;
                }
                const auto colon = line.find(L':');
                if (colon == std::wstring_view::npos || colon == 0U)
                {
                    return HubResult<std::vector<CatalogHttpHeader>>::Failure(
                        HttpCatalogError("The response contains a malformed header.", false));
                }
                auto value = line.substr(colon + 1U);
                while (!value.empty() && (value.front() == L' ' || value.front() == L'\t'))
                    value.remove_prefix(1U);
                while (!value.empty() && (value.back() == L' ' || value.back() == L'\t'))
                    value.remove_suffix(1U);
                auto nameUtf8 = ToUtf8(line.substr(0U, colon));
                auto valueUtf8 = ToUtf8(value);
                if (!nameUtf8)
                    return HubResult<std::vector<CatalogHttpHeader>>::Failure(nameUtf8.Error());
                if (!valueUtf8)
                    return HubResult<std::vector<CatalogHttpHeader>>::Failure(valueUtf8.Error());
                if (result.size() >= 128U)
                {
                    return HubResult<std::vector<CatalogHttpHeader>>::Failure(
                        HttpCatalogError("The response contains too many headers.", false));
                }
                result.push_back({.Name = std::move(nameUtf8).Value(), .Value = std::move(valueUtf8).Value()});
            }
            if (auto status = ValidateHttpHeaders(result, maximumBytes); !status)
                return HubResult<std::vector<CatalogHttpHeader>>::Failure(status.Error());
            return HubResult<std::vector<CatalogHttpHeader>>::Success(std::move(result));
        }

        [[nodiscard]] HubResult<HttpHandle> OpenSession(const NativeHttpTransportOptions& options)
        {
            std::wstring proxy;
            if (options.CustomProxyUrl)
            {
                auto converted = ToWide(*options.CustomProxyUrl);
                if (!converted)
                    return HubResult<HttpHandle>::Failure(converted.Error());
                proxy = std::move(converted).Value();
            }
            HttpHandle session(WinHttpOpen(
                L"KeireHub/1",
                options.CustomProxyUrl ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                options.CustomProxyUrl ? proxy.c_str() : WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session)
                return HubResult<HttpHandle>::Failure(HttpCatalogError(WindowsFailure("Opening HTTP session")));
            const auto timeout = [](const std::chrono::milliseconds value)
            { return static_cast<int>(std::min<std::int64_t>(value.count(), std::numeric_limits<int>::max())); };
            if (!WinHttpSetTimeouts(session.get(), timeout(options.ConnectTimeout), timeout(options.ConnectTimeout),
                                    timeout(options.ConnectTimeout), timeout(options.IdleTimeout)))
            {
                return HubResult<HttpHandle>::Failure(HttpCatalogError(WindowsFailure("Configuring HTTP timeouts")));
            }
            return HubResult<HttpHandle>::Success(std::move(session));
        }

        [[nodiscard]] bool ValidOutboundHeader(const std::string_view value) noexcept
        {
            return value.size() <= 512U && std::ranges::all_of(value, [](const unsigned char character)
                                                               { return character >= 0x20U && character <= 0x7eU; });
        }

        [[nodiscard]] HubResult<WindowsResponse> ExecuteOnce(const NativeHttpTransportOptions& options, std::string url,
                                                             const std::vector<CatalogHttpHeader>& requestHeaders)
        {
            auto parsed = ParseHttpUrl(url, options.AllowInsecureLoopbackDevelopment);
            if (!parsed)
                return HubResult<WindowsResponse>::Failure(parsed.Error());
            auto session = OpenSession(options);
            if (!session)
                return HubResult<WindowsResponse>::Failure(session.Error());
            if (parsed.Value().Loopback)
            {
                WINHTTP_PROXY_INFO direct{.dwAccessType = WINHTTP_ACCESS_TYPE_NO_PROXY,
                                          .lpszProxy = WINHTTP_NO_PROXY_NAME,
                                          .lpszProxyBypass = WINHTTP_NO_PROXY_BYPASS};
                if (!WinHttpSetOption(session.Value().get(), WINHTTP_OPTION_PROXY, &direct, sizeof(direct)))
                {
                    return HubResult<WindowsResponse>::Failure(
                        HttpCatalogError(WindowsFailure("Configuring direct loopback access")));
                }
            }

            auto host = parsed.Value().Host;
            if (host.starts_with('[') && host.ends_with(']'))
                host = host.substr(1U, host.size() - 2U);
            auto wideHost = ToWide(host);
            auto wideTarget = ToWide(parsed.Value().Target);
            if (!wideHost)
                return HubResult<WindowsResponse>::Failure(wideHost.Error());
            if (!wideTarget)
                return HubResult<WindowsResponse>::Failure(wideTarget.Error());
            const auto port = parsed.Value().Port.value_or(parsed.Value().Secure ? INTERNET_DEFAULT_HTTPS_PORT
                                                                                 : INTERNET_DEFAULT_HTTP_PORT);
            HttpHandle connection(WinHttpConnect(session.Value().get(), wideHost.Value().c_str(), port, 0));
            if (!connection)
                return HubResult<WindowsResponse>::Failure(HttpCatalogError(WindowsFailure("Connecting")));
            HttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", wideTarget.Value().c_str(), nullptr,
                                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                  parsed.Value().Secure ? WINHTTP_FLAG_SECURE : 0));
            if (!request)
                return HubResult<WindowsResponse>::Failure(HttpCatalogError(WindowsFailure("Opening request")));
            DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
            if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
            {
                return HubResult<WindowsResponse>::Failure(
                    HttpCatalogError(WindowsFailure("Disabling automatic redirects")));
            }
            for (const auto& header : requestHeaders)
            {
                if (!ValidOutboundHeader(header.Name) || !ValidOutboundHeader(header.Value))
                {
                    return HubResult<WindowsResponse>::Failure(
                        HttpCatalogError("A request header is malformed.", false));
                }
                auto wideHeader = ToWide(header.Name + ": " + header.Value);
                if (!wideHeader)
                    return HubResult<WindowsResponse>::Failure(wideHeader.Error());
                if (!WinHttpAddRequestHeaders(request.get(), wideHeader.Value().c_str(),
                                              static_cast<DWORD>(wideHeader.Value().size()),
                                              WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
                {
                    return HubResult<WindowsResponse>::Failure(
                        HttpCatalogError(WindowsFailure("Adding request header")));
                }
            }
            if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                                    0) ||
                !WinHttpReceiveResponse(request.get(), nullptr))
            {
                return HubResult<WindowsResponse>::Failure(HttpCatalogError(WindowsFailure("Sending request")));
            }
            DWORD statusCode = 0;
            DWORD statusBytes = sizeof(statusCode);
            if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusBytes,
                                     WINHTTP_NO_HEADER_INDEX) ||
                statusCode > std::numeric_limits<std::uint16_t>::max())
            {
                return HubResult<WindowsResponse>::Failure(HttpCatalogError(WindowsFailure("Reading response status")));
            }
            auto headers = QueryHeaders(request.get(), options.MaximumHeaderBytes);
            if (!headers)
                return HubResult<WindowsResponse>::Failure(headers.Error());
            return HubResult<WindowsResponse>::Success({.Session = std::move(session).Value(),
                                                        .Connection = std::move(connection),
                                                        .Request = std::move(request),
                                                        .StatusCode = static_cast<std::uint16_t>(statusCode),
                                                        .EffectiveUrl = std::move(url),
                                                        .Headers = std::move(headers).Value()});
        }

        [[nodiscard]] HubResult<WindowsResponse> Execute(const NativeHttpTransportOptions& options, std::string url,
                                                         const std::vector<CatalogHttpHeader>& requestHeaders)
        {
            for (std::uint32_t redirects = 0;; ++redirects)
            {
                auto response = ExecuteOnce(options, url, requestHeaders);
                if (!response || !IsHttpRedirectStatus(response.Value().StatusCode))
                    return response;
                if (redirects >= options.MaximumRedirects)
                {
                    return HubResult<WindowsResponse>::Failure(
                        HttpCatalogError("The response exceeded the redirect limit.", false));
                }
                auto location = FindSingleHttpHeader(response.Value().Headers, "Location");
                if (!location || !location.Value() || location.Value()->empty())
                {
                    return HubResult<WindowsResponse>::Failure(
                        HttpCatalogError("The redirect response has no valid Location header.", false));
                }
                auto redirect = ResolveHttpRedirect(url, *location.Value(), options.AllowInsecureLoopbackDevelopment);
                if (!redirect)
                    return HubResult<WindowsResponse>::Failure(redirect.Error());
                url = std::move(redirect).Value();
            }
        }

        [[nodiscard]] HubResult<std::vector<std::byte>> ReadBoundedBody(void* request, const std::size_t maximumBytes)
        {
            std::vector<std::byte> result;
            std::array<std::byte, 64U * 1024U> buffer{};
            for (;;)
            {
                DWORD count = 0;
                if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &count))
                {
                    return HubResult<std::vector<std::byte>>::Failure(
                        HttpCatalogError(WindowsFailure("Reading response body")));
                }
                if (count == 0U)
                    return HubResult<std::vector<std::byte>>::Success(std::move(result));
                if (result.size() > maximumBytes || count > maximumBytes - result.size())
                {
                    return HubResult<std::vector<std::byte>>::Failure(
                        HttpCatalogError("The catalog response exceeds its size limit.", false));
                }
                result.insert(result.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
            }
        }

        [[nodiscard]] HubError AsDownloadError(const HubError& error)
        {
            return HttpDownloadError(error.Retryable ? HubErrorCode::DownloadUnavailable
                                                     : HubErrorCode::DownloadProtocolInvalid,
                                     error.TechnicalDetails, error.Retryable);
        }

        class WindowsDownloadStream final : public DownloadByteStream
        {
          public:
            WindowsDownloadStream(WindowsResponse response, const std::uint64_t bodyBytes)
                : m_Response(std::move(response)), m_Remaining(bodyBytes)
            {
            }

            HubResult<std::size_t> Read(const std::span<std::byte> destination) override
            {
                if (destination.empty() || m_Remaining == 0U)
                    return HubResult<std::size_t>::Success(0U);
                const auto requested = static_cast<DWORD>(
                    std::min<std::uint64_t>({destination.size(), m_Remaining, std::numeric_limits<DWORD>::max()}));
                DWORD count = 0;
                if (!WinHttpReadData(m_Response.Request.get(), destination.data(), requested, &count))
                {
                    return HubResult<std::size_t>::Failure(HttpDownloadError(
                        HubErrorCode::DownloadUnavailable, WindowsFailure("Reading package response"), true));
                }
                if (count == 0U)
                {
                    return HubResult<std::size_t>::Failure(HttpDownloadError(
                        HubErrorCode::DownloadSizeMismatch, "The package response ended before Content-Length."));
                }
                m_Remaining -= count;
                return HubResult<std::size_t>::Success(static_cast<std::size_t>(count));
            }

          private:
            WindowsResponse m_Response;
            std::uint64_t m_Remaining = 0;
        };
    } // namespace

    HubResult<CatalogHttpResponse> FetchCatalogNative(const NativeHttpTransportOptions& options,
                                                      const CatalogHttpRequest& request)
    {
        if (request.MaximumResponseBytes == 0U || request.MaximumResponseBytes > MaximumCatalogBytes ||
            (request.IfNoneMatch && !ValidOutboundHeader(*request.IfNoneMatch)))
        {
            return HubResult<CatalogHttpResponse>::Failure(
                HttpCatalogError("The catalog request violates the transport limits.", false));
        }
        if (auto parsed = ParseHttpUrl(request.Url, options.AllowInsecureLoopbackDevelopment); !parsed)
            return HubResult<CatalogHttpResponse>::Failure(parsed.Error());
        std::vector<CatalogHttpHeader> headers{{"Accept-Encoding", "identity"}};
        if (request.IfNoneMatch)
            headers.push_back({"If-None-Match", *request.IfNoneMatch});
        auto response = Execute(options, request.Url, headers);
        if (!response)
            return HubResult<CatalogHttpResponse>::Failure(response.Error());
        if (auto status = ValidateIdentityHttpEncoding(response.Value().Headers); !status)
            return HubResult<CatalogHttpResponse>::Failure(status.Error());
        std::vector<std::byte> body;
        if (response.Value().StatusCode != 304U)
        {
            auto read = ReadBoundedBody(response.Value().Request.get(), request.MaximumResponseBytes);
            if (!read)
                return HubResult<CatalogHttpResponse>::Failure(read.Error());
            body = std::move(read).Value();
        }
        return HubResult<CatalogHttpResponse>::Success({.StatusCode = response.Value().StatusCode,
                                                        .EffectiveUrl = std::move(response.Value().EffectiveUrl),
                                                        .Headers = std::move(response.Value().Headers),
                                                        .Body = std::move(body)});
    }

    HubResult<DownloadTransportResponse> OpenDownloadNative(const NativeHttpTransportOptions& options,
                                                            const DownloadTransportRequest& request)
    {
        if (auto parsed = ParseHttpUrl(request.Url, options.AllowInsecureLoopbackDevelopment); !parsed)
            return HubResult<DownloadTransportResponse>::Failure(AsDownloadError(parsed.Error()));
        if ((request.Offset != 0U && !IsStrongHttpETag(request.IfRange)) ||
            (!request.IfRange.empty() && !ValidOutboundHeader(request.IfRange)))
        {
            return HubResult<DownloadTransportResponse>::Failure(HttpDownloadError(
                HubErrorCode::DownloadProtocolInvalid, "The package If-Range validator is malformed."));
        }
        const auto makeHeaders = [&](const bool ranged)
        {
            std::vector<CatalogHttpHeader> headers{{"Accept-Encoding", "identity"}};
            if (ranged && request.Offset != 0U)
            {
                headers.push_back({"Range", "bytes=" + std::to_string(request.Offset) + '-'});
                if (!request.IfRange.empty())
                    headers.push_back({"If-Range", request.IfRange});
            }
            return headers;
        };
        auto response = Execute(options, request.Url, makeHeaders(true));
        if (!response)
            return HubResult<DownloadTransportResponse>::Failure(AsDownloadError(response.Error()));
        if (response.Value().StatusCode == 416U && request.Offset != 0U)
        {
            response = Execute(options, request.Url, makeHeaders(false));
            if (!response)
                return HubResult<DownloadTransportResponse>::Failure(AsDownloadError(response.Error()));
        }
        auto metadata = ParseDownloadResponse(response.Value().StatusCode, response.Value().Headers, request.Offset);
        if (!metadata)
            return HubResult<DownloadTransportResponse>::Failure(metadata.Error());
        auto body = std::make_unique<WindowsDownloadStream>(std::move(response).Value(), metadata.Value().BodyBytes);
        return HubResult<DownloadTransportResponse>::Success({.AcceptedOffset = metadata.Value().AcceptedOffset,
                                                              .TotalBytes = metadata.Value().TotalBytes,
                                                              .ETag = std::move(metadata.Value().ETag),
                                                              .Body = std::move(body)});
    }
} // namespace KeireHub::Detail
#endif
