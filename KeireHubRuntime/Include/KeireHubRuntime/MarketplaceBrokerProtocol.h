#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace KeireHub
{
    enum class MarketplaceBrokerTransport : std::uint8_t
    {
        WindowsNamedPipe,
        UnixDomainSocket
    };

    struct MarketplaceBrokerEndpoint final
    {
        MarketplaceBrokerTransport Transport = MarketplaceBrokerTransport::UnixDomainSocket;
        std::filesystem::path Address;
        bool CurrentUserOnly = true;
    };

    enum class MarketplaceBrokerRequestKind : std::uint8_t
    {
        Hello,
        CatalogSnapshot,
        LibrarySnapshot,
        DownloadStatus,
        VerifiedCachePath
    };

    struct MarketplaceBrokerRequest final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        MarketplaceBrokerRequestKind Kind = MarketplaceBrokerRequestKind::Hello;
        std::string RequestId;
        std::string ClientNonce;
        std::string SessionNonce;
        std::string ProductId;
        std::string PackageId;
        std::string Version;
    };

    struct MarketplaceBrokerResponse final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::string RequestId;
        bool Success = true;
        std::string SessionNonce;
        std::string Snapshot;
        std::filesystem::path VerifiedCachePath;
        std::string ArchiveSha256;
        std::uint64_t ArchiveSizeBytes = 0;
        std::string DownloadState;
        std::string ErrorCode;
        std::string ErrorMessage;
    };

    [[nodiscard]] MarketplaceBrokerEndpoint ResolveMarketplaceBrokerEndpoint(const std::filesystem::path& userDataRoot,
                                                                             std::string_view userIdentityHash);
    [[nodiscard]] HubResult<std::string> EncodeMarketplaceBrokerRequest(const MarketplaceBrokerRequest& request);
    [[nodiscard]] HubResult<MarketplaceBrokerRequest> DecodeMarketplaceBrokerRequest(std::string_view document);
    [[nodiscard]] HubResult<std::string> EncodeMarketplaceBrokerResponse(const MarketplaceBrokerResponse& response);
    [[nodiscard]] HubResult<MarketplaceBrokerResponse> DecodeMarketplaceBrokerResponse(std::string_view document);

    class MarketplaceBrokerHandshake final
    {
      public:
        explicit MarketplaceBrokerHandshake(std::string serverNonce);

        MarketplaceBrokerHandshake(const MarketplaceBrokerHandshake&) = delete;
        MarketplaceBrokerHandshake& operator=(const MarketplaceBrokerHandshake&) = delete;

        [[nodiscard]] HubResult<MarketplaceBrokerResponse> AcceptHello(const MarketplaceBrokerRequest& request);
        [[nodiscard]] HubStatus Authorize(const MarketplaceBrokerRequest& request) const;

      private:
        std::string m_ServerNonce;
        std::string m_ClientNonce;
        std::string m_SessionNonce;
        bool m_Complete = false;
    };
} // namespace KeireHub
