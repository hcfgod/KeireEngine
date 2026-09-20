#include "doctest/doctest.h"

#include "KeireClient/Editor/EditorAssetFileService.h"

#include <filesystem>
#include <stdexcept>
#include <system_error>

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
