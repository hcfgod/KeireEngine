#include "Keire/Assets/Asset.h"
#include "Keire/Project/Project.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectFileTransaction.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace Keire::Detail;
    const std::filesystem::path Rendering = "ProjectSettings/Rendering.keiresettings";
    const std::filesystem::path Authoring = "ProjectSettings/Authoring.keiresettings";
    const std::filesystem::path Journal = "Library/ProjectSettingsSave/interrupted/journal.json";

    std::vector<std::byte> Bytes(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
        return {bytes.begin(), bytes.end()};
    }

    std::string Digest(const std::string_view text) { return DigestToString(Sha256(Bytes(text))); }

    struct Fixture
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() /
                                     ("Keire-Settings-Transaction-" + Keire::AssetId::Generate().ToString());

        Fixture() { std::filesystem::create_directories(Root / "ProjectSettings"); }

        ~Fixture()
        {
            SetAnchoredFileSystemOperationHookForTesting({});
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    };

    void Interrupted(const AnchoredFileSystem& fs)
    {
        fs.CreateDirectories(Journal.parent_path());
        fs.WriteFileAtomically(Journal.parent_path() / "0.backup", Bytes("old"));
        fs.WriteFileAtomically(Rendering, Bytes("new"));
        fs.WriteFileAtomically(Authoring, Bytes("created"));
        const nlohmann::json journal{
            {"schemaVersion", 1},
            {"state", "prepared"},
            {"files",
             {{{"destination", PathToUtf8(Rendering)}, {"original", Digest("old")}, {"replacement", Digest("new")}},
              {{"destination", PathToUtf8(Authoring)}, {"original", nullptr}, {"replacement", Digest("created")}}}}};
        fs.WriteFileAtomically(Journal, Bytes(journal.dump()));
    }
} // namespace

TEST_CASE("settings group publication bounds completed history and keeps material journals separate")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    fs.CreateDirectories("Library/MaterialShaderUpgrade/unrelated");
    fs.WriteFileAtomically("Library/MaterialShaderUpgrade/unrelated/keep.txt", Bytes("keep"));
    std::optional<std::vector<std::byte>> previous;
    for (int iteration = 0; iteration < 12; ++iteration)
    {
        const auto contents = Bytes(std::to_string(iteration));
        const std::vector<ProjectFileReplacement> files{{Rendering, previous, contents}};
        PublishProjectSettingsFiles(fixture.Root, files);
        CHECK(fs.Read(Rendering, 1024) == contents);
        CHECK_FALSE(HasPendingProjectSettingsSave(fixture.Root));
        CHECK(std::filesystem::is_empty(fixture.Root / "Library/ProjectSettingsSave"));
        previous = contents;
    }
    CHECK(fs.Exists("Library/MaterialShaderUpgrade/unrelated/keep.txt"));
}

TEST_CASE("settings group publication rejects stale snapshots and destinations before changing files")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    fs.WriteFileAtomically(Rendering, Bytes("external"));
    std::vector<ProjectFileReplacement> files{{Authoring, std::nullopt, Bytes("created")},
                                              {Rendering, Bytes("old"), Bytes("new")}};
    SUBCASE("stale snapshot") { CHECK_THROWS_AS(PublishProjectSettingsFiles(fixture.Root, files), std::runtime_error); }
    SUBCASE("unrelated settings")
    {
        files[1] = {"ProjectSettings/Player.keiresettings", std::nullopt, Bytes("new")};
        CHECK_THROWS_AS(PublishProjectSettingsFiles(fixture.Root, files), std::invalid_argument);
    }
    SUBCASE("traversal alias")
    {
        files[1] = {"ProjectSettings/../ProjectSettings/Rendering.keiresettings", std::nullopt, Bytes("new")};
        CHECK_THROWS_AS(PublishProjectSettingsFiles(fixture.Root, files), std::invalid_argument);
    }
    CHECK_FALSE(fs.Exists(Authoring));
    CHECK(fs.Read(Rendering, 1024) == Bytes("external"));
}

TEST_CASE("settings group interrupted recovery restores existing files and removes newly created files")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    Interrupted(fs);
    CHECK(HasPendingProjectSettingsSave(fixture.Root));
    const std::vector<ProjectFileReplacement> files{{Rendering, Bytes("new"), Bytes("later")}};
    CHECK_THROWS_AS(PublishProjectSettingsFiles(fixture.Root, files), std::runtime_error);
    CHECK(RecoverProjectSettingsFiles(fixture.Root) == 1);
    CHECK(fs.Read(Rendering, 1024) == Bytes("old"));
    CHECK_FALSE(fs.Exists(Authoring));
    CHECK_FALSE(HasPendingProjectSettingsSave(fixture.Root));
    CHECK(RecoverProjectSettingsFiles(fixture.Root) == 0);
}

