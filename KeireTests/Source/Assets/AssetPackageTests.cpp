#include "Keire/Core.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    class AssetPackageFixture final
    {
      public:
        AssetPackageFixture() : Root(KeireTests::MakeTestDirectory("asset-package")), Payload(Root / "Payload")
        {
            std::filesystem::create_directories(Payload / "Assets");
            Write(Payload / "Assets" / "Material.keirematerial", "material-bytes\n");
            Write(Payload / "Assets" / "Material.keiremeta", "{\"schemaVersion\":1}\n");
            Write(Payload / "LICENSE", "MIT test license\n");
        }

        ~AssetPackageFixture() noexcept
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] Keire::AssetPackageManifest Manifest() const
        {
            Keire::AssetPackageManifest manifest;
            manifest.PackageId = "com.keire.tests.materials";
            manifest.Version = "1.2.3";
            manifest.PublisherId = "keire.tests";
            manifest.DisplayName = "Kéire Test Materials";
            manifest.Summary = "A deterministic package fixture.";
            manifest.InstallKind = Keire::AssetPackageInstallKind::AssetImport;
            manifest.Compatibility.MinimumEngineVersion = "0.3.1";
            manifest.Compatibility.Platforms = {"linux", "macos", "windows"};
            manifest.Compatibility.Architectures = {"arm64", "x86_64"};
            manifest.Compatibility.RendererCapabilities = {"pbr"};
            manifest.Compatibility.ManagedApiVersion = "0.3.1";
            manifest.Assets = {{.Id = Keire::AssetId(1, 2),
                                .Type = Keire::AssetTypeId(Keire::AssetId(3, 4)),
                                .SourcePath = "Assets/Material.keirematerial",
                                .MetadataPath = "Assets/Material.keiremeta"}};
            manifest.Licenses = {{.Id = "MIT", .Path = "LICENSE"}};
            manifest.EntryPoints = {"Assets/Material.keirematerial"};
            return Keire::InventoryAssetPackagePayload(std::move(manifest), Payload);
        }

        static void Write(const std::filesystem::path& path, const std::string_view bytes)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!stream)
                throw std::runtime_error("Could not prepare the asset-package test fixture.");
        }

        std::filesystem::path Root;
        std::filesystem::path Payload;
    };

    [[nodiscard]] Keire::AssetPackageManifest MinimalManifestWithFiles(std::vector<Keire::AssetPackageFile> files)
    {
        Keire::AssetPackageManifest manifest;
        manifest.PackageId = "com.keire.tests.invalid";
        manifest.Version = "1.0.0";
        manifest.PublisherId = "keire.tests";
        manifest.DisplayName = "Invalid package fixture";
        manifest.Compatibility.MinimumEngineVersion = "0.3.1";
        manifest.Files = std::move(files);
        for (const auto& file : manifest.Files)
            manifest.InstalledSizeBytes += file.SizeBytes;
        return manifest;
    }
} // namespace

TEST_CASE("asset-package manifests encode canonically and reject noncanonical paths")
{
    AssetPackageFixture fixture;
    const auto manifest = fixture.Manifest();
    const auto first = Keire::EncodeAssetPackageManifest(manifest);
    const auto second = Keire::EncodeAssetPackageManifest(Keire::DecodeAssetPackageManifest(first));

    CHECK(first == second);
    CHECK(Keire::DecodeAssetPackageManifest(first) == manifest);

    constexpr auto digest = "0000000000000000000000000000000000000000000000000000000000000000";
    auto traversal = MinimalManifestWithFiles({{.Path = "../escape", .Sha256 = digest}});
    CHECK_THROWS_AS(Keire::ValidateAssetPackageManifest(traversal), std::invalid_argument);

    auto reserved = MinimalManifestWithFiles({{.Path = "Assets/CON.txt", .Sha256 = digest}});
    CHECK_THROWS_AS(Keire::ValidateAssetPackageManifest(reserved), std::invalid_argument);

    auto collision = MinimalManifestWithFiles(
        {{.Path = "Assets/A.txt", .Sha256 = digest}, {.Path = "Assets/a.txt", .Sha256 = digest}});
    CHECK_THROWS_AS(Keire::ValidateAssetPackageManifest(collision), std::invalid_argument);

    auto invalidVersion = MinimalManifestWithFiles({{.Path = "Assets/a.txt", .Sha256 = digest}});
    invalidVersion.Version = "1.0.0-";
    CHECK_THROWS_AS(Keire::ValidateAssetPackageManifest(invalidVersion), std::invalid_argument);
}

