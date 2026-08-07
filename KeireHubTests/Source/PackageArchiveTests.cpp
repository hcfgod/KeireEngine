#include "TestSodium.h"
#include "TestSupport.h"

#include "KeireHubRuntime/PackageArchive.h"

#include "DistributionEncoding.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <zstd.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    struct RawRecord final
    {
        std::uint8_t Kind = 1;
        std::string Path;
        std::uint64_t DeclaredSize = 0;
        std::string Bytes;
    };

    [[nodiscard]] std::string Digest(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        return KeireHub::Detail::Sha256Hex(bytes);
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Could not read archive fixture.");
        const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        return {reinterpret_cast<const std::byte*>(bytes.data()),
                reinterpret_cast<const std::byte*>(bytes.data() + bytes.size())};
    }

    [[nodiscard]] SemanticVersion Version(const std::string_view value)
    {
        auto parsed = SemanticVersion::Parse(value);
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] PackageManifest Manifest(const std::vector<std::pair<std::string, std::string>>& files,
                                           const std::string& keyId = "release-key")
    {
        PackageManifest result{.Id = "keire.editor",
                               .Version = Version("1.2.3"),
                               .Kind = PackageKind::Editor,
                               .DisplayName = "Kéire Editor 1.2.3",
                               .Channel = "stable",
                               .Platform = "windows",
                               .Architecture = "x86_64",
                               .ArtifactSizeBytes = 1,
                               .ArtifactSha256 = KeireHubTests::Digest('a'),
                               .SignatureKeyId = keyId};
        for (const auto& [path, bytes] : files)
        {
            result.Files.push_back({.Path = path,
                                    .SizeBytes = bytes.size(),
                                    .Sha256 = Digest(bytes),
                                    .Mode = path.ends_with(".exe") ? 0755U : 0644U});
            result.InstalledSizeBytes += bytes.size();
        }
        return result;
    }

    void WritePayload(const std::filesystem::path& root, const std::vector<std::pair<std::string, std::string>>& files)
    {
        for (const auto& [path, bytes] : files)
            KeireHubTests::WriteText(root / path, bytes);
    }

    [[nodiscard]] CatalogTrustStore Trust(const KeireHubTests::TestSodiumSigner& signer)
    {
        auto trust = CatalogTrustStore::Create(
            {.TrustedPublicKeyDocuments = {signer.PublicKeyDocument()}, .NativeLibraryPath = signer.LibraryPath()});
        if (!trust)
            throw std::runtime_error(trust.Error().TechnicalDetails);
        return std::move(trust).Value();
    }

    template <std::unsigned_integral Integer> void Append(std::vector<std::byte>& bytes, Integer value)
    {
        for (std::size_t index = 0; index < sizeof(Integer); ++index)
        {
            bytes.push_back(static_cast<std::byte>(value & 0xffU));
            value >>= 8U;
        }
    }

    void Append(std::vector<std::byte>& destination, const std::span<const std::byte> source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    [[nodiscard]] std::vector<std::byte> SignatureBytes(const std::optional<DetachedSignatureMetadata>& signature)
    {
        if (!signature)
            return {};
        const auto text = nlohmann::json{{"algorithm", signature->Algorithm},
                                         {"keyId", signature->KeyId},
                                         {"signature", signature->Signature}}
                              .dump();
        return KeireHubTests::Bytes(text);
    }

    void WriteRawArchive(const std::filesystem::path& output, const std::span<const std::byte> exactManifest,
                         const std::optional<DetachedSignatureMetadata>& signature,
                         const std::vector<RawRecord>& records, const std::uint64_t endCount,
                         const std::uint64_t endBytes, const std::string_view trailing = {})
    {
        constexpr std::array<char, 8> magic{'K', 'E', 'I', 'R', 'P', 'K', 'G', '1'};
        std::vector<std::byte> plain;
        Append(plain, std::as_bytes(std::span(magic)));
        Append<std::uint32_t>(plain, 1);
        Append<std::uint64_t>(plain, exactManifest.size());
        const auto encodedSignature = SignatureBytes(signature);
        Append<std::uint32_t>(plain, static_cast<std::uint32_t>(encodedSignature.size()));
        Append(plain, exactManifest);
        Append(plain, encodedSignature);
        for (const auto& record : records)
        {
            Append<std::uint8_t>(plain, record.Kind);
            if (record.Kind != 1)
                continue;
            Append<std::uint32_t>(plain, static_cast<std::uint32_t>(record.Path.size()));
            Append<std::uint64_t>(plain, record.DeclaredSize);
            Append(plain, std::as_bytes(std::span(record.Path.data(), record.Path.size())));
            Append(plain, std::as_bytes(std::span(record.Bytes.data(), record.Bytes.size())));
        }
        Append<std::uint8_t>(plain, 0);
        Append<std::uint64_t>(plain, endCount);
        Append<std::uint64_t>(plain, endBytes);
        Append(plain, std::as_bytes(std::span(trailing.data(), trailing.size())));

        std::vector<std::byte> compressed(ZSTD_compressBound(plain.size()));
        const auto size = ZSTD_compress(compressed.data(), compressed.size(), plain.data(), plain.size(), 3);
        if (ZSTD_isError(size))
            throw std::runtime_error(ZSTD_getErrorName(size));
        compressed.resize(size);
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
        if (!stream)
            throw std::runtime_error("Could not write raw archive fixture.");
    }

    void AppendCompressedFrame(const std::filesystem::path& output, const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        std::vector<std::byte> compressed(ZSTD_compressBound(bytes.size()));
        const auto size = ZSTD_compress(compressed.data(), compressed.size(), bytes.data(), bytes.size(), 3);
        if (ZSTD_isError(size))
            throw std::runtime_error(ZSTD_getErrorName(size));
        std::ofstream stream(output, std::ios::binary | std::ios::app);
        stream.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(size));
        if (!stream)
            throw std::runtime_error("Could not append a compressed archive frame fixture.");
    }

    void WriteCompressedPlain(const std::filesystem::path& output, const std::span<const std::byte> plain)
    {
        std::vector<std::byte> compressed(ZSTD_compressBound(plain.size()));
        const auto size = ZSTD_compress(compressed.data(), compressed.size(), plain.data(), plain.size(), 3);
        if (ZSTD_isError(size))
            throw std::runtime_error(ZSTD_getErrorName(size));
        std::ofstream stream(output, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(size));
        if (!stream)
            throw std::runtime_error("Could not write a compressed archive fixture.");
    }

    void BindArchiveArtifact(PackageManifest& manifest, const std::filesystem::path& archive)
    {
        const auto bytes = ReadBytes(archive);
        manifest.ArtifactSizeBytes = bytes.size();
        manifest.ArtifactSha256 = KeireHub::Detail::Sha256Hex(bytes);
    }

    [[nodiscard]] HubResult<PackageArchiveExtraction> ExtractOnline(const std::filesystem::path& archive,
                                                                    const std::filesystem::path& staging,
                                                                    const PackageManifest& expected,
                                                                    const PackageArchiveCallbacks& callbacks = {})
    {
        return ExtractPackageArchiveToStaging(
            archive, staging, {.SignedCatalogManifest = &expected, .AllowedStagingParent = staging.parent_path()},
            callbacks);
    }
} // namespace

