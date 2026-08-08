#include <KeireHubRuntimeInternal/NativeHttpTransportPlatform.h>

#if defined(__APPLE__)
#include <KeireHubRuntimeInternal/NativeHttpTransportPolicy.h>

#import <CFNetwork/CFNetwork.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace KeireHub::Detail
{
    struct MacStreamState final
    {
        std::mutex Mutex;
        std::condition_variable Changed;
        std::vector<CatalogHttpHeader> Headers;
        std::vector<std::byte> Buffer;
        std::optional<HubError> Failure;
        std::optional<std::uint64_t> MaximumBodyBytes;
        std::string CurrentUrl;
        std::string EffectiveUrl;
        std::size_t BufferOffset = 0;
        std::size_t BufferLimit = 0;
        std::size_t MaximumHeaderBytes = 0;
        std::uint64_t ReceivedBytes = 0;
        std::uint32_t Redirects = 0;
        std::uint32_t MaximumRedirects = 0;
        std::uint16_t StatusCode = 0;
        bool AllowInsecureLoopback = false;
        bool HeaderComplete = false;
        bool Finished = false;
        bool Cancelled = false;
    };
} // namespace KeireHub::Detail

@interface KeireHubHttpDelegate : NSObject <NSURLSessionDataDelegate, NSURLSessionTaskDelegate>
{
  @public
    std::shared_ptr<KeireHub::Detail::MacStreamState> State;
}
- (instancetype)initWithState:(std::shared_ptr<KeireHub::Detail::MacStreamState>)state;
@end

@implementation KeireHubHttpDelegate
- (instancetype)initWithState:(std::shared_ptr<KeireHub::Detail::MacStreamState>)state
{
    self = [super init];
    if (self)
        State = std::move(state);
    return self;
}

- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest* _Nullable))completionHandler
{
    (void)session;
    (void)task;
    (void)response;
    const char* value = request.URL.absoluteString.UTF8String;
    const std::string target = value ? value : "";
    std::string source;
    bool allowed = false;
    {
        std::scoped_lock lock(State->Mutex);
        source = State->CurrentUrl;
        if (!State->Cancelled && State->Redirects < State->MaximumRedirects)
        {
            const auto status = KeireHub::Detail::ValidateHttpRedirect(source, target, State->AllowInsecureLoopback);
            if (status)
            {
                ++State->Redirects;
                State->CurrentUrl = target;
                allowed = true;
            }
            else
                State->Failure = status.Error();
        }
        else if (!State->Cancelled)
        {
            State->Failure = KeireHub::Detail::HttpCatalogError("The response exceeded the redirect limit.", false);
        }
        State->Changed.notify_all();
    }
    completionHandler(allowed ? request : nil);
}

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler
{
    (void)session;
    (void)dataTask;
    auto* http = [response isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse*)response : nil;
    std::vector<KeireHub::CatalogHttpHeader> headers;
    std::size_t headerBytes = 0;
    if (http)
    {
        for (id key in http.allHeaderFields)
        {
            if (headers.size() >= 128U)
            {
                headerBytes = std::numeric_limits<std::size_t>::max();
                break;
            }
            NSString* name = [key description];
            NSString* headerValue = [[http.allHeaderFields objectForKey:key] description];
            const char* nameText = name.UTF8String;
            const char* valueText = headerValue.UTF8String;
            if (!nameText || !valueText)
            {
                headerBytes = std::numeric_limits<std::size_t>::max();
                break;
            }
            headers.push_back({.Name = nameText, .Value = valueText});
            const auto bytes = headers.back().Name.size() + headers.back().Value.size() + 4U;
            if (headerBytes > std::numeric_limits<std::size_t>::max() - bytes)
            {
                headerBytes = std::numeric_limits<std::size_t>::max();
                break;
            }
            headerBytes += bytes;
        }
    }
    const char* effective = response.URL.absoluteString.UTF8String;
    bool cancel = false;
    {
        std::scoped_lock lock(State->Mutex);
        if (!http)
            State->Failure = KeireHub::Detail::HttpCatalogError("Foundation returned a non-HTTP response.", false);
        else if (headerBytes > State->MaximumHeaderBytes)
            State->Failure = KeireHub::Detail::HttpCatalogError("Response headers exceed their limit.", false);
        else
        {
            State->StatusCode = static_cast<std::uint16_t>(http.statusCode);
            State->Headers = std::move(headers);
            State->EffectiveUrl = effective ? effective : State->CurrentUrl;
            State->HeaderComplete = true;
        }
        cancel = State->Failure.has_value();
        State->Changed.notify_all();
    }
    completionHandler(cancel ? NSURLSessionResponseCancel : NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)dataTask didReceiveData:(NSData*)data
{
    (void)session;
    (void)dataTask;
    const auto* source = static_cast<const std::byte*>(data.bytes);
    std::size_t remaining = data.length;
    while (remaining != 0U)
    {
        std::unique_lock lock(State->Mutex);
        State->Changed.wait(lock,
                            [&]
                            {
                                return State->Cancelled || State->Failure ||
                                       State->Buffer.size() - State->BufferOffset < State->BufferLimit;
                            });
        if (State->Cancelled || State->Failure)
            return;
        const auto buffered = State->Buffer.size() - State->BufferOffset;
        const auto count = std::min(remaining, State->BufferLimit - buffered);
        if (State->MaximumBodyBytes &&
            count > *State->MaximumBodyBytes - std::min(State->ReceivedBytes, *State->MaximumBodyBytes))
        {
            State->Failure = KeireHub::Detail::HttpDownloadError(KeireHub::HubErrorCode::DownloadSizeMismatch,
                                                                 "The package response exceeds Content-Length.");
            State->Changed.notify_all();
            return;
        }
        if (State->BufferOffset != 0U && State->BufferOffset >= State->Buffer.size() / 2U)
        {
            State->Buffer.erase(State->Buffer.begin(),
                                State->Buffer.begin() + static_cast<std::ptrdiff_t>(State->BufferOffset));
            State->BufferOffset = 0U;
        }
        State->Buffer.insert(State->Buffer.end(), source, source + count);
        State->ReceivedBytes += count;
        State->Changed.notify_all();
        source += count;
        remaining -= count;
    }
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error
{
    (void)session;
    (void)task;
    std::scoped_lock lock(State->Mutex);
    if (error && !State->Cancelled && !State->Failure)
    {
        State->Failure =
            KeireHub::Detail::HttpCatalogError("Foundation request failed (code " + std::to_string(error.code) + ").");
    }
    State->Finished = true;
    State->Changed.notify_all();
}
@end

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::size_t MaximumCatalogBytes = 32U * 1024U * 1024U;
        constexpr std::size_t MaximumRequestBodyBytes = 1024U * 1024U;
        constexpr std::size_t MaximumResponseBodyBytes = 4U * 1024U * 1024U;

        struct MacTransfer final
        {
            std::shared_ptr<MacStreamState> State;
            __strong NSURLSession* Session = nil;
            __strong NSURLSessionDataTask* Task = nil;
            __strong KeireHubHttpDelegate* Delegate = nil;
        };

        [[nodiscard]] NSString* String(const std::string& value)
        {
            return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
        }

        void StopTransfer(const std::shared_ptr<MacTransfer>& transfer, const std::chrono::milliseconds timeout)
        {
            {
                std::scoped_lock lock(transfer->State->Mutex);
                transfer->State->Cancelled = true;
                transfer->State->Changed.notify_all();
            }
            [transfer->Task cancel];
            [transfer->Session invalidateAndCancel];
            std::unique_lock lock(transfer->State->Mutex);
            transfer->State->Changed.wait_for(lock, timeout, [&] { return transfer->State->Finished; });
        }

        [[nodiscard]] HubResult<std::shared_ptr<MacTransfer>>
        StartTransfer(const NativeHttpTransportOptions& options, const std::string& url,
                      const std::vector<CatalogHttpHeader>& headers, const std::string_view method = "GET",
                      const std::span<const std::byte> requestBody = {}, const bool allowRedirects = true)
        {
            auto parsed = ParseHttpUrl(url, options.AllowInsecureLoopbackDevelopment);
            if (!parsed)
                return HubResult<std::shared_ptr<MacTransfer>>::Failure(parsed.Error());
            @autoreleasepool
            {
                auto state = std::make_shared<MacStreamState>();
                state->CurrentUrl = url;
                state->BufferLimit = options.DownloadBufferBytes;
                state->MaximumHeaderBytes = options.MaximumHeaderBytes;
                state->MaximumRedirects = allowRedirects ? options.MaximumRedirects : 0U;
                state->AllowInsecureLoopback = options.AllowInsecureLoopbackDevelopment;
                auto transfer = std::make_shared<MacTransfer>();
                transfer->State = state;
                transfer->Delegate = [[KeireHubHttpDelegate alloc] initWithState:state];

                auto* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
                configuration.timeoutIntervalForRequest = std::chrono::duration<double>(options.IdleTimeout).count();
                configuration.timeoutIntervalForResource = 7.0 * 24.0 * 60.0 * 60.0;
                configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
                if (parsed.Value().Loopback)
                    configuration.connectionProxyDictionary = @{};
                else if (options.CustomProxyUrl)
                {
                    auto proxy = ParseProxyUrl(*options.CustomProxyUrl);
                    if (!proxy)
                        return HubResult<std::shared_ptr<MacTransfer>>::Failure(proxy.Error());
                    auto host = proxy.Value().Host;
                    if (host.starts_with('[') && host.ends_with(']'))
                        host = host.substr(1U, host.size() - 2U);
                    const auto port = proxy.Value().Port.value_or(proxy.Value().Secure ? 443U : 80U);
                    NSString* proxyHost = String(host);
                    configuration.connectionProxyDictionary = @{
                        (__bridge NSString*)kCFNetworkProxiesHTTPEnable : @YES,
                        (__bridge NSString*)kCFNetworkProxiesHTTPProxy : proxyHost,
                        (__bridge NSString*)kCFNetworkProxiesHTTPPort : @(port),
                        (__bridge NSString*)kCFNetworkProxiesHTTPSEnable : @YES,
                        (__bridge NSString*)kCFNetworkProxiesHTTPSProxy : proxyHost,
                        (__bridge NSString*)kCFNetworkProxiesHTTPSPort : @(port)
                    };
                }
                auto* queue = [[NSOperationQueue alloc] init];
                queue.maxConcurrentOperationCount = 1;
                transfer->Session = [NSURLSession sessionWithConfiguration:configuration
                                                                  delegate:transfer->Delegate
                                                             delegateQueue:queue];
                if (!transfer->Session)
                {
                    return HubResult<std::shared_ptr<MacTransfer>>::Failure(
                        HttpCatalogError("Foundation could not create an HTTP session."));
                }
                NSString* urlText = String(url);
                NSURL* nativeUrl = [NSURL URLWithString:urlText];
                if (!nativeUrl)
                    return HubResult<std::shared_ptr<MacTransfer>>::Failure(
                        HttpCatalogError("Foundation rejected the request URL.", false));
                auto* request =
                    [NSMutableURLRequest requestWithURL:nativeUrl
                                            cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                        timeoutInterval:std::chrono::duration<double>(options.IdleTimeout).count()];
                NSString* methodText = String(std::string(method));
                if (!methodText)
                    return HubResult<std::shared_ptr<MacTransfer>>::Failure(
                        HttpCatalogError("The HTTP method is not valid UTF-8.", false));
                request.HTTPMethod = methodText;
                if (!requestBody.empty())
                    request.HTTPBody = [NSData dataWithBytes:requestBody.data() length:requestBody.size()];
                for (const auto& header : headers)
                {
                    NSString* name = String(header.Name);
                    NSString* value = String(header.Value);
                    if (!name || !value)
                        return HubResult<std::shared_ptr<MacTransfer>>::Failure(
                            HttpCatalogError("A request header is not valid UTF-8.", false));
                    [request setValue:value forHTTPHeaderField:name];
                }
                transfer->Task = [transfer->Session dataTaskWithRequest:request];
                if (!transfer->Task)
                {
                    [transfer->Session invalidateAndCancel];
                    return HubResult<std::shared_ptr<MacTransfer>>::Failure(
                        HttpCatalogError("Foundation could not create an HTTP request."));
                }
                [transfer->Task resume];
                {
                    std::unique_lock lock(state->Mutex);
                    const auto ready =
                        state->Changed.wait_for(lock, options.ConnectTimeout, [&]
                                                { return state->HeaderComplete || state->Finished || state->Failure; });
                    if (!ready)
                    {
                        lock.unlock();
                        StopTransfer(transfer, options.IdleTimeout);
                        return HubResult<std::shared_ptr<MacTransfer>>::Failure(
                            HttpCatalogError("Foundation timed out before receiving response headers."));
                    }
                    if (state->Failure || (!state->HeaderComplete && state->Finished))
                    {
                        auto error =
                            state->Failure.value_or(HttpCatalogError("The response ended before its HTTP headers."));
                        lock.unlock();
                        StopTransfer(transfer, options.IdleTimeout);
                        return HubResult<std::shared_ptr<MacTransfer>>::Failure(std::move(error));
                    }
                }
                return HubResult<std::shared_ptr<MacTransfer>>::Success(std::move(transfer));
            }
        }

        [[nodiscard]] HubResult<std::size_t> ReadTransfer(const std::shared_ptr<MacTransfer>& transfer,
                                                          const std::span<std::byte> destination)
        {
            if (destination.empty())
                return HubResult<std::size_t>::Success(0U);
            std::unique_lock lock(transfer->State->Mutex);
            transfer->State->Changed.wait(lock,
                                          [&]
                                          {
                                              return transfer->State->BufferOffset < transfer->State->Buffer.size() ||
                                                     transfer->State->Finished || transfer->State->Failure;
                                          });
            const auto available = transfer->State->Buffer.size() - transfer->State->BufferOffset;
            if (available == 0U)
            {
                if (transfer->State->Failure)
                    return HubResult<std::size_t>::Failure(*transfer->State->Failure);
                return HubResult<std::size_t>::Success(0U);
            }
            const auto count = std::min(destination.size(), available);
            std::memcpy(destination.data(), transfer->State->Buffer.data() + transfer->State->BufferOffset, count);
            transfer->State->BufferOffset += count;
            if (transfer->State->BufferOffset == transfer->State->Buffer.size())
            {
                transfer->State->Buffer.clear();
                transfer->State->BufferOffset = 0U;
            }
            transfer->State->Changed.notify_all();
            return HubResult<std::size_t>::Success(count);
        }

        class MacDownloadStream final : public DownloadByteStream
        {
          public:
            MacDownloadStream(std::shared_ptr<MacTransfer> transfer, const std::uint64_t bodyBytes,
                              const std::chrono::milliseconds shutdownTimeout)
                : m_Transfer(std::move(transfer)), m_Remaining(bodyBytes), m_ShutdownTimeout(shutdownTimeout)
            {
                std::scoped_lock lock(m_Transfer->State->Mutex);
                m_Transfer->State->MaximumBodyBytes = bodyBytes;
                if (m_Transfer->State->ReceivedBytes > bodyBytes)
                {
                    m_Transfer->State->Failure = HttpDownloadError(HubErrorCode::DownloadSizeMismatch,
                                                                   "The package response exceeds Content-Length.");
                }
                m_Transfer->State->Changed.notify_all();
            }

            ~MacDownloadStream() override { StopTransfer(m_Transfer, m_ShutdownTimeout); }

            HubResult<std::size_t> Read(const std::span<std::byte> destination) override
            {
                if (destination.empty() || m_Remaining == 0U)
                    return HubResult<std::size_t>::Success(0U);
                const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(destination.size(), m_Remaining));
                auto read = ReadTransfer(m_Transfer, destination.first(size));
                if (!read)
                {
                    if (read.Error().Code == HubErrorCode::CatalogTransportFailed)
                    {
                        return HubResult<std::size_t>::Failure(
                            HttpDownloadError(HubErrorCode::DownloadUnavailable, read.Error().TechnicalDetails, true));
                    }
                    return read;
                }
                if (read.Value() == 0U)
                {
                    return HubResult<std::size_t>::Failure(HttpDownloadError(
                        HubErrorCode::DownloadSizeMismatch, "The package response ended before Content-Length."));
                }
                m_Remaining -= read.Value();
                return read;
            }

          private:
            std::shared_ptr<MacTransfer> m_Transfer;
            std::uint64_t m_Remaining = 0;
            std::chrono::milliseconds m_ShutdownTimeout{0};
        };

        [[nodiscard]] bool ValidOutboundValue(const std::string_view value) noexcept
        {
            return value.size() <= 512U && std::ranges::all_of(value, [](const unsigned char character)
                                                               { return character >= 0x20U && character <= 0x7eU; });
        }

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
            (request.IfNoneMatch && !ValidOutboundValue(*request.IfNoneMatch)))
        {
            return HubResult<CatalogHttpResponse>::Failure(
                HttpCatalogError("The catalog request violates the transport limits.", false));
        }
        std::vector<CatalogHttpHeader> headers{{"Accept-Encoding", "identity"}};
        if (request.IfNoneMatch)
            headers.push_back({"If-None-Match", *request.IfNoneMatch});
        auto transfer = StartTransfer(options, request.Url, headers);
        if (!transfer)
            return HubResult<CatalogHttpResponse>::Failure(transfer.Error());
        CatalogHttpResponse result;
        {
            std::scoped_lock lock(transfer.Value()->State->Mutex);
            result.StatusCode = transfer.Value()->State->StatusCode;
            result.EffectiveUrl = transfer.Value()->State->EffectiveUrl;
            result.Headers = transfer.Value()->State->Headers;
        }
        if (auto status = ValidateHttpHeaders(result.Headers, options.MaximumHeaderBytes); !status)
        {
            StopTransfer(transfer.Value(), options.IdleTimeout);
            return HubResult<CatalogHttpResponse>::Failure(status.Error());
        }
        if (auto status = ValidateIdentityHttpEncoding(result.Headers); !status)
        {
            StopTransfer(transfer.Value(), options.IdleTimeout);
            return HubResult<CatalogHttpResponse>::Failure(status.Error());
        }
        std::vector<std::byte> buffer(std::min<std::size_t>(options.DownloadBufferBytes, 64U * 1024U));
        while (result.StatusCode != 304U)
        {
            auto read = ReadTransfer(transfer.Value(), buffer);
            if (!read)
            {
                StopTransfer(transfer.Value(), options.IdleTimeout);
                return HubResult<CatalogHttpResponse>::Failure(read.Error());
            }
            if (read.Value() == 0U)
                break;
            if (read.Value() >
                request.MaximumResponseBytes - std::min(result.Body.size(), request.MaximumResponseBytes))
            {
                StopTransfer(transfer.Value(), options.IdleTimeout);
                return HubResult<CatalogHttpResponse>::Failure(
                    HttpCatalogError("The catalog response exceeds its size limit.", false));
            }
            result.Body.insert(result.Body.end(), buffer.begin(),
                               buffer.begin() + static_cast<std::ptrdiff_t>(read.Value()));
        }
        StopTransfer(transfer.Value(), options.IdleTimeout);
        return HubResult<CatalogHttpResponse>::Success(std::move(result));
    }

    HubResult<NativeHttpResponse> SendRequestNative(const NativeHttpTransportOptions& options,
                                                    const NativeHttpRequest& request)
    {
        if (request.Url.empty() || request.Body.size() > MaximumRequestBodyBytes ||
            request.MaximumResponseBytes == 0U || request.MaximumResponseBytes > MaximumResponseBodyBytes ||
            request.Headers.size() > 32U)
        {
            return HubResult<NativeHttpResponse>::Failure(
                HttpCatalogError("The HTTP request violates the transport limits.", false));
        }
        if (auto parsed = ParseHttpUrl(request.Url, options.AllowInsecureLoopbackDevelopment); !parsed)
            return HubResult<NativeHttpResponse>::Failure(parsed.Error());
        std::vector<CatalogHttpHeader> headers{{"Accept-Encoding", "identity"}};
        headers.insert(headers.end(), request.Headers.begin(), request.Headers.end());
        if (auto status = ValidateHttpHeaders(headers, options.MaximumHeaderBytes); !status)
            return HubResult<NativeHttpResponse>::Failure(status.Error());
        const auto method = request.Method == NativeHttpMethod::Get     ? std::string_view("GET")
                            : request.Method == NativeHttpMethod::Post  ? std::string_view("POST")
                            : request.Method == NativeHttpMethod::Patch ? std::string_view("PATCH")
                                                                        : std::string_view("DELETE");
        auto transfer = StartTransfer(options, request.Url, headers, method, request.Body, false);
        if (!transfer)
            return HubResult<NativeHttpResponse>::Failure(transfer.Error());
        NativeHttpResponse result;
        {
            std::scoped_lock lock(transfer.Value()->State->Mutex);
            result.StatusCode = transfer.Value()->State->StatusCode;
            result.EffectiveUrl = transfer.Value()->State->EffectiveUrl;
            result.Headers = transfer.Value()->State->Headers;
        }
        if (auto status = ValidateHttpHeaders(result.Headers, options.MaximumHeaderBytes); !status)
        {
            StopTransfer(transfer.Value(), options.IdleTimeout);
            return HubResult<NativeHttpResponse>::Failure(status.Error());
        }
        if (auto status = ValidateIdentityHttpEncoding(result.Headers); !status)
        {
            StopTransfer(transfer.Value(), options.IdleTimeout);
            return HubResult<NativeHttpResponse>::Failure(status.Error());
        }
        std::vector<std::byte> buffer(std::min<std::size_t>(options.DownloadBufferBytes, 64U * 1024U));
        for (;;)
        {
            auto read = ReadTransfer(transfer.Value(), buffer);
            if (!read)
            {
                StopTransfer(transfer.Value(), options.IdleTimeout);
                return HubResult<NativeHttpResponse>::Failure(read.Error());
            }
            if (read.Value() == 0U)
                break;
            if (read.Value() >
                request.MaximumResponseBytes - std::min(result.Body.size(), request.MaximumResponseBytes))
            {
                StopTransfer(transfer.Value(), options.IdleTimeout);
                return HubResult<NativeHttpResponse>::Failure(
                    HttpCatalogError("The HTTP response exceeds its size limit.", false));
            }
            result.Body.insert(result.Body.end(), buffer.begin(),
                               buffer.begin() + static_cast<std::ptrdiff_t>(read.Value()));
        }
        StopTransfer(transfer.Value(), options.IdleTimeout);
        return HubResult<NativeHttpResponse>::Success(std::move(result));
    }

    HubResult<DownloadTransportResponse> OpenDownloadNative(const NativeHttpTransportOptions& options,
                                                            const DownloadTransportRequest& request)
    {
        if ((request.Offset != 0U && !IsStrongHttpETag(request.IfRange)) || !ValidOutboundValue(request.IfRange))
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
        auto transfer = StartTransfer(options, request.Url, makeHeaders(true));
        if (!transfer)
            return HubResult<DownloadTransportResponse>::Failure(CatalogToDownload(transfer.Error()));
        std::uint16_t statusCode = 0;
        std::vector<CatalogHttpHeader> headers;
        {
            std::scoped_lock lock(transfer.Value()->State->Mutex);
            statusCode = transfer.Value()->State->StatusCode;
            headers = transfer.Value()->State->Headers;
        }
        if (statusCode == 416U && request.Offset != 0U)
        {
            StopTransfer(transfer.Value(), options.IdleTimeout);
            transfer = StartTransfer(options, request.Url, makeHeaders(false));
            if (!transfer)
                return HubResult<DownloadTransportResponse>::Failure(CatalogToDownload(transfer.Error()));
            std::scoped_lock lock(transfer.Value()->State->Mutex);
            statusCode = transfer.Value()->State->StatusCode;
            headers = transfer.Value()->State->Headers;
        }
        if (auto status = ValidateHttpHeaders(headers, options.MaximumHeaderBytes); !status)
        {
            StopTransfer(transfer.Value(), options.IdleTimeout);
            return HubResult<DownloadTransportResponse>::Failure(CatalogToDownload(status.Error()));
        }
        auto metadata = ParseDownloadResponse(statusCode, headers, request.Offset);
        if (!metadata)
        {
            StopTransfer(transfer.Value(), options.IdleTimeout);
            return HubResult<DownloadTransportResponse>::Failure(metadata.Error());
        }
        auto body =
            std::make_unique<MacDownloadStream>(transfer.Value(), metadata.Value().BodyBytes, options.IdleTimeout);
        return HubResult<DownloadTransportResponse>::Success({.AcceptedOffset = metadata.Value().AcceptedOffset,
                                                              .TotalBytes = metadata.Value().TotalBytes,
                                                              .ETag = std::move(metadata.Value().ETag),
                                                              .Body = std::move(body)});
    }
} // namespace KeireHub::Detail
#endif
