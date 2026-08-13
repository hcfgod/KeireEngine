#include "KeireHubRuntime/MarketplaceClient.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumTokenBytes = std::size_t{16U} * 1024U;
        constexpr std::size_t MaximumPublicationBytes = std::size_t{64U} * 1024U;

        [[nodiscard]] HubError MarketplaceError(const HubErrorCode code, std::string message,
                                                std::string technicalDetails = {}, const bool retryable = false,
                                                std::string correlationId = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = "marketplace",
                    .TechnicalDetails = std::move(technicalDetails),
                    .LogReference = std::move(correlationId)};
        }

        [[nodiscard]] bool IsBoundedToken(const std::string_view value, const std::size_t maximum) noexcept
        {
            return !value.empty() && value.size() <= maximum &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return character >= 0x21U && character <= 0x7eU; });
        }

        [[nodiscard]] bool IsUuid(const std::string_view value) noexcept
        {
            if (value.size() != 36U)
                return false;
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                if (index == 8U || index == 13U || index == 18U || index == 23U)
                {
                    if (value[index] != '-')
                        return false;
                }
                else if (!std::isxdigit(static_cast<unsigned char>(value[index])))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool IsSha256(const std::string_view value) noexcept
        {
            return value.size() == 64U &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return std::isdigit(character) || (character >= 'a' && character <= 'f'); });
        }

        [[nodiscard]] bool IsSafeBaseUrl(const std::string_view value) noexcept
        {
            if (!value.starts_with("https://") || value.ends_with('/') || value.size() > 2048U ||
                value.find_first_of("\r\n?#") != std::string_view::npos)
            {
                return false;
            }
            const auto authority = value.substr(8U);
            return !authority.empty() && authority.find('@') == std::string_view::npos &&
                   authority.find('/') == std::string_view::npos;
        }

        [[nodiscard]] std::string EncodeQuery(const std::string_view value)
        {
            constexpr char Hex[] = "0123456789ABCDEF";
            std::string result;
            result.reserve(value.size());
            for (const auto character : value)
            {
                const auto byte = static_cast<unsigned char>(character);
                if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
                    byte == '-' || byte == '_' || byte == '.' || byte == '~')
                {
                    result.push_back(character);
                }
                else
                {
                    result.push_back('%');
                    result.push_back(Hex[(byte >> 4U) & 0x0fU]);
                    result.push_back(Hex[byte & 0x0fU]);
                }
            }
            return result;
        }

        [[nodiscard]] std::vector<std::byte> JsonBody(const Detail::Json& document)
        {
            const auto text = document.dump();
            const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
            return {bytes.begin(), bytes.end()};
        }

        [[nodiscard]] std::string Header(const NativeHttpResponse& response, const std::string_view name)
        {
            const auto found = std::ranges::find_if(
                response.Headers,
                [&](const CatalogHttpHeader& header)
                {
                    return std::ranges::equal(
                        header.Name, name, {}, [](const unsigned char value) { return std::tolower(value); },
                        [](const unsigned char value) { return std::tolower(value); });
                });
            return found == response.Headers.end() ? std::string{} : found->Value;
        }

        [[nodiscard]] HubResult<Detail::Json> ParseResponse(const NativeHttpResponse& response)
        {
            try
            {
                if (response.Body.empty())
                    throw std::invalid_argument("empty response body");
                const auto* data = reinterpret_cast<const char*>(response.Body.data());
                auto result = Detail::Json::parse(std::string_view(data, response.Body.size()));
                if (!result.is_object())
                    throw std::invalid_argument("response root is not an object");
                return HubResult<Detail::Json>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<Detail::Json>::Failure(
                    MarketplaceError(HubErrorCode::InvalidData, "The marketplace returned an invalid response.",
                                     error.what(), false, Header(response, "x-correlation-id")));
            }
        }

        [[nodiscard]] HubError ResponseError(const NativeHttpResponse& response, const Detail::Json& document)
        {
            std::string code = "marketplace.request_failed";
            std::string message = "The marketplace request could not be completed.";
            std::string correlationId = Header(response, "x-correlation-id");
            if (const auto error = document.find("error"); error != document.end() && error->is_object())
            {
                code = error->value("code", code);
                message = error->value("message", message);
                correlationId = error->value("correlationId", correlationId);
            }
            const auto mapped = response.StatusCode == 401U || response.StatusCode == 403U
                                    ? HubErrorCode::AccountAuthenticationFailed
                                : response.StatusCode == 404U ? HubErrorCode::NotFound
                                                              : HubErrorCode::CatalogTransportFailed;
            return MarketplaceError(mapped, std::move(message), std::move(code),
                                    response.StatusCode == 429U || response.StatusCode >= 500U,
                                    std::move(correlationId));
        }

        [[nodiscard]] HubResult<Detail::Json> Send(const MarketplaceTransport& transport, NativeHttpRequest request,
                                                   const std::size_t maximumResponseBytes)
        {
            request.MaximumResponseBytes = maximumResponseBytes;
            auto response = transport(request);
            if (!response)
            {
                auto error = response.Error();
                error.Code = HubErrorCode::CatalogTransportFailed;
                error.Message = "The marketplace could not be reached.";
                error.AffectedItem = "marketplace";
                return HubResult<Detail::Json>::Failure(std::move(error));
            }
            auto document = ParseResponse(response.Value());
            if (!document)
                return document;
            if (response.Value().StatusCode < 200U || response.Value().StatusCode >= 300U)
            {
                return HubResult<Detail::Json>::Failure(ResponseError(response.Value(), document.Value()));
            }
            return document;
        }

        [[nodiscard]] MarketplaceProduct ParseProduct(const Detail::Json& value)
        {
            MarketplaceProduct result;
            result.Id = value.at("id").get<std::string>();
            result.Slug = value.at("slug").get<std::string>();
            result.DisplayName = value.at("display_name").get<std::string>();
            result.ShortDescription = value.at("short_description").get<std::string>();
            result.CategorySlug = value.value("category_slug", "");
            result.CategoryName = value.value("category_name", "");
            result.LicenseSpdx = value.value("license_spdx", "");
            result.LicenseRevision = value.value("license_revision", std::string{});
            result.RatingAverage = value.value("rating_average", 0.0);
            result.RatingCount = value.value("rating_count", std::uint64_t{0});
            result.Featured = value.value("featured", false);
            result.Publisher.Id = value.value("publisher_id", "");
            result.Publisher.Slug = value.value("publisher_slug", "");
            result.Publisher.DisplayName = value.value("publisher_name", "");
            result.Publisher.Verified = value.value("publisher_verified", false);
            if (!IsUuid(result.Id) || result.Slug.empty() || result.Slug.size() > 64U || result.DisplayName.empty() ||
                result.DisplayName.size() > 128U || result.ShortDescription.size() > 512U ||
                result.LicenseRevision.size() > 64U || result.RatingAverage < 0.0 || result.RatingAverage > 5.0)
            {
                throw std::invalid_argument("invalid marketplace product fields");
            }
            return result;
        }

        [[nodiscard]] std::string OptionalString(const Detail::Json& value, const std::string_view field)
        {
            const auto found = value.find(field);
            return found == value.end() || found->is_null() ? std::string{} : found->get<std::string>();
        }

        void AppendPageMetadata(const Detail::Json& document, std::string& nextCursor, std::uint32_t& limit,
                                std::string& correlationId)
        {
            const auto& page = document.at("page");
            if (!page.at("nextCursor").is_null())
                nextCursor = page.at("nextCursor").get<std::string>();
            limit = page.at("limit").get<std::uint32_t>();
            correlationId = document.at("meta").at("correlationId").get<std::string>();
            if (nextCursor.size() > 1024U || limit == 0U || limit > 50U || correlationId.size() > 128U)
                throw std::invalid_argument("invalid marketplace page metadata");
        }

        [[nodiscard]] NativeHttpRequest GetRequest(std::string url, const std::optional<std::string_view> accessToken)
        {
            NativeHttpRequest result{
                .Method = NativeHttpMethod::Get, .Url = std::move(url), .Headers = {{"Accept", "application/json"}}};
            if (accessToken)
                result.Headers.push_back({"Authorization", "Bearer " + std::string(*accessToken)});
            return result;
        }
    } // namespace

    HubResult<MarketplacePublication> DecodeMarketplacePublication(const std::string_view envelope)
    {
        try
        {
            if (envelope.empty() || envelope.size() > MaximumPublicationBytes)
                throw std::invalid_argument("signed publication size is invalid");
            const auto root = Detail::Json::parse(envelope);
            if (!root.is_object() || root.size() != 3U || root.at("schemaVersion").get<std::uint32_t>() != 1U)
                throw std::invalid_argument("signed publication envelope schema is invalid");
            const auto& signature = root.at("signature");
            if (!signature.is_object() || signature.size() != 5U)
                throw std::invalid_argument("signed publication signature schema is invalid");

            MarketplacePublication result{.Envelope = std::string(envelope),
                                          .Document = root.at("document").get<std::string>(),
                                          .Algorithm = signature.at("algorithm").get<std::string>(),
                                          .KeyId = signature.at("keyId").get<std::string>(),
                                          .Signature = signature.at("value").get<std::string>(),
                                          .Sequence = signature.at("sequence").get<std::uint64_t>(),
                                          .ExpiresAt = signature.at("expiresAt").get<std::string>()};
            if (result.Document.empty() || result.Document.size() > MaximumPublicationBytes ||
                result.Algorithm != "ed25519" || !Detail::IsDistributionKeyId(result.KeyId) || result.Sequence == 0U ||
                result.Sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
                !Detail::ParseUtcInstant(result.ExpiresAt))
            {
                throw std::invalid_argument("signed publication signature metadata is invalid");
            }
            const auto decodedSignature = Detail::DecodeCanonicalBase64(result.Signature, 64U);
            if (!decodedSignature || decodedSignature->size() != 64U)
                throw std::invalid_argument("signed publication signature encoding is invalid");

            const auto document = Detail::Json::parse(result.Document);
            if (!document.is_object() || document.size() != 10U ||
                document.at("schemaVersion").get<std::uint32_t>() != 1U)
            {
                throw std::invalid_argument("signed publication document schema is invalid");
            }
            result.ProductId = document.at("productId").get<std::string>();
            result.VersionId = document.at("versionId").get<std::string>();
            result.ArtifactSha256 = document.at("artifactSha256").get<std::string>();
            result.ArtifactSizeBytes = document.at("artifactSizeBytes").get<std::uint64_t>();
            result.ManifestSha256 = document.at("manifestSha256").get<std::string>();
            result.ReleaseStoragePath = document.at("releaseStoragePath").get<std::string>();
            if (!IsUuid(result.ProductId) || !IsUuid(result.VersionId) || !IsSha256(result.ArtifactSha256) ||
                result.ArtifactSizeBytes == 0U || !IsSha256(result.ManifestSha256) ||
                document.at("keyId").get<std::string>() != result.KeyId ||
                document.at("sequence").get<std::uint64_t>() != result.Sequence ||
                document.at("expiresAt").get<std::string>() != result.ExpiresAt ||
                result.ReleaseStoragePath !=
                    result.ProductId + '/' + result.VersionId + '/' + result.ArtifactSha256 + ".keireassetpackage")
            {
                throw std::invalid_argument("signed publication identity is invalid");
            }
            return HubResult<MarketplacePublication>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<MarketplacePublication>::Failure(MarketplaceError(
                HubErrorCode::InvalidData, "The marketplace publication proof is invalid.", error.what()));
        }
    }

    HubStatus VerifyMarketplacePublication(const MarketplacePublication& publication,
                                           const std::string_view expectedProductId,
                                           const std::string_view expectedVersionId,
                                           const std::string_view expectedArchiveSha256,
                                           const std::uint64_t expectedArchiveSizeBytes, const CatalogTrustStore& trust,
                                           const std::chrono::system_clock::time_point now)
    {
        if (publication.ProductId != expectedProductId || publication.VersionId != expectedVersionId ||
            publication.ArtifactSha256 != expectedArchiveSha256 ||
            publication.ArtifactSizeBytes != expectedArchiveSizeBytes)
        {
            return HubStatus::Failure(
                MarketplaceError(HubErrorCode::CatalogIdentityMismatch,
                                 "The signed marketplace publication does not match the selected package."));
        }
        const auto expiry = Detail::ParseUtcInstant(publication.ExpiresAt);
        if (!expiry || !Detail::HasMinimumValidity(*expiry, Detail::ToUtcInstant(now), std::chrono::seconds{0}))
        {
            return HubStatus::Failure(
                MarketplaceError(HubErrorCode::CatalogExpired, "The signed marketplace publication has expired."));
        }
        const auto bytes = std::as_bytes(std::span(publication.Document.data(), publication.Document.size()));
        return trust.VerifyDetached(
            bytes, {.Algorithm = "Ed25519", .KeyId = publication.KeyId, .Signature = publication.Signature},
            "marketplace-publication");
    }

    MarketplaceClient::MarketplaceClient(MarketplaceClientOptions options, MarketplaceTransport transport)
        : m_Options(std::move(options)), m_Transport(std::move(transport))
    {
    }

    HubResult<MarketplaceClient> MarketplaceClient::Create(MarketplaceClientOptions options,
                                                           MarketplaceTransport transport)
    {
        if (!IsSafeBaseUrl(options.ServiceBaseUrl) || !transport || options.MaximumResponseBytes < 1024U ||
            options.MaximumResponseBytes > std::size_t{16U} * 1024U * 1024U)
        {
            return HubResult<MarketplaceClient>::Failure(
                MarketplaceError(HubErrorCode::InvalidArgument, "The marketplace client configuration is invalid."));
        }
        return HubResult<MarketplaceClient>::Success(MarketplaceClient(std::move(options), std::move(transport)));
    }

    HubResult<MarketplaceCatalogPage> MarketplaceClient::Catalog(const MarketplaceCatalogQuery& query) const
    {
        if (query.Search.size() > 100U || query.Category.size() > 64U || query.Cursor.size() > 1024U ||
            query.Limit == 0U || query.Limit > 50U)
        {
            return HubResult<MarketplaceCatalogPage>::Failure(
                MarketplaceError(HubErrorCode::InvalidArgument, "The marketplace catalog query is invalid."));
        }
        auto url = m_Options.ServiceBaseUrl + "/marketplace/v1/catalog/?limit=" + std::to_string(query.Limit);
        if (!query.Search.empty())
            url += "&q=" + EncodeQuery(query.Search);
        if (!query.Category.empty())
            url += "&category=" + EncodeQuery(query.Category);
        if (!query.Cursor.empty())
            url += "&cursor=" + EncodeQuery(query.Cursor);
        auto document = Send(m_Transport, GetRequest(std::move(url), std::nullopt), m_Options.MaximumResponseBytes);
        if (!document)
            return HubResult<MarketplaceCatalogPage>::Failure(document.Error());
        try
        {
            MarketplaceCatalogPage result;
            const auto& data = document.Value().at("data");
            if (!data.is_array() || data.size() > query.Limit)
                throw std::invalid_argument("invalid marketplace catalog length");
            result.Products.reserve(data.size());
            for (const auto& item : data)
                result.Products.push_back(ParseProduct(item));
            AppendPageMetadata(document.Value(), result.NextCursor, result.Limit, result.CorrelationId);
            return HubResult<MarketplaceCatalogPage>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<MarketplaceCatalogPage>::Failure(MarketplaceError(
                HubErrorCode::InvalidData, "The marketplace catalog response is invalid.", error.what()));
        }
    }

    HubResult<MarketplaceProductDetails> MarketplaceClient::Product(const std::string_view productId) const
    {
        if (!IsUuid(productId))
        {
            return HubResult<MarketplaceProductDetails>::Failure(
                MarketplaceError(HubErrorCode::InvalidArgument, "The marketplace product ID is invalid."));
        }
        auto document =
            Send(m_Transport,
                 GetRequest(m_Options.ServiceBaseUrl + "/marketplace/v1/products/" + std::string(productId) + '/',
                            std::nullopt),
                 m_Options.MaximumResponseBytes);
        if (!document)
            return HubResult<MarketplaceProductDetails>::Failure(document.Error());
        try
        {
            const auto& data = document.Value().at("data");
            MarketplaceProductDetails result;
            result.Product = ParseProduct(data);
            if (result.Product.Id != productId)
                throw std::invalid_argument("marketplace product identity mismatch");
            const auto& versions = data.at("versions");
            if (!versions.is_array() || versions.size() > 256U)
                throw std::invalid_argument("invalid marketplace product version length");
            for (const auto& value : versions)
            {
                MarketplaceProductVersion version{
                    .Id = value.at("id").get<std::string>(),
                    .Version = value.at("version").get<std::string>(),
                    .State = value.at("state").get<std::string>(),
                    .InstallKind = value.at("install_kind").get<std::string>(),
                    .MinimumEngineVersion = OptionalString(value, "minimum_engine_version"),
                    .MaximumEngineVersion = OptionalString(value, "maximum_engine_version"),
                    .Platforms = value.value("platforms", std::vector<std::string>{}),
                    .Architectures = value.value("architectures", std::vector<std::string>{}),
                    .RendererCapabilities = value.value("renderer_capabilities", std::vector<std::string>{}),
                    .ManagedApiVersion = OptionalString(value, "managed_api_version"),
                    .ReleaseNotesMarkdown = OptionalString(value, "release_notes_markdown"),
                    .PublishedAt = OptionalString(value, "published_at")};
                const auto validState =
                    version.State == "published" || version.State == "withdrawn" || version.State == "security_revoked";
                const auto validKind = version.InstallKind == "registry" || version.InstallKind == "asset_import" ||
                                       version.InstallKind == "complete_project";
                if (!IsUuid(version.Id) || version.Version.empty() || version.Version.size() > 128U || !validState ||
                    !validKind || version.Platforms.size() > 16U || version.Architectures.size() > 16U ||
                    version.RendererCapabilities.size() > 64U || version.ReleaseNotesMarkdown.size() > 100'000U)
                {
                    throw std::invalid_argument("invalid marketplace product version fields");
                }
                result.Versions.push_back(std::move(version));
            }
            result.CorrelationId = document.Value().at("meta").at("correlationId").get<std::string>();
            if (result.CorrelationId.size() > 128U)
                throw std::invalid_argument("invalid marketplace product correlation ID");
            return HubResult<MarketplaceProductDetails>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<MarketplaceProductDetails>::Failure(MarketplaceError(
                HubErrorCode::InvalidData, "The marketplace product response is invalid.", error.what()));
        }
    }

    HubResult<MarketplaceLibraryPage> MarketplaceClient::Library(const std::string_view accessToken,
                                                                 const std::optional<std::string_view> organizationId,
                                                                 const std::string_view cursor) const
    {
        if (!IsBoundedToken(accessToken, MaximumTokenBytes) || (organizationId && !IsUuid(*organizationId)) ||
            cursor.size() > 1024U)
        {
            return HubResult<MarketplaceLibraryPage>::Failure(
                MarketplaceError(HubErrorCode::InvalidArgument, "The marketplace library request is invalid."));
        }
        auto url = m_Options.ServiceBaseUrl + "/marketplace/v1/library/";
        if (organizationId)
            url += "?organizationId=" + EncodeQuery(*organizationId);
        if (!cursor.empty())
            url += organizationId ? "&cursor=" + EncodeQuery(cursor) : "?cursor=" + EncodeQuery(cursor);
        auto document = Send(m_Transport, GetRequest(std::move(url), accessToken), m_Options.MaximumResponseBytes);
        if (!document)
            return HubResult<MarketplaceLibraryPage>::Failure(document.Error());
        try
        {
            MarketplaceLibraryPage result;
            const auto& data = document.Value().at("data");
            if (!data.is_array() || data.size() > 24U)
                throw std::invalid_argument("invalid marketplace library length");
            result.Items.reserve(data.size());
            for (const auto& item : data)
            {
                MarketplaceLibraryItem parsed;
                parsed.EntitlementId = item.at("id").get<std::string>();
                parsed.ProductId = item.at("product_id").get<std::string>();
                parsed.GrantedAt = item.at("granted_at").get<std::string>();
                if (!item.at("organization_id").is_null())
                    parsed.OrganizationId = item.at("organization_id").get<std::string>();
                auto product = item.at("marketplace_products");
                product["publisher_id"] = "";
                if (const auto publisher = product.find("publishers"); publisher != product.end())
                {
                    product["publisher_slug"] = publisher->value("slug", "");
                    product["publisher_name"] = publisher->value("display_name", "");
                }
                product["category_slug"] = "";
                product["category_name"] = "";
                product["license_spdx"] = product.value("license_spdx", "");
                product["short_description"] = product.value("short_description", "");
                parsed.Product = ParseProduct(product);
                if (!IsUuid(parsed.EntitlementId) || !IsUuid(parsed.ProductId) ||
                    (parsed.OrganizationId && !IsUuid(*parsed.OrganizationId)))
                {
                    throw std::invalid_argument("invalid marketplace entitlement identity");
                }
                result.Items.push_back(std::move(parsed));
            }
            AppendPageMetadata(document.Value(), result.NextCursor, result.Limit, result.CorrelationId);
            return HubResult<MarketplaceLibraryPage>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<MarketplaceLibraryPage>::Failure(MarketplaceError(
                HubErrorCode::InvalidData, "The marketplace library response is invalid.", error.what()));
        }
    }

    HubResult<MarketplaceClaimResult> MarketplaceClient::Claim(const std::string_view accessToken,
                                                               const MarketplaceClaimRequest& request) const
    {
        if (!IsBoundedToken(accessToken, MaximumTokenBytes) || !IsUuid(request.ProductId) ||
            (request.OrganizationId && !IsUuid(*request.OrganizationId)) || request.IdempotencyKey.size() < 16U ||
            request.IdempotencyKey.size() > 128U || request.AcceptedLicenseSnapshot.empty() ||
            request.AcceptedLicenseSnapshot.size() > 100'000U)
        {
            return HubResult<MarketplaceClaimResult>::Failure(
                MarketplaceError(HubErrorCode::InvalidArgument, "The marketplace claim request is invalid."));
        }
        Detail::Json body{{"productId", request.ProductId},
                          {"ownership", request.OrganizationId ? "organization" : "personal"},
                          {"acceptedLicenseSnapshot", request.AcceptedLicenseSnapshot}};
        if (request.OrganizationId)
            body["organizationId"] = *request.OrganizationId;
        NativeHttpRequest httpRequest{.Method = NativeHttpMethod::Post,
                                      .Url = m_Options.ServiceBaseUrl + "/marketplace/v1/claims/",
                                      .Headers = {{"Accept", "application/json"},
                                                  {"Content-Type", "application/json"},
                                                  {"Authorization", "Bearer " + std::string(accessToken)},
                                                  {"Idempotency-Key", request.IdempotencyKey}},
                                      .Body = JsonBody(body)};
        auto document = Send(m_Transport, std::move(httpRequest), m_Options.MaximumResponseBytes);
        if (!document)
            return HubResult<MarketplaceClaimResult>::Failure(document.Error());
        try
        {
            const auto& data = document.Value().at("data");
            MarketplaceClaimResult result;
            result.EntitlementId = data.at("entitlementId").get<std::string>();
            if (!data.at("organizationId").is_null())
                result.OrganizationId = data.at("organizationId").get<std::string>();
            result.CorrelationId = document.Value().at("meta").at("correlationId").get<std::string>();
            if (!IsUuid(result.EntitlementId) || (result.OrganizationId && !IsUuid(*result.OrganizationId)) ||
                result.CorrelationId.size() > 128U)
            {
                throw std::invalid_argument("invalid marketplace claim response");
            }
            return HubResult<MarketplaceClaimResult>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<MarketplaceClaimResult>::Failure(MarketplaceError(
                HubErrorCode::InvalidData, "The marketplace claim response is invalid.", error.what()));
        }
    }

    HubResult<MarketplaceDeviceSession>
    MarketplaceClient::RegisterDeviceSession(const std::string_view accessToken,
                                             const std::string_view deviceName) const
    {
        if (!IsBoundedToken(accessToken, MaximumTokenBytes) || deviceName.empty() || deviceName.size() > 128U)
        {
            return HubResult<MarketplaceDeviceSession>::Failure(
                MarketplaceError(HubErrorCode::InvalidArgument, "The marketplace device registration is invalid."));
        }
        NativeHttpRequest request{.Method = NativeHttpMethod::Post,
                                  .Url = m_Options.ServiceBaseUrl + "/marketplace/v1/sessions/",
                                  .Headers = {{"Accept", "application/json"},
                                              {"Content-Type", "application/json"},
                                              {"Authorization", "Bearer " + std::string(accessToken)}},
                                  .Body = JsonBody({{"deviceName", deviceName}})};
        auto document = Send(m_Transport, std::move(request), m_Options.MaximumResponseBytes);
        if (!document)
            return HubResult<MarketplaceDeviceSession>::Failure(document.Error());
        try
        {
            const auto& data = document.Value().at("data");
            MarketplaceDeviceSession result{.Id = data.at("id").get<std::string>(),
                                            .OAuthSessionId = data.at("sessionId").get<std::string>(),
                                            .Client = data.at("client").get<std::string>(),
                                            .CorrelationId =
                                                document.Value().at("meta").at("correlationId").get<std::string>()};
            if (!IsUuid(result.Id) || !IsBoundedToken(result.OAuthSessionId, 256U) || result.Client != "hub" ||
                result.CorrelationId.size() > 128U)
            {
                throw std::invalid_argument("invalid marketplace device session response");
            }
            return HubResult<MarketplaceDeviceSession>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<MarketplaceDeviceSession>::Failure(MarketplaceError(
                HubErrorCode::InvalidData, "The marketplace device response is invalid.", error.what()));
        }
    }

    HubResult<MarketplaceDownloadGrant>
    MarketplaceClient::RequestDownload(const std::string_view accessToken,
                                       const MarketplaceDownloadRequest& request) const
    {
        if (!IsBoundedToken(accessToken, MaximumTokenBytes) || !IsUuid(request.VersionId) ||
            !IsUuid(request.DeviceSessionId) || (request.OrganizationId && !IsUuid(*request.OrganizationId)))
        {
            return HubResult<MarketplaceDownloadGrant>::Failure(
                MarketplaceError(HubErrorCode::InvalidArgument, "The marketplace download request is invalid."));
        }
        Detail::Json body{{"versionId", request.VersionId}, {"deviceSessionId", request.DeviceSessionId}};
        if (request.OrganizationId)
            body["organizationId"] = *request.OrganizationId;
        NativeHttpRequest httpRequest{.Method = NativeHttpMethod::Post,
                                      .Url = m_Options.ServiceBaseUrl + "/marketplace/v1/downloads/",
                                      .Headers = {{"Accept", "application/json"},
                                                  {"Content-Type", "application/json"},
                                                  {"Authorization", "Bearer " + std::string(accessToken)}},
                                      .Body = JsonBody(body)};
        auto document = Send(m_Transport, std::move(httpRequest), m_Options.MaximumResponseBytes);
        if (!document)
            return HubResult<MarketplaceDownloadGrant>::Failure(document.Error());
        try
        {
            const auto& data = document.Value().at("data");
            MarketplaceDownloadGrant result{.GrantId = data.at("grantId").get<std::string>(),
                                            .Url = data.at("url").get<std::string>(),
                                            .ExpiresAt = data.at("expiresAt").get<std::string>(),
                                            .ArchiveSha256 = data.at("archiveSha256").get<std::string>(),
                                            .ArchiveSizeBytes = data.at("archiveSizeBytes").get<std::uint64_t>(),
                                            .SignedPublication = data.at("signedPublication").get<std::string>(),
                                            .CorrelationId =
                                                document.Value().at("meta").at("correlationId").get<std::string>()};
            if (!IsUuid(result.GrantId) || !IsSafeBaseUrl(result.Url.substr(0U, result.Url.find('/', 8U))) ||
                result.Url.find('?') == result.Url.npos || result.Url.size() > 4096U ||
                !IsSha256(result.ArchiveSha256) || result.ArchiveSizeBytes == 0U || result.ExpiresAt.empty() ||
                result.ExpiresAt.size() > 64U || result.SignedPublication.empty() ||
                result.SignedPublication.size() > MaximumPublicationBytes || result.CorrelationId.size() > 128U)
            {
                throw std::invalid_argument("invalid marketplace download grant response");
            }
            const auto publication = DecodeMarketplacePublication(result.SignedPublication);
            if (!publication || publication.Value().VersionId != request.VersionId ||
                publication.Value().ArtifactSha256 != result.ArchiveSha256 ||
                publication.Value().ArtifactSizeBytes != result.ArchiveSizeBytes)
            {
                throw std::invalid_argument("marketplace download grant publication identity is invalid");
            }
            return HubResult<MarketplaceDownloadGrant>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<MarketplaceDownloadGrant>::Failure(MarketplaceError(
                HubErrorCode::InvalidData, "The marketplace download response is invalid.", error.what()));
        }
    }
} // namespace KeireHub