TEST_CASE("Generic package archives round trip canonical manifests and stream into staging")
{
    KeireHubTests::TemporaryDirectory temporary;
    const std::vector<std::pair<std::string, std::string>> files{{"bin/KeireClient.exe", "editor-binary"},
                                                                 {"Licenses/MIT.txt", "license-text"}};
    const auto payload = temporary.Path() / "payload";
    WritePayload(payload, files);
    const auto archive = temporary.Path() / "editor.keirepackage";

    auto written = WritePackageArchive(
        {.Manifest = Manifest(files), .PayloadRoot = payload, .Output = archive, .CompressionLevel = 5});
    REQUIRE(written);
    CHECK(written.Value().ArchiveSizeBytes == std::filesystem::file_size(archive));
    CHECK(written.Value().ArchiveSha256 == written.Value().Manifest.ArtifactSha256);
    CHECK(written.Value().ExactManifestBytes);
    CHECK_FALSE(written.Value().EmbeddedSignature.has_value());
    REQUIRE(written.Value().Manifest.Files.size() == 2U);
    CHECK(written.Value().Manifest.Files.front().Path == "Licenses/MIT.txt");
    CHECK(written.Value().Manifest.Files.back().Mode == 0755U);

    std::vector<PackageArchiveProgress> progress;
    const auto staging = temporary.Path() / ".keire-stage-roundtrip";
    auto extracted =
        ExtractOnline(archive, staging, written.Value().Manifest,
                      {.Progress = [&](const PackageArchiveProgress& value) { progress.push_back(value); }});
    REQUIRE(extracted);
    CHECK(extracted.Value().StagingRoot == staging);
    CHECK(extracted.Value().Metadata.Manifest.ArtifactSha256 == written.Value().ArchiveSha256);
    REQUIRE(extracted.Value().Metadata.ExactManifestBytes);
    REQUIRE(written.Value().ExactManifestBytes);
    CHECK(*extracted.Value().Metadata.ExactManifestBytes == *written.Value().ExactManifestBytes);
    CHECK(KeireHubTests::ReadText(staging / "bin/KeireClient.exe") == "editor-binary");
    CHECK(KeireHubTests::ReadText(staging / "Licenses/MIT.txt") == "license-text");
#if !defined(_WIN32)
    constexpr auto expectedDirectoryMode = std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                           std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                           std::filesystem::perms::others_exec;
    CHECK((std::filesystem::status(staging).permissions() & std::filesystem::perms::mask) == expectedDirectoryMode);
    CHECK((std::filesystem::status(staging / "bin").permissions() & std::filesystem::perms::mask) ==
          expectedDirectoryMode);
#endif
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.back().CompletedBytes == written.Value().Manifest.InstalledSizeBytes);
    CHECK(progress.back().TotalBytes == written.Value().Manifest.InstalledSizeBytes);
}