TEST_CASE("settings recovery preflights every source and backup before touching any destination")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    Interrupted(fs);
    SUBCASE("last destination externally edited") { fs.WriteFileAtomically(Authoring, Bytes("external")); }
    SUBCASE("backup corrupted") { fs.WriteFileAtomically(Journal.parent_path() / "0.backup", Bytes("corrupt")); }
    SUBCASE("original source externally removed") { fs.Remove(Rendering); }
    CHECK_THROWS_AS((void)RecoverProjectSettingsFiles(fixture.Root), std::runtime_error);
    CHECK(HasPendingProjectSettingsSave(fixture.Root));
    if (fs.Exists(Rendering))
    {
        CHECK(fs.Read(Rendering, 1024) == Bytes("new"));
    }
    CHECK(fs.Exists(Authoring));
}

TEST_CASE("settings rollback preserves external edits made after group preflight")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    Interrupted(fs);
    SUBCASE("existing original would be restored") {}
    SUBCASE("new destination would be removed")
    {
        const auto bytes = fs.Read(Journal, 4096);
        auto journal = nlohmann::json::parse(reinterpret_cast<const char*>(bytes.data()),
                                             reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        journal["files"][0]["original"] = nullptr;
        fs.WriteFileAtomically(Journal, Bytes(journal.dump()));
    }
    bool externalEditInjected = false;
    SetAnchoredFileSystemOperationHookForTesting(
        [&](const std::string_view operation, const auto& path)
        {
            if (operation == "remove" && path == Authoring && !externalEditInjected)
            {
                externalEditInjected = true;
                fs.WriteFileAtomically(Rendering, Bytes("external during rollback"));
            }
        });
    CHECK_THROWS_AS((void)RecoverProjectSettingsFiles(fixture.Root), std::runtime_error);
    SetAnchoredFileSystemOperationHookForTesting({});
    CHECK(externalEditInjected);
    CHECK(fs.Read(Rendering, 1024) == Bytes("external during rollback"));
    CHECK_FALSE(fs.Exists(Authoring));
    CHECK(HasPendingProjectSettingsSave(fixture.Root));
    CHECK_THROWS_AS((void)RecoverProjectSettingsFiles(fixture.Root), std::runtime_error);
    CHECK(fs.Read(Rendering, 1024) == Bytes("external during rollback"));
}

TEST_CASE("settings publication rolls back earlier writes after a later write fails")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    fs.WriteFileAtomically(Rendering, Bytes("old"));
    const std::vector<ProjectFileReplacement> files{{Rendering, Bytes("old"), Bytes("new")},
                                                    {Authoring, std::nullopt, Bytes("created")}};
    SetAnchoredFileSystemOperationHookForTesting(
        [](const std::string_view operation, const auto& path)
        {
            if (operation == "write" && path == Authoring)
            {
                throw std::runtime_error("Injected settings publication failure");
            }
        });
    CHECK_THROWS_AS(PublishProjectSettingsFiles(fixture.Root, files), std::runtime_error);
    SetAnchoredFileSystemOperationHookForTesting({});
    CHECK(fs.Read(Rendering, 1024) == Bytes("old"));
    CHECK_FALSE(fs.Exists(Authoring));
    CHECK_FALSE(HasPendingProjectSettingsSave(fixture.Root));
    CHECK(RecoverProjectSettingsFiles(fixture.Root) == 0);
}

