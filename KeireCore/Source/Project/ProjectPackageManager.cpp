#include "Keire/Project/ProjectPackageManager.h"

#include "Keire/Assets/Asset.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <compare>
#include <fstream>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumProjectPackageDocumentBytes = 16U * 1024U * 1024U;
        constexpr std::string_view PackageMinimumEngineVersion = "0.3.1";

        struct SemanticVersion final
        {
            std::uint64_t Major = 0;
            std::uint64_t Minor = 0;
            std::uint64_t Patch = 0;
            std::string Suffix;

            [[nodiscard]] auto operator<=>(const SemanticVersion& other) const noexcept
            {
                if (const auto comparison = Major <=> other.Major; comparison != 0)
                    return comparison;
                if (const auto comparison = Minor <=> other.Minor; comparison != 0)
                    return comparison;
                if (const auto comparison = Patch <=> other.Patch; comparison != 0)
                    return comparison;
                if (Suffix == other.Suffix)
                    return std::strong_ordering::equal;
                if (Suffix.empty())
                    return std::strong_ordering::greater;
                if (other.Suffix.empty())
                    return std::strong_ordering::less;
                return Suffix <=> other.Suffix;
            }

            [[nodiscard]] bool operator==(const SemanticVersion& other) const noexcept
            {
                return (*this <=> other) == std::strong_ordering::equal;
            }
        };

        [[nodiscard]] SemanticVersion ParseSemanticVersion(const std::string_view value)
        {
            if (value.empty() || value.size() > 128)
                throw std::invalid_argument("Package version is empty or oversized.");
            SemanticVersion result;
            std::array<std::uint64_t*, 3> fields{&result.Major, &result.Minor, &result.Patch};
            const auto suffixBegin = value.find_first_of("-+");
            const auto core = value.substr(0, suffixBegin);
            std::size_t begin = 0;
            for (std::size_t index = 0; index < fields.size(); ++index)
            {
                const auto end = core.find('.', begin);
                const auto component =
                    core.substr(begin, end == std::string_view::npos ? core.size() - begin : end - begin);
                if (component.empty() || (component.size() > 1 && component.front() == '0') ||
                    !std::ranges::all_of(component,
                                         [](const unsigned char character) { return std::isdigit(character); }))
                    throw std::invalid_argument("Package version is not Semantic Version 2.0.0.");
                std::uint64_t parsed = 0;
                for (const char character : component)
                {
                    if (parsed > (UINT64_MAX - static_cast<unsigned>(character - '0')) / 10U)
                        throw std::invalid_argument("Package version component is out of range.");
                    parsed = parsed * 10U + static_cast<unsigned>(character - '0');
                }
                *fields[index] = parsed;
                if ((index < 2 && end == std::string_view::npos) || (index == 2 && end != std::string_view::npos))
                    throw std::invalid_argument("Package version is not Semantic Version 2.0.0.");
                begin = end == std::string_view::npos ? core.size() : end + 1;
            }
            if (suffixBegin != std::string_view::npos)
            {
                const auto suffix = value.substr(suffixBegin + 1);
                if (suffix.empty() ||
                    !std::ranges::all_of(suffix, [](const unsigned char character)
                                         { return std::isalnum(character) || character == '.' || character == '-'; }))
                    throw std::invalid_argument("Package version suffix is invalid.");
                result.Suffix = std::string(value.substr(suffixBegin));
            }
            return result;
        }

        [[nodiscard]] std::string_view Trim(std::string_view value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.remove_prefix(1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.remove_suffix(1);
            return value;
        }

        [[nodiscard]] bool IsPackageId(const std::string_view value)
        {
            return !value.empty() && value.size() <= 128 && value.find('.') != std::string_view::npos &&
                   !value.starts_with('.') && !value.ends_with('.') && value.find("..") == std::string_view::npos &&
                   std::ranges::all_of(value,
                                       [](const unsigned char character)
                                       {
                                           return std::islower(character) || std::isdigit(character) ||
                                                  character == '.' || character == '-';
                                       });
        }

        [[nodiscard]] bool IsSha256(const std::string_view value)
        {
            return value.size() == 64 &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return std::isdigit(character) || (character >= 'a' && character <= 'f'); });
        }

        [[nodiscard]] bool IsSafeSource(const std::string_view value)
        {
            return !value.empty() && value.size() <= 2048 && value.find_first_of("\r\n\0") == std::string_view::npos;
        }

        void ValidateRequirements(const std::vector<ProjectPackageRequirement>& dependencies)
        {
            std::string previous;
            for (const auto& dependency : dependencies)
            {
                if (!IsPackageId(dependency.PackageId) || dependency.VersionRange.empty() ||
                    dependency.VersionRange.size() > 128 || (!previous.empty() && previous >= dependency.PackageId))
                    throw std::invalid_argument("Project package dependencies must be unique, sorted, and bounded.");
                static_cast<void>(AssetPackageVersionSatisfies("1.0.0", dependency.VersionRange));
                previous = dependency.PackageId;
            }
        }

        [[nodiscard]] std::vector<ProjectPackageRequirement>
        NormalizeRequirements(std::vector<ProjectPackageRequirement> dependencies)
        {
            std::ranges::sort(dependencies, {}, &ProjectPackageRequirement::PackageId);
            ValidateRequirements(dependencies);
            return dependencies;
        }

        [[nodiscard]] std::vector<AssetPackageDependency>
        NormalizeDependencies(std::vector<AssetPackageDependency> dependencies)
        {
            std::ranges::sort(dependencies, {}, &AssetPackageDependency::PackageId);
            std::string previous;
            for (const auto& dependency : dependencies)
            {
                if (!IsPackageId(dependency.PackageId) || dependency.VersionRange.empty() ||
                    dependency.VersionRange.size() > 128 || (!previous.empty() && previous >= dependency.PackageId))
                    throw std::invalid_argument("Locked package dependencies must be unique, sorted, and bounded.");
                static_cast<void>(AssetPackageVersionSatisfies("1.0.0", dependency.VersionRange));
                previous = dependency.PackageId;
            }
            return dependencies;
        }

        [[nodiscard]] ProjectPackageManifest NormalizeManifest(ProjectPackageManifest manifest)
        {
            if (manifest.SchemaVersion != ProjectPackageManifest::CurrentSchemaVersion)
                throw std::invalid_argument("Project package manifest schema is unsupported.");
            manifest.Dependencies = NormalizeRequirements(std::move(manifest.Dependencies));
            return manifest;
        }

        [[nodiscard]] ProjectPackageLock NormalizeLock(ProjectPackageLock lock)
        {
            if (lock.SchemaVersion != ProjectPackageLock::CurrentSchemaVersion)
                throw std::invalid_argument("Project package lock schema is unsupported.");
            std::ranges::sort(lock.Packages, {}, &ProjectPackageLockEntry::PackageId);
            std::string previous;
            for (auto& package : lock.Packages)
            {
                static_cast<void>(ParseSemanticVersion(package.Version));
                if (!IsPackageId(package.PackageId) || !IsSha256(package.ArchiveSha256) ||
                    package.ArchiveSizeBytes == 0 || !IsSafeSource(package.Source) ||
                    (!package.SignatureKeyId.empty() && package.SignatureKeyId.size() > 128) ||
                    package.InstallKind != AssetPackageInstallKind::Registry ||
                    (!previous.empty() && previous >= package.PackageId))
                    throw std::invalid_argument("Project package lock entry is invalid or noncanonical.");
                package.Dependencies = NormalizeDependencies(std::move(package.Dependencies));
                previous = package.PackageId;
            }
            return lock;
        }

        [[nodiscard]] std::string InstallKindText(const AssetPackageInstallKind kind)
        {
            if (kind == AssetPackageInstallKind::Registry)
                return "registry";
            if (kind == AssetPackageInstallKind::AssetImport)
                return "assetImport";
            return "completeProject";
        }

        [[nodiscard]] AssetPackageInstallKind ParseInstallKind(const std::string_view value)
        {
            if (value == "registry")
                return AssetPackageInstallKind::Registry;
            if (value == "assetImport")
                return AssetPackageInstallKind::AssetImport;
            if (value == "completeProject")
                return AssetPackageInstallKind::CompleteProject;
            throw std::invalid_argument("Locked package installation kind is invalid.");
        }

        [[nodiscard]] Json EncodeDependencies(const std::vector<AssetPackageDependency>& dependencies)
        {
            Json result = Json::array();
            for (const auto& dependency : dependencies)
                result.push_back({{"packageId", dependency.PackageId}, {"version", dependency.VersionRange}});
            return result;
        }

        [[nodiscard]] ProjectPackageManifest ReadManifest(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return {};
            return DecodeProjectPackageManifest(Detail::ReadTextFile(path, MaximumProjectPackageDocumentBytes));
        }

        [[nodiscard]] ProjectPackageLock ReadLock(const std::filesystem::path& path)
        {
            if (!std::filesystem::exists(path))
                return {};
            return DecodeProjectPackageLock(Detail::ReadTextFile(path, MaximumProjectPackageDocumentBytes));
        }

        [[nodiscard]] bool Contains(const std::vector<std::string>& values, const std::string_view value)
        {
            return values.empty() || std::ranges::find(values, value) != values.end();
        }

        [[nodiscard]] bool Compatible(const AssetPackageManifest& manifest,
                                      const ProjectPackageManagerSpecification& specification,
                                      ProjectPackageConflict& conflict)
        {
            const auto engine = ParseSemanticVersion(specification.EngineVersion);
            if (engine < ParseSemanticVersion(manifest.Compatibility.MinimumEngineVersion) ||
                (manifest.Compatibility.MaximumEngineVersion &&
                 engine > ParseSemanticVersion(*manifest.Compatibility.MaximumEngineVersion)))
            {
                conflict = {ProjectPackageConflictKind::EngineIncompatible,
                            manifest.PackageId,
                            {},
                            "Package version " + manifest.Version + " does not support Kéire " +
                                specification.EngineVersion + '.'};
                return false;
            }
            if (!Contains(manifest.Compatibility.Platforms, specification.Platform) ||
                !Contains(manifest.Compatibility.Architectures, specification.Architecture))
            {
                conflict = {ProjectPackageConflictKind::PlatformIncompatible,
                            manifest.PackageId,
                            {},
                            "Package version " + manifest.Version + " does not support this platform target."};
                return false;
            }
            for (const auto& capability : manifest.Compatibility.RendererCapabilities)
            {
                if (std::ranges::find(specification.RendererCapabilities, capability) ==
                    specification.RendererCapabilities.end())
                {
                    conflict = {ProjectPackageConflictKind::RendererIncompatible,
                                manifest.PackageId,
                                {},
                                "Package requires renderer capability '" + capability + "'."};
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::filesystem::path CacheContentRoot(const ProjectPackageManagerSpecification& specification,
                                                             const std::string_view digest)
        {
            return specification.GlobalCacheRoot / "Content" / Detail::PathFromUtf8(digest);
        }

        [[nodiscard]] std::filesystem::path EmbeddedRoot(const ProjectPackageManagerSpecification& specification,
                                                         const std::string_view packageId)
        {
            return specification.ProjectRoot / "Packages" / Detail::PathFromUtf8(packageId);
        }

        void SetReadOnly(const std::filesystem::path& root, const bool readOnly)
        {
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(root, error), end; !error && iterator != end;
                 iterator.increment(error))
            {
                if (!iterator->is_regular_file(error))
                    continue;
                std::filesystem::permissions(
                    iterator->path(), std::filesystem::perms::owner_write,
                    readOnly ? std::filesystem::perm_options::remove : std::filesystem::perm_options::add, error);
                if (error)
                    break;
            }
            if (error)
                throw std::runtime_error("Could not update package content permissions: " + error.message());
        }

        void ValidateCachedContent(const AssetPackageManifest& manifest, const std::filesystem::path& root)
        {
            if (!std::filesystem::is_directory(root))
                throw std::runtime_error("Verified package content is missing from the global cache.");
            std::set<std::string> declared;
            for (const auto& file : manifest.Files)
            {
                const auto path = root / file.Path;
                if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path) ||
                    std::filesystem::file_size(path) != file.SizeBytes ||
                    Detail::DigestToString(Detail::Sha256File(path, AssetPackageArchiveLimits::MaximumFileBytes)) !=
                        file.Sha256)
                    throw std::runtime_error("Verified package cache content is missing or corrupt: " +
                                             Detail::PathToUtf8(file.Path));
                declared.emplace(Detail::PathToUtf8(file.Path));
            }
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(root, error), end; !error && iterator != end;
                 iterator.increment(error))
            {
                if (iterator->is_symlink(error) ||
                    (!iterator->is_regular_file(error) && !iterator->is_directory(error)))
                    throw std::runtime_error("Verified package cache contains an unsafe filesystem entry.");
                if (iterator->is_regular_file(error))
                {
                    const auto relative = Detail::PathToUtf8(iterator->path().lexically_relative(root));
                    if (!declared.contains(relative))
                        throw std::runtime_error("Verified package cache contains undeclared content.");
                }
            }
            if (error)
                throw std::runtime_error("Could not validate package cache content: " + error.message());
        }

        [[nodiscard]] Json UpgradeProjectDescriptor(const std::filesystem::path& projectRoot,
                                                    const std::string_view engineVersion)
        {
            const auto path = projectRoot / "ProjectSettings" / "Project.keireproject";
            auto descriptor = Json::parse(Detail::ReadTextFile(path, MaximumProjectPackageDocumentBytes));
            if (!descriptor.is_object() || !descriptor.contains("minimumEngineVersion"))
                throw std::runtime_error("Project descriptor cannot be version-gated for package management.");
            const auto minimum = descriptor.at("minimumEngineVersion").get<std::string>();
            if (ParseSemanticVersion(minimum) < ParseSemanticVersion(PackageMinimumEngineVersion))
                descriptor["minimumEngineVersion"] = PackageMinimumEngineVersion;
            if (descriptor.contains("lastSavedWithEngineVersion"))
                descriptor["lastSavedWithEngineVersion"] = engineVersion;
            return descriptor;
        }

        [[nodiscard]] std::vector<std::filesystem::path> TransactionalFiles(const std::filesystem::path& projectRoot)
        {
            return {ProjectPackageManager::ManifestPath(projectRoot), ProjectPackageManager::LockPath(projectRoot),
                    projectRoot / "ProjectSettings" / "Project.keireproject"};
        }

        void RestoreTransaction(const std::filesystem::path& projectRoot, const std::filesystem::path& transaction,
                                const Json& journal)
        {
            const auto files = TransactionalFiles(projectRoot);
            if (!journal.contains("files") || !journal.at("files").is_array() ||
                journal.at("files").size() != files.size())
                throw std::runtime_error("Package recovery journal is malformed.");
            for (std::size_t index = 0; index < files.size(); ++index)
            {
                const auto& record = journal.at("files").at(index);
                if (record.at("existed").get<bool>())
                {
                    const auto backup = transaction / "Before" / std::to_string(index);
                    Detail::WriteTextFileAtomically(files[index],
                                                    Detail::ReadTextFile(backup, MaximumProjectPackageDocumentBytes));
                }
                else
                {
                    std::error_code ignored;
                    std::filesystem::remove(files[index], ignored);
                }
            }
            if (journal.contains("createdPaths") && journal.at("createdPaths").is_array())
            {
                for (const auto& encoded : journal.at("createdPaths"))
                {
                    const auto path = projectRoot / Detail::PathFromUtf8(encoded.get<std::string>());
                    const auto normalized = std::filesystem::absolute(path).lexically_normal();
                    const auto packages = std::filesystem::absolute(projectRoot / "Packages").lexically_normal();
                    if (normalized.parent_path() == packages)
                    {
                        std::error_code ignored;
                        std::filesystem::remove_all(normalized, ignored);
                    }
                }
            }
        }

        void PublishProjectState(const ProjectPackageManagerSpecification& specification,
                                 const ProjectPackageManifest& manifest, const ProjectPackageLock& lock,
                                 const std::vector<std::filesystem::path>& createdPaths = {})
        {
            const auto operationId = AssetId::Generate().ToString();
            const auto transaction = specification.ProjectRoot / "Library" / "PackageTransactions" / operationId;
            std::filesystem::create_directories(transaction / "Before");
            const auto files = TransactionalFiles(specification.ProjectRoot);
            Json journal{{"schemaVersion", 1},
                         {"operationId", operationId},
                         {"state", "prepared"},
                         {"files", Json::array()},
                         {"createdPaths", Json::array()}};
            for (std::size_t index = 0; index < files.size(); ++index)
            {
                const bool existed = std::filesystem::is_regular_file(files[index]);
                journal["files"].push_back({{"existed", existed}});
                if (existed)
                    Detail::WriteTextFileAtomically(
                        transaction / "Before" / std::to_string(index),
                        Detail::ReadTextFile(files[index], MaximumProjectPackageDocumentBytes));
            }
            for (const auto& path : createdPaths)
            {
                const auto relative = path.lexically_relative(specification.ProjectRoot);
                if (relative.empty() || relative.begin() == relative.end() || *relative.begin() == "..")
                    throw std::invalid_argument("Package transaction created path escapes the project.");
                journal["createdPaths"].push_back(Detail::PathToUtf8(relative));
            }
            const auto journalPath = transaction / "journal.json";
            Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');
            try
            {
                journal["state"] = "publishing";
                Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');
                Detail::WriteTextFileAtomically(ProjectPackageManager::ManifestPath(specification.ProjectRoot),
                                                EncodeProjectPackageManifest(manifest));
                Detail::WriteTextFileAtomically(ProjectPackageManager::LockPath(specification.ProjectRoot),
                                                EncodeProjectPackageLock(lock));
                Detail::WriteTextFileAtomically(
                    specification.ProjectRoot / "ProjectSettings" / "Project.keireproject",
                    UpgradeProjectDescriptor(specification.ProjectRoot, specification.EngineVersion).dump(2) + '\n');
                journal["state"] = "committed";
                Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');
            }
            catch (...)
            {
                try
                {
                    RestoreTransaction(specification.ProjectRoot, transaction, journal);
                }
                catch (...)
                {
                }
                throw;
            }
            std::error_code ignored;
            std::filesystem::remove_all(transaction, ignored);
        }

        void Report(const ProjectPackageManagerSpecification& specification, const ProjectPackageState state,
                    const std::string_view operationId, const std::string_view packageId,
                    const std::string_view message)
        {
            if (specification.Events)
                specification.Events(
                    {state, std::string(operationId), std::string(packageId), 0, 0, std::string(message)});
        }
    } // namespace

    std::string EncodeProjectPackageManifest(const ProjectPackageManifest& value)
    {
        const auto manifest = NormalizeManifest(value);
        Json dependencies = Json::array();
        for (const auto& dependency : manifest.Dependencies)
            dependencies.push_back({{"packageId", dependency.PackageId}, {"version", dependency.VersionRange}});
        return Json{{"schemaVersion", manifest.SchemaVersion}, {"dependencies", std::move(dependencies)}}.dump(2) +
               '\n';
    }

    ProjectPackageManifest DecodeProjectPackageManifest(const std::string_view document)
    {
        if (document.empty() || document.size() > MaximumProjectPackageDocumentBytes)
            throw std::invalid_argument("Project package manifest is empty or oversized.");
        const auto root = Json::parse(document);
        if (!root.is_object() || root.size() != 2 || !root.at("dependencies").is_array())
            throw std::invalid_argument("Project package manifest shape is invalid.");
        ProjectPackageManifest result;
        result.SchemaVersion = root.at("schemaVersion").get<std::uint32_t>();
        for (const auto& value : root.at("dependencies"))
        {
            if (!value.is_object() || value.size() != 2)
                throw std::invalid_argument("Project package dependency shape is invalid.");
            result.Dependencies.push_back(
                {value.at("packageId").get<std::string>(), value.at("version").get<std::string>()});
        }
        return NormalizeManifest(std::move(result));
    }

    std::string EncodeProjectPackageLock(const ProjectPackageLock& value)
    {
        const auto lock = NormalizeLock(value);
        Json packages = Json::array();
        for (const auto& package : lock.Packages)
        {
            packages.push_back({{"packageId", package.PackageId},
                                {"version", package.Version},
                                {"archiveSha256", package.ArchiveSha256},
                                {"archiveSizeBytes", package.ArchiveSizeBytes},
                                {"source", package.Source},
                                {"signatureKeyId", package.SignatureKeyId},
                                {"installKind", InstallKindText(package.InstallKind)},
                                {"embedded", package.Embedded},
                                {"dependencies", EncodeDependencies(package.Dependencies)}});
        }
        return Json{{"schemaVersion", lock.SchemaVersion}, {"packages", std::move(packages)}}.dump(2) + '\n';
    }

    ProjectPackageLock DecodeProjectPackageLock(const std::string_view document)
    {
        if (document.empty() || document.size() > MaximumProjectPackageDocumentBytes)
            throw std::invalid_argument("Project package lock is empty or oversized.");
        const auto root = Json::parse(document);
        if (!root.is_object() || root.size() != 2 || !root.at("packages").is_array())
            throw std::invalid_argument("Project package lock shape is invalid.");
        ProjectPackageLock result;
        result.SchemaVersion = root.at("schemaVersion").get<std::uint32_t>();
        for (const auto& value : root.at("packages"))
        {
            if (!value.is_object() || value.size() != 9 || !value.at("dependencies").is_array())
                throw std::invalid_argument("Project package lock entry shape is invalid.");
            ProjectPackageLockEntry package;
            package.PackageId = value.at("packageId").get<std::string>();
            package.Version = value.at("version").get<std::string>();
            package.ArchiveSha256 = value.at("archiveSha256").get<std::string>();
            package.ArchiveSizeBytes = value.at("archiveSizeBytes").get<std::uint64_t>();
            package.Source = value.at("source").get<std::string>();
            package.SignatureKeyId = value.at("signatureKeyId").get<std::string>();
            package.InstallKind = ParseInstallKind(value.at("installKind").get<std::string>());
            package.Embedded = value.at("embedded").get<bool>();
            for (const auto& dependency : value.at("dependencies"))
            {
                if (!dependency.is_object() || dependency.size() != 2)
                    throw std::invalid_argument("Locked package dependency shape is invalid.");
                package.Dependencies.push_back(
                    {dependency.at("packageId").get<std::string>(), dependency.at("version").get<std::string>()});
            }
            result.Packages.push_back(std::move(package));
        }
        return NormalizeLock(std::move(result));
    }

    bool AssetPackageVersionSatisfies(const std::string_view versionText, const std::string_view rangeText)
    {
        const auto version = ParseSemanticVersion(versionText);
        auto range = Trim(rangeText);
        if (range.empty())
            throw std::invalid_argument("Package version range is empty.");
        if (range == "*")
            return true;
        if (range.front() == '^' || range.front() == '~')
        {
            const char kind = range.front();
            const auto lower = ParseSemanticVersion(Trim(range.substr(1)));
            auto upper = lower;
            upper.Suffix.clear();
            if (kind == '~')
            {
                ++upper.Minor;
                upper.Patch = 0;
            }
            else if (lower.Major != 0)
            {
                ++upper.Major;
                upper.Minor = 0;
                upper.Patch = 0;
            }
            else if (lower.Minor != 0)
            {
                ++upper.Minor;
                upper.Patch = 0;
            }
            else
            {
                ++upper.Patch;
            }
            return version >= lower && version < upper;
        }
        if (range.find(' ') == std::string_view::npos && !range.starts_with('>') && !range.starts_with('<') &&
            !range.starts_with('='))
            return versionText == range;

        while (!range.empty())
        {
            const auto separator = range.find(' ');
            const auto token = range.substr(0, separator);
            range = separator == std::string_view::npos ? std::string_view{} : Trim(range.substr(separator + 1));
            std::string_view operation;
            if (token.starts_with(">=") || token.starts_with("<=") || token.starts_with("=="))
                operation = token.substr(0, 2);
            else if (token.starts_with('>') || token.starts_with('<') || token.starts_with('='))
                operation = token.substr(0, 1);
            else
                throw std::invalid_argument("Package version range comparator is unsupported.");
            const auto expected = ParseSemanticVersion(token.substr(operation.size()));
            const bool matches = operation == ">="   ? version >= expected
                                 : operation == "<=" ? version <= expected
                                 : operation == ">"  ? version > expected
                                 : operation == "<"  ? version < expected
                                                     : version == expected;
            if (!matches)
                return false;
        }
        return true;
    }

    class ProjectPackageManager::Impl final
    {
      public:
        explicit Impl(ProjectPackageManagerSpecification value) : Specification(std::move(value))
        {
            if (Specification.ProjectRoot.empty() || Specification.GlobalCacheRoot.empty() ||
                Specification.EngineVersion.empty() || Specification.Platform.empty() ||
                Specification.Architecture.empty())
                throw std::invalid_argument("Project package manager specification is incomplete.");
            Specification.ProjectRoot = Detail::CanonicalExistingPath(Specification.ProjectRoot);
            if (!std::filesystem::is_regular_file(Specification.ProjectRoot / "ProjectSettings" /
                                                  "Project.keireproject"))
                throw std::invalid_argument("Project package manager requires a Kéire project root.");
            std::filesystem::create_directories(Specification.GlobalCacheRoot / "Content");
            std::filesystem::create_directories(Specification.GlobalCacheRoot / "Staging");
            Specification.GlobalCacheRoot = Detail::CanonicalExistingPath(Specification.GlobalCacheRoot);
            static_cast<void>(ParseSemanticVersion(Specification.EngineVersion));
        }

        ProjectPackageManagerSpecification Specification;
        mutable std::mutex OperationMutex;
    };

    ProjectPackageManager::ProjectPackageManager(ProjectPackageManagerSpecification specification)
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
    }

    ProjectPackageManager::~ProjectPackageManager() = default;

    ProjectPackageManifest ProjectPackageManager::Manifest() const
    {
        std::scoped_lock lock(m_Impl->OperationMutex);
        return ReadManifest(ManifestPath(m_Impl->Specification.ProjectRoot));
    }

    ProjectPackageLock ProjectPackageManager::Lock() const
    {
        std::scoped_lock lock(m_Impl->OperationMutex);
        return ReadLock(LockPath(m_Impl->Specification.ProjectRoot));
    }

    ProjectPackageInstallPlan ProjectPackageManager::PreflightInstall(const ProjectPackageInstallRequest& request) const
    {
        std::scoped_lock lock(m_Impl->OperationMutex);
        ProjectPackageInstallPlan plan{.Manifest = ReadManifest(ManifestPath(m_Impl->Specification.ProjectRoot)),
                                       .Lock = ReadLock(LockPath(m_Impl->Specification.ProjectRoot))};
        try
        {
            plan.Manifest.Dependencies = NormalizeRequirements(request.DirectDependencies);
        }
        catch (const std::exception& exception)
        {
            plan.Conflicts.push_back({ProjectPackageConflictKind::VersionMismatch, {}, {}, exception.what()});
            return plan;
        }
        std::map<std::string, AssetPackageArchiveMetadata, std::less<>> incoming;
        std::map<std::string, const ProjectPackageArchiveSource*, std::less<>> sources;
        for (const auto& source : request.Archives)
        {
            try
            {
                if (source.ExpectedArchiveSizeBytes == 0 || !IsSha256(source.ExpectedArchiveSha256) ||
                    !IsSafeSource(source.CatalogSource) ||
                    (!source.TrustedSignatureKeyId.empty() &&
                     (source.TrustedSignatureKeyId.size() > 128U || !IsSafeSource(source.TrustedSignatureKeyId))) ||
                    (source.RequireMarketplaceSignature && !source.TrustedSignatureKeyId.empty()))
                    throw std::invalid_argument("Registry packages require a trusted catalog size, hash, and source.");
                auto metadata = InspectAssetPackageArchive(
                    source.Archive, {.RequireSignature = source.RequireMarketplaceSignature,
                                     .ExpectedArchiveSizeBytes = source.ExpectedArchiveSizeBytes,
                                     .ExpectedArchiveSha256 = source.ExpectedArchiveSha256,
                                     .VerifySignature = m_Impl->Specification.VerifyMarketplaceSignature});
                if (metadata.Manifest.InstallKind != AssetPackageInstallKind::Registry)
                    throw std::invalid_argument("Only Registry packages can be added to the project package manifest.");
                ProjectPackageConflict conflict;
                if (!Compatible(metadata.Manifest, m_Impl->Specification, conflict))
                {
                    plan.Conflicts.push_back(std::move(conflict));
                    continue;
                }
                if (!incoming.emplace(metadata.Manifest.PackageId, metadata).second)
                    throw std::invalid_argument("Install request contains duplicate package identities.");
                sources.emplace(metadata.Manifest.PackageId, &source);
            }
            catch (const std::exception& exception)
            {
                plan.Conflicts.push_back({ProjectPackageConflictKind::VersionMismatch, {}, {}, exception.what()});
            }
        }
        if (!plan.Conflicts.empty())
            return plan;

        std::map<std::string, ProjectPackageLockEntry, std::less<>> resolved;
        for (const auto& package : plan.Lock.Packages)
            resolved.emplace(package.PackageId, package);
        for (const auto& [packageId, metadata] : incoming)
        {
            const auto existing = resolved.find(packageId);
            if (existing != resolved.end() && existing->second.Embedded &&
                existing->second.Version != metadata.Manifest.Version)
            {
                plan.Conflicts.push_back({ProjectPackageConflictKind::EmbeddedPackage,
                                          packageId,
                                          {},
                                          "Embedded packages must be reverted before they can be updated."});
                continue;
            }
            const auto& source = *sources.at(packageId);
            resolved[packageId] = {.PackageId = packageId,
                                   .Version = metadata.Manifest.Version,
                                   .ArchiveSha256 = metadata.ArchiveSha256,
                                   .ArchiveSizeBytes = metadata.ArchiveSizeBytes,
                                   .Source = source.CatalogSource,
                                   .SignatureKeyId = metadata.Manifest.SignatureKeyId.empty()
                                                         ? source.TrustedSignatureKeyId
                                                         : metadata.Manifest.SignatureKeyId,
                                   .InstallKind = AssetPackageInstallKind::Registry,
                                   .Dependencies = metadata.Manifest.Dependencies,
                                   .Embedded = existing != resolved.end() && existing->second.Embedded};
        }

        std::set<std::string> reachable;
        std::set<std::string> visiting;
        const auto visit = [&](const auto& self, const std::string& packageId, const std::string_view range,
                               const std::string_view parent) -> void
        {
            const auto package = resolved.find(packageId);
            if (package == resolved.end())
            {
                plan.Conflicts.push_back({ProjectPackageConflictKind::MissingDependency, packageId, std::string(parent),
                                          "Required package is not installed or supplied."});
                return;
            }
            if (!AssetPackageVersionSatisfies(package->second.Version, range))
            {
                plan.Conflicts.push_back({ProjectPackageConflictKind::VersionMismatch, packageId, std::string(parent),
                                          "Resolved package version " + package->second.Version + " does not satisfy " +
                                              std::string(range) + '.'});
                return;
            }
            if (reachable.contains(packageId))
                return;
            if (!visiting.emplace(packageId).second)
            {
                plan.Conflicts.push_back({ProjectPackageConflictKind::VersionMismatch, packageId, std::string(parent),
                                          "Package dependency graph contains a cycle."});
                return;
            }
            for (const auto& dependency : package->second.Dependencies)
                self(self, dependency.PackageId, dependency.VersionRange, packageId);
            visiting.erase(packageId);
            reachable.emplace(packageId);
        };
        for (const auto& dependency : plan.Manifest.Dependencies)
            visit(visit, dependency.PackageId, dependency.VersionRange, {});

        for (const auto& packageId : reachable)
        {
            const auto metadata = incoming.find(packageId);
            if (metadata == incoming.end())
                continue;
            for (const auto& conflict : metadata->second.Manifest.Conflicts)
            {
                const auto related = resolved.find(conflict.PackageId);
                if (related != resolved.end() && reachable.contains(conflict.PackageId) &&
                    AssetPackageVersionSatisfies(related->second.Version, conflict.VersionRange))
                    plan.Conflicts.push_back({ProjectPackageConflictKind::PackageConflict, packageId,
                                              conflict.PackageId, "Resolved packages declare a conflict."});
            }
        }
        if (!plan.Conflicts.empty())
            return plan;

        plan.Lock.Packages.clear();
        for (const auto& packageId : reachable)
            plan.Lock.Packages.push_back(resolved.at(packageId));
        plan.Lock = NormalizeLock(std::move(plan.Lock));
        for (const auto& [packageId, metadata] : incoming)
        {
            if (reachable.contains(packageId))
                plan.Archives.push_back(metadata);
        }
        return plan;
    }

    ProjectPackageLock ProjectPackageManager::Install(const ProjectPackageInstallRequest& request)
    {
        const auto operationId = AssetId::Generate().ToString();
        Report(m_Impl->Specification, ProjectPackageState::Resolving, operationId, {}, "Resolving package closure.");
        const auto plan = PreflightInstall(request);
        if (!plan.Valid())
            throw std::runtime_error(plan.Conflicts.front().Message);

        std::scoped_lock operation(m_Impl->OperationMutex);
        Report(m_Impl->Specification, ProjectPackageState::Staging, operationId, {}, "Preparing verified cache roots.");
        for (const auto& metadata : plan.Archives)
        {
            const auto finalRoot = CacheContentRoot(m_Impl->Specification, metadata.ArchiveSha256);
            if (std::filesystem::exists(finalRoot))
            {
                ValidateCachedContent(metadata.Manifest, finalRoot);
                continue;
            }
            const auto source =
                std::ranges::find_if(request.Archives, [&](const auto& candidate)
                                     { return candidate.ExpectedArchiveSha256 == metadata.ArchiveSha256; });
            if (source == request.Archives.end())
                throw std::logic_error("Package install plan lost its archive source.");
            const auto stagingParent = m_Impl->Specification.GlobalCacheRoot / "Staging";
            const auto staging = stagingParent / Detail::PathFromUtf8(operationId + "-" + metadata.Manifest.PackageId);
            static_cast<void>(ExtractAssetPackageToStaging(
                {.Archive = source->Archive,
                 .AllowedStagingParent = stagingParent,
                 .StagingRoot = staging,
                 .Verification = {.RequireSignature = source->RequireMarketplaceSignature,
                                  .ExpectedArchiveSizeBytes = source->ExpectedArchiveSizeBytes,
                                  .ExpectedArchiveSha256 = source->ExpectedArchiveSha256,
                                  .VerifySignature = m_Impl->Specification.VerifyMarketplaceSignature}}));
            SetReadOnly(staging, true);
            std::error_code error;
            if (!Detail::TryRenamePathWithRetry(staging, finalRoot, error))
            {
                if (!std::filesystem::is_directory(finalRoot))
                {
                    std::filesystem::remove_all(staging, error);
                    throw std::runtime_error("Could not publish verified package cache content: " + error.message());
                }
                std::filesystem::remove_all(staging, error);
            }
            ValidateCachedContent(metadata.Manifest, finalRoot);
        }

        Report(m_Impl->Specification, ProjectPackageState::Publishing, operationId, {},
               "Publishing package manifest and lockfile.");
        PublishProjectState(m_Impl->Specification, plan.Manifest, plan.Lock);
        Report(m_Impl->Specification, ProjectPackageState::Completed, operationId, {}, "Package install completed.");
        return plan.Lock;
    }

    ProjectPackageLock ProjectPackageManager::Remove(const std::string_view packageId)
    {
        std::scoped_lock operation(m_Impl->OperationMutex);
        auto manifest = ReadManifest(ManifestPath(m_Impl->Specification.ProjectRoot));
        const auto direct = std::ranges::find(manifest.Dependencies, packageId, &ProjectPackageRequirement::PackageId);
        if (direct == manifest.Dependencies.end())
            throw std::invalid_argument("Only direct project package dependencies can be removed.");
        manifest.Dependencies.erase(direct);

        const auto current = ReadLock(LockPath(m_Impl->Specification.ProjectRoot));
        std::map<std::string, ProjectPackageLockEntry, std::less<>> resolved;
        for (const auto& package : current.Packages)
            resolved.emplace(package.PackageId, package);
        std::set<std::string> reachable;
        const auto visit = [&](const auto& self, const std::string& id) -> void
        {
            if (!reachable.emplace(id).second)
                return;
            const auto package = resolved.find(id);
            if (package == resolved.end())
                throw std::runtime_error("Project package lock is missing a required dependency.");
            for (const auto& dependency : package->second.Dependencies)
                self(self, dependency.PackageId);
        };
        for (const auto& dependency : manifest.Dependencies)
            visit(visit, dependency.PackageId);
        ProjectPackageLock updated;
        for (const auto& id : reachable)
            updated.Packages.push_back(resolved.at(id));
        updated = NormalizeLock(std::move(updated));
        PublishProjectState(m_Impl->Specification, manifest, updated);
        for (const auto& package : current.Packages)
        {
            if (package.Embedded && !reachable.contains(package.PackageId))
            {
                std::error_code ignored;
                std::filesystem::remove_all(EmbeddedRoot(m_Impl->Specification, package.PackageId), ignored);
            }
        }
        return updated;
    }

    ProjectPackageLock ProjectPackageManager::Embed(const std::string_view packageId)
    {
        std::scoped_lock operation(m_Impl->OperationMutex);
        const auto manifest = ReadManifest(ManifestPath(m_Impl->Specification.ProjectRoot));
        auto lock = ReadLock(LockPath(m_Impl->Specification.ProjectRoot));
        const auto entry = std::ranges::find(lock.Packages, packageId, &ProjectPackageLockEntry::PackageId);
        if (entry == lock.Packages.end())
            throw std::invalid_argument("Cannot embed a package that is not in the project.");
        if (entry->Embedded)
            return lock;
        const auto source = CacheContentRoot(m_Impl->Specification, entry->ArchiveSha256);
        const auto destination = EmbeddedRoot(m_Impl->Specification, entry->PackageId);
        if (!std::filesystem::is_directory(source) || std::filesystem::exists(destination))
            throw std::runtime_error("Package cannot be embedded because its source or destination is invalid.");
        const auto staging = m_Impl->Specification.ProjectRoot / "Library" / "PackageEmbed" /
                             Detail::PathFromUtf8(AssetId::Generate().ToString());
        std::filesystem::create_directories(staging.parent_path());
        try
        {
            std::filesystem::copy(source, staging, std::filesystem::copy_options::recursive);
            SetReadOnly(staging, false);
            Detail::RenamePathWithRetry(staging, destination);
            entry->Embedded = true;
            entry->Source = "embedded";
            PublishProjectState(m_Impl->Specification, manifest, lock, {destination});
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove_all(staging, ignored);
            std::filesystem::remove_all(destination, ignored);
            throw;
        }
        return lock;
    }

    ProjectPackageLock ProjectPackageManager::RevertEmbedded(const std::string_view packageId)
    {
        std::scoped_lock operation(m_Impl->OperationMutex);
        const auto manifest = ReadManifest(ManifestPath(m_Impl->Specification.ProjectRoot));
        auto lock = ReadLock(LockPath(m_Impl->Specification.ProjectRoot));
        const auto entry = std::ranges::find(lock.Packages, packageId, &ProjectPackageLockEntry::PackageId);
        if (entry == lock.Packages.end() || !entry->Embedded)
            throw std::invalid_argument("Package is not embedded.");
        const auto cache = CacheContentRoot(m_Impl->Specification, entry->ArchiveSha256);
        if (!std::filesystem::is_directory(cache))
            throw std::runtime_error("Embedded package cannot be reverted while its verified cache entry is missing.");
        entry->Embedded = false;
        entry->Source = "cache:" + entry->ArchiveSha256;
        PublishProjectState(m_Impl->Specification, manifest, lock);
        std::error_code ignored;
        std::filesystem::remove_all(EmbeddedRoot(m_Impl->Specification, packageId), ignored);
        return lock;
    }

    std::vector<ProjectPackageMount> ProjectPackageManager::Mounts() const
    {
        std::scoped_lock operation(m_Impl->OperationMutex);
        const auto lock = ReadLock(LockPath(m_Impl->Specification.ProjectRoot));
        std::vector<ProjectPackageMount> result;
        for (const auto& package : lock.Packages)
        {
            const auto root = package.Embedded ? EmbeddedRoot(m_Impl->Specification, package.PackageId)
                                               : CacheContentRoot(m_Impl->Specification, package.ArchiveSha256);
            if (!std::filesystem::is_directory(root))
                throw std::runtime_error("Resolved package content is missing: " + package.PackageId);
            result.push_back({.PackageId = package.PackageId,
                              .Version = package.Version,
                              .Root = root,
                              .Trust = package.Embedded ? ProjectPackageTrust::Embedded
                                       : package.SignatureKeyId.empty()
                                           ? ProjectPackageTrust::CatalogHashVerified
                                           : ProjectPackageTrust::MarketplaceSignatureVerified,
                              .ReadOnly = !package.Embedded});
        }
        return result;
    }

    ProjectPackageRecoveryResult ProjectPackageManager::RecoverInterruptedOperations()
    {
        std::scoped_lock operation(m_Impl->OperationMutex);
        ProjectPackageRecoveryResult result;
        const auto operations = m_Impl->Specification.ProjectRoot / "Library" / "PackageTransactions";
        if (!std::filesystem::is_directory(operations))
            return result;
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(operations, error), end; !error && iterator != end;
             iterator.increment(error))
        {
            if (!iterator->is_directory(error))
                continue;
            try
            {
                const auto journal = Json::parse(
                    Detail::ReadTextFile(iterator->path() / "journal.json", MaximumProjectPackageDocumentBytes));
                if (!journal.is_object() || journal.value("schemaVersion", 0U) != 1U)
                    throw std::runtime_error("Package recovery journal has an unsupported schema.");
                if (journal.value("state", std::string{}) != "committed")
                    RestoreTransaction(m_Impl->Specification.ProjectRoot, iterator->path(), journal);
                std::filesystem::remove_all(iterator->path());
                ++result.RecoveredOperations;
            }
            catch (const std::exception& exception)
            {
                result.Diagnostics.push_back(iterator->path().filename().string() + ": " + exception.what());
            }
        }
        if (error)
            result.Diagnostics.push_back("Could not enumerate package recovery operations: " + error.message());
        return result;
    }

    std::filesystem::path ProjectPackageManager::ManifestPath(const std::filesystem::path& projectRoot)
    {
        return projectRoot / "Packages" / "manifest.keirepackages";
    }

    std::filesystem::path ProjectPackageManager::LockPath(const std::filesystem::path& projectRoot)
    {
        return projectRoot / "Packages" / "packages-lock.keirejson";
    }
} // namespace Keire
