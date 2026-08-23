#include "KeireClient/Editor/ManagedDataTypeCache.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("Keire-ManagedDataTypeCache-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    [[nodiscard]] Keire::AssetId Id(const std::uint64_t value) { return Keire::AssetId(0x4d414e4147454454ULL, value); }

    [[nodiscard]] Keire::AssetSourceRecord Record(const Keire::AssetId asset, std::string digest,
                                                  std::filesystem::path path)
    {
        return {.Id = asset,
                .Type = Keire::ManagedDataAsset::StaticType(),
                .RelativePath = std::move(path),
                .SourceDigest = std::move(digest)};
    }

    void Write(const std::filesystem::path& path, const std::span<const std::byte> bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(output);
    }
} // namespace

TEST_CASE("Managed data type cache decodes changed sources and reuses unchanged results")
{
    TemporaryDirectory directory;
    const auto asset = Id(1);
    const auto managedType = Keire::ManagedTypeId(Id(2));
    Keire::ManagedDataDefinition definition{.ManagedType = managedType, .ManagedTypeName = "Tests.Settings"};
    const auto path = directory.Path / "Settings.keiredata";
    Write(path, Keire::ManagedDataAsset::Encode(definition));

    KeireEditor::ManagedDataTypeCache cache;
    auto records = std::vector{Record(asset, "digest-1", path.filename())};
    const auto first = cache.Refresh(records, directory.Path, 1024U * 1024U);
    CHECK(first.DecodedSources == 1);
    CHECK(first.ReusedSources == 0);
    CHECK(first.Diagnostics.empty());
    CHECK(cache.Type(asset) == managedType);

    const auto second = cache.Refresh(records, directory.Path, 1024U * 1024U);
    CHECK(second.DecodedSources == 0);
    CHECK(second.ReusedSources == 1);
    CHECK(second.Diagnostics.empty());
    CHECK(cache.Type(asset) == managedType);

    definition.ManagedType = Keire::ManagedTypeId(Id(3));
    Write(path, Keire::ManagedDataAsset::Encode(definition));
    records.front().SourceDigest = "digest-2";
    const auto changed = cache.Refresh(records, directory.Path, 1024U * 1024U);
    CHECK(changed.DecodedSources == 1);
    CHECK(changed.ReusedSources == 0);
    CHECK(changed.Diagnostics.empty());
    CHECK(cache.Type(asset) == definition.ManagedType);
}

TEST_CASE("Managed data type cache bounds decoding and diagnoses a failed source once per signature")
{
    TemporaryDirectory directory;
    const auto asset = Id(10);
    const auto path = directory.Path / "Broken.keiredata";
    const std::string malformed = "not managed data";
    Write(path, std::as_bytes(std::span(malformed)));
    const auto records = std::vector{Record(asset, "broken-digest", path.filename())};

    KeireEditor::ManagedDataTypeCache cache;
    const auto failed = cache.Refresh(records, directory.Path, 1024U * 1024U);
    CHECK(failed.DecodedSources == 1);
    REQUIRE(failed.Diagnostics.size() == 1);
    CHECK(failed.Diagnostics.front().find("Broken.keiredata") != std::string::npos);
    CHECK_FALSE(cache.Type(asset));

    const auto reusedFailure = cache.Refresh(records, directory.Path, 1024U * 1024U);
    CHECK(reusedFailure.DecodedSources == 0);
    CHECK(reusedFailure.ReusedSources == 1);
    CHECK(reusedFailure.Diagnostics.empty());

    KeireEditor::ManagedDataTypeCache bounded;
    const auto oversized = bounded.Refresh(records, directory.Path, malformed.size() - 1U);
    CHECK(oversized.DecodedSources == 1);
    REQUIRE(oversized.Diagnostics.size() == 1);
    CHECK(oversized.Diagnostics.front().find("configured") != std::string::npos);
    CHECK_FALSE(bounded.Type(asset));

    const auto recovered = bounded.Refresh(records, directory.Path, 1024U * 1024U);
    CHECK(recovered.DecodedSources == 1);
    CHECK(recovered.ReusedSources == 0);
    REQUIRE(recovered.Diagnostics.size() == 1);
    CHECK(recovered.Diagnostics.front().find("configured") == std::string::npos);
}

TEST_CASE("Managed data type cache retries transient source access failures")
{
    TemporaryDirectory directory;
    const auto asset = Id(15);
    const auto managedType = Keire::ManagedTypeId(Id(16));
    const Keire::ManagedDataDefinition definition{.ManagedType = managedType, .ManagedTypeName = "Tests.Retry"};
    const auto path = directory.Path / "Retry.keiredata";
    Write(path, Keire::ManagedDataAsset::Encode(definition));
    const auto records = std::vector{Record(asset, "retry", path.filename())};

    KeireEditor::ManagedDataTypeCache cache;
    const auto unavailable = directory.Path / "Retry.unavailable";
    std::filesystem::rename(path, unavailable);
    const auto failed = cache.Refresh(records, directory.Path, 1024U * 1024U);
    CHECK(failed.DecodedSources == 0);
    CHECK(failed.ReusedSources == 0);
    REQUIRE(failed.Diagnostics.size() == 1);
    CHECK_FALSE(cache.Type(asset));

    std::filesystem::rename(unavailable, path);
    const auto recovered = cache.Refresh(records, directory.Path, 1024U * 1024U);
    CHECK(recovered.DecodedSources == 1);
    CHECK(recovered.ReusedSources == 0);
    CHECK(recovered.Diagnostics.empty());
    CHECK(cache.Type(asset) == managedType);
}

TEST_CASE("Managed data type cache removes records that no longer exist")
{
    TemporaryDirectory directory;
    const auto asset = Id(20);
    const auto managedType = Keire::ManagedTypeId(Id(21));
    const Keire::ManagedDataDefinition definition{.ManagedType = managedType, .ManagedTypeName = "Tests.Removed"};
    const auto path = directory.Path / "Removed.keiredata";
    Write(path, Keire::ManagedDataAsset::Encode(definition));

    KeireEditor::ManagedDataTypeCache cache;
    const auto records = std::vector{Record(asset, "present", path.filename())};
    REQUIRE(cache.Refresh(records, directory.Path, 1024U * 1024U).Diagnostics.empty());
    REQUIRE(cache.Type(asset) == managedType);

    CHECK(cache.Refresh({}, directory.Path, 1024U * 1024U).Diagnostics.empty());
    CHECK_FALSE(cache.Type(asset));
}
