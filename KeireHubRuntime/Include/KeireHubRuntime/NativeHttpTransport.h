#pragma once

#include "KeireHubRuntime/CatalogClient.h"
#include "KeireHubRuntime/DownloadManager.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace KeireHub
{
    struct NativeHttpTransportOptions final
    {
        std::optional<std::string> CustomProxyUrl;
        bool AllowInsecureLoopbackDevelopment = false;
        std::size_t MaximumHeaderBytes = 64U * 1024U;
        std::size_t DownloadBufferBytes = 256U * 1024U;
        std::uint32_t MaximumRedirects = 5;
        std::chrono::milliseconds ConnectTimeout{15'000};
        std::chrono::milliseconds IdleTimeout{30'000};
    };

    class NativeHttpTransport final : public DownloadTransport
    {
      public:
        [[nodiscard]] static HubResult<NativeHttpTransport> Create(NativeHttpTransportOptions options = {});
        [[nodiscard]] static HubStatus ValidateOptions(const NativeHttpTransportOptions& options);

        [[nodiscard]] CatalogTransport CreateCatalogTransport() const;
        [[nodiscard]] HubResult<CatalogHttpResponse> FetchCatalog(const CatalogHttpRequest& request) const;
        [[nodiscard]] HubResult<DownloadTransportResponse> Open(const DownloadTransportRequest& request) override;

      private:
        struct Impl;
        explicit NativeHttpTransport(std::shared_ptr<const Impl> implementation);

        std::shared_ptr<const Impl> m_Impl;
    };
} // namespace KeireHub