#if defined(_WIN32)
TEST_CASE("Package archive output cannot use a case alias inside its payload root")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto payload = temporary.Path() / "CasePayload";
    const std::vector<std::pair<std::string, std::string>> files{{"bin/tool", "payload"}};
    WritePayload(payload, files);
    const auto alias = payload.parent_path() / "casepayload";
    std::error_code equivalentError;
    if (!std::filesystem::equivalent(payload, alias, equivalentError) || equivalentError)
        return;

    const auto archive = alias / "inside.keirepackage";
    const auto written = WritePackageArchive({.Manifest = Manifest(files), .PayloadRoot = payload, .Output = archive});
    REQUIRE_FALSE(written);
    CHECK(written.Error().Code == HubErrorCode::InvalidArgument);
    CHECK_FALSE(std::filesystem::exists(archive));
}
#endif

TEST_CASE("Catalog-bound extraction accepts content-addressed download cache names")
{
    KeireHubTests::TemporaryDirectory temporary;
    const std::vector<std::pair<std::string, std::string>> files{{"bin/tool", "cached-package"}};
    const auto payload = temporary.Path() / "payload";
    WritePayload(payload, files);
    const auto archive = temporary.Path() / "source.keirepackage";
    auto written = WritePackageArchive({.Manifest = Manifest(files), .PayloadRoot = payload, .Output = archive});
    REQUIRE(written);
    const auto cache = temporary.Path() / (written.Value().ArchiveSha256 + ".package");
    std::filesystem::copy_file(archive, cache);

    const auto staging = temporary.Path() / ".keire-stage-cache";
    auto extracted = ExtractOnline(cache, staging, written.Value().Manifest);
    REQUIRE(extracted);
    CHECK(KeireHubTests::ReadText(staging / "bin/tool") == "cached-package");
}

