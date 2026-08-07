#pragma once

#include "KeireHubRuntime/HubError.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    struct SemanticVersion final
    {
        std::uint64_t Major = 0;
        std::uint64_t Minor = 0;
        std::uint64_t Patch = 0;
        std::vector<std::string> Prerelease;
        std::vector<std::string> BuildMetadata;

        [[nodiscard]] static HubResult<SemanticVersion> Parse(std::string_view value);
        [[nodiscard]] std::string ToString() const;

        friend bool operator==(const SemanticVersion&, const SemanticVersion&) noexcept;
        friend std::strong_ordering operator<=>(const SemanticVersion&, const SemanticVersion&) noexcept;
    };

    enum class VersionComparison
    {
        Equal,
        Less,
        LessOrEqual,
        Greater,
        GreaterOrEqual
    };

    struct VersionClause final
    {
        VersionComparison Comparison = VersionComparison::Equal;
        SemanticVersion Version;
    };

    class VersionConstraint final
    {
      public:
        VersionConstraint() = default;

        [[nodiscard]] static HubResult<VersionConstraint> Parse(std::string_view value);
        [[nodiscard]] bool Matches(const SemanticVersion& version) const noexcept;
        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] bool IsAny() const noexcept;

      private:
        explicit VersionConstraint(std::vector<VersionClause> clauses);
        std::vector<VersionClause> m_Clauses;
    };

    enum class PackageKind
    {
        HubInstaller,
        Editor,
        BuildSupport,
        Template,
        LearningContent,
        Toolchain
    };

    struct PackageDependency final
    {
        std::string PackageId;
        VersionConstraint Versions;
    };

    struct PackageConflict final
    {
        std::string PackageId;
        VersionConstraint Versions;
    };

    struct PackageFile final
    {
        std::filesystem::path Path;
        std::uint64_t SizeBytes = 0;
        std::string Sha256;
        std::uint32_t Mode = 0644U;
    };

    struct PackageManifest final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::uint32_t SchemaVersion = CurrentSchemaVersion;
        std::string Id;
        SemanticVersion Version;
        PackageKind Kind = PackageKind::Editor;
        std::string DisplayName;
        std::string Channel;
        std::string Platform;
        std::string Architecture;
        std::optional<VersionConstraint> EngineCompatibility;
        std::vector<PackageDependency> Dependencies;
        std::vector<PackageConflict> Conflicts;
        std::uint64_t ArtifactSizeBytes = 0;
        std::string ArtifactSha256;
        std::uint64_t InstalledSizeBytes = 0;
        std::vector<PackageFile> Files;
        std::vector<std::string> LicenseReferences;
        std::string SignatureKeyId;
    };

    struct PackageRequirement final
    {
        std::string PackageId;
        VersionConstraint Versions;
    };

    struct PackageHost final
    {
        std::string Platform;
        std::string Architecture;
        std::optional<SemanticVersion> EngineVersion;
        std::optional<std::uint64_t> AvailableDiskBytes;
    };

    struct PackageResolution final
    {
        std::vector<PackageManifest> InstallOrder;
        std::uint64_t RequiredDiskBytes = 0;
    };

    [[nodiscard]] HubStatus ValidatePackageManifest(const PackageManifest& manifest);
    [[nodiscard]] HubResult<std::string> EncodePackageManifest(const PackageManifest& manifest);
    [[nodiscard]] HubResult<PackageManifest> ParsePackageManifest(std::string_view document);

    class PackageResolver final
    {
      public:
        [[nodiscard]] HubResult<PackageResolution> Resolve(const std::vector<PackageManifest>& available,
                                                           const std::vector<PackageRequirement>& requested,
                                                           const PackageHost& host) const;
    };
} // namespace KeireHub
