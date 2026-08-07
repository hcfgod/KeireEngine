#include "KeireHubRuntime/NativeHttpTransport.h"

#include "NativeHttpTransportPlatform.h"
#include "NativeHttpTransportPolicy.h"

#include <exception>
#include <utility>

namespace KeireHub
{
    struct NativeHttpTransport::Impl final
    {
        NativeHttpTransportOptions Options;
    };

    HubResult<NativeHttpTransport> NativeHttpTransport::Create(NativeHttpTransportOptions options)
    {
        try
        {
            if (auto status = ValidateOptions(options); !status)
                return HubResult<NativeHttpTransport>::Failure(status.Error());
            return HubResult<NativeHttpTransport>::Success(
                NativeHttpTransport(std::make_shared<const Impl>(Impl{.Options = std::move(options)})));
        }
        catch (...)
        {
            return HubResult<NativeHttpTransport>::Failure(
                Detail::HttpCatalogError("The native HTTP transport could not be initialized."));
        }
    }

    HubStatus NativeHttpTransport::ValidateOptions(const NativeHttpTransportOptions& options)
    {
        constexpr auto minimumTimeout = std::chrono::milliseconds(100);
        constexpr auto maximumTimeout = std::chrono::minutes(10);
        if (options.MaximumHeaderBytes < 4U * 1024U || options.MaximumHeaderBytes > 256U * 1024U ||
            options.DownloadBufferBytes < 16U * 1024U || options.DownloadBufferBytes > 4U * 1024U * 1024U ||
            options.MaximumRedirects > 10U || options.ConnectTimeout < minimumTimeout ||
            options.ConnectTimeout > maximumTimeout || options.IdleTimeout < minimumTimeout ||
            options.IdleTimeout > maximumTimeout)
        {
            return HubStatus::Failure({.Code = HubErrorCode::DistributionConfigurationInvalid,
                                       .Message = "The native HTTP transport configuration is invalid.",
                                       .AffectedItem = "http-transport"});
        }
        if (options.CustomProxyUrl)
        {
            auto proxy = Detail::ParseProxyUrl(*options.CustomProxyUrl);
            if (!proxy)
                return HubStatus::Failure(proxy.Error());
        }
        return HubStatus::Success();
    }

    CatalogTransport NativeHttpTransport::CreateCatalogTransport() const
    {
        const auto implementation = m_Impl;
        return [implementation](const CatalogHttpRequest& request)
        {
            try
            {
                return Detail::FetchCatalogNative(implementation->Options, request);
            }
            catch (...)
            {
                return HubResult<CatalogHttpResponse>::Failure(
                    Detail::HttpCatalogError("The native catalog request failed unexpectedly."));
            }
        };
    }

    HubResult<CatalogHttpResponse> NativeHttpTransport::FetchCatalog(const CatalogHttpRequest& request) const
    {
        try
        {
            return Detail::FetchCatalogNative(m_Impl->Options, request);
        }
        catch (...)
        {
            return HubResult<CatalogHttpResponse>::Failure(
                Detail::HttpCatalogError("The native catalog request failed unexpectedly."));
        }
    }

    HubResult<NativeHttpResponse> NativeHttpTransport::Send(const NativeHttpRequest& request) const
    {
        try
        {
            return Detail::SendRequestNative(m_Impl->Options, request);
        }
        catch (...)
        {
            return HubResult<NativeHttpResponse>::Failure(
                Detail::HttpCatalogError("The native HTTP request failed unexpectedly."));
        }
    }

    HubResult<DownloadTransportResponse> NativeHttpTransport::Open(const DownloadTransportRequest& request)
    {
        try
        {
            return Detail::OpenDownloadNative(m_Impl->Options, request);
        }
        catch (...)
        {
            return HubResult<DownloadTransportResponse>::Failure(Detail::HttpDownloadError(
                HubErrorCode::DownloadUnavailable, "The native package request failed unexpectedly.", true));
        }
    }

    NativeHttpTransport::NativeHttpTransport(std::shared_ptr<const Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }
} // namespace KeireHub