TEST_CASE("Offline package imports require a trusted exact-byte Ed25519 signature")
{
    KeireHubTests::TemporaryDirectory temporary;
    KeireHubTests::TestSodiumSigner signer;
    const std::vector<std::pair<std::string, std::string>> files{{"bin/tool.exe", "signed-tool"}};
    const auto payload = temporary.Path() / "payload";
    WritePayload(payload, files);
    auto manifest = Manifest(files, signer.KeyId());
    auto exact = EncodePackageArchiveManifest(manifest);
    REQUIRE(exact);
    const DetachedSignatureMetadata signature{
        .Algorithm = "Ed25519", .KeyId = signer.KeyId(), .Signature = signer.SignBase64(exact.Value())};
    const auto archive = temporary.Path() / "offline.keirepackage";
    auto written = WritePackageArchive(
        {.Manifest = manifest, .PayloadRoot = payload, .Output = archive, .EmbeddedSignature = signature});
    REQUIRE(written);

    auto trust = Trust(signer);
    auto extracted =
        ExtractPackageArchiveToStaging(archive, temporary.Path() / ".keire-stage-offline",
                                       {.OfflineTrustStore = &trust, .AllowedStagingParent = temporary.Path()});
    REQUIRE(extracted);
    REQUIRE(extracted.Value().Metadata.EmbeddedSignature.has_value());
    CHECK(extracted.Value().Metadata.EmbeddedSignature->KeyId == signer.KeyId());
    CHECK(extracted.Value().Metadata.Manifest.ArtifactSha256 == written.Value().ArchiveSha256);

    const auto unsignedArchive = temporary.Path() / "unsigned.keirepackage";
    REQUIRE(WritePackageArchive({.Manifest = manifest, .PayloadRoot = payload, .Output = unsignedArchive}));
    const auto unsignedImport =
        ExtractPackageArchiveToStaging(unsignedArchive, temporary.Path() / ".keire-stage-unsigned",
                                       {.OfflineTrustStore = &trust, .AllowedStagingParent = temporary.Path()});
    REQUIRE_FALSE(unsignedImport);
    CHECK(unsignedImport.Error().Code == HubErrorCode::CatalogSignatureInvalid);
    CHECK_FALSE(std::filesystem::exists(temporary.Path() / ".keire-stage-unsigned"));

    const auto badArchive = temporary.Path() / "bad-signature.keirepackage";
    const DetachedSignatureMetadata badSignature{.Algorithm = "Ed25519",
                                                 .KeyId = signer.KeyId(),
                                                 .Signature =
                                                     signer.SignBase64(KeireHubTests::Bytes("not-the-manifest"))};
    REQUIRE(WritePackageArchive(
        {.Manifest = manifest, .PayloadRoot = payload, .Output = badArchive, .EmbeddedSignature = badSignature}));
    const auto badImport =
        ExtractPackageArchiveToStaging(badArchive, temporary.Path() / ".keire-stage-bad-signature",
                                       {.OfflineTrustStore = &trust, .AllowedStagingParent = temporary.Path()});
    REQUIRE_FALSE(badImport);
    CHECK(badImport.Error().Code == HubErrorCode::CatalogSignatureInvalid);
    CHECK_FALSE(std::filesystem::exists(temporary.Path() / ".keire-stage-bad-signature"));

    KeireHubTests::TestSodiumSigner otherSigner(static_cast<unsigned char>(7));
    auto otherTrust = Trust(otherSigner);
    const auto untrusted =
        ExtractPackageArchiveToStaging(archive, temporary.Path() / ".keire-stage-untrusted",
                                       {.OfflineTrustStore = &otherTrust, .AllowedStagingParent = temporary.Path()});
    REQUIRE_FALSE(untrusted);
    CHECK(untrusted.Error().Code == HubErrorCode::CatalogUntrustedKey);
    CHECK_FALSE(std::filesystem::exists(temporary.Path() / ".keire-stage-untrusted"));
}

TEST_CASE("Online package extraction rejects catalog identity size and digest mismatches")
{
    KeireHubTests::TemporaryDirectory temporary;
    const std::vector<std::pair<std::string, std::string>> files{{"payload.bin", "payload"}};
    const auto payload = temporary.Path() / "payload";
    WritePayload(payload, files);
    const auto archive = temporary.Path() / "catalog.keirepackage";
    auto written = WritePackageArchive({.Manifest = Manifest(files), .PayloadRoot = payload, .Output = archive});
    REQUIRE(written);

    auto wrongIdentity = written.Value().Manifest;
    wrongIdentity.Version = Version("1.2.4");
    const auto identity = ExtractOnline(archive, temporary.Path() / ".keire-stage-wrong-identity", wrongIdentity);
    REQUIRE_FALSE(identity);
    CHECK(identity.Error().Code == HubErrorCode::CatalogIdentityMismatch);

    auto wrongSize = written.Value().Manifest;
    ++wrongSize.ArtifactSizeBytes;
    const auto size = ExtractOnline(archive, temporary.Path() / ".keire-stage-wrong-size", wrongSize);
    REQUIRE_FALSE(size);
    CHECK(size.Error().Code == HubErrorCode::DownloadSizeMismatch);

    auto wrongDigest = written.Value().Manifest;
    wrongDigest.ArtifactSha256 = KeireHubTests::Digest('f');
    const auto digest = ExtractOnline(archive, temporary.Path() / ".keire-stage-wrong-digest", wrongDigest);
    REQUIRE_FALSE(digest);
    CHECK(digest.Error().Code == HubErrorCode::DownloadChecksumMismatch);
}