TEST_CASE("asset-package archives are deterministic, inspectable, and extracted only into owned staging")
{
    AssetPackageFixture fixture;
    const auto manifest = fixture.Manifest();
    const auto firstPath = fixture.Root / "first.keireassetpackage";
    const auto secondPath = fixture.Root / "second.keireassetpackage";

    const auto first =
        Keire::WriteAssetPackageArchive({.Manifest = manifest, .PayloadRoot = fixture.Payload, .Output = firstPath});
    const auto second =
        Keire::WriteAssetPackageArchive({.Manifest = manifest, .PayloadRoot = fixture.Payload, .Output = secondPath});

    CHECK(first.ArchiveSha256 == second.ArchiveSha256);
    CHECK(first.ArchiveSizeBytes == second.ArchiveSizeBytes);
    CHECK(first.Manifest == manifest);

    const auto stagingParent = fixture.Root / "Staging";
    const auto staging = stagingParent / "Operation";
    std::filesystem::create_directory(stagingParent);
    const auto extracted =
        Keire::ExtractAssetPackageToStaging({.Archive = firstPath,
                                             .AllowedStagingParent = stagingParent,
                                             .StagingRoot = staging,
                                             .Verification = {.ExpectedArchiveSizeBytes = first.ArchiveSizeBytes,
                                                              .ExpectedArchiveSha256 = first.ArchiveSha256}});

    CHECK(extracted.Metadata.Manifest == manifest);
    CHECK(extracted.StagingRoot == std::filesystem::canonical(staging));
    CHECK(KeireTests::ReadFile(staging / "Assets" / "Material.keirematerial") == "material-bytes\n");
    CHECK(KeireTests::ReadFile(staging / "Assets" / "Material.keiremeta") == "{\"schemaVersion\":1}\n");
    CHECK_THROWS_AS(
        static_cast<void>(Keire::ExtractAssetPackageToStaging(
            {.Archive = firstPath, .AllowedStagingParent = stagingParent, .StagingRoot = fixture.Root / "escape"})),
        std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(Keire::WriteAssetPackageArchive(
                        {.Manifest = manifest, .PayloadRoot = fixture.Payload, .Output = firstPath})),
                    std::invalid_argument);
}

#if !defined(_WIN32)
TEST_CASE("asset-package extraction resolves an authorized parent path alias before creating staging")
{
    AssetPackageFixture fixture;
    const auto archive = fixture.Root / "alias.keireassetpackage";
    static_cast<void>(Keire::WriteAssetPackageArchive(
        {.Manifest = fixture.Manifest(), .PayloadRoot = fixture.Payload, .Output = archive}));
    const auto stagingParent = fixture.Root / "CanonicalStaging";
    const auto stagingAlias = fixture.Root / "StagingAlias";
    std::filesystem::create_directory(stagingParent);
    std::filesystem::create_directory_symlink(stagingParent, stagingAlias);

    const auto extracted = Keire::ExtractAssetPackageToStaging(
        {.Archive = archive, .AllowedStagingParent = stagingAlias, .StagingRoot = stagingAlias / "Operation"});

    CHECK(extracted.StagingRoot == std::filesystem::canonical(stagingParent) / "Operation");
    CHECK(KeireTests::ReadFile(extracted.StagingRoot / "Assets" / "Material.keirematerial") == "material-bytes\n");
}
#endif

