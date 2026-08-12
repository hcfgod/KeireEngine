#include "KeireHubRuntime/MarketplaceBrokerProtocol.h"

#include <KeireHubRuntimeInternal/Persistence.h>
#include <KeireHubRuntimeInternal/Sha256.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumBrokerMessageBytes = 2U * 1024U * 1024U;

        [[nodiscard]] HubError BrokerError(const std::string_view details)
        {
            return {.Code = HubErrorCode::WorkerProtocolInvalid,
                    .Message = "The local Hub marketplace broker rejected a message.",
                    .AffectedItem = "marketplace-broker",
                    .TechnicalDetails = std::string(details)};
        }

        [[nodiscard]] bool IsIdentifier(const std::string_view value, const std::size_t maximum = 128U) noexcept
        {
            return !value.empty() && value.size() <= maximum &&
                   std::ranges::all_of(
                       value, [](const unsigned char character)
                       { return std::isalnum(character) || character == '-' || character == '_' || character == '.'; });
        }

        [[nodiscard]] bool IsNonce(const std::string_view value) noexcept
        {
            return value.size() >= 32U && value.size() <= 256U &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return std::isalnum(character) || character == '-' || character == '_'; });
        }

        [[nodiscard]] bool IsSha256(const std::string_view value) noexcept
        {
            return value.size() == 64U &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return std::isdigit(character) || (character >= 'a' && character <= 'f'); });
        }

        [[nodiscard]] std::string_view KindName(const MarketplaceBrokerRequestKind kind) noexcept
        {
            switch (kind)
            {
            case MarketplaceBrokerRequestKind::Hello:
                return "hello";
            case MarketplaceBrokerRequestKind::CatalogSnapshot:
                return "catalogSnapshot";
            case MarketplaceBrokerRequestKind::LibrarySnapshot:
                return "librarySnapshot";
            case MarketplaceBrokerRequestKind::DownloadStatus:
                return "downloadStatus";
            case MarketplaceBrokerRequestKind::VerifiedCachePath:
                return "verifiedCachePath";
            }
            return "invalid";
        }

        [[nodiscard]] MarketplaceBrokerRequestKind ParseKind(const std::string_view value)
        {
            if (value == "hello")
                return MarketplaceBrokerRequestKind::Hello;
            if (value == "catalogSnapshot")
                return MarketplaceBrokerRequestKind::CatalogSnapshot;
            if (value == "librarySnapshot")
                return MarketplaceBrokerRequestKind::LibrarySnapshot;
            if (value == "downloadStatus")
                return MarketplaceBrokerRequestKind::DownloadStatus;
            if (value == "verifiedCachePath")
                return MarketplaceBrokerRequestKind::VerifiedCachePath;
            throw std::invalid_argument("Unknown marketplace broker request kind.");
        }

        [[nodiscard]] HubStatus ValidateRequest(const MarketplaceBrokerRequest& request)
        {
            if (!IsIdentifier(request.RequestId))
                return HubStatus::Failure(BrokerError("Request ID is invalid."));
            if (request.Kind == MarketplaceBrokerRequestKind::Hello)
            {
                if (!IsNonce(request.ClientNonce) || !request.SessionNonce.empty() || !request.ProductId.empty() ||
                    !request.PackageId.empty() || !request.Version.empty())
                {
                    return HubStatus::Failure(BrokerError("Hello request fields are invalid."));
                }
                return HubStatus::Success();
            }
            if (!IsNonce(request.SessionNonce) || !request.ClientNonce.empty())
                return HubStatus::Failure(BrokerError("Authenticated broker request nonce is invalid."));
            if (request.Kind == MarketplaceBrokerRequestKind::CatalogSnapshot ||
                request.Kind == MarketplaceBrokerRequestKind::LibrarySnapshot)
            {
                if ((!request.ProductId.empty() && !IsIdentifier(request.ProductId)) || !request.PackageId.empty() ||
                    !request.Version.empty())
                    return HubStatus::Failure(BrokerError("Snapshot request fields are invalid."));
            }
            else if (!IsIdentifier(request.PackageId) || request.Version.empty() || request.Version.size() > 128U)
                return HubStatus::Failure(BrokerError("Package status request fields are invalid."));
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus ValidateResponse(const MarketplaceBrokerResponse& response)
        {
            if (!IsIdentifier(response.RequestId) ||
                (!response.SessionNonce.empty() && !IsNonce(response.SessionNonce)) ||
                response.Snapshot.size() > MaximumBrokerMessageBytes || response.DownloadState.size() > 64U)
            {
                return HubStatus::Failure(BrokerError("Broker response fields are invalid."));
            }
            if (response.Success)
            {
                if (!response.ErrorCode.empty() || !response.ErrorMessage.empty() ||
                    (!response.VerifiedCachePath.empty() &&
                     (!response.VerifiedCachePath.is_absolute() || !IsSha256(response.ArchiveSha256) ||
                      response.ArchiveSizeBytes == 0U)))
                {
                    return HubStatus::Failure(BrokerError("Successful broker response fields are invalid."));
                }
            }
            else if (!IsIdentifier(response.ErrorCode, 256U) || response.ErrorMessage.empty() ||
                     response.ErrorMessage.size() > 4096U || !response.Snapshot.empty() ||
                     !response.VerifiedCachePath.empty())
            {
                return HubStatus::Failure(BrokerError("Failed broker response fields are invalid."));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] std::string SessionNonce(const std::string_view clientNonce, const std::string_view serverNonce)
        {
            Detail::Sha256Builder builder;
            constexpr std::string_view Context = "keire-marketplace-broker-v1\n";
            builder.Update(std::as_bytes(std::span(Context.data(), Context.size())));
            builder.Update(std::as_bytes(std::span(clientNonce.data(), clientNonce.size())));
            builder.Update(std::as_bytes(std::span(serverNonce.data(), serverNonce.size())));
            return Detail::DigestToString(builder.Finish());
        }

        [[nodiscard]] bool ConstantTimeEqual(const std::string_view left, const std::string_view right) noexcept
        {
            const auto maximum = std::max(left.size(), right.size());
            std::size_t difference = left.size() ^ right.size();
            for (std::size_t index = 0; index < maximum; ++index)
            {
                const auto a = index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
                const auto b = index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
                difference |= a ^ b;
            }
            return difference == 0U;
        }
    } // namespace

    MarketplaceBrokerEndpoint ResolveMarketplaceBrokerEndpoint(const std::filesystem::path& userDataRoot,
                                                               const std::string_view userIdentityHash)
    {
        if (userDataRoot.empty() || !IsSha256(userIdentityHash))
            throw std::invalid_argument("Marketplace broker endpoint identity is invalid.");
#if defined(_WIN32)
        return {.Transport = MarketplaceBrokerTransport::WindowsNamedPipe,
                .Address = Detail::PathFromUtf8("\\\\.\\pipe\\KeireHub.Marketplace." +
                                                std::string(userIdentityHash.substr(0U, 24U)))};
#else
        return {.Transport = MarketplaceBrokerTransport::UnixDomainSocket,
                .Address =
                    userDataRoot / "run" /
                    Detail::PathFromUtf8("marketplace-" + std::string(userIdentityHash.substr(0U, 24U)) + ".sock")};
#endif
    }

    HubResult<std::string> EncodeMarketplaceBrokerRequest(const MarketplaceBrokerRequest& request)
    {
        if (const auto status = ValidateRequest(request); !status)
            return HubResult<std::string>::Failure(status.Error());
        Detail::Json document{{"schemaVersion", MarketplaceBrokerRequest::CurrentSchemaVersion},
                              {"kind", KindName(request.Kind)},
                              {"requestId", request.RequestId}};
        if (!request.ClientNonce.empty())
            document["clientNonce"] = request.ClientNonce;
        if (!request.SessionNonce.empty())
            document["sessionNonce"] = request.SessionNonce;
        if (!request.ProductId.empty())
            document["productId"] = request.ProductId;
        if (!request.PackageId.empty())
            document["packageId"] = request.PackageId;
        if (!request.Version.empty())
            document["version"] = request.Version;
        return HubResult<std::string>::Success(document.dump() + '\n');
    }

    HubResult<MarketplaceBrokerRequest> DecodeMarketplaceBrokerRequest(const std::string_view document)
    {
        try
        {
            if (document.empty() || document.size() > MaximumBrokerMessageBytes)
                throw std::invalid_argument("Broker request size is invalid.");
            const auto value = Detail::Json::parse(document);
            MarketplaceBrokerRequest result{.Kind = ParseKind(value.at("kind").get<std::string>()),
                                            .RequestId = value.at("requestId").get<std::string>(),
                                            .ClientNonce = value.value("clientNonce", std::string{}),
                                            .SessionNonce = value.value("sessionNonce", std::string{}),
                                            .ProductId = value.value("productId", std::string{}),
                                            .PackageId = value.value("packageId", std::string{}),
                                            .Version = value.value("version", std::string{})};
            if (value.at("schemaVersion").get<std::uint32_t>() != MarketplaceBrokerRequest::CurrentSchemaVersion ||
                value.size() != 3U + (!result.ClientNonce.empty() ? 1U : 0U) +
                                    (!result.SessionNonce.empty() ? 1U : 0U) + (!result.ProductId.empty() ? 1U : 0U) +
                                    (!result.PackageId.empty() ? 1U : 0U) + (!result.Version.empty() ? 1U : 0U))
            {
                throw std::invalid_argument("Broker request schema or fields are invalid.");
            }
            if (const auto status = ValidateRequest(result); !status)
                return HubResult<MarketplaceBrokerRequest>::Failure(status.Error());
            return HubResult<MarketplaceBrokerRequest>::Success(std::move(result));
        }
        catch (const std::exception& exception)
        {
            return HubResult<MarketplaceBrokerRequest>::Failure(BrokerError(exception.what()));
        }
    }

    HubResult<std::string> EncodeMarketplaceBrokerResponse(const MarketplaceBrokerResponse& response)
    {
        if (const auto status = ValidateResponse(response); !status)
            return HubResult<std::string>::Failure(status.Error());
        Detail::Json document{{"schemaVersion", MarketplaceBrokerResponse::CurrentSchemaVersion},
                              {"requestId", response.RequestId},
                              {"success", response.Success}};
        if (!response.SessionNonce.empty())
            document["sessionNonce"] = response.SessionNonce;
        if (!response.Snapshot.empty())
            document["snapshot"] = response.Snapshot;
        if (!response.VerifiedCachePath.empty())
        {
            document["verifiedCachePath"] = Detail::PathToUtf8(response.VerifiedCachePath);
            document["archiveSha256"] = response.ArchiveSha256;
            document["archiveSizeBytes"] = response.ArchiveSizeBytes;
        }
        if (!response.DownloadState.empty())
            document["downloadState"] = response.DownloadState;
        if (!response.Success)
        {
            document["errorCode"] = response.ErrorCode;
            document["errorMessage"] = response.ErrorMessage;
        }
        return HubResult<std::string>::Success(document.dump() + '\n');
    }

    HubResult<MarketplaceBrokerResponse> DecodeMarketplaceBrokerResponse(const std::string_view document)
    {
        try
        {
            if (document.empty() || document.size() > MaximumBrokerMessageBytes)
                throw std::invalid_argument("Broker response size is invalid.");
            const auto value = Detail::Json::parse(document);
            MarketplaceBrokerResponse result{.RequestId = value.at("requestId").get<std::string>(),
                                             .Success = value.at("success").get<bool>(),
                                             .SessionNonce = value.value("sessionNonce", std::string{}),
                                             .Snapshot = value.value("snapshot", std::string{}),
                                             .ArchiveSha256 = value.value("archiveSha256", std::string{}),
                                             .ArchiveSizeBytes = value.value("archiveSizeBytes", std::uint64_t{0}),
                                             .DownloadState = value.value("downloadState", std::string{}),
                                             .ErrorCode = value.value("errorCode", std::string{}),
                                             .ErrorMessage = value.value("errorMessage", std::string{})};
            if (value.contains("verifiedCachePath"))
                result.VerifiedCachePath = Detail::PathFromUtf8(value.at("verifiedCachePath").get<std::string>());
            if (value.at("schemaVersion").get<std::uint32_t>() != MarketplaceBrokerResponse::CurrentSchemaVersion)
                throw std::invalid_argument("Broker response schema is unsupported.");
            if (const auto status = ValidateResponse(result); !status)
                return HubResult<MarketplaceBrokerResponse>::Failure(status.Error());
            return HubResult<MarketplaceBrokerResponse>::Success(std::move(result));
        }
        catch (const std::exception& exception)
        {
            return HubResult<MarketplaceBrokerResponse>::Failure(BrokerError(exception.what()));
        }
    }

    MarketplaceBrokerHandshake::MarketplaceBrokerHandshake(std::string serverNonce)
        : m_ServerNonce(std::move(serverNonce))
    {
        if (!IsNonce(m_ServerNonce))
            throw std::invalid_argument("Marketplace broker server nonce is invalid.");
    }

    HubResult<MarketplaceBrokerResponse>
    MarketplaceBrokerHandshake::AcceptHello(const MarketplaceBrokerRequest& request)
    {
        if (m_Complete)
            return HubResult<MarketplaceBrokerResponse>::Failure(BrokerError("Broker hello has already been used."));
        if (const auto status = ValidateRequest(request);
            !status || request.Kind != MarketplaceBrokerRequestKind::Hello)
            return HubResult<MarketplaceBrokerResponse>::Failure(status ? BrokerError("Expected broker hello.")
                                                                        : status.Error());
        m_ClientNonce = request.ClientNonce;
        m_SessionNonce = SessionNonce(m_ClientNonce, m_ServerNonce);
        m_Complete = true;
        return HubResult<MarketplaceBrokerResponse>::Success(
            {.RequestId = request.RequestId, .Success = true, .SessionNonce = m_SessionNonce});
    }

    HubStatus MarketplaceBrokerHandshake::Authorize(const MarketplaceBrokerRequest& request) const
    {
        if (!m_Complete || request.Kind == MarketplaceBrokerRequestKind::Hello ||
            !ConstantTimeEqual(request.SessionNonce, m_SessionNonce))
        {
            return HubStatus::Failure(BrokerError("Broker request is not bound to this authenticated session."));
        }
        return ValidateRequest(request);
    }
} // namespace KeireHub