TEST_CASE("Package extraction stays inside the caller-authorized staging parent")
{
    KeireHubTests::TemporaryDirectory temporary;
    const std::vector<std::pair<std::string, std::string>> files{{"payload.bin", "payload"}};
    const auto payload = temporary.Path() / "payload";
    WritePayload(payload, files);
    const auto archive = temporary.Path() / "confined.keirepackage";
    auto written = WritePackageArchive({.Manifest = Manifest(files), .PayloadRoot = payload, .Output = archive});
    REQUIRE(written);

    const auto authorized = temporary.Path() / "authorized";
    const auto other = temporary.Path() / "other";
    std::filesystem::create_directory(authorized);
    std::filesystem::create_directory(other);
    const auto staging = authorized / ".keire-stage-confined";
    const auto escaped = ExtractPackageArchiveToStaging(
        archive, staging, {.SignedCatalogManifest = &written.Value().Manifest, .AllowedStagingParent = other});
    REQUIRE_FALSE(escaped);
    CHECK(escaped.Error().Code == HubErrorCode::InvalidArgument);
    CHECK_FALSE(std::filesystem::exists(staging));

    const auto realParent = temporary.Path() / "real-staging";
    const auto linkedParent = temporary.Path() / "linked-staging";
    std::filesystem::create_directory(realParent);
    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(realParent, linkedParent, symlinkError);
    if (!symlinkError)
    {
        const auto linkedStaging = linkedParent / ".keire-stage-linked";
        const auto linked = ExtractPackageArchiveToStaging(
            archive, linkedStaging,
            {.SignedCatalogManifest = &written.Value().Manifest, .AllowedStagingParent = linkedParent});
        REQUIRE_FALSE(linked);
        CHECK(linked.Error().Code == HubErrorCode::UnsafeInstallRoot);
        CHECK_FALSE(std::filesystem::exists(realParent / ".keire-stage-linked"));
    }

    const auto realArchiveParent = temporary.Path() / "real-archives";
    const auto linkedArchiveParent = temporary.Path() / "linked-archives";
    std::filesystem::create_directory(realArchiveParent);
    std::filesystem::copy_file(archive, realArchiveParent / "linked-source.keirepackage");
    std::error_code archiveSymlinkError;
    std::filesystem::create_directory_symlink(realArchiveParent, linkedArchiveParent, archiveSymlinkError);
    if (!archiveSymlinkError)
    {
        const auto linkedSource = ExtractPackageArchiveToStaging(
            linkedArchiveParent / "linked-source.keirepackage", authorized / ".keire-stage-source-link",
            {.SignedCatalogManifest = &written.Value().Manifest, .AllowedStagingParent = authorized});
        REQUIRE_FALSE(linkedSource);
        CHECK(linkedSource.Error().Code == HubErrorCode::IoRead);
        CHECK_FALSE(std::filesystem::exists(authorized / ".keire-stage-source-link"));
    }
}