TEST_CASE("every settings publication boundary restores every combination of existing and absent originals")
{
    const std::array<std::filesystem::path, 4> destinations{Rendering, Authoring,
                                                            "ProjectSettings/EnvironmentTime.keiresettings",
                                                            "ProjectSettings/EnvironmentLighting.keiresettings"};
    for (unsigned originalMask = 0; originalMask < 16; ++originalMask)
    {
        for (std::size_t failureBoundary = 0; failureBoundary <= destinations.size(); ++failureBoundary)
        {
            CAPTURE(originalMask);
            CAPTURE(failureBoundary);
            Fixture fixture;
            const AnchoredFileSystem fs(fixture.Root);
            std::vector<ProjectFileReplacement> files;
            for (std::size_t index = 0; index < destinations.size(); ++index)
            {
                std::optional<std::vector<std::byte>> original;
                if ((originalMask & (1U << index)) != 0)
                {
                    original = Bytes("original-" + std::to_string(index));
                    fs.WriteFileAtomically(destinations[index], *original);
                }
                files.push_back({destinations[index], original, Bytes("replacement-" + std::to_string(index))});
            }
            unsigned journalWrites = 0;
            bool injected = false;
            SetAnchoredFileSystemOperationHookForTesting(
                [&](const std::string_view operation, const auto& path)
                {
                    if (operation != "write" || injected)
                    {
                        return;
                    }
                    if (path.filename() == "journal.json")
                    {
                        ++journalWrites;
                    }
                    const bool destinationFailure =
                        failureBoundary < destinations.size() && path == destinations[failureBoundary];
                    const bool commitFailure = failureBoundary == destinations.size() && journalWrites == 2;
                    if (destinationFailure || commitFailure)
                    {
                        injected = true;
                        throw std::runtime_error("Injected publication boundary failure");
                    }
                });
            CHECK_THROWS_AS(PublishProjectSettingsFiles(fixture.Root, files), std::runtime_error);
            SetAnchoredFileSystemOperationHookForTesting({});
            REQUIRE(injected);
            for (const auto& file : files)
            {
                CHECK(fs.Exists(file.Destination) == file.ExpectedOriginal.has_value());
                if (file.ExpectedOriginal)
                {
                    CHECK(fs.Read(file.Destination, 1024) == *file.ExpectedOriginal);
                }
            }
            CHECK_FALSE(HasPendingProjectSettingsSave(fixture.Root));
            CHECK(RecoverProjectSettingsFiles(fixture.Root) == 0);
            CHECK(std::filesystem::is_empty(fixture.Root / "Library/ProjectSettingsSave"));
        }
    }
}

TEST_CASE("settings completed journal cleanup can resume without rolling back committed content")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    const std::vector<ProjectFileReplacement> files{{Rendering, std::nullopt, Bytes("new")}};
    SetAnchoredFileSystemOperationHookForTesting(
        [](const std::string_view operation, const auto& path)
        {
            if (operation == "remove" && path.extension() == ".staged")
            {
                throw std::runtime_error("Injected cleanup failure");
            }
        });
    CHECK_NOTHROW(PublishProjectSettingsFiles(fixture.Root, files));
    SetAnchoredFileSystemOperationHookForTesting({});
    CHECK_FALSE(HasPendingProjectSettingsSave(fixture.Root));
    CHECK_FALSE(std::filesystem::is_empty(fixture.Root / "Library/ProjectSettingsSave"));
    CHECK(RecoverProjectSettingsFiles(fixture.Root) == 0);
    CHECK(fs.Read(Rendering, 1024) == Bytes("new"));
    CHECK(std::filesystem::is_empty(fixture.Root / "Library/ProjectSettingsSave"));
}

TEST_CASE("settings recovery rejects a journal targeting another project file")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    Interrupted(fs);
    const auto bytes = fs.Read(Journal, 4096);
    auto journal = nlohmann::json::parse(reinterpret_cast<const char*>(bytes.data()),
                                         reinterpret_cast<const char*>(bytes.data() + bytes.size()));
    journal["files"][1]["destination"] = "ProjectSettings/Player.keiresettings";
    fs.WriteFileAtomically(Journal, Bytes(journal.dump()));
    CHECK_THROWS_AS((void)RecoverProjectSettingsFiles(fixture.Root), std::invalid_argument);
    CHECK(fs.Read(Rendering, 1024) == Bytes("new"));
    CHECK(fs.Read(Authoring, 1024) == Bytes("created"));
    CHECK(HasPendingProjectSettingsSave(fixture.Root));
}

TEST_CASE("settings completed cleanup preserves unknown diagnostic files")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    Interrupted(fs);
    fs.WriteFileAtomically(Journal.parent_path() / "notes.txt", Bytes("user diagnostic"));
    CHECK(RecoverProjectSettingsFiles(fixture.Root) == 1);
    CHECK(fs.Read(Journal.parent_path() / "notes.txt", 1024) == Bytes("user diagnostic"));
    CHECK_FALSE(fs.Exists(Journal));
    CHECK_FALSE(HasPendingProjectSettingsSave(fixture.Root));
}

