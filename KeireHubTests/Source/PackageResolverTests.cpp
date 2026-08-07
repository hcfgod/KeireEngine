#include "TestSupport.h"

#include "KeireHubRuntime/PackageResolver.h"

#include <doctest/doctest.h>

#include <algorithm>

using namespace KeireHub;

namespace
{
    [[nodiscard]] SemanticVersion Version(const std::string_view value)
    {
        auto parsed = SemanticVersion::Parse(value);
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] VersionConstraint Constraint(const std::string_view value)
    {
        auto parsed = VersionConstraint::Parse(value);
        if (!parsed)
            throw std::runtime_error(parsed.Error().Message);
        return std::move(parsed).Value();
    }

    [[nodiscard]] PackageManifest Package(std::string id, const std::string_view version,
                                          const std::string_view platform = "windows")
    {
        auto fileName = id + ".bin";
        return {.Id = std::move(id),
                .Version = Version(version),
                .Kind = PackageKind::Editor,
                .DisplayName = "Test Package",
                .Channel = "stable",
                .Platform = std::string(platform),
                .Architecture = "x86_64",
                .ArtifactSizeBytes = 1,
                .ArtifactSha256 = KeireHubTests::Digest(),
                .InstalledSizeBytes = 1,
                .Files = {{std::move(fileName), 1, KeireHubTests::Digest('b')}},
                .LicenseReferences = {"Licenses/LICENSE.txt"},
                .SignatureKeyId = "release-key"};
    }
} // namespace

TEST_CASE("Semantic versions follow SemVer precedence and ignore build metadata")
{
    CHECK(Version("1.0.0-alpha") < Version("1.0.0-alpha.1"));
    CHECK(Version("1.0.0-alpha.1") < Version("1.0.0-alpha.beta"));
    CHECK(Version("1.0.0-beta.11") < Version("1.0.0-rc.1"));
    CHECK(Version("1.0.0-rc.1") < Version("1.0.0"));
    CHECK(Version("1.0.0+build.1") == Version("1.0.0+build.2"));
    CHECK(Version("2.3.4-alpha.5+sha.abc").ToString() == "2.3.4-alpha.5+sha.abc");

    CHECK_FALSE(SemanticVersion::Parse("01.0.0"));
    CHECK_FALSE(SemanticVersion::Parse("1.0"));
    CHECK_FALSE(SemanticVersion::Parse("1.0.0-01"));
    CHECK_FALSE(SemanticVersion::Parse("1.0.0+"));
}

TEST_CASE("Version constraints support exact comparators caret and tilde ranges")
{
    CHECK(Constraint("^1.2.3").Matches(Version("1.9.0")));
    CHECK_FALSE(Constraint("^1.2.3").Matches(Version("2.0.0")));
    CHECK(Constraint("^0.2.3").Matches(Version("0.2.9")));
    CHECK_FALSE(Constraint("^0.2.3").Matches(Version("0.3.0")));
    CHECK(Constraint("~2.4.1").Matches(Version("2.4.99")));
    CHECK_FALSE(Constraint("~2.4.1").Matches(Version("2.5.0")));
    CHECK(Constraint(">=1.0.0 <2.0.0").Matches(Version("1.5.0")));
    CHECK_FALSE(Constraint(">=1.0.0 <2.0.0").Matches(Version("2.0.0")));
    CHECK(Constraint("*").IsAny());
    CHECK_FALSE(VersionConstraint::Parse(">=broken"));
    CHECK_FALSE(VersionConstraint::Parse("1.0.0 || 2.0.0"));
}