TEST_CASE("Package archive writers reject unsafe or incomplete payload inventories")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto payload = temporary.Path() / "payload";
    const std::vector<std::pair<std::string, std::string>> files{{"Data/file.bin", "data"}};
    WritePayload(payload, files);

    auto traversal = Manifest(files);
    traversal.Files.front().Path = "../outside.bin";
    CHECK_FALSE(EncodePackageArchiveManifest(traversal));

    auto windowsTraversal = Manifest(files);
    windowsTraversal.Files.front().Path = R"(..\outside.bin)";
    CHECK_FALSE(EncodePackageArchiveManifest(windowsTraversal));

    auto absolute = Manifest(files);
    absolute.Files.front().Path = "C:/outside.bin";
    CHECK_FALSE(EncodePackageArchiveManifest(absolute));

    auto device = Manifest(files);
    device.Files.front().Path = "CON";
    CHECK_FALSE(EncodePackageArchiveManifest(device));

    auto deviceExtension = Manifest(files);
    deviceExtension.Files.front().Path = "NUL.txt";
    CHECK_FALSE(EncodePackageArchiveManifest(deviceExtension));

    auto alternateStream = Manifest(files);
    alternateStream.Files.front().Path = "Data/file.bin:stream";
    CHECK_FALSE(EncodePackageArchiveManifest(alternateStream));

    auto trailingDot = Manifest(files);
    trailingDot.Files.front().Path = "Data/file.";
    CHECK_FALSE(EncodePackageArchiveManifest(trailingDot));

    auto trailingSpace = Manifest(files);
    trailingSpace.Files.front().Path = "Data/file ";
    CHECK_FALSE(EncodePackageArchiveManifest(trailingSpace));

    auto badMode = Manifest(files);
    badMode.Files.front().Mode = 0777U;
    CHECK_FALSE(EncodePackageArchiveManifest(badMode));

    auto collision = Manifest(files);
    collision.Files.push_back({.Path = "data/FILE.bin", .SizeBytes = 1, .Sha256 = Digest("x"), .Mode = 0644U});
    ++collision.InstalledSizeBytes;
    CHECK_FALSE(EncodePackageArchiveManifest(collision));

    KeireHubTests::WriteText(payload / "undeclared.bin", "extra");
    const auto undeclared = WritePackageArchive(
        {.Manifest = Manifest(files), .PayloadRoot = payload, .Output = temporary.Path() / "undeclared.keirepackage"});
    REQUIRE_FALSE(undeclared);
    CHECK(undeclared.Error().Code == HubErrorCode::PackageManifestInvalid);
    std::filesystem::remove(payload / "undeclared.bin");

    auto missing = Manifest(files);
    missing.Files.push_back({.Path = "missing.bin", .SizeBytes = 1, .Sha256 = Digest("x"), .Mode = 0644U});
    ++missing.InstalledSizeBytes;
    const auto missingFile = WritePackageArchive(
        {.Manifest = missing, .PayloadRoot = payload, .Output = temporary.Path() / "missing.keirepackage"});
    REQUIRE_FALSE(missingFile);
    CHECK(missingFile.Error().Code == HubErrorCode::PackageManifestInvalid);

    std::error_code symlinkError;
    std::filesystem::create_symlink(payload / "Data/file.bin", payload / "link.bin", symlinkError);
    if (!symlinkError)
    {
        const auto symlink = WritePackageArchive(
            {.Manifest = Manifest(files), .PayloadRoot = payload, .Output = temporary.Path() / "symlink.keirepackage"});
        REQUIRE_FALSE(symlink);
        CHECK(symlink.Error().Code == HubErrorCode::PackageManifestInvalid);
    }

    const auto realParent = temporary.Path() / "real-parent";
    const auto linkedParent = temporary.Path() / "linked-parent";
    std::filesystem::create_directory(realParent);
    std::error_code ancestorError;
    std::filesystem::create_directory_symlink(realParent, linkedParent, ancestorError);
    if (!ancestorError)
    {
        const auto linkedPayload = linkedParent / "payload";
        WritePayload(linkedPayload, files);
        const auto linked = WritePackageArchive({.Manifest = Manifest(files),
                                                 .PayloadRoot = linkedPayload,
                                                 .Output = temporary.Path() / "linked-ancestor.keirepackage"});
        REQUIRE_FALSE(linked);
        const bool expectedFailure = linked.Error().Code == HubErrorCode::PackageManifestInvalid ||
                                     linked.Error().Code == HubErrorCode::InvalidArgument;
        CHECK(expectedFailure);
    }
}

TEST_CASE("Package archive publication never follows a preplanted dangling output link")
{
    KeireHubTests::TemporaryDirectory temporary;
    const std::vector<std::pair<std::string, std::string>> files{{"payload.bin", "payload"}};
    const auto payload = temporary.Path() / "payload";
    WritePayload(payload, files);
    const auto outside = temporary.Path() / "outside.bin";
    const auto output = temporary.Path() / "linked-output.keirepackage";
    std::error_code symlinkError;
    std::filesystem::create_symlink(outside, output, symlinkError);
    if (symlinkError)
        return;

    const auto written = WritePackageArchive({.Manifest = Manifest(files), .PayloadRoot = payload, .Output = output});
    REQUIRE_FALSE(written);
    const bool expectedFailure =
        written.Error().Code == HubErrorCode::IoWrite || written.Error().Code == HubErrorCode::DestinationConflict;
    CHECK(expectedFailure);
    CHECK(std::filesystem::is_symlink(std::filesystem::symlink_status(output)));
    CHECK_FALSE(std::filesystem::exists(outside));
    for (const auto& entry : std::filesystem::directory_iterator(temporary.Path()))
        CHECK_FALSE(entry.path().filename().string().starts_with("linked-output.keirepackage.tmp-"));
}

