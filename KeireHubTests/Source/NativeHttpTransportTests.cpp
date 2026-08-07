#include "TestSupport.h"

#include "KeireHubRuntime/HubWorkerProtocol.h"
#include "KeireHubRuntime/NativeHttpTransport.h"
#include "NativeHttpTransportPolicy.h"

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <vector>

using namespace KeireHub;

namespace
{
    [[nodiscard]] std::vector<CatalogHttpHeader> CompleteHeaders(const std::string& length,
                                                                 const std::string& etag = "\"package-v1\"")
    {
        return {{"Content-Length", length}, {"ETag", etag}, {"Content-Encoding", "identity"}};
    }

    [[nodiscard]] DownloadRequest Request(const std::filesystem::path& cacheRoot)
    {
        return {.PackageId = "editor.windows.x86_64",
                .Url = "https://packages.keire.test/editor.package",
                .Sha256 = "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9",
                .SizeBytes = 11,
                .CacheRoot = cacheRoot};
    }
} // namespace

TEST_CASE("Native HTTP transport accepts bounded production configuration without network access")
{
    auto transport = NativeHttpTransport::Create();
    REQUIRE(transport);
    CHECK(static_cast<bool>(transport.Value().CreateCatalogTransport()));

    NativeHttpTransportOptions smallHeaders;
    smallHeaders.MaximumHeaderBytes = 1024;
    const auto invalid = NativeHttpTransport::ValidateOptions(smallHeaders);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.Error().Code == HubErrorCode::DistributionConfigurationInvalid);

    NativeHttpTransportOptions tooManyRedirects;
    tooManyRedirects.MaximumRedirects = 11;
    CHECK_FALSE(NativeHttpTransport::ValidateOptions(tooManyRedirects));

    NativeHttpTransportOptions credentials;
    credentials.CustomProxyUrl = "http://user:secret@proxy.example:8080";
    CHECK_FALSE(NativeHttpTransport::Create(credentials));
}

TEST_CASE("HTTP URL policy permits only HTTPS and explicit loopback development")
{
    const auto secure = KeireHub::Detail::ParseHttpUrl("https://packages.keire.test:8443/v1/a?part=1", false);
    REQUIRE(secure);
    CHECK(secure.Value().Secure);
    CHECK(secure.Value().Host == "packages.keire.test");
    REQUIRE(secure.Value().Port);
    CHECK(*secure.Value().Port == 8443);
    CHECK(secure.Value().Target == "/v1/a?part=1");

    CHECK_FALSE(KeireHub::Detail::ParseHttpUrl("http://127.0.0.42:5000/package", false));
    const auto loopback = KeireHub::Detail::ParseHttpUrl("http://127.0.0.42:5000/package", true);
    REQUIRE(loopback);
    CHECK(loopback.Value().Loopback);
    CHECK_FALSE(loopback.Value().Secure);
    CHECK(KeireHub::Detail::ParseHttpUrl("http://[::1]:5000/package", true));

    CHECK_FALSE(KeireHub::Detail::ParseHttpUrl("http://distribution.keire.test/package", true));
    CHECK_FALSE(KeireHub::Detail::ParseHttpUrl("https://user@packages.keire.test/package", false));
    CHECK_FALSE(KeireHub::Detail::ParseHttpUrl("https://packages.keire.test/a#fragment", false));
    CHECK_FALSE(KeireHub::Detail::ParseHttpUrl("https://packages.keire.test\\package", false));
    CHECK_FALSE(KeireHub::Detail::ParseHttpUrl("http://localhost.evil/package", true));

    KeireHubTests::TemporaryDirectory temporary;
    auto developmentDownload = Request(temporary.Path() / "Cache");
    developmentDownload.Url = "http://127.0.0.42:5000/package";
    developmentDownload.AllowInsecureLoopbackDevelopment = true;
    CHECK(DownloadManager::Validate(developmentDownload));
}

TEST_CASE("Custom proxy policy accepts credential-free HTTP and HTTPS origins")
{
    const auto http = KeireHub::Detail::ParseProxyUrl("http://proxy.example:8080");
    REQUIRE(http);
    CHECK_FALSE(http.Value().Secure);
    const auto https = KeireHub::Detail::ParseProxyUrl("https://proxy.example:8443");
    REQUIRE(https);
    CHECK(https.Value().Secure);

    CHECK_FALSE(KeireHub::Detail::ParseProxyUrl("socks5://proxy.example:1080"));
    CHECK_FALSE(KeireHub::Detail::ParseProxyUrl("http://user:secret@proxy.example:8080"));
    CHECK_FALSE(KeireHub::Detail::ParseProxyUrl("https://proxy.example:8443/config"));
    CHECK_FALSE(KeireHub::Detail::ParseProxyUrl("https://proxy.example:0"));
}

TEST_CASE("Redirect policy resolves relative targets and refuses insecure hops")
{
    auto relative =
        KeireHub::Detail::ResolveHttpRedirect("https://distribution.keire.test/v1/catalog/stable", "../next", false);
    REQUIRE(relative);
    CHECK(relative.Value() == "https://distribution.keire.test/v1/next");

    CHECK(
        KeireHub::Detail::ValidateHttpRedirect("https://distribution.keire.test/a", "https://cdn.keire.test/b", false));
    CHECK_FALSE(
        KeireHub::Detail::ValidateHttpRedirect("https://distribution.keire.test/a", "http://127.0.0.1/b", true));
    CHECK_FALSE(KeireHub::Detail::ValidateHttpRedirect("http://localhost:5000/a", "http://downloads.example/b", true));
}

