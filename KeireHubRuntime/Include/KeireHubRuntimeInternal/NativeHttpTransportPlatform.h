#pragma once

#include "KeireHubRuntime/NativeHttpTransport.h"

namespace KeireHub::Detail
{
    [[nodiscard]] HubResult<CatalogHttpResponse> FetchCatalogNative(const NativeHttpTransportOptions& options,
                                                                    const CatalogHttpRequest& request);
    [[nodiscard]] HubResult<NativeHttpResponse> SendRequestNative(const NativeHttpTransportOptions& options,
                                                                  const NativeHttpRequest& request);
    [[nodiscard]] HubResult<DownloadTransportResponse> OpenDownloadNative(const NativeHttpTransportOptions& options,
                                                                          const DownloadTransportRequest& request);
} // namespace KeireHub::Detail
