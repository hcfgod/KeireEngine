#include "KeireHubRuntime/CatalogClient.h"

#include "DistributionEncoding.h"
#include "Persistence.h"

#include <system_error>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumCatalogBytes = 32 * 1024 * 1024;
        constexpr std::size_t MaximumCacheBytes = 48 * 1024 * 1024;
        constexpr std::size_t MaximumCacheDepth = 16;

        [[nodiscard]] HubError CacheError(std::string message, const std::filesystem::path& path,
                                          std::string details = {})
        {
            return {.Code = HubErrorCode::CatalogCacheInvalid,
                    .Message = std::move(message),
                    .Retryable = true,
                    .AffectedItem = Detail::PathToUtf8(path.filename()),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] std::string KindName(const CatalogDocumentKind kind)
        {
            return kind == CatalogDocumentKind::PackageCatalog ? "catalog" : "content";
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

        [[nodiscard]] std::filesystem::path CachePath(const std::filesystem::path& root,
                                                      const CatalogEndpoint& endpoint)
        {
            const auto origin = Detail::Sha256Hex(std::as_bytes(std::span(endpoint.ServiceBaseUrl)));
            if (endpoint.Kind == CatalogDocumentKind::ContentCatalog)
                return root / origin / "content" / (endpoint.Locale + ".json");
            return root / origin / "catalogs" / endpoint.Channel / endpoint.Platform /
                   (endpoint.Architecture + ".json");
        }

        [[nodiscard]] Detail::Json EndpointJson(const CatalogEndpoint& endpoint)
        {
            return {{"kind", KindName(endpoint.Kind)},       {"serviceBaseUrl", endpoint.ServiceBaseUrl},
                    {"channel", endpoint.Channel},           {"platform", endpoint.Platform},
                    {"architecture", endpoint.Architecture}, {"locale", endpoint.Locale}};
        }

        [[nodiscard]] Detail::Json SignatureJson(const CatalogSignatureMetadata& signature)
        {
            return {{"algorithm", signature.Algorithm},
                    {"keyId", signature.KeyId},
                    {"signature", signature.Signature},
                    {"sequence", signature.Sequence},
                    {"expiresAt", signature.ExpiresAt}};
        }

        [[nodiscard]] bool MatchesEndpoint(const Detail::Json& value, const CatalogEndpoint& endpoint)
        {
            return value.is_object() && value.size() == 6U &&
                   value.at("kind").get<std::string>() == KindName(endpoint.Kind) &&
                   value.at("serviceBaseUrl").get<std::string>() == endpoint.ServiceBaseUrl &&
                   value.at("channel").get<std::string>() == endpoint.Channel &&
                   value.at("platform").get<std::string>() == endpoint.Platform &&
                   value.at("architecture").get<std::string>() == endpoint.Architecture &&
                   value.at("locale").get<std::string>() == endpoint.Locale;
        }
    } // namespace

    CatalogCache::CatalogCache(std::filesystem::path root) : m_Root(std::move(root)) {}

    HubResult<std::optional<CachedCatalogDocument>> CatalogCache::Load(const CatalogEndpoint& endpoint) const
    {
        if (m_Root.empty() || !IsEndpointValid(endpoint))
        {
            return HubResult<std::optional<CachedCatalogDocument>>::Failure(
                CacheError("The catalog cache configuration is invalid.", m_Root));
        }
        const auto path = CachePath(m_Root, endpoint);
        std::error_code error;
        if (!std::filesystem::exists(path, error))
        {
            if (error)
                return HubResult<std::optional<CachedCatalogDocument>>::Failure(
                    CacheError("The catalog cache could not be inspected.", path, error.message()));
            return HubResult<std::optional<CachedCatalogDocument>>::Success(std::nullopt);
        }
        const auto status = std::filesystem::symlink_status(path, error);
        if (error || status.type() != std::filesystem::file_type::regular)
        {
            return HubResult<std::optional<CachedCatalogDocument>>::Failure(
                CacheError("The catalog cache entry is not a regular file.", path, error.message()));
        }
        auto text = Detail::ReadTextFile(path, MaximumCacheBytes);
        if (!text)
        {
            auto result = text.Error();
            result.Code = HubErrorCode::CatalogCacheInvalid;
            result.Message = "The catalog cache entry could not be read.";
            return HubResult<std::optional<CachedCatalogDocument>>::Failure(std::move(result));
        }
        auto document =
            Detail::ParseStrictJson(text.Value(), MaximumCacheDepth, HubErrorCode::CatalogCacheInvalid,
                                    "The catalog cache entry is malformed.", Detail::PathToUtf8(path.filename()));
        if (!document)
            return HubResult<std::optional<CachedCatalogDocument>>::Failure(document.Error());
        try
        {
            const auto& root = document.Value();
            if (!root.is_object() || root.size() != 5U || !root.at("schemaVersion").is_number_unsigned() ||
                root.at("schemaVersion").get<std::uint64_t>() != 1U || !MatchesEndpoint(root.at("endpoint"), endpoint))
            {
                throw std::invalid_argument("The cache header or endpoint identity is invalid.");
            }
            const auto& signature = root.at("signature");
            if (!signature.is_object() || signature.size() != 5U || !signature.at("sequence").is_number_unsigned())
                throw std::invalid_argument("The cached signature metadata is invalid.");
            CachedCatalogDocument result;
            result.Endpoint = endpoint;
            result.Signature = {.Algorithm = signature.at("algorithm").get<std::string>(),
                                .KeyId = signature.at("keyId").get<std::string>(),
                                .Signature = signature.at("signature").get<std::string>(),
                                .Sequence = signature.at("sequence").get<std::uint64_t>(),
                                .ExpiresAt = signature.at("expiresAt").get<std::string>()};
            result.ETag = root.at("etag").get<std::string>();
            const auto encoded = root.at("exactBytes").get<std::string>();
            auto bytes = Detail::DecodeCanonicalBase64(encoded, MaximumCatalogBytes);
            if (!bytes || bytes->empty() || result.ETag != Detail::MakeDistributionETag(*bytes))
                throw std::invalid_argument("The cached exact bytes or ETag are invalid.");
            result.ExactBytes = std::move(*bytes);
            return HubResult<std::optional<CachedCatalogDocument>>::Success(std::move(result));
        }
        catch (const std::exception& parseError)
        {
            return HubResult<std::optional<CachedCatalogDocument>>::Failure(
                CacheError("The catalog cache entry is invalid.", path, parseError.what()));
        }
    }

    HubStatus CatalogCache::Store(const CachedCatalogDocument& document) const
    {
        if (m_Root.empty() || !IsEndpointValid(document.Endpoint) || document.ExactBytes.empty() ||
            document.ExactBytes.size() > MaximumCatalogBytes ||
            document.ETag != Detail::MakeDistributionETag(document.ExactBytes))
        {
            return HubStatus::Failure(
                CacheError("The verified catalog could not be written to an invalid cache target.", m_Root));
        }
        const auto path = CachePath(m_Root, document.Endpoint);
        const Detail::Json value{{"schemaVersion", 1},
                                 {"endpoint", EndpointJson(document.Endpoint)},
                                 {"signature", SignatureJson(document.Signature)},
                                 {"etag", document.ETag},
                                 {"exactBytes", Detail::EncodeBase64(document.ExactBytes)}};
        if (auto status = Detail::WriteJsonFileAtomically(path, value); !status)
        {
            auto error = status.Error();
            error.Code = HubErrorCode::CatalogCacheInvalid;
            error.Message = "The verified catalog could not be saved to the cache.";
            return HubStatus::Failure(std::move(error));
        }
        return HubStatus::Success();
    }

    const std::filesystem::path& CatalogCache::Root() const noexcept { return m_Root; }
} // namespace KeireHub