TEST_CASE("HTTP response headers are bounded and security fields cannot repeat")
{
    const std::vector<CatalogHttpHeader> valid{{"ETag", "\"v1\""}, {"X-Test", "value"}};
    CHECK(KeireHub::Detail::ValidateHttpHeaders(valid, 1024));
    CHECK_FALSE(KeireHub::Detail::ValidateHttpHeaders(valid, 8));

    auto malformed = valid;
    malformed.push_back({"Bad Header", "value"});
    CHECK_FALSE(KeireHub::Detail::ValidateHttpHeaders(malformed, 1024));
    malformed = valid;
    malformed.push_back({"X-Value", "line\r\nbreak"});
    CHECK_FALSE(KeireHub::Detail::ValidateHttpHeaders(malformed, 1024));

    CHECK(KeireHub::Detail::ValidateIdentityHttpEncoding(valid));
    const std::vector<CatalogHttpHeader> compressed{{"Content-Encoding", "gzip"}};
    CHECK_FALSE(KeireHub::Detail::ValidateIdentityHttpEncoding(compressed));

    const std::vector<CatalogHttpHeader> repeated{{"ETag", "\"v1\""}, {"etag", "\"v1\""}};
    CHECK_FALSE(KeireHub::Detail::FindSingleHttpHeader(repeated, "ETag"));
}

TEST_CASE("Package response policy validates full and resumed byte streams")
{
    CHECK(KeireHub::Detail::IsStrongHttpETag("\"package-v1\""));
    CHECK_FALSE(KeireHub::Detail::IsStrongHttpETag("W/\"package-v1\""));

    auto fullHeaders = CompleteHeaders("11");
    const auto full = KeireHub::Detail::ParseDownloadResponse(200, fullHeaders, 5);
    REQUIRE(full);
    CHECK(full.Value().AcceptedOffset == 0);
    CHECK(full.Value().TotalBytes == 11);
    CHECK(full.Value().BodyBytes == 11);

    auto partialHeaders = CompleteHeaders("6");
    partialHeaders.push_back({"Content-Range", "bytes 5-10/11"});
    const auto partial = KeireHub::Detail::ParseDownloadResponse(206, partialHeaders, 5);
    REQUIRE(partial);
    CHECK(partial.Value().AcceptedOffset == 5);
    CHECK(partial.Value().TotalBytes == 11);
    CHECK(partial.Value().BodyBytes == 6);

    auto wrongStart = partialHeaders;
    wrongStart.back().Value = "bytes 4-9/11";
    CHECK_FALSE(KeireHub::Detail::ParseDownloadResponse(206, wrongStart, 5));
    CHECK_FALSE(KeireHub::Detail::ParseDownloadResponse(206, fullHeaders, 5));

    auto weakETag = CompleteHeaders("11", "W/\"package-v1\"");
    CHECK_FALSE(KeireHub::Detail::ParseDownloadResponse(200, weakETag, 0));
    auto encoded = CompleteHeaders("11");
    encoded.back().Value = "gzip";
    CHECK_FALSE(KeireHub::Detail::ParseDownloadResponse(200, encoded, 0));
    auto chunked = CompleteHeaders("11");
    chunked.push_back({"Transfer-Encoding", "chunked"});
    CHECK_FALSE(KeireHub::Detail::ParseDownloadResponse(200, chunked, 0));
    auto oversized = CompleteHeaders(std::to_string(DownloadManager::MaximumPackageBytes + 1U));
    CHECK_FALSE(KeireHub::Detail::ParseDownloadResponse(200, oversized, 0));
    CHECK_FALSE(KeireHub::Detail::ParseDownloadResponse(503, fullHeaders, 0));
}

TEST_CASE("Worker request journals preserve an optional validated custom proxy")
{
    KeireHubTests::TemporaryDirectory temporary;
    auto download = Request(temporary.Path() / "Cache");
    download.CustomProxyUrl = "https://proxy.example:8443";
    download.BandwidthLimitBytesPerSecond = 8ULL * 1024ULL * 1024ULL;
    const auto path = temporary.Path() / "request.json";
    REQUIRE(WriteHubWorkerRequest(path, {.TaskId = "download.editor", .Download = download}));
    const auto loaded = ReadHubWorkerRequest(path);
    REQUIRE(loaded);
    REQUIRE(loaded.Value().Download.CustomProxyUrl);
    CHECK(*loaded.Value().Download.CustomProxyUrl == "https://proxy.example:8443");
    CHECK(loaded.Value().Download.BandwidthLimitBytesPerSecond == 8ULL * 1024ULL * 1024ULL);

    download.CustomProxyUrl = "http://user:secret@proxy.example:8080";
    const auto invalid = DownloadManager::Validate(download);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.Error().Code == HubErrorCode::DistributionConfigurationInvalid);
}
