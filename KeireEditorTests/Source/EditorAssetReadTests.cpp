#include "doctest/doctest.h"

#include "KeireClient/Editor/EditorAssetFileService.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <system_error>

namespace
{
    struct AssetReadFixture final
    {
        std::filesystem::path Root =
            std::filesystem::temp_directory_path() / ("Keire-Editor-Read-" + Keire::AssetId::Generate().ToString());

        AssetReadFixture() { std::filesystem::create_directories(Root); }
        ~AssetReadFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    };

    class FailedReadBuffer final : public std::streambuf
    {
      protected:
        std::streamsize xsgetn(char*, std::streamsize) override { throw std::runtime_error("injected read failure"); }
        int_type underflow() override { throw std::runtime_error("injected read failure"); }
    };
} // namespace

TEST_CASE("Shared editor asset reads report generic and explicit asset kinds")
{
    const auto missing = std::filesystem::temp_directory_path() / "Keire-Missing-Editor-Asset.bin";
    std::error_code error;
    std::filesystem::remove(missing, error);
    CHECK_THROWS_WITH_AS((void)KeireEditor::Detail::ReadBytes(missing), doctest::Contains("Cannot open asset:"),
                         std::runtime_error);
    CHECK_THROWS_WITH_AS((void)KeireEditor::Detail::ReadBytes(missing, "prefab asset"),
                         doctest::Contains("Cannot open prefab asset:"), std::runtime_error);
}

TEST_CASE("Bounded editor reads preserve binary data and accept exact limits and empty files")
{
    AssetReadFixture fixture;
    const auto path = fixture.Root / "source.bin";
    const std::string content{"\0\xff\nA", 4};
    {
        std::ofstream output(path, std::ios::binary);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        REQUIRE(output.good());
    }
    const auto bytes = KeireEditor::Detail::ReadBytes(path, "fixture", content.size());
    REQUIRE(bytes.size() == content.size());
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == content);
    CHECK_THROWS_WITH_AS((void)KeireEditor::Detail::ReadBytes(path, "fixture", content.size() - 1),
                         doctest::Contains("byte limit"), std::runtime_error);
    std::ofstream(path, std::ios::binary | std::ios::trunc).close();
    CHECK(KeireEditor::Detail::ReadBytes(path, "fixture", 0).empty());
    CHECK_THROWS_AS((void)KeireEditor::Detail::ReadBytes(fixture.Root), std::runtime_error);
}

TEST_CASE("Bounded editor reads reject oversized typed sources before allocation")
{
    AssetReadFixture fixture;
    for (const auto* extension : {".asmref", ".ASMREF", ".keirephysicsmaterial"})
    {
        const auto path = fixture.Root / (std::string("large") + extension);
        {
            std::ofstream output(path, std::ios::binary);
            output.seekp(64U * 1024U);
            output.put('x');
            REQUIRE(output.good());
        }
        CHECK_THROWS_WITH_AS((void)KeireEditor::Detail::ReadBytes(path), doctest::Contains("byte limit"),
                             std::runtime_error);
        CHECK_THROWS_AS((void)KeireEditor::Detail::ReadBytes(path, "fixture", 1024U * 1024U), std::runtime_error);
    }
    std::istringstream source("small");
    CHECK_THROWS_AS((void)KeireEditor::Detail::ReadAssetStreamBytes(source, std::numeric_limits<std::size_t>::max(), 8),
                    std::runtime_error);
    CHECK(source.tellg() == 0);
}

TEST_CASE("Bounded editor reads reject truncated growing and failed streams without returning partial bytes")
{
    std::istringstream truncated("abc");
    CHECK_THROWS_WITH_AS((void)KeireEditor::Detail::ReadAssetStreamBytes(truncated, 4, 4),
                         doctest::Contains("incomplete"), std::runtime_error);
    std::istringstream growing("abcde");
    CHECK_THROWS_WITH_AS((void)KeireEditor::Detail::ReadAssetStreamBytes(growing, 4, 4),
                         doctest::Contains("changed size"), std::runtime_error);
    std::istringstream initiallyEmpty("a");
    CHECK_THROWS_AS((void)KeireEditor::Detail::ReadAssetStreamBytes(initiallyEmpty, 0, 0), std::runtime_error);
    FailedReadBuffer failed;
    std::istream readFailure(&failed);
    CHECK_THROWS_AS((void)KeireEditor::Detail::ReadAssetStreamBytes(readFailure, 1, 1), std::runtime_error);
    std::istream endFailure(&failed);
    CHECK_THROWS_AS((void)KeireEditor::Detail::ReadAssetStreamBytes(endFailure, 0, 1), std::runtime_error);
    std::istringstream unavailable("abc");
    unavailable.setstate(std::ios::badbit);
    CHECK_THROWS_AS((void)KeireEditor::Detail::ReadAssetStreamBytes(unavailable, 3, 3), std::runtime_error);
}