TEST_CASE("asset-package signatures are independently verified against exact manifest bytes")
{
    AssetPackageFixture fixture;
    auto manifest = fixture.Manifest();
    manifest.SignatureKeyId = "marketplace-test-2026";
    const std::array signatureBytes{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    const auto exactManifest = Keire::EncodeAssetPackageManifest(manifest);
    const auto archive = fixture.Root / "signed.keireassetpackage";

    const auto written = Keire::WriteAssetPackageArchive(
        {.Manifest = manifest,
         .PayloadRoot = fixture.Payload,
         .Output = archive,
         .Signature = Keire::AssetPackageSignature{
             .KeyId = manifest.SignatureKeyId, .Bytes = std::vector(signatureBytes.begin(), signatureBytes.end())}});
    bool verifierCalled = false;
    const auto inspected = Keire::InspectAssetPackageArchive(
        archive,
        {.RequireSignature = true,
         .ExpectedArchiveSizeBytes = written.ArchiveSizeBytes,
         .ExpectedArchiveSha256 = written.ArchiveSha256,
         .VerifySignature = [&](const std::string_view algorithm, const std::string_view keyId,
                                const std::span<const std::byte> message, const std::span<const std::byte> signature)
         {
             verifierCalled = true;
             return algorithm == "ed25519" && keyId == manifest.SignatureKeyId &&
                    std::ranges::equal(message, std::as_bytes(std::span(exactManifest))) &&
                    std::ranges::equal(signature, signatureBytes);
         }});

    CHECK(verifierCalled);
    REQUIRE(inspected.Signature);
    CHECK(inspected.Signature->KeyId == manifest.SignatureKeyId);

    CHECK_THROWS_AS(static_cast<void>(Keire::InspectAssetPackageArchive(
                        archive, {.RequireSignature = true, .VerifySignature = [](auto&&...) { return false; }})),
                    std::runtime_error);
}

TEST_CASE("asset-package extraction removes partial staging after cancellation or archive corruption")
{
    AssetPackageFixture fixture;
    const auto manifest = fixture.Manifest();
    const auto archive = fixture.Root / "recovery.keireassetpackage";
    static_cast<void>(
        Keire::WriteAssetPackageArchive({.Manifest = manifest, .PayloadRoot = fixture.Payload, .Output = archive}));
    const auto stagingParent = fixture.Root / "Staging";
    std::filesystem::create_directory(stagingParent);

    const auto cancelledStaging = stagingParent / "Cancelled";
    CHECK_THROWS_AS(
        static_cast<void>(Keire::ExtractAssetPackageToStaging({.Archive = archive,
                                                               .AllowedStagingParent = stagingParent,
                                                               .StagingRoot = cancelledStaging,
                                                               .Callbacks = {.Cancelled = [] { return true; }}})),
        std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(cancelledStaging));

    {
        std::ofstream stream(archive, std::ios::binary | std::ios::app);
        stream.put('\0');
    }
    const auto corruptStaging = stagingParent / "Corrupt";
    CHECK_THROWS_AS(static_cast<void>(Keire::ExtractAssetPackageToStaging(
                        {.Archive = archive, .AllowedStagingParent = stagingParent, .StagingRoot = corruptStaging})),
                    std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(corruptStaging));
}

TEST_CASE("asset-package deterministic mutation corpus fails safely without partial extraction")
{
    AssetPackageFixture fixture;
    const auto archive = fixture.Root / "source.keireassetpackage";
    static_cast<void>(Keire::WriteAssetPackageArchive(
        {.Manifest = fixture.Manifest(), .PayloadRoot = fixture.Payload, .Output = archive}));
    const auto source = KeireTests::ReadFile(archive);
    REQUIRE(source.size() > 32U);

    std::vector<std::string> corpus;
    for (const auto length : {std::size_t{0}, std::size_t{1}, std::size_t{8}, std::size_t{9}, std::size_t{12},
                              std::size_t{13}, source.size() / 2U, source.size() - 1U})
    {
        corpus.push_back(source.substr(0, std::min(length, source.size())));
    }
    for (std::size_t index = 0; index < 24U; ++index)
    {
        auto mutated = source;
        const auto offset = std::min(index * (source.size() - 1U) / 23U, source.size() - 1U);
        mutated[offset] = static_cast<char>(static_cast<unsigned char>(mutated[offset]) ^
                                            static_cast<unsigned char>(1U << (index % 8U)));
        corpus.push_back(std::move(mutated));
    }
    corpus.push_back(source + std::string(1, '\0'));
    corpus.push_back(source + "trailing-garbage");

    const auto stagingParent = fixture.Root / "MutationStaging";
    std::filesystem::create_directory(stagingParent);
    for (std::size_t index = 0; index < corpus.size(); ++index)
    {
        const auto mutatedArchive = fixture.Root / ("mutated-" + std::to_string(index) + ".keireassetpackage");
        AssetPackageFixture::Write(mutatedArchive, corpus[index]);
        const auto staging = stagingParent / std::to_string(index);
        bool extracted = false;
        try
        {
            static_cast<void>(Keire::InspectAssetPackageArchive(mutatedArchive));
            static_cast<void>(Keire::ExtractAssetPackageToStaging(
                {.Archive = mutatedArchive, .AllowedStagingParent = stagingParent, .StagingRoot = staging}));
            extracted = true;
        }
        catch (const std::exception&)
        {
        }

        if (extracted)
        {
            CHECK(KeireTests::ReadFile(staging / "Assets" / "Material.keirematerial") == "material-bytes\n");
            CHECK(KeireTests::ReadFile(staging / "Assets" / "Material.keiremeta") == "{\"schemaVersion\":1}\n");
            CHECK(KeireTests::ReadFile(staging / "LICENSE") == "MIT test license\n");
            std::error_code ignored;
            std::filesystem::remove_all(staging, ignored);
        }
        else
        {
            CHECK_FALSE(std::filesystem::exists(staging));
        }
    }
}