TEST_CASE("Package readers reject hostile records and remove only their owned staging")
{
    KeireHubTests::TemporaryDirectory temporary;
    const std::vector<std::pair<std::string, std::string>> files{{"bin/tool.exe", "safe"}};
    auto manifest = Manifest(files);
    auto exact = EncodePackageArchiveManifest(manifest);
    REQUIRE(exact);

    const auto exercise = [&](const std::string_view name, const std::vector<RawRecord>& records,
                              const std::uint64_t count, const std::uint64_t bytes,
                              const std::string_view trailing = {})
    {
        const auto archive = temporary.Path() / (std::string(name) + ".keirepackage");
        WriteRawArchive(archive, exact.Value(), std::nullopt, records, count, bytes, trailing);
        auto expected = manifest;
        BindArchiveArtifact(expected, archive);
        const auto staging = temporary.Path() / (".keire-stage-" + std::string(name));
        const auto result = ExtractOnline(archive, staging, expected);
        CHECK_FALSE(result);
        CHECK_FALSE(std::filesystem::exists(staging));
        return result ? HubErrorCode::InvalidArgument : result.Error().Code;
    };

    CHECK(exercise("traversal", {{1, "../outside", 4, "safe"}}, 1, 4) == HubErrorCode::InvalidData);
    CHECK(exercise("absolute", {{1, "/outside", 4, "safe"}}, 1, 4) == HubErrorCode::InvalidData);
    CHECK(exercise("windows-absolute", {{1, "C:/outside", 4, "safe"}}, 1, 4) == HubErrorCode::InvalidData);
    CHECK(exercise("backslash-traversal", {{1, R"(..\outside)", 4, "safe"}}, 1, 4) == HubErrorCode::InvalidData);
    CHECK(exercise("undeclared", {{1, "bin/other.exe", 4, "safe"}}, 1, 4) == HubErrorCode::InvalidData);
    CHECK(exercise("missing", {}, 0, 0) == HubErrorCode::InvalidData);
    CHECK(exercise("symlink-record", {{2, {}, 0, {}}}, 1, 4) == HubErrorCode::InvalidData);
    CHECK(exercise("oversized", {{1, "bin/tool.exe", PackageArchiveLimits::MaximumFileBytes + 1U, {}}}, 1, 4) ==
          HubErrorCode::InvalidData);
    CHECK(exercise("bad-digest", {{1, "bin/tool.exe", 4, "evil"}}, 1, 4) == HubErrorCode::DownloadChecksumMismatch);
    CHECK(exercise("duplicate", {{1, "bin/tool.exe", 4, "safe"}, {1, "bin/tool.exe", 4, "safe"}}, 2, 8) ==
          HubErrorCode::InvalidData);
    CHECK(exercise("bad-total", {{1, "bin/tool.exe", 4, "safe"}}, 2, 4) == HubErrorCode::InvalidData);
    CHECK(exercise("trailing", {{1, "bin/tool.exe", 4, "safe"}}, 1, 4, "extra") == HubErrorCode::InvalidData);

    const auto trailingFrameArchive = temporary.Path() / "trailing-frame.keirepackage";
    WriteRawArchive(trailingFrameArchive, exact.Value(), std::nullopt, {{1, "bin/tool.exe", 4, "safe"}}, 1, 4);
    AppendCompressedFrame(trailingFrameArchive, "second-frame");
    auto trailingFrameManifest = manifest;
    BindArchiveArtifact(trailingFrameManifest, trailingFrameArchive);
    const auto trailingFrame =
        ExtractOnline(trailingFrameArchive, temporary.Path() / ".keire-stage-trailing-frame", trailingFrameManifest);
    REQUIRE_FALSE(trailingFrame);
    CHECK(trailingFrame.Error().Code == HubErrorCode::InvalidData);
    CHECK_FALSE(std::filesystem::exists(temporary.Path() / ".keire-stage-trailing-frame"));

    const auto truncatedArchive = temporary.Path() / "truncated.keirepackage";
    WriteRawArchive(truncatedArchive, exact.Value(), std::nullopt, {{1, "bin/tool.exe", 4, "safe"}}, 1, 4);
    const auto originalSize = std::filesystem::file_size(truncatedArchive);
    REQUIRE(originalSize > 2U);
    std::filesystem::resize_file(truncatedArchive, originalSize - 2U);
    auto truncatedManifest = manifest;
    BindArchiveArtifact(truncatedManifest, truncatedArchive);
    const auto truncated =
        ExtractOnline(truncatedArchive, temporary.Path() / ".keire-stage-truncated", truncatedManifest);
    REQUIRE_FALSE(truncated);
    CHECK(truncated.Error().Code == HubErrorCode::InvalidData);
    CHECK_FALSE(std::filesystem::exists(temporary.Path() / ".keire-stage-truncated"));

    const auto rejectHeader =
        [&](const std::string_view name, const std::uint64_t manifestSize, const std::uint32_t signatureSize)
    {
        constexpr std::array<char, 8> magic{'K', 'E', 'I', 'R', 'P', 'K', 'G', '1'};
        std::vector<std::byte> plain;
        Append(plain, std::as_bytes(std::span(magic)));
        Append<std::uint32_t>(plain, 1);
        Append(plain, manifestSize);
        Append(plain, signatureSize);
        const auto archive = temporary.Path() / (std::string(name) + ".keirepackage");
        WriteCompressedPlain(archive, plain);
        auto expected = manifest;
        BindArchiveArtifact(expected, archive);
        const auto staging = temporary.Path() / (".keire-stage-" + std::string(name));
        const auto result = ExtractOnline(archive, staging, expected);
        CHECK_FALSE(result);
        CHECK_FALSE(std::filesystem::exists(staging));
        return result ? HubErrorCode::InvalidArgument : result.Error().Code;
    };
    CHECK(rejectHeader("oversized-manifest",
                       static_cast<std::uint64_t>(PackageArchiveLimits::MaximumManifestBytes) + 1U,
                       0) == HubErrorCode::InvalidData);
    CHECK(rejectHeader("oversized-signature", 1,
                       static_cast<std::uint32_t>(PackageArchiveLimits::MaximumSignatureBytes + 1U)) ==
          HubErrorCode::InvalidData);
}

