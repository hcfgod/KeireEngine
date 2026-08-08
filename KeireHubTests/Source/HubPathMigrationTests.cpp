#include "KeireHub/HubPathMigration.h"

#include <KeireHubTests/TestSupport.h>

#include <doctest/doctest.h>

using namespace KeireHub;

TEST_CASE("Hub migrates legacy mojibake preference and configured storage roots")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto canonicalPreference =
        temporary.Path() / std::filesystem::path(u8"Kéire") / std::filesystem::path(u8"Kéire");
    const auto legacyPreference =
        temporary.Path() / std::filesystem::path(u8"KÃ©ire") / std::filesystem::path(u8"KÃ©ire");
    KeireHubTests::WriteText(canonicalPreference / "Editor" / "workspace.json", "existing editor state");
    KeireHubTests::WriteText(legacyPreference / "Hub" / "settings.json", "preserved");

    REQUIRE(MigrateLegacyHubPreferenceRoot(canonicalPreference));
    CHECK(KeireHubTests::ReadText(canonicalPreference / "Hub" / "settings.json") == "preserved");
    CHECK(KeireHubTests::ReadText(canonicalPreference / "Editor" / "workspace.json") == "existing editor state");
    CHECK_FALSE(std::filesystem::exists(legacyPreference));

    const auto settingsPath = canonicalPreference / "Hub" / "settings-store.json";
    HubSettingsStore store(settingsPath);
    REQUIRE(store.Load());
    HubSettings settings;
    const auto legacyStorage =
        temporary.Path() / "Configured" / std::filesystem::path(u8"KÃ©ire") / std::filesystem::path(u8"KÃ©ire") / "Hub";
    settings.DefaultEditorRoot = legacyStorage / "Editors";
    settings.CacheRoot = legacyStorage / "Cache";
    settings.TemporaryRoot = legacyStorage / "Temporary";
    REQUIRE(store.Save(settings));
    KeireHubTests::WriteText(legacyStorage / "Temporary" / "PackageOperations" / "operation" / "request.json",
                             "preserved request");

    REQUIRE(RepairLegacyHubStorageRoots(store));
    const auto canonicalStorage = RepairLegacyHubPath(legacyStorage);
    const auto migrated = store.Snapshot();
    CHECK(migrated->DefaultEditorRoot == canonicalStorage / "Editors");
    CHECK(migrated->CacheRoot == canonicalStorage / "Cache");
    CHECK(migrated->TemporaryRoot == canonicalStorage / "Temporary");
    CHECK(KeireHubTests::ReadText(canonicalStorage / "Temporary" / "PackageOperations" / "operation" /
                                  "request.json") == "preserved request");
    CHECK_FALSE(std::filesystem::exists(legacyStorage));
}