TEST_CASE("Package manifests encode and parse without losing signed catalog fields")
{
    auto package = Package("editor", "2.3.4-preview.1+build.7");
    package.EngineCompatibility = Constraint(">=2.0.0 <3.0.0");
    package.Dependencies.push_back({"toolchain", Constraint("^1.2.0")});
    package.Conflicts.push_back({"legacy-toolchain", Constraint("<1.0.0")});
    auto encoded = EncodePackageManifest(package);
    REQUIRE(encoded);
    auto decoded = ParsePackageManifest(encoded.Value());
    REQUIRE(decoded);
    CHECK(decoded.Value().Id == package.Id);
    CHECK(decoded.Value().Version.ToString() == package.Version.ToString());
    CHECK(decoded.Value().Kind == package.Kind);
    CHECK(decoded.Value().ArtifactSha256 == package.ArtifactSha256);
    REQUIRE(decoded.Value().Files.size() == package.Files.size());
    CHECK(decoded.Value().Files.front().Path == package.Files.front().Path);
    CHECK(decoded.Value().Files.front().SizeBytes == package.Files.front().SizeBytes);
    CHECK(decoded.Value().Files.front().Sha256 == package.Files.front().Sha256);
    CHECK(decoded.Value().Files.front().Mode == package.Files.front().Mode);
    REQUIRE(decoded.Value().EngineCompatibility);
    CHECK(decoded.Value().EngineCompatibility->ToString() == package.EngineCompatibility->ToString());
    REQUIRE(decoded.Value().Dependencies.size() == 1);
    CHECK(decoded.Value().Dependencies.front().Versions.ToString() == package.Dependencies.front().Versions.ToString());
    REQUIRE(decoded.Value().Conflicts.size() == 1);
    CHECK(decoded.Value().Conflicts.front().Versions.ToString() == "<1.0.0");
}

TEST_CASE("Package resolver selects the highest compatible dependency in topological order")
{
    auto editor = Package("editor", "3.0.0");
    editor.Dependencies.push_back({"module", Constraint("^1.0.0")});
    const auto moduleOld = Package("module", "1.1.0");
    const auto moduleNew = Package("module", "1.8.0");
    const auto moduleWrongMajor = Package("module", "2.0.0");

    const PackageResolver resolver;
    auto result = resolver.Resolve({editor, moduleOld, moduleNew, moduleWrongMajor}, {{"editor", Constraint("=3.0.0")}},
                                   {.Platform = "windows", .Architecture = "x86_64", .AvailableDiskBytes = 100});
    REQUIRE(result);
    REQUIRE(result.Value().InstallOrder.size() == 2);
    CHECK(result.Value().InstallOrder[0].Id == "module");
    CHECK(result.Value().InstallOrder[0].Version == Version("1.8.0"));
    CHECK(result.Value().InstallOrder[1].Id == "editor");
    CHECK(result.Value().RequiredDiskBytes == 2);
}

TEST_CASE("Package resolver backtracks when a newer dependency conflicts")
{
    auto root = Package("root", "1.0.0");
    root.Dependencies = {{"addon", Constraint("*")}, {"support", Constraint("*")}};
    auto addonNew = Package("addon", "2.0.0");
    addonNew.Conflicts.push_back({"support", Constraint("*")});
    const auto addonOld = Package("addon", "1.0.0");
    const auto support = Package("support", "1.0.0");

    auto result =
        PackageResolver{}.Resolve({root, addonNew, addonOld, support}, {{"root", Constraint("*")}},
                                  {.Platform = "windows", .Architecture = "x86_64", .AvailableDiskBytes = 100});
    REQUIRE(result);
    const auto addon = std::ranges::find(result.Value().InstallOrder, "addon", &PackageManifest::Id);
    REQUIRE(addon != result.Value().InstallOrder.end());
    CHECK(addon->Version == Version("1.0.0"));
}

TEST_CASE("Package resolver reports missing and unsatisfied dependencies distinctly")
{
    auto root = Package("root", "1.0.0");
    root.Dependencies.push_back({"missing", Constraint("*")});
    auto missing =
        PackageResolver{}.Resolve({root}, {{"root", Constraint("*")}},
                                  {.Platform = "windows", .Architecture = "x86_64", .AvailableDiskBytes = 100});
    REQUIRE_FALSE(missing);
    CHECK(missing.Error().Code == HubErrorCode::PackageMissingDependency);

    root.Dependencies = {{"module", Constraint("^2.0.0")}};
    auto unsatisfied =
        PackageResolver{}.Resolve({root, Package("module", "1.0.0")}, {{"root", Constraint("*")}},
                                  {.Platform = "windows", .Architecture = "x86_64", .AvailableDiskBytes = 100});
    REQUIRE_FALSE(unsatisfied);
    CHECK(unsatisfied.Error().Code == HubErrorCode::PackageVersionUnsatisfied);
}

