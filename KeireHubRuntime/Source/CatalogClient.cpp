#include "KeireHubRuntime/CatalogClient.h"

#include "KeireHubRuntime/NativeHttpTransport.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/NativeHttpTransportPolicy.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>
#include <ranges>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumCatalogBytes = std::size_t{32} * 1024 * 1024;

        [[nodiscard]] HubError ClientError(const HubErrorCode code, std::string message, std::string item,
                                           std::string details = {}, const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] std::string EndpointItem(const CatalogEndpoint& endpoint)
        {
            if (endpoint.Kind == CatalogDocumentKind::ContentCatalog)
                return "content/" + endpoint.Locale;
            return "catalog/" + endpoint.Channel + '/' + endpoint.Platform + '/' + endpoint.Architecture;
        }

        [[nodiscard]] std::string EndpointUrl(const CatalogEndpoint& endpoint)
        {
            if (endpoint.Kind == CatalogDocumentKind::ContentCatalog)
                return endpoint.ServiceBaseUrl + "/v1/content/" + endpoint.Locale;
            return endpoint.ServiceBaseUrl + "/v2/catalog/" + endpoint.Channel + '/' + endpoint.Platform + '/' +
                   endpoint.Architecture;
        }

        [[nodiscard]] HubResult<std::string> SingleHeader(const CatalogHttpResponse& response,
                                                          const std::string_view requested, const std::string& item)
        {
            const std::string* result = nullptr;
            for (const auto& header : response.Headers)
            {
                if (header.Name.empty() || header.Name.size() > 128U ||
                    std::ranges::any_of(header.Name,
                                        [](const unsigned char value) { return value <= 0x20U || value >= 0x7fU; }))
                {
                    return HubResult<std::string>::Failure(
                        ClientError(HubErrorCode::CatalogSignatureInvalid,
                                    "The distribution response contains malformed headers.", item));
                }
                if (!Detail::EqualsCaseInsensitiveAscii(header.Name, requested))
                    continue;
                if (header.Value.empty() || header.Value.size() > 4096U ||
                    std::ranges::any_of(header.Value, [](const unsigned char value)
                                        { return value == 0U || value == '\r' || value == '\n'; }))
                {
                    return HubResult<std::string>::Failure(
                        ClientError(HubErrorCode::CatalogSignatureInvalid,
                                    "The distribution response contains malformed security metadata.", item,
                                    std::string(requested)));
                }
                if (result)
                {
                    return HubResult<std::string>::Failure(ClientError(
                        HubErrorCode::CatalogSignatureInvalid, "The distribution response repeats security metadata.",
                        item, std::string(requested)));
                }
                result = &header.Value;
            }
            if (!result || std::isspace(static_cast<unsigned char>(result->front())) ||
                std::isspace(static_cast<unsigned char>(result->back())))
            {
                return HubResult<std::string>::Failure(ClientError(
                    HubErrorCode::CatalogSignatureInvalid,
                    "The distribution response is missing required security metadata.", item, std::string(requested)));
            }
            return HubResult<std::string>::Success(*result);
        }

        struct ResponseMetadata final
        {
            CatalogSignatureMetadata Signature;
            std::string ETag;
        };

        [[nodiscard]] HubResult<ResponseMetadata> ParseResponseMetadata(const CatalogHttpResponse& response,
                                                                        const std::string& item)
        {
            auto algorithm = SingleHeader(response, "X-Keire-Signature-Algorithm", item);
            auto keyId = SingleHeader(response, "X-Keire-Signature-Key-Id", item);
            auto signature = SingleHeader(response, "X-Keire-Signature", item);
            auto sequence = SingleHeader(response, "X-Keire-Sequence", item);
            auto expires = SingleHeader(response, "X-Keire-Expires", item);
            auto etag = SingleHeader(response, "ETag", item);
            if (!algorithm)
                return HubResult<ResponseMetadata>::Failure(algorithm.Error());
            if (!keyId)
                return HubResult<ResponseMetadata>::Failure(keyId.Error());
            if (!signature)
                return HubResult<ResponseMetadata>::Failure(signature.Error());
            if (!sequence)
                return HubResult<ResponseMetadata>::Failure(sequence.Error());
            if (!expires)
                return HubResult<ResponseMetadata>::Failure(expires.Error());
            if (!etag)
                return HubResult<ResponseMetadata>::Failure(etag.Error());

            std::uint64_t sequenceValue = 0;
            const auto [end, parseError] = std::from_chars(
                sequence.Value().data(), sequence.Value().data() + sequence.Value().size(), sequenceValue);
            if (parseError != std::errc{} || end != sequence.Value().data() + sequence.Value().size() ||
                sequence.Value() != std::to_string(sequenceValue) ||
                sequenceValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
                !Detail::ParseUtcInstant(expires.Value()))
            {
                return HubResult<ResponseMetadata>::Failure(
                    ClientError(HubErrorCode::CatalogSignatureInvalid,
                                "The distribution response signature metadata is invalid.", item));
            }
            return HubResult<ResponseMetadata>::Success({.Signature = {.Algorithm = std::move(algorithm).Value(),
                                                                       .KeyId = std::move(keyId).Value(),
                                                                       .Signature = std::move(signature).Value(),
                                                                       .Sequence = sequenceValue,
                                                                       .ExpiresAt = std::move(expires).Value()},
                                                         .ETag = std::move(etag).Value()});
        }

        [[nodiscard]] CatalogVerificationPolicy Policy(const CatalogClientOptions& options,
                                                       const std::uint64_t minimumSequence, const bool allowExpired)
        {
            return {.MinimumSequence = minimumSequence,
                    .MinimumRemainingValidity = options.MinimumRemainingValidity,
                    .Now = options.Clock(),
                    .AllowExpired = allowExpired};
        }

        [[nodiscard]] HubResult<VerifiedCatalogDocument> MakeCachedResult(const CatalogEndpoint& endpoint,
                                                                          const CachedCatalogDocument& cached,
                                                                          const bool networkValidated = false)
        {
            return HubResult<VerifiedCatalogDocument>::Success(
                {.Endpoint = endpoint,
                 .ExactBytes = std::make_shared<const std::vector<std::byte>>(cached.ExactBytes),
                 .Signature = cached.Signature,
                 .ETag = cached.ETag,
                 .FromCache = true,
                 .NetworkValidated = networkValidated});
        }

        [[nodiscard]] bool SameSignature(const CatalogSignatureMetadata& left,
                                         const CatalogSignatureMetadata& right) noexcept
        {
            return left.Algorithm == right.Algorithm && left.KeyId == right.KeyId &&
                   left.Signature == right.Signature && left.Sequence == right.Sequence &&
                   left.ExpiresAt == right.ExpiresAt;
        }
    } // namespace

    HubResult<CatalogClient> CatalogClient::Create(CatalogClientOptions options, CatalogTrustStore trustStore,
                                                   CatalogTransport transport)
    {
        auto normalized =
            Detail::NormalizeServiceBaseUrl(options.ServiceBaseUrl, options.AllowInsecureLoopbackDevelopment);
        if (!normalized || !Detail::IsDistributionRouteToken(options.Platform) ||
            !Detail::IsDistributionRouteToken(options.Architecture) || options.CacheRoot.empty() ||
            options.MinimumSequence == 0U || options.MinimumRemainingValidity.count() < 0)
        {
            return HubResult<CatalogClient>::Failure(
                ClientError(HubErrorCode::DistributionConfigurationInvalid,
                            "The distribution catalog client configuration is invalid.", "distribution-service"));
        }
        options.ServiceBaseUrl = std::move(*normalized);
        if (options.CustomProxyUrl)
        {
            const auto proxyStatus = NativeHttpTransport::ValidateOptions(
                {.CustomProxyUrl = options.CustomProxyUrl,
                 .AllowInsecureLoopbackDevelopment = options.AllowInsecureLoopbackDevelopment});
            if (!proxyStatus)
                return HubResult<CatalogClient>::Failure(proxyStatus.Error());
        }
        if (!options.Clock)
            options.Clock = [] { return std::chrono::system_clock::now(); };
        if (!options.Offline && !transport)
        {
            auto nativeTransport = NativeHttpTransport::Create(
                {.CustomProxyUrl = options.CustomProxyUrl,
                 .AllowInsecureLoopbackDevelopment = options.AllowInsecureLoopbackDevelopment});
            if (!nativeTransport)
                return HubResult<CatalogClient>::Failure(nativeTransport.Error());
            transport = nativeTransport.Value().CreateCatalogTransport();
        }
        return HubResult<CatalogClient>::Success(
            CatalogClient(std::move(options), std::move(trustStore), std::move(transport)));
    }

    HubResult<VerifiedCatalogDocument> CatalogClient::FetchPackageCatalog(std::string channel) const
    {
        if (!Detail::IsDistributionRouteToken(channel))
        {
            return HubResult<VerifiedCatalogDocument>::Failure(ClientError(
                HubErrorCode::InvalidArgument, "The requested distribution channel is invalid.", std::move(channel)));
        }
        return Fetch({.Kind = CatalogDocumentKind::PackageCatalog,
                      .ServiceBaseUrl = m_Options.ServiceBaseUrl,
                      .Channel = std::move(channel),
                      .Platform = m_Options.Platform,
                      .Architecture = m_Options.Architecture});
    }

    HubResult<VerifiedCatalogDocument> CatalogClient::FetchContentCatalog(std::string locale) const
    {
        if (!Detail::IsDistributionLocale(locale))
        {
            return HubResult<VerifiedCatalogDocument>::Failure(ClientError(
                HubErrorCode::InvalidArgument, "The requested content locale is invalid.", std::move(locale)));
        }
        return Fetch({.Kind = CatalogDocumentKind::ContentCatalog,
                      .ServiceBaseUrl = m_Options.ServiceBaseUrl,
                      .Locale = std::move(locale)});
    }

    HubResult<VerifiedCatalogDocument> CatalogClient::Fetch(CatalogEndpoint endpoint) const
    {
        const auto item = EndpointItem(endpoint);
        auto loaded = m_Cache.Load(endpoint);
        if (!loaded)
            return HubResult<VerifiedCatalogDocument>::Failure(loaded.Error());
        std::optional<CachedCatalogDocument> cached = std::move(loaded).Value();
        if (cached)
        {
            if (const auto status = m_TrustStore.VerifyExact(endpoint, cached->ExactBytes, cached->Signature,
                                                             Policy(m_Options, m_Options.MinimumSequence, true));
                !status)
            {
                return HubResult<VerifiedCatalogDocument>::Failure(ClientError(
                    HubErrorCode::CatalogCacheInvalid, "The last-known-good catalog cache failed trust validation.",
                    item, std::string(ToString(status.Error().Code)) + ": " + status.Error().TechnicalDetails));
            }
        }
        const auto cachedIsUsable = [&]
        {
            return cached && m_TrustStore.VerifyExact(endpoint, cached->ExactBytes, cached->Signature,
                                                      Policy(m_Options, m_Options.MinimumSequence, false));
        };
        if (m_Options.Offline)
        {
            if (cachedIsUsable())
                return MakeCachedResult(endpoint, *cached);
            if (cached)
            {
                return HubResult<VerifiedCatalogDocument>::Failure(ClientError(
                    HubErrorCode::CatalogExpired, "The cached catalog cannot be used because it has expired.", item));
            }
            return HubResult<VerifiedCatalogDocument>::Failure(
                ClientError(HubErrorCode::CatalogTransportFailed,
                            "Offline mode has no last-known-good catalog for this endpoint.", item, {}, true));
        }

        CatalogHttpRequest request{.Url = EndpointUrl(endpoint),
                                   .IfNoneMatch = cached ? std::optional(cached->ETag) : std::nullopt,
                                   .MaximumResponseBytes = MaximumCatalogBytes};
        auto fetched = m_Transport(request);
        if (!fetched)
        {
            if (cachedIsUsable())
                return MakeCachedResult(endpoint, *cached);
            return HubResult<VerifiedCatalogDocument>::Failure(ClientError(
                HubErrorCode::CatalogTransportFailed, "The distribution service could not be reached.", item,
                std::string(ToString(fetched.Error().Code)) + ": " + fetched.Error().TechnicalDetails, true));
        }
        auto& response = fetched.Value();
        const auto redirectStatus = response.EffectiveUrl.empty() || response.EffectiveUrl == request.Url
                                        ? HubStatus::Success()
                                        : Detail::ValidateHttpRedirect(request.Url, response.EffectiveUrl,
                                                                       m_Options.AllowInsecureLoopbackDevelopment);
        if (!redirectStatus || response.Body.size() > MaximumCatalogBytes)
        {
            return HubResult<VerifiedCatalogDocument>::Failure(
                ClientError(HubErrorCode::CatalogTransportFailed,
                            "The distribution response violated its endpoint or size contract.", item));
        }
        if (response.StatusCode != 200U && response.StatusCode != 304U)
        {
            if (cachedIsUsable())
                return MakeCachedResult(endpoint, *cached);
            if (response.StatusCode == 404U)
            {
                return HubResult<VerifiedCatalogDocument>::Failure(
                    ClientError(HubErrorCode::NotFound, "No catalog has been published for this endpoint.", item));
            }
            return HubResult<VerifiedCatalogDocument>::Failure(ClientError(
                HubErrorCode::CatalogTransportFailed, "The distribution service did not return a usable catalog.", item,
                "HTTP status " + std::to_string(response.StatusCode),
                response.StatusCode == 0U || response.StatusCode == 408U || response.StatusCode == 429U ||
                    response.StatusCode >= 500U));
        }

        auto metadata = ParseResponseMetadata(response, item);
        if (!metadata)
            return HubResult<VerifiedCatalogDocument>::Failure(metadata.Error());
        if (response.StatusCode == 304U)
        {
            if (!cached || !response.Body.empty() || metadata.Value().ETag != cached->ETag ||
                !SameSignature(metadata.Value().Signature, cached->Signature))
            {
                return HubResult<VerifiedCatalogDocument>::Failure(
                    ClientError(HubErrorCode::CatalogSignatureInvalid,
                                "A not-modified response did not match the cached catalog metadata.", item));
            }
            if (const auto status = m_TrustStore.VerifyExact(
                    endpoint, cached->ExactBytes, cached->Signature,
                    Policy(m_Options, std::max(m_Options.MinimumSequence, cached->Signature.Sequence), false));
                !status)
            {
                return HubResult<VerifiedCatalogDocument>::Failure(status.Error());
            }
            return MakeCachedResult(endpoint, *cached, true);
        }

        if (response.Body.empty() || metadata.Value().ETag != Detail::MakeDistributionETag(response.Body))
        {
            return HubResult<VerifiedCatalogDocument>::Failure(
                ClientError(HubErrorCode::CatalogSignatureInvalid,
                            "The catalog response ETag does not match its exact bytes.", item));
        }
        const auto sequenceFloor =
            cached ? std::max(m_Options.MinimumSequence, cached->Signature.Sequence) : m_Options.MinimumSequence;
        if (const auto status = m_TrustStore.VerifyExact(endpoint, response.Body, metadata.Value().Signature,
                                                         Policy(m_Options, sequenceFloor, false));
            !status)
        {
            return HubResult<VerifiedCatalogDocument>::Failure(status.Error());
        }
        if (cached && metadata.Value().Signature.Sequence == cached->Signature.Sequence &&
            (response.Body != cached->ExactBytes || !SameSignature(metadata.Value().Signature, cached->Signature)))
        {
            return HubResult<VerifiedCatalogDocument>::Failure(
                ClientError(HubErrorCode::CatalogReplay,
                            "The service returned different signed bytes for an already accepted sequence.", item));
        }
        CachedCatalogDocument accepted{.Endpoint = endpoint,
                                       .ExactBytes = response.Body,
                                       .Signature = metadata.Value().Signature,
                                       .ETag = metadata.Value().ETag};
        if (const auto status = m_Cache.Store(accepted); !status)
            return HubResult<VerifiedCatalogDocument>::Failure(status.Error());
        return HubResult<VerifiedCatalogDocument>::Success(
            {.Endpoint = std::move(endpoint),
             .ExactBytes = std::make_shared<const std::vector<std::byte>>(std::move(response.Body)),
             .Signature = std::move(metadata).Value().Signature,
             .ETag = accepted.ETag,
             .FromCache = false,
             .NetworkValidated = true});
    }

    CatalogClient::CatalogClient(CatalogClientOptions options, CatalogTrustStore trustStore, CatalogTransport transport)
        : m_Options(std::move(options)), m_TrustStore(std::move(trustStore)), m_Cache(m_Options.CacheRoot),
          m_Transport(std::move(transport))
    {
    }
} // namespace KeireHub
