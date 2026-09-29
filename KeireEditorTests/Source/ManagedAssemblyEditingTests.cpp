#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ManagedAssemblyReferenceAsset.h"
#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/ManagedAssemblyEditing.h"
#include "KeireInternal/FileSystem.h"
#include <array>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("Assembly Inspector saves validated drafts and preserves external edits")
{
    const auto directory =
        std::filesystem::temp_directory_path() / ("KeireAssemblyDraft-" + Keire::AssetId::Generate().ToString());
    struct Cleanup
    {
        std::filesystem::path Directory;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Directory, ignored);
        }
    } cleanup{directory};
    std::filesystem::create_directories(directory);
    const auto path = directory / "Game.keireasm";
    Keire::ManagedAssemblyDefinition definition;
    definition.Name = "Game";
    definition.RootNamespace = "Game";
    auto original = Keire::ManagedAssemblyAsset::Encode(definition);
    Keire::Detail::WriteFileAtomically(path, original);
    definition.IncludePlatforms = {"Linux"};
    const auto edited = Keire::ManagedAssemblyAsset::Encode(definition);
    KeireEditor::SaveManagedAssemblySettings(path, edited, original);
    CHECK(Keire::Detail::ReadTextFile(path, 65536) ==
          std::string(reinterpret_cast<const char*>(edited.data()), edited.size()));
    CHECK_THROWS_AS(KeireEditor::SaveManagedAssemblySettings(path, original, original), std::runtime_error);
    const std::string invalid = "{}";
    CHECK_THROWS(KeireEditor::SaveManagedAssemblySettings(path, std::as_bytes(std::span(invalid)), edited));
    CHECK(Keire::Detail::ReadTextFile(path, 65536) ==
          std::string(reinterpret_cast<const char*>(edited.data()), edited.size()));
    const auto reference = directory / "Shared.asmref";
    const auto empty = Keire::ManagedAssemblyReferenceAsset::Encode("");
    Keire::Detail::WriteFileAtomically(reference, empty);
    CHECK_THROWS_AS(KeireEditor::SaveManagedAssemblySettings(reference, empty, empty), std::invalid_argument);
    const auto selected = Keire::ManagedAssemblyReferenceAsset::Encode("GUID:" + Keire::AssetId::Generate().ToString());
    CHECK_NOTHROW(KeireEditor::SaveManagedAssemblySettings(reference, selected, empty));
    std::filesystem::create_directories(directory / "Assets/Shared");
    std::filesystem::create_directories(directory / "Assets/Elsewhere");
    const auto assemblyId = Keire::AssetId::Generate();
    definition.Name = "Shared";
    definition.RootNamespace = "Company.Shared";
    Keire::Detail::WriteFileAtomically(directory / "Assets/Shared/Shared.keireasm",
                                       Keire::ManagedAssemblyAsset::Encode(definition));
    Keire::Detail::WriteFileAtomically(directory / "Assets/Elsewhere/Shared.asmref",
                                       Keire::ManagedAssemblyReferenceAsset::Encode("GUID:" + assemblyId.ToString()));
    const std::array records{
        Keire::AssetSourceRecord{assemblyId, Keire::ManagedAssemblyAsset::StaticType(), "Shared/Shared.keireasm"},
        Keire::AssetSourceRecord{Keire::AssetId::Generate(), Keire::ManagedAssemblyReferenceAsset::StaticType(),
                                 "Elsewhere/Shared.asmref"}};
    const auto candidates = KeireEditor::ReadManagedScriptAssemblies(directory, records);
    const auto placement = KeireEditor::ResolveManagedScriptPlacement(candidates, "Elsewhere/Children");
    CHECK(placement.Assembly == assemblyId);
    CHECK(placement.RootNamespace == "Company.Shared");
    CHECK(placement.SourceRootToAdd.empty());
}