TEST_CASE("Package resolver rejects conflicts cycles host mismatch and insufficient disk")
{
    auto left = Package("left", "1.0.0");
    auto right = Package("right", "1.0.0");
    left.Conflicts.push_back({"right", Constraint("*")});
    auto conflict =
        PackageResolver{}.Resolve({left, right}, {{"left", Constraint("*")}, {"right", Constraint("*")}},
                                  {.Platform = "windows", .Architecture = "x86_64", .AvailableDiskBytes = 100});
    REQUIRE_FALSE(conflict);
    CHECK(conflict.Error().Code == HubErrorCode::PackageConflict);

    auto cycleA = Package("cycle-a", "1.0.0");
    auto cycleB = Package("cycle-b", "1.0.0");
    cycleA.Dependencies.push_back({"cycle-b", Constraint("*")});
    cycleB.Dependencies.push_back({"cycle-a", Constraint("*")});
    auto cycle =
        PackageResolver{}.Resolve({cycleA, cycleB}, {{"cycle-a", Constraint("*")}},
                                  {.Platform = "windows", .Architecture = "x86_64", .AvailableDiskBytes = 100});
    REQUIRE_FALSE(cycle);
    CHECK(cycle.Error().Code == HubErrorCode::PackageDependencyCycle);

    auto linuxOnly = Package("linux-editor", "1.0.0", "linux");
    auto host = PackageResolver{}.Resolve({linuxOnly}, {{"linux-editor", Constraint("*")}},
                                          {.Platform = "windows", .Architecture = "x86_64", .AvailableDiskBytes = 100});
    REQUIRE_FALSE(host);
    CHECK(host.Error().Code == HubErrorCode::PackageHostIncompatible);

    auto large = Package("large", "1.0.0");
    large.Files.front().SizeBytes = 2;
    large.InstalledSizeBytes = 2;
    auto noDisk = PackageResolver{}.Resolve({large}, {{"large", Constraint("*")}},
                                            {.Platform = "windows", .Architecture = "x86_64", .AvailableDiskBytes = 1});
    REQUIRE_FALSE(noDisk);
    CHECK(noDisk.Error().Code == HubErrorCode::InsufficientDiskSpace);
}

TEST_CASE("Package manifests reject traversal case collisions and inconsistent sizes")
{
    const auto manifest = std::string(R"({
      "schemaVersion":1,"packageId":"editor","version":"1.2.3","type":"editor",
      "displayName":"Editor","channel":"stable","platform":"windows","architecture":"x86_64",
      "artifact":{"sizeBytes":50,"sha256":")") +
                          KeireHubTests::Digest() + R"("},"installedSizeBytes":2,
      "files":[
        {"path":"Bin/Editor.exe","sizeBytes":1,"sha256":")" +
                          KeireHubTests::Digest('b') + R"("},
        {"path":"bin/editor.exe","sizeBytes":1,"sha256":")" +
                          KeireHubTests::Digest('c') + R"("}
      ],"licenses":["Licenses/LICENSE.txt"],"signatureKeyId":"release-key"
    })";
    auto collision = ParsePackageManifest(manifest);
    REQUIRE_FALSE(collision);
    CHECK(collision.Error().Code == HubErrorCode::PackageManifestInvalid);

    auto unsafe = Package("unsafe", "1.0.0");
    unsafe.Files.front().Path = "../outside";
    CHECK_FALSE(ValidatePackageManifest(unsafe));

    auto fileDirectoryAlias = Package("file-directory-alias", "1.0.0");
    fileDirectoryAlias.InstalledSizeBytes = 2;
    fileDirectoryAlias.Files = {{"Foo", 1, KeireHubTests::Digest('b')}, {"foo/bar", 1, KeireHubTests::Digest('c')}};
    CHECK_FALSE(ValidatePackageManifest(fileDirectoryAlias));

    auto directoryCaseAlias = Package("directory-case-alias", "1.0.0");
    directoryCaseAlias.InstalledSizeBytes = 2;
    directoryCaseAlias.Files = {{"Dir/a", 1, KeireHubTests::Digest('b')}, {"dir/b", 1, KeireHubTests::Digest('c')}};
    CHECK_FALSE(ValidatePackageManifest(directoryCaseAlias));

    auto inconsistent = Package("inconsistent", "1.0.0");
    inconsistent.InstalledSizeBytes = 2;
    CHECK_FALSE(ValidatePackageManifest(inconsistent));
}