TEST_CASE("Package readers reject unsafe embedded inventories before creating staging")
{
    KeireHubTests::TemporaryDirectory temporary;
    const std::vector<std::pair<std::string, std::string>> files{{"Data/File.bin", "a"}};
    auto expected = Manifest(files);
    auto exact = EncodePackageArchiveManifest(expected);
    REQUIRE(exact);
    auto json = nlohmann::json::parse(KeireHubTests::Text(exact.Value()));
    json["files"].push_back({{"path", "data/file.bin"}, {"sizeBytes", 1}, {"sha256", Digest("b")}, {"mode", 420}});
    json["installedSizeBytes"] = 2;
    const auto hostile = KeireHubTests::Bytes(json.dump());
    const auto archive = temporary.Path() / "case-collision.keirepackage";
    WriteRawArchive(archive, hostile, std::nullopt, {}, 0, 0);
    BindArchiveArtifact(expected, archive);

    const auto staging = temporary.Path() / ".keire-stage-case-collision";
    const auto result = ExtractOnline(archive, staging, expected);
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::PackageManifestInvalid);
    CHECK_FALSE(std::filesystem::exists(staging));

    const std::vector<std::pair<std::string, std::string>> hostilePaths{{"device", "NUL.txt"},
                                                                        {"alternate-stream", "Data/file.bin:stream"},
                                                                        {"trailing-dot", "Data/file."},
                                                                        {"trailing-space", "Data/file "}};
    for (const auto& [name, path] : hostilePaths)
    {
        auto hostileJson = nlohmann::json::parse(KeireHubTests::Text(exact.Value()));
        hostileJson["files"][0]["path"] = path;
        const auto hostileManifest = KeireHubTests::Bytes(hostileJson.dump());
        const auto hostileArchive = temporary.Path() / (name + ".keirepackage");
        WriteRawArchive(hostileArchive, hostileManifest, std::nullopt, {}, 0, 0);
        auto hostileExpected = Manifest(files);
        BindArchiveArtifact(hostileExpected, hostileArchive);
        const auto hostileStaging = temporary.Path() / (".keire-stage-" + name);
        const auto hostileResult = ExtractOnline(hostileArchive, hostileStaging, hostileExpected);
        REQUIRE_FALSE(hostileResult);
        CHECK(hostileResult.Error().Code == HubErrorCode::PackageManifestInvalid);
        CHECK_FALSE(std::filesystem::exists(hostileStaging));
    }
}

TEST_CASE("Package extraction cancellation leaves no partial staging tree")
{
    KeireHubTests::TemporaryDirectory temporary;
    const std::vector<std::pair<std::string, std::string>> files{{"large.bin", std::string(512U * 1024U, 'x')}};
    const auto payload = temporary.Path() / "payload";
    WritePayload(payload, files);
    const auto archive = temporary.Path() / "cancel.keirepackage";
    auto written = WritePackageArchive({.Manifest = Manifest(files), .PayloadRoot = payload, .Output = archive});
    REQUIRE(written);

    std::size_t polls = 0;
    const auto staging = temporary.Path() / ".keire-stage-cancel";
    const auto result =
        ExtractOnline(archive, staging, written.Value().Manifest, {.Cancelled = [&] { return ++polls > 2U; }});
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::WorkerInterrupted);
    CHECK_FALSE(std::filesystem::exists(staging));
}