TEST_CASE("project open recovers settings before exposing a writable project")
{
    Fixture fixture;
    const auto created = Keire::Project::Create({fixture.Root, "Recovery", Keire::ProjectTemplate::Empty});
    const auto root = fixture.Root / "Recovery";
    const AnchoredFileSystem fs(root);
    Interrupted(fs);
    CHECK(Keire::Project::InspectMetadata(root).Status == Keire::ProjectStatus::RecoveryRequired);
    CHECK_THROWS_AS((void)Keire::Project::Open(root, Keire::ProjectOpenMode::ReadOnly), std::runtime_error);
    CHECK(fs.Read(Rendering, 1024) == Bytes("new"));
    CHECK(fs.Read(Authoring, 1024) == Bytes("created"));
    {
        const auto opened = Keire::Project::Open(root, Keire::ProjectOpenMode::Exclusive);
        CHECK(fs.Read(Rendering, 1024) == Bytes("old"));
        CHECK_FALSE(fs.Exists(Authoring));
        CHECK(Keire::Project::InspectMetadata(root).Status == Keire::ProjectStatus::Ready);
    }
    CHECK_NOTHROW((void)Keire::Project::Open(root, Keire::ProjectOpenMode::ReadOnly));
}

TEST_CASE("project open refuses a settings recovery conflict without overwriting other destinations")
{
    Fixture fixture;
    const auto created = Keire::Project::Create({fixture.Root, "Recovery", Keire::ProjectTemplate::Empty});
    const auto root = fixture.Root / "Recovery";
    const AnchoredFileSystem fs(root);
    Interrupted(fs);
    fs.WriteFileAtomically(Authoring, Bytes("external"));
    CHECK_THROWS_AS((void)Keire::Project::Open(root, Keire::ProjectOpenMode::Exclusive), std::runtime_error);
    CHECK(fs.Read(Rendering, 1024) == Bytes("new"));
    CHECK(fs.Read(Authoring, 1024) == Bytes("external"));
    CHECK(HasPendingProjectSettingsSave(root));
    CHECK(Keire::Project::InspectMetadata(root).Status == Keire::ProjectStatus::RecoveryRequired);
}

TEST_CASE("settings preparation interruptions reuse bounded staging before publication")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    fs.WriteFileAtomically(Rendering, Bytes("old"));
    const std::vector<ProjectFileReplacement> files{{Rendering, Bytes("old"), Bytes("new")}};
    for (int iteration = 0; iteration < 5; ++iteration)
    {
        SetAnchoredFileSystemOperationHookForTesting(
            [](const std::string_view operation, const auto& path)
            {
                if (operation == "write" && path.filename() == "journal.json")
                {
                    throw std::runtime_error("Injected preparation interruption");
                }
            });
        CHECK_THROWS_AS(PublishProjectSettingsFiles(fixture.Root, files), std::runtime_error);
        SetAnchoredFileSystemOperationHookForTesting({});
        CHECK(fs.Read(Rendering, 1024) == Bytes("old"));
        CHECK_FALSE(HasPendingProjectSettingsSave(fixture.Root));
        std::size_t directories = 0;
        for (const auto& entry : std::filesystem::directory_iterator(fixture.Root / "Library/ProjectSettingsSave"))
        {
            CHECK(entry.path().filename() == "active");
            ++directories;
        }
        CHECK(directories == 1);
    }
    CHECK(RecoverProjectSettingsFiles(fixture.Root) == 0);
    CHECK(std::filesystem::is_empty(fixture.Root / "Library/ProjectSettingsSave"));
    CHECK_NOTHROW(PublishProjectSettingsFiles(fixture.Root, files));
    CHECK(fs.Read(Rendering, 1024) == Bytes("new"));
}

TEST_CASE("unprepared settings cleanup preserves unknown files and blocks further staging")
{
    Fixture fixture;
    const AnchoredFileSystem fs(fixture.Root);
    const std::filesystem::path staging = "Library/ProjectSettingsSave/active";
    fs.CreateDirectories(staging);
    fs.WriteFileAtomically(staging / "notes.txt", Bytes("diagnostic"));
    fs.WriteFileAtomically(staging / "0.backup", Bytes("unused backup"));
    const std::vector<ProjectFileReplacement> files{{Rendering, std::nullopt, Bytes("new")}};
    CHECK_THROWS_AS(PublishProjectSettingsFiles(fixture.Root, files), std::runtime_error);
    CHECK(fs.Read(staging / "notes.txt", 1024) == Bytes("diagnostic"));
    CHECK_FALSE(fs.Exists(Rendering));
    CHECK_FALSE(fs.Exists(staging / "0.backup"));
}
