#include "NativeHttpTransportPlatform.h"

#if defined(__linux__)
#include "NativeHttpTransportPolicy.h"

#include <curl/curl.h>

#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::size_t MaximumCatalogBytes = 32U * 1024U * 1024U;

        struct CurlHandleCloser final
        {
            void operator()(CURL* handle) const noexcept
            {
                if (handle)
                    curl_easy_cleanup(handle);
            }
        };

        struct CurlHeadersCloser final
        {
            void operator()(curl_slist* headers) const noexcept
            {
                if (headers)
                    curl_slist_free_all(headers);
            }
        };

        using CurlHandle = std::unique_ptr<CURL, CurlHandleCloser>;
        using CurlHeaders = std::unique_ptr<curl_slist, CurlHeadersCloser>;

        [[nodiscard]] HubStatus EnsureCurl()
        {
            static std::once_flag once;
            static CURLcode result = CURLE_FAILED_INIT;
            std::call_once(once, [] { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
            if (result != CURLE_OK)
                return HubStatus::Failure(HttpCatalogError("libcurl initialization failed."));
            return HubStatus::Success();
        }

        [[nodiscard]] std::string CurlFailure(const CURLcode code)
        {
            return "libcurl request failed (code " + std::to_string(static_cast<int>(code)) + ").";
        }

        [[nodiscard]] bool ParseStatusLine(const std::string_view line, std::uint16_t& statusCode) noexcept
        {
            if (!line.starts_with("HTTP/"))
                return false;
            const auto separator = line.find(' ');
            if (separator == std::string_view::npos)
                return false;
            const auto value = line.substr(separator + 1U, 3U);
            unsigned int parsed = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (value.size() != 3U || error != std::errc{} || end != value.data() + value.size() || parsed > 999U)
                return false;
            statusCode = static_cast<std::uint16_t>(parsed);
            return true;
        }

        [[nodiscard]] bool AppendHeaderLine(std::vector<CatalogHttpHeader>& headers, std::string_view line)
        {
            if (headers.size() >= 128U)
                return false;
            const auto colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0U)
                return false;
            auto value = line.substr(colon + 1U);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                value.remove_prefix(1U);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
                value.remove_suffix(1U);
            headers.push_back({.Name = std::string(line.substr(0U, colon)), .Value = std::string(value)});
            return true;
        }

        [[nodiscard]] CurlHeaders MakeRequestHeaders(const std::vector<CatalogHttpHeader>& headers)
        {
            curl_slist* result = nullptr;
            for (const auto& header : headers)
            {
                auto* next = curl_slist_append(result, (header.Name + ": " + header.Value).c_str());
                if (!next)
                {
                    curl_slist_free_all(result);
                    return {};
                }
                result = next;
            }
            return CurlHeaders(result);
        }

        [[nodiscard]] CURLcode SetAllowedProtocol(CURL* curl, const bool secure)
        {
#if LIBCURL_VERSION_NUM >= 0x075500
            return curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, secure ? "https" : "http");
#else
            return curl_easy_setopt(curl, CURLOPT_PROTOCOLS, secure ? CURLPROTO_HTTPS : CURLPROTO_HTTP);
#endif
        }

        [[nodiscard]] HubStatus ConfigureCurl(CURL* curl, const NativeHttpTransportOptions& options,
                                              const std::string& url, const ParsedHttpUrl& parsed, curl_slist* headers)
        {
            const auto connectTimeout = static_cast<long>(std::min<std::int64_t>(
                options.ConnectTimeout.count(), static_cast<std::int64_t>(std::numeric_limits<long>::max())));
            const auto idleSeconds =
                static_cast<long>(std::max<std::int64_t>(1, (options.IdleTimeout.count() + 999) / 1000));
            const auto set = [&](const CURLoption option, const auto value)
            { return curl_easy_setopt(curl, option, value); };
            if (set(CURLOPT_URL, url.c_str()) != CURLE_OK || set(CURLOPT_HTTPHEADER, headers) != CURLE_OK ||
                set(CURLOPT_FOLLOWLOCATION, 0L) != CURLE_OK || set(CURLOPT_NOSIGNAL, 1L) != CURLE_OK ||
                set(CURLOPT_USERAGENT, "KeireHub/1") != CURLE_OK ||
                set(CURLOPT_CONNECTTIMEOUT_MS, connectTimeout) != CURLE_OK ||
                set(CURLOPT_LOW_SPEED_LIMIT, 1L) != CURLE_OK || set(CURLOPT_LOW_SPEED_TIME, idleSeconds) != CURLE_OK ||
                SetAllowedProtocol(curl, parsed.Secure) != CURLE_OK ||
                set(CURLOPT_HTTP_CONTENT_DECODING, 0L) != CURLE_OK ||
                set(CURLOPT_HEADEROPT, CURLHEADER_SEPARATE) != CURLE_OK ||
                set(CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L) != CURLE_OK)
            {
                return HubStatus::Failure(HttpCatalogError("libcurl request configuration failed."));
            }
            if (parsed.Loopback && set(CURLOPT_NOPROXY, "*") != CURLE_OK)
                return HubStatus::Failure(HttpCatalogError("libcurl loopback proxy exclusion failed."));
            if (options.CustomProxyUrl && set(CURLOPT_PROXY, options.CustomProxyUrl->c_str()) != CURLE_OK)
                return HubStatus::Failure(HttpCatalogError("libcurl custom proxy configuration failed."));
            return HubStatus::Success();
        }

        struct CatalogContext final
        {
            std::vector<CatalogHttpHeader> Headers;
            std::vector<std::byte> Body;
            std::size_t MaximumHeaderBytes = 0;
            std::size_t HeaderBytes = 0;
            std::size_t MaximumBodyBytes = 0;
            std::uint16_t StatusCode = 0;
            bool HeaderFailed = false;
            bool BodyFailed = false;
            bool DiscardBody = false;
        };

        std::size_t CatalogHeaderImpl(void* data, const std::size_t size, const std::size_t count, void* user)
        {
            auto& context = *static_cast<CatalogContext*>(user);
            if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size)
                return 0U;
            const auto bytes = size * count;
            if (bytes > context.MaximumHeaderBytes - std::min(context.HeaderBytes, context.MaximumHeaderBytes))
            {
                context.HeaderFailed = true;
                return 0U;
            }
            context.HeaderBytes += bytes;
            std::string_view line(static_cast<const char*>(data), bytes);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.remove_suffix(1U);
            if (line.empty())
            {
                context.DiscardBody = IsHttpRedirectStatus(context.StatusCode);
                return bytes;
            }
            std::uint16_t status = 0;
            if (ParseStatusLine(line, status))
            {
                context.StatusCode = status;
                context.Headers.clear();
                return bytes;
            }
            if (!AppendHeaderLine(context.Headers, line))
            {
                context.HeaderFailed = true;
                return 0U;
            }
            return bytes;
        }

        std::size_t CatalogHeader(void* data, const std::size_t size, const std::size_t count, void* user) noexcept
        {
            try
            {
                return CatalogHeaderImpl(data, size, count, user);
            }
            catch (...)
            {
                static_cast<CatalogContext*>(user)->HeaderFailed = true;
                return 0U;
            }
        }

        std::size_t CatalogWriteImpl(void* data, const std::size_t size, const std::size_t count, void* user)
        {
            auto& context = *static_cast<CatalogContext*>(user);
            if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size)
                return 0U;
            const auto bytes = size * count;
            if (context.DiscardBody)
                return bytes;
            if (bytes > context.MaximumBodyBytes - std::min(context.Body.size(), context.MaximumBodyBytes))
            {
                context.BodyFailed = true;
                return 0U;
            }
            const auto* begin = static_cast<const std::byte*>(data);
            context.Body.insert(context.Body.end(), begin, begin + bytes);
            return bytes;
        }

        std::size_t CatalogWrite(void* data, const std::size_t size, const std::size_t count, void* user) noexcept
        {
            try
            {
                return CatalogWriteImpl(data, size, count, user);
            }
            catch (...)
            {
                static_cast<CatalogContext*>(user)->BodyFailed = true;
                return 0U;
            }
        }

        struct CatalogAttempt final
        {
            std::uint16_t StatusCode = 0;
            std::vector<CatalogHttpHeader> Headers;
            std::vector<std::byte> Body;
        };

        [[nodiscard]] HubResult<CatalogAttempt> FetchCatalogOnce(const NativeHttpTransportOptions& options,
                                                                 const std::string& url,
                                                                 const std::vector<CatalogHttpHeader>& headers,
                                                                 const std::size_t maximumBodyBytes)
        {
            auto parsed = ParseHttpUrl(url, options.AllowInsecureLoopbackDevelopment);
            if (!parsed)
                return HubResult<CatalogAttempt>::Failure(parsed.Error());
            if (auto initialized = EnsureCurl(); !initialized)
                return HubResult<CatalogAttempt>::Failure(initialized.Error());
            CurlHandle curl(curl_easy_init());
            auto requestHeaders = MakeRequestHeaders(headers);
            if (!curl || (!headers.empty() && !requestHeaders))
                return HubResult<CatalogAttempt>::Failure(HttpCatalogError("libcurl allocation failed."));
            if (auto status = ConfigureCurl(curl.get(), options, url, parsed.Value(), requestHeaders.get()); !status)
                return HubResult<CatalogAttempt>::Failure(status.Error());
            CatalogContext context{.MaximumHeaderBytes = options.MaximumHeaderBytes,
                                   .MaximumBodyBytes = maximumBodyBytes};
            if (curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, CatalogHeader) != CURLE_OK ||
                curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &context) != CURLE_OK ||
                curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, CatalogWrite) != CURLE_OK ||
                curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &context) != CURLE_OK)
            {
                return HubResult<CatalogAttempt>::Failure(HttpCatalogError("libcurl callbacks could not be set."));
            }
            const auto result = curl_easy_perform(curl.get());
            if (context.HeaderFailed)
                return HubResult<CatalogAttempt>::Failure(
                    HttpCatalogError("Response headers exceed their limit.", false));
            if (context.BodyFailed)
                return HubResult<CatalogAttempt>::Failure(
                    HttpCatalogError("Catalog response exceeds its limit.", false));
            if (result != CURLE_OK)
                return HubResult<CatalogAttempt>::Failure(HttpCatalogError(CurlFailure(result)));
            long statusCode = 0;
            if (curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &statusCode) != CURLE_OK || statusCode <= 0L ||
                statusCode > static_cast<long>(std::numeric_limits<std::uint16_t>::max()))
            {
                return HubResult<CatalogAttempt>::Failure(HttpCatalogError("libcurl returned no valid HTTP status."));
            }
            if (auto status = ValidateHttpHeaders(context.Headers, options.MaximumHeaderBytes); !status)
                return HubResult<CatalogAttempt>::Failure(status.Error());
            if (auto status = ValidateIdentityHttpEncoding(context.Headers); !status)
                return HubResult<CatalogAttempt>::Failure(status.Error());
            return HubResult<CatalogAttempt>::Success({.StatusCode = static_cast<std::uint16_t>(statusCode),
                                                       .Headers = std::move(context.Headers),
                                                       .Body = std::move(context.Body)});
        }

        struct CurlStreamState final
        {
            std::mutex Mutex;
            std::condition_variable Changed;
            std::vector<CatalogHttpHeader> Headers;
            std::vector<std::byte> Buffer;
            std::optional<HubError> Failure;
            std::optional<std::uint64_t> MaximumBodyBytes;
            std::thread Worker;
            std::size_t BufferOffset = 0;
            std::size_t BufferLimit = 0;
            std::size_t MaximumHeaderBytes = 0;
            std::size_t HeaderBytes = 0;
            std::uint64_t ReceivedBytes = 0;
            std::uint16_t StatusCode = 0;
            bool HeaderComplete = false;
            bool Finished = false;
            bool Cancelled = false;
            bool CallbackFailed = false;
        };

        std::size_t StreamHeaderImpl(void* data, const std::size_t size, const std::size_t count, void* user)
        {
            auto& state = *static_cast<CurlStreamState*>(user);
            if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size)
                return 0U;
            const auto bytes = size * count;
            std::scoped_lock lock(state.Mutex);
            if (state.Cancelled)
                return 0U;
            if (bytes > state.MaximumHeaderBytes - std::min(state.HeaderBytes, state.MaximumHeaderBytes))
            {
                state.Failure = HttpDownloadError(HubErrorCode::DownloadProtocolInvalid,
                                                  "The package response headers exceed their limit.");
                state.Changed.notify_all();
                return 0U;
            }
            state.HeaderBytes += bytes;
            std::string_view line(static_cast<const char*>(data), bytes);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.remove_suffix(1U);
            if (line.empty())
            {
                state.HeaderComplete = true;
                state.Changed.notify_all();
                return bytes;
            }
            if (state.HeaderComplete)
            {
                state.Failure = HttpDownloadError(HubErrorCode::DownloadProtocolInvalid,
                                                  "Package response trailers are not permitted.");
                state.Changed.notify_all();
                return 0U;
            }
            std::uint16_t status = 0;
            if (ParseStatusLine(line, status))
            {
                state.StatusCode = status;
                state.Headers.clear();
                return bytes;
            }
            if (!AppendHeaderLine(state.Headers, line))
            {
                state.Failure = HttpDownloadError(HubErrorCode::DownloadProtocolInvalid,
                                                  "The package response contains a malformed header.");
                state.Changed.notify_all();
                return 0U;
            }
            return bytes;
        }

        std::size_t StreamHeader(void* data, const std::size_t size, const std::size_t count, void* user) noexcept
        {
            try
            {
                return StreamHeaderImpl(data, size, count, user);
            }
            catch (...)
            {
                auto& state = *static_cast<CurlStreamState*>(user);
                std::scoped_lock lock(state.Mutex);
                state.CallbackFailed = true;
                state.Changed.notify_all();
                return 0U;
            }
        }

        std::size_t StreamWriteImpl(void* data, const std::size_t size, const std::size_t count, void* user)
        {
            auto& state = *static_cast<CurlStreamState*>(user);
            if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size)
                return 0U;
            const auto bytes = size * count;
            std::unique_lock lock(state.Mutex);
            if (bytes > state.BufferLimit)
            {
                state.Failure = HttpDownloadError(HubErrorCode::DownloadProtocolInvalid,
                                                  "A package response chunk exceeds the stream buffer.");
                state.Changed.notify_all();
                return 0U;
            }
            state.Changed.wait(lock,
                               [&]
                               {
                                   const auto buffered = state.Buffer.size() - state.BufferOffset;
                                   return state.Cancelled || state.Failure || bytes <= state.BufferLimit - buffered;
                               });
            if (state.Cancelled || state.Failure)
                return 0U;
            if (state.MaximumBodyBytes &&
                bytes > *state.MaximumBodyBytes - std::min(state.ReceivedBytes, *state.MaximumBodyBytes))
            {
                state.Failure = HttpDownloadError(HubErrorCode::DownloadSizeMismatch,
                                                  "The package response exceeds Content-Length.");
                state.Changed.notify_all();
                return 0U;
            }
            if (state.BufferOffset != 0U && state.BufferOffset >= state.Buffer.size() / 2U)
            {
                state.Buffer.erase(state.Buffer.begin(),
                                   state.Buffer.begin() + static_cast<std::ptrdiff_t>(state.BufferOffset));
                state.BufferOffset = 0U;
            }
            const auto* begin = static_cast<const std::byte*>(data);
            state.Buffer.insert(state.Buffer.end(), begin, begin + bytes);
            state.ReceivedBytes += bytes;
            state.Changed.notify_all();
            return bytes;
        }

        std::size_t StreamWrite(void* data, const std::size_t size, const std::size_t count, void* user) noexcept
        {
            try
            {
                return StreamWriteImpl(data, size, count, user);
            }
            catch (...)
            {
                auto& state = *static_cast<CurlStreamState*>(user);
                std::scoped_lock lock(state.Mutex);
                state.CallbackFailed = true;
                state.Changed.notify_all();
                return 0U;
            }
        }

        int StreamProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
        {
            auto& state = *static_cast<CurlStreamState*>(user);
            std::scoped_lock lock(state.Mutex);
            return state.Cancelled || state.Failure || state.CallbackFailed ? 1 : 0;
        }

        void StopStream(const std::shared_ptr<CurlStreamState>& state)
        {
            {
                std::scoped_lock lock(state->Mutex);
                state->Cancelled = true;
                state->Changed.notify_all();
            }
            if (state->Worker.joinable())
                state->Worker.join();
        }

        [[nodiscard]] HubResult<std::shared_ptr<CurlStreamState>>
        StartStream(const NativeHttpTransportOptions& options, const std::string& url,
                    const std::vector<CatalogHttpHeader>& headers)
        {
            auto parsed = ParseHttpUrl(url, options.AllowInsecureLoopbackDevelopment);
            if (!parsed)
                return HubResult<std::shared_ptr<CurlStreamState>>::Failure(parsed.Error());
            if (auto initialized = EnsureCurl(); !initialized)
                return HubResult<std::shared_ptr<CurlStreamState>>::Failure(initialized.Error());
            auto state = std::make_shared<CurlStreamState>();
            state->BufferLimit = options.DownloadBufferBytes;
            state->MaximumHeaderBytes = options.MaximumHeaderBytes;
            try
            {
                state->Worker = std::thread(
                    [state, options, url, parsed = std::move(parsed).Value(), headers]
                    {
                        try
                        {
                            CurlHandle curl(curl_easy_init());
                            auto requestHeaders = MakeRequestHeaders(headers);
                            HubStatus configured =
                                !curl || (!headers.empty() && !requestHeaders)
                                    ? HubStatus::Failure(HttpCatalogError("libcurl allocation failed."))
                                    : ConfigureCurl(curl.get(), options, url, parsed, requestHeaders.get());
                            if (configured)
                            {
                                if (curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, StreamHeader) != CURLE_OK ||
                                    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, state.get()) != CURLE_OK ||
                                    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, StreamWrite) != CURLE_OK ||
                                    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, state.get()) != CURLE_OK ||
                                    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L) != CURLE_OK ||
                                    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, StreamProgress) !=
                                        CURLE_OK ||
                                    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, state.get()) != CURLE_OK)
                                {
                                    configured =
                                        HubStatus::Failure(HttpCatalogError("libcurl callbacks could not be set."));
                                }
                            }
                            CURLcode result = CURLE_FAILED_INIT;
                            if (configured)
                                result = curl_easy_perform(curl.get());
                            {
                                std::scoped_lock lock(state->Mutex);
                                if (state->CallbackFailed && !state->Failure)
                                    state->Failure = HttpDownloadError(HubErrorCode::DownloadUnavailable,
                                                                       "A package stream callback failed.", true);
                                else if (!configured && !state->Failure)
                                    state->Failure = HttpDownloadError(HubErrorCode::DownloadUnavailable,
                                                                       configured.Error().TechnicalDetails, true);
                                else if (result != CURLE_OK && !state->Cancelled && !state->Failure)
                                    state->Failure =
                                        HttpDownloadError(HubErrorCode::DownloadUnavailable, CurlFailure(result), true);
                                state->Finished = true;
                                state->Changed.notify_all();
                            }
                        }
                        catch (...)
                        {
                            std::scoped_lock lock(state->Mutex);
                            if (!state->Failure)
                            {
                                state->Failure = HttpDownloadError(HubErrorCode::DownloadUnavailable,
                                                                   "The package stream worker failed.", true);
                            }
                            state->Finished = true;
                            state->Changed.notify_all();
                        }
                    });
            }
            catch (...)
            {
                return HubResult<std::shared_ptr<CurlStreamState>>::Failure(HttpDownloadError(
                    HubErrorCode::DownloadUnavailable, "The package stream worker could not be started.", true));
            }
            {
                std::unique_lock lock(state->Mutex);
                state->Changed.wait(lock, [&] { return state->HeaderComplete || state->Finished || state->Failure; });
                if (state->Failure || (!state->HeaderComplete && state->Finished))
                {
                    auto error = state->Failure.value_or(HttpDownloadError(
                        HubErrorCode::DownloadUnavailable, "The package response ended before its headers.", true));
                    lock.unlock();
                    StopStream(state);
                    return HubResult<std::shared_ptr<CurlStreamState>>::Failure(std::move(error));
                }
            }
            return HubResult<std::shared_ptr<CurlStreamState>>::Success(std::move(state));
        }

        [[nodiscard]] HubResult<std::shared_ptr<CurlStreamState>>
        OpenFollowingRedirects(const NativeHttpTransportOptions& options, std::string url,
                               const std::vector<CatalogHttpHeader>& headers)
        {
            for (std::uint32_t redirects = 0;; ++redirects)
            {
                auto stream = StartStream(options, url, headers);
                if (!stream)
                    return stream;
                std::uint16_t statusCode = 0;
                std::vector<CatalogHttpHeader> responseHeaders;
                {
                    std::scoped_lock lock(stream.Value()->Mutex);
                    statusCode = stream.Value()->StatusCode;
                    responseHeaders = stream.Value()->Headers;
                }
                if (!IsHttpRedirectStatus(statusCode))
                    return stream;
                StopStream(stream.Value());
                if (redirects >= options.MaximumRedirects)
                {
                    return HubResult<std::shared_ptr<CurlStreamState>>::Failure(HttpDownloadError(
                        HubErrorCode::DownloadProtocolInvalid, "The package response exceeded the redirect limit."));
                }
                auto location = FindSingleHttpHeader(responseHeaders, "Location");
                if (!location || !location.Value() || location.Value()->empty())
                {
                    return HubResult<std::shared_ptr<CurlStreamState>>::Failure(HttpDownloadError(
                        HubErrorCode::DownloadProtocolInvalid, "The package redirect has no valid Location header."));
                }
                auto redirect = ResolveHttpRedirect(url, *location.Value(), options.AllowInsecureLoopbackDevelopment);
                if (!redirect)
                {
                    return HubResult<std::shared_ptr<CurlStreamState>>::Failure(
                        HttpDownloadError(HubErrorCode::DownloadProtocolInvalid, redirect.Error().TechnicalDetails));
                }
                url = std::move(redirect).Value();
            }
        }

        class CurlDownloadStream final : public DownloadByteStream
        {
          public:
            CurlDownloadStream(std::shared_ptr<CurlStreamState> state, const std::uint64_t bodyBytes)
                : m_State(std::move(state)), m_Remaining(bodyBytes)
            {
                std::scoped_lock lock(m_State->Mutex);
                m_State->MaximumBodyBytes = bodyBytes;
                if (m_State->ReceivedBytes > bodyBytes)
                    m_State->Failure = HttpDownloadError(HubErrorCode::DownloadSizeMismatch,
                                                         "The package response exceeds Content-Length.");
                m_State->Changed.notify_all();
            }

            ~CurlDownloadStream() override { StopStream(m_State); }

            HubResult<std::size_t> Read(const std::span<std::byte> destination) override
            {
                if (destination.empty() || m_Remaining == 0U)
                    return HubResult<std::size_t>::Success(0U);
                std::unique_lock lock(m_State->Mutex);
                m_State->Changed.wait(lock,
                                      [&]
                                      {
                                          return m_State->BufferOffset < m_State->Buffer.size() || m_State->Finished ||
                                                 m_State->Failure;
                                      });
                const auto available = m_State->Buffer.size() - m_State->BufferOffset;
                if (available == 0U)
                {
                    if (m_State->Failure)
                        return HubResult<std::size_t>::Failure(*m_State->Failure);
                    return HubResult<std::size_t>::Failure(HttpDownloadError(
                        HubErrorCode::DownloadSizeMismatch, "The package response ended before Content-Length."));
                }
                const auto count =
                    static_cast<std::size_t>(std::min<std::uint64_t>({destination.size(), available, m_Remaining}));
                std::memcpy(destination.data(), m_State->Buffer.data() + m_State->BufferOffset, count);
                m_State->BufferOffset += count;
                m_Remaining -= count;
                if (m_State->BufferOffset == m_State->Buffer.size())
                {
                    m_State->Buffer.clear();
                    m_State->BufferOffset = 0U;
                }
                m_State->Changed.notify_all();
                return HubResult<std::size_t>::Success(count);
            }

          private:
            std::shared_ptr<CurlStreamState> m_State;
            std::uint64_t m_Remaining = 0;
        };

        [[nodiscard]] HubError CatalogToDownload(const HubError& error)
        {
            return HttpDownloadError(error.Retryable ? HubErrorCode::DownloadUnavailable
                                                     : HubErrorCode::DownloadProtocolInvalid,
                                     error.TechnicalDetails, error.Retryable);
        }
    } // namespace

    HubResult<CatalogHttpResponse> FetchCatalogNative(const NativeHttpTransportOptions& options,
                                                      const CatalogHttpRequest& request)
    {
        if (request.MaximumResponseBytes == 0U || request.MaximumResponseBytes > MaximumCatalogBytes ||
            (request.IfNoneMatch && (request.IfNoneMatch->size() > 512U ||
                                     !std::ranges::all_of(*request.IfNoneMatch, [](const unsigned char value)
                                                          { return value >= 0x20U && value <= 0x7eU; }))))
        {
            return HubResult<CatalogHttpResponse>::Failure(
                HttpCatalogError("The catalog request violates the transport limits.", false));
        }
        std::vector<CatalogHttpHeader> requestHeaders{{"Accept-Encoding", "identity"}};
        if (request.IfNoneMatch)
            requestHeaders.push_back({"If-None-Match", *request.IfNoneMatch});
        std::string url = request.Url;
        for (std::uint32_t redirects = 0;; ++redirects)
        {
            auto response = FetchCatalogOnce(options, url, requestHeaders, request.MaximumResponseBytes);
            if (!response)
                return HubResult<CatalogHttpResponse>::Failure(response.Error());
            if (!IsHttpRedirectStatus(response.Value().StatusCode))
            {
                if (response.Value().StatusCode == 304U)
                    response.Value().Body.clear();
                return HubResult<CatalogHttpResponse>::Success({.StatusCode = response.Value().StatusCode,
                                                                .EffectiveUrl = std::move(url),
                                                                .Headers = std::move(response.Value().Headers),
                                                                .Body = std::move(response.Value().Body)});
            }
            if (redirects >= options.MaximumRedirects)
            {
                return HubResult<CatalogHttpResponse>::Failure(
                    HttpCatalogError("The response exceeded the redirect limit.", false));
            }
            auto location = FindSingleHttpHeader(response.Value().Headers, "Location");
            if (!location || !location.Value() || location.Value()->empty())
            {
                return HubResult<CatalogHttpResponse>::Failure(
                    HttpCatalogError("The redirect response has no valid Location header.", false));
            }
            auto redirect = ResolveHttpRedirect(url, *location.Value(), options.AllowInsecureLoopbackDevelopment);
            if (!redirect)
                return HubResult<CatalogHttpResponse>::Failure(redirect.Error());
            url = std::move(redirect).Value();
        }
    }

    HubResult<DownloadTransportResponse> OpenDownloadNative(const NativeHttpTransportOptions& options,
                                                            const DownloadTransportRequest& request)
    {
        if ((request.Offset != 0U && !IsStrongHttpETag(request.IfRange)) || request.IfRange.size() > 512U ||
            !std::ranges::all_of(request.IfRange,
                                 [](const unsigned char value) { return value >= 0x20U && value <= 0x7eU; }))
        {
            return HubResult<DownloadTransportResponse>::Failure(HttpDownloadError(
                HubErrorCode::DownloadProtocolInvalid, "The package If-Range validator is malformed."));
        }
        const auto makeHeaders = [&](const bool ranged)
        {
            std::vector<CatalogHttpHeader> result{{"Accept-Encoding", "identity"}};
            if (ranged && request.Offset != 0U)
            {
                result.push_back({"Range", "bytes=" + std::to_string(request.Offset) + '-'});
                if (!request.IfRange.empty())
                    result.push_back({"If-Range", request.IfRange});
            }
            return result;
        };
        auto stream = OpenFollowingRedirects(options, request.Url, makeHeaders(true));
        if (!stream)
            return HubResult<DownloadTransportResponse>::Failure(CatalogToDownload(stream.Error()));
        std::uint16_t statusCode = 0;
        std::vector<CatalogHttpHeader> headers;
        {
            std::scoped_lock lock(stream.Value()->Mutex);
            statusCode = stream.Value()->StatusCode;
            headers = stream.Value()->Headers;
        }
        if (statusCode == 416U && request.Offset != 0U)
        {
            StopStream(stream.Value());
            stream = OpenFollowingRedirects(options, request.Url, makeHeaders(false));
            if (!stream)
                return HubResult<DownloadTransportResponse>::Failure(CatalogToDownload(stream.Error()));
            std::scoped_lock lock(stream.Value()->Mutex);
            statusCode = stream.Value()->StatusCode;
            headers = stream.Value()->Headers;
        }
        if (auto status = ValidateHttpHeaders(headers, options.MaximumHeaderBytes); !status)
        {
            StopStream(stream.Value());
            return HubResult<DownloadTransportResponse>::Failure(CatalogToDownload(status.Error()));
        }
        auto metadata = ParseDownloadResponse(statusCode, headers, request.Offset);
        if (!metadata)
        {
            StopStream(stream.Value());
            return HubResult<DownloadTransportResponse>::Failure(metadata.Error());
        }
        auto body = std::make_unique<CurlDownloadStream>(stream.Value(), metadata.Value().BodyBytes);
        return HubResult<DownloadTransportResponse>::Success({.AcceptedOffset = metadata.Value().AcceptedOffset,
                                                              .TotalBytes = metadata.Value().TotalBytes,
                                                              .ETag = std::move(metadata.Value().ETag),
                                                              .Body = std::move(body)});
    }
} // namespace KeireHub::Detail
#endif
