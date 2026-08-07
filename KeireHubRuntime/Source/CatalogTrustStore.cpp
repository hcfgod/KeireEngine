#include "KeireHubRuntime/CatalogClient.h"

#include "DistributionEncoding.h"
#include "SodiumVerifier.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <set>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumTrustedKeys = 16;
        constexpr std::size_t MaximumPublicKeyDocumentBytes = 16 * 1024;
        constexpr std::size_t MaximumCatalogBytes = 32 * 1024 * 1024;
        constexpr std::size_t MaximumJsonDepth = 128;

        struct TrustedKey final
        {
            std::string Id;
            std::array<std::byte, 32> PublicKey{};
        };

        [[nodiscard]] HubError TrustError(const HubErrorCode code, std::string message, std::string item,
                                          std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool IsEndpointValid(const CatalogEndpoint& endpoint) noexcept
        {
            if (endpoint.ServiceBaseUrl.empty())
                return false;
            if (endpoint.Kind == CatalogDocumentKind::PackageCatalog)
            {
                return Detail::IsDistributionRouteToken(endpoint.Channel) &&
                       Detail::IsDistributionRouteToken(endpoint.Platform) &&
                       Detail::IsDistributionRouteToken(endpoint.Architecture);
            }
            return Detail::IsDistributionLocale(endpoint.Locale);
        }

        [[nodiscard]] std::string EndpointItem(const CatalogEndpoint& endpoint)
        {
            if (endpoint.Kind == CatalogDocumentKind::ContentCatalog)
                return "content/" + endpoint.Locale;
            return "catalog/" + endpoint.Channel + '/' + endpoint.Platform + '/' + endpoint.Architecture;
        }

        [[nodiscard]] HubResult<TrustedKey> ParseTrustedKey(const std::string_view document)
        {
            if (document.empty() || document.size() > MaximumPublicKeyDocumentBytes)
            {
                return HubResult<TrustedKey>::Failure(
                    TrustError(HubErrorCode::DistributionConfigurationInvalid,
                               "A trusted distribution key document is outside its size limit.", "trusted-key"));
            }
            auto parsed =
                Detail::ParseStrictJson(document, MaximumJsonDepth, HubErrorCode::DistributionConfigurationInvalid,
                                        "A trusted distribution key document is malformed.", "trusted-key");
            if (!parsed)
                return HubResult<TrustedKey>::Failure(parsed.Error());
            try
            {
                const auto& value = parsed.Value();
                if (!value.is_object() || value.size() != 5U || !value.at("schemaVersion").is_number_unsigned() ||
                    value.at("schemaVersion").get<std::uint64_t>() != 1U ||
                    value.at("algorithm").get<std::string>() != "Ed25519")
                {
                    throw std::invalid_argument("The trusted key header is invalid.");
                }
                const auto keyId = value.at("keyId").get<std::string>();
                const auto encodedKey = value.at("publicKey").get<std::string>();
                const auto fingerprint = value.at("fingerprint").get<std::string>();
                auto key = Detail::DecodeCanonicalBase64(encodedKey, 32);
                if (!Detail::IsDistributionKeyId(keyId) || !key || key->size() != 32U)
                    throw std::invalid_argument("The trusted public key is invalid.");
                const auto digest = Detail::Sha256Hex(*key);
                if (keyId != "ed25519-" + digest.substr(0, 32) || fingerprint != "sha256:" + digest)
                    throw std::invalid_argument("The trusted key identity or fingerprint is invalid.");
                TrustedKey result{.Id = keyId};
                std::ranges::copy(*key, result.PublicKey.begin());
                return HubResult<TrustedKey>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<TrustedKey>::Failure(TrustError(HubErrorCode::DistributionConfigurationInvalid,
                                                                 "A trusted distribution key document is invalid.",
                                                                 "trusted-key", error.what()));
            }
        }

        [[nodiscard]] HubStatus ValidateDocumentIdentity(const CatalogEndpoint& endpoint,
                                                         const std::span<const std::byte> exactBytes,
                                                         const CatalogSignatureMetadata& signature)
        {
            const auto text = std::string_view(reinterpret_cast<const char*>(exactBytes.data()), exactBytes.size());
            auto parsed = Detail::ParseStrictJson(text, MaximumJsonDepth, HubErrorCode::CatalogIdentityMismatch,
                                                  "The signed catalog identity is malformed.", EndpointItem(endpoint));
            if (!parsed)
                return HubStatus::Failure(parsed.Error());
            try
            {
                const auto& document = parsed.Value();
                if (!document.is_object() || !document.at("schemaVersion").is_number_unsigned() ||
                    document.at("schemaVersion").get<std::uint64_t>() == 0U ||
                    document.at("schemaVersion").get<std::uint64_t>() >
                        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) ||
                    !document.at("sequence").is_number_unsigned())
                {
                    throw std::invalid_argument("The signed identity header is invalid.");
                }
                const auto keyId = document.at("keyId").get<std::string>();
                const auto sequence = document.at("sequence").get<std::uint64_t>();
                const auto expiresAt = document.at("expiresAt").get<std::string>();
                const auto documentExpiry = Detail::ParseUtcInstant(expiresAt);
                const auto metadataExpiry = Detail::ParseUtcInstant(signature.ExpiresAt);
                if (!Detail::IsDistributionKeyId(keyId) ||
                    sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
                    !documentExpiry || !metadataExpiry || keyId != signature.KeyId || sequence != signature.Sequence ||
                    *documentExpiry != *metadataExpiry)
                {
                    throw std::invalid_argument("The signed identity does not match its detached metadata.");
                }
                if (endpoint.Kind == CatalogDocumentKind::PackageCatalog)
                {
                    if (document.at("channel").get<std::string>() != endpoint.Channel ||
                        document.at("platform").get<std::string>() != endpoint.Platform ||
                        document.at("architecture").get<std::string>() != endpoint.Architecture)
                    {
                        throw std::invalid_argument("The catalog route identity does not match its request.");
                    }
                }
                else if (document.at("locale").get<std::string>() != endpoint.Locale)
                {
                    throw std::invalid_argument("The content locale does not match its request.");
                }
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                return HubStatus::Failure(TrustError(HubErrorCode::CatalogIdentityMismatch,
                                                     "The signed catalog does not match the requested endpoint.",
                                                     EndpointItem(endpoint), error.what()));
            }
        }
    } // namespace

    struct CatalogTrustStore::Impl final
    {
        std::vector<TrustedKey> Keys;
        std::shared_ptr<const Detail::SodiumVerifier> Verifier;
    };

    HubResult<CatalogTrustStore> CatalogTrustStore::Create(const CatalogTrustConfiguration& configuration)
    {
        if (configuration.TrustedPublicKeyDocuments.empty() ||
            configuration.TrustedPublicKeyDocuments.size() > MaximumTrustedKeys)
        {
            return HubResult<CatalogTrustStore>::Failure(
                TrustError(HubErrorCode::DistributionConfigurationInvalid,
                           "At least one bounded trusted distribution key is required.", "trusted-keys"));
        }
        auto implementation = std::make_shared<Impl>();
        std::set<std::string, std::less<>> identities;
        for (const auto& document : configuration.TrustedPublicKeyDocuments)
        {
            auto parsed = ParseTrustedKey(document);
            if (!parsed)
                return HubResult<CatalogTrustStore>::Failure(parsed.Error());
            if (!identities.insert(parsed.Value().Id).second)
            {
                return HubResult<CatalogTrustStore>::Failure(
                    TrustError(HubErrorCode::DistributionConfigurationInvalid,
                               "Trusted distribution key identifiers must be unique.", parsed.Value().Id));
            }
            implementation->Keys.push_back(std::move(parsed).Value());
        }
        auto verifier = Detail::SodiumVerifier::Load(configuration.NativeLibraryPath);
        if (!verifier)
            return HubResult<CatalogTrustStore>::Failure(verifier.Error());
        implementation->Verifier = std::move(verifier).Value();
        return HubResult<CatalogTrustStore>::Success(CatalogTrustStore(std::move(implementation)));
    }

    HubStatus CatalogTrustStore::VerifyExact(const CatalogEndpoint& endpoint,
                                             const std::span<const std::byte> exactBytes,
                                             const CatalogSignatureMetadata& signature,
                                             const CatalogVerificationPolicy& policy) const
    {
        const auto item = EndpointItem(endpoint);
        if (!m_Impl || !IsEndpointValid(endpoint) || exactBytes.empty() || exactBytes.size() > MaximumCatalogBytes ||
            signature.Algorithm != "Ed25519" || !Detail::IsDistributionKeyId(signature.KeyId) ||
            signature.Sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            policy.MinimumSequence == 0U || policy.MinimumRemainingValidity.count() < 0)
        {
            return HubStatus::Failure(
                TrustError(HubErrorCode::CatalogSignatureInvalid, "The catalog signature metadata is invalid.", item));
        }
        const auto key = std::ranges::find(m_Impl->Keys, signature.KeyId, &TrustedKey::Id);
        if (key == m_Impl->Keys.end())
        {
            return HubStatus::Failure(TrustError(HubErrorCode::CatalogUntrustedKey,
                                                 "The catalog was signed by an untrusted key.", item, signature.KeyId));
        }
        auto decodedSignature = Detail::DecodeCanonicalBase64(signature.Signature, 64);
        if (!decodedSignature || decodedSignature->size() != 64U ||
            !m_Impl->Verifier->Verify(*decodedSignature, exactBytes, key->PublicKey))
        {
            return HubStatus::Failure(TrustError(HubErrorCode::CatalogSignatureInvalid,
                                                 "The catalog signature could not be verified.", item));
        }
        if (const auto status = ValidateDocumentIdentity(endpoint, exactBytes, signature); !status)
            return status;
        if (signature.Sequence < policy.MinimumSequence)
        {
            return HubStatus::Failure(TrustError(
                HubErrorCode::CatalogReplay, "The catalog sequence is older than the accepted sequence floor.", item));
        }
        const auto expiry = Detail::ParseUtcInstant(signature.ExpiresAt);
        if (!expiry)
        {
            return HubStatus::Failure(
                TrustError(HubErrorCode::CatalogSignatureInvalid, "The catalog expiration metadata is invalid.", item));
        }
        if (!policy.AllowExpired &&
            !Detail::HasMinimumValidity(*expiry, Detail::ToUtcInstant(policy.Now), policy.MinimumRemainingValidity))
        {
            return HubStatus::Failure(TrustError(HubErrorCode::CatalogExpired,
                                                 "The signed catalog has expired or is too close to expiry.", item));
        }
        return HubStatus::Success();
    }

    HubStatus CatalogTrustStore::VerifyDetached(const std::span<const std::byte> exactBytes,
                                                const DetachedSignatureMetadata& signature,
                                                const std::string_view affectedItem) const
    {
        const auto item = affectedItem.empty() ? std::string{"signed-document"} : std::string{affectedItem};
        if (!m_Impl || exactBytes.empty() || exactBytes.size() > MaximumCatalogBytes || item.size() > 256U ||
            signature.Algorithm != "Ed25519" || !Detail::IsDistributionKeyId(signature.KeyId))
        {
            return HubStatus::Failure(
                TrustError(HubErrorCode::CatalogSignatureInvalid, "The detached signature metadata is invalid.", item));
        }
        const auto key = std::ranges::find(m_Impl->Keys, signature.KeyId, &TrustedKey::Id);
        if (key == m_Impl->Keys.end())
        {
            return HubStatus::Failure(TrustError(HubErrorCode::CatalogUntrustedKey,
                                                 "The document was signed by an untrusted key.", item,
                                                 signature.KeyId));
        }
        auto decodedSignature = Detail::DecodeCanonicalBase64(signature.Signature, 64);
        if (!decodedSignature || decodedSignature->size() != 64U ||
            !m_Impl->Verifier->Verify(*decodedSignature, exactBytes, key->PublicKey))
        {
            return HubStatus::Failure(TrustError(HubErrorCode::CatalogSignatureInvalid,
                                                 "The detached signature could not be verified.", item));
        }
        return HubStatus::Success();
    }

    CatalogTrustStore::CatalogTrustStore(std::shared_ptr<const Impl> implementation) : m_Impl(std::move(implementation))
    {
    }
} // namespace KeireHub
