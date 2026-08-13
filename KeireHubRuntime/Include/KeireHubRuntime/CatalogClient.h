#pragma once

#include "KeireHubRuntime/HubError.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    enum class CatalogDocumentKind
    {
        PackageCatalog,
        ContentCatalog
    };

    struct CatalogEndpoint final
    {
        CatalogDocumentKind Kind = CatalogDocumentKind::PackageCatalog;
        std::string ServiceBaseUrl;
        std::string Channel;
        std::string Platform;
        std::string Architecture;
        std::string Locale;
    };

    struct CatalogSignatureMetadata final
    {
        std::string Algorithm;
        std::string KeyId;
        std::string Signature;
        std::uint64_t Sequence = 0;
        std::string ExpiresAt;
    };

    struct DetachedSignatureMetadata final
    {
        std::string Algorithm;
        std::string KeyId;
        std::string Signature;
    };

    struct CatalogHttpHeader final
    {
        std::string Name;
        std::string Value;
    };

    struct CatalogHttpRequest final
    {
        std::string Url;
        std::optional<std::string> IfNoneMatch;
        std::size_t MaximumResponseBytes = 0;
    };

    struct CatalogHttpResponse final
    {
        std::uint16_t StatusCode = 0;
        std::string EffectiveUrl;
        std::vector<CatalogHttpHeader> Headers;
        std::vector<std::byte> Body;
    };

    using CatalogTransport = std::function<HubResult<CatalogHttpResponse>(const CatalogHttpRequest&)>;
    using CatalogClock = std::function<std::chrono::system_clock::time_point()>;

    struct CatalogTrustConfiguration final
    {
        std::vector<std::string> TrustedPublicKeyDocuments;
        std::filesystem::path NativeLibraryPath;
    };

    struct CatalogVerificationPolicy final
    {
        std::uint64_t MinimumSequence = 1;
        std::chrono::seconds MinimumRemainingValidity{0};
        std::chrono::system_clock::time_point Now = std::chrono::system_clock::now();
        bool AllowExpired = false;
    };

    struct VerifiedCatalogDocument final
    {
        CatalogEndpoint Endpoint;
        std::shared_ptr<const std::vector<std::byte>> ExactBytes;
        CatalogSignatureMetadata Signature;
        std::string ETag;
        bool FromCache = false;
        bool NetworkValidated = false;
    };

    class CatalogTrustStore final
    {
      public:
        [[nodiscard]] static HubResult<CatalogTrustStore> Create(const CatalogTrustConfiguration& configuration);

        [[nodiscard]] HubStatus VerifyExact(const CatalogEndpoint& endpoint, std::span<const std::byte> exactBytes,
                                            const CatalogSignatureMetadata& signature,
                                            const CatalogVerificationPolicy& policy) const;
        [[nodiscard]] HubStatus VerifyDetached(std::span<const std::byte> exactBytes,
                                               const DetachedSignatureMetadata& signature,
                                               std::string_view affectedItem) const;
        [[nodiscard]] bool VerifySignature(std::string_view algorithm, std::string_view keyId,
                                           std::span<const std::byte> exactBytes,
                                           std::span<const std::byte> signature) const noexcept;

      private:
        struct Impl;
        explicit CatalogTrustStore(std::shared_ptr<const Impl> implementation);

        std::shared_ptr<const Impl> m_Impl;
    };

    struct CachedCatalogDocument final
    {
        CatalogEndpoint Endpoint;
        std::vector<std::byte> ExactBytes;
        CatalogSignatureMetadata Signature;
        std::string ETag;
    };

    class CatalogCache final
    {
      public:
        explicit CatalogCache(std::filesystem::path root);

        [[nodiscard]] HubResult<std::optional<CachedCatalogDocument>> Load(const CatalogEndpoint& endpoint) const;
        [[nodiscard]] HubStatus Store(const CachedCatalogDocument& document) const;
        [[nodiscard]] const std::filesystem::path& Root() const noexcept;

      private:
        std::filesystem::path m_Root;
    };

    struct CatalogClientOptions final
    {
        std::string ServiceBaseUrl;
        std::string Platform;
        std::string Architecture;
        std::filesystem::path CacheRoot;
        bool Offline = false;
        bool AllowInsecureLoopbackDevelopment = false;
        std::uint64_t MinimumSequence = 1;
        std::chrono::seconds MinimumRemainingValidity{0};
        CatalogClock Clock;
        std::optional<std::string> CustomProxyUrl;
    };

    class CatalogClient final
    {
      public:
        [[nodiscard]] static HubResult<CatalogClient> Create(CatalogClientOptions options, CatalogTrustStore trustStore,
                                                             CatalogTransport transport = {});

        [[nodiscard]] HubResult<VerifiedCatalogDocument> FetchPackageCatalog(std::string channel) const;
        [[nodiscard]] HubResult<VerifiedCatalogDocument> FetchContentCatalog(std::string locale) const;

      private:
        CatalogClient(CatalogClientOptions options, CatalogTrustStore trustStore, CatalogTransport transport);
        [[nodiscard]] HubResult<VerifiedCatalogDocument> Fetch(CatalogEndpoint endpoint) const;

        CatalogClientOptions m_Options;
        CatalogTrustStore m_TrustStore;
        CatalogCache m_Cache;
        CatalogTransport m_Transport;
    };
} // namespace KeireHub
