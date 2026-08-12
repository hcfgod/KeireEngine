#include "Keire/Project/ProjectAssetPackageImporter.h"

#include "Keire/Project/ProjectPackageManager.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::string_view PackageMinimumEngineVersion = "0.3.1";
        constexpr std::size_t MaximumReceiptBytes = 16U * 1024U * 1024U;

        [[nodiscard]] std::string PathText(const std::filesystem::path& path)
        {
            return Detail::PathToUtf8(path.lexically_normal());
        }

        [[nodiscard]] std::string HashFile(const std::filesystem::path& path)
        {
            return Detail::DigestToString(Detail::Sha256File(path, AssetPackageArchiveLimits::MaximumFileBytes));
        }

        [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
        {
            const auto size = std::filesystem::file_size(path);
            if (size > AssetPackageArchiveLimits::MaximumFileBytes || size > SIZE_MAX)
                throw std::runtime_error("Asset package import file exceeds the supported size.");
            std::vector<std::byte> result(static_cast<std::size_t>(size));
            std::ifstream stream(path, std::ios::binary);
            if (!stream || (!result.empty() &&
                            !stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size))))
            {
                throw std::runtime_error("Could not read asset package import file: " + PathText(path));
            }
            return result;
        }

        [[nodiscard]] std::filesystem::path Confined(const std::filesystem::path& root,
                                                     const std::filesystem::path& relative)
        {
            if (relative.empty() || relative.is_absolute() || relative.has_root_name())
                throw std::invalid_argument("Asset package import paths must be project relative.");
            const auto normalizedRoot = std::filesystem::absolute(root).lexically_normal();
            const auto result = (normalizedRoot / relative).lexically_normal();
            const auto remainder = result.lexically_relative(normalizedRoot);
            if (remainder.empty() || remainder == "." || remainder.is_absolute() ||
                (!remainder.empty() && *remainder.begin() == ".."))
            {
                throw std::invalid_argument("Asset package import path escapes the project root.");
            }
            return result;
        }

        [[nodiscard]] std::filesystem::path ProjectAssetPath(const std::filesystem::path& packagePath)
        {
            auto normalized = packagePath.lexically_normal();
            if (!normalized.empty() && *normalized.begin() == "Assets")
                return normalized;
            return std::filesystem::path("Assets") / normalized;
        }

        [[nodiscard]] bool IsPackageId(const std::string_view value)
        {
            return !value.empty() && value.size() <= 128U && value.find('.') != value.npos && !value.starts_with('.') &&
                   !value.ends_with('.') && value.find("..") == value.npos &&
                   std::ranges::all_of(value,
                                       [](const unsigned char character)
                                       {
                                           return std::islower(character) || std::isdigit(character) ||
                                                  character == '.' || character == '-';
                                       });
        }

        [[nodiscard]] ProjectAssetImportReceipt NormalizeReceipt(ProjectAssetImportReceipt receipt)
        {
            if (receipt.SchemaVersion != ProjectAssetImportReceipt::CurrentSchemaVersion ||
                !IsPackageId(receipt.PackageId) || receipt.Version.empty() || receipt.ArchiveSha256.size() != 64U)
            {
                throw std::invalid_argument("Project asset import receipt is invalid.");
            }
            if ((receipt.ExecutableCodeApproved && receipt.ExecutableCodeFingerprint.size() != 64U) ||
                (!receipt.ExecutableCodeApproved && !receipt.ExecutableCodeFingerprint.empty()))
            {
                throw std::invalid_argument("Project asset import executable-code receipt is invalid.");
            }
            std::ranges::sort(receipt.Entries, {}, [](const auto& entry) { return PathText(entry.ProjectPath); });
            std::string previous;
            for (const auto& entry : receipt.Entries)
            {
                const auto path = PathText(entry.ProjectPath);
                if (!entry.ProjectPath.is_relative() || !path.starts_with("Assets/") || path == previous ||
                    entry.PackageSha256.size() != 64U)
                {
                    throw std::invalid_argument("Project asset import receipt entry is invalid.");
                }
                previous = path;
            }
            return receipt;
        }

        [[nodiscard]] Json ReceiptJson(const ProjectAssetImportReceipt& receipt)
        {
            Json entries = Json::array();
            for (const auto& entry : receipt.Entries)
            {
                entries.push_back({{"path", PathText(entry.ProjectPath)},
                                   {"packageSha256", entry.PackageSha256},
                                   {"owned", entry.Owned}});
            }
            return {{"schemaVersion", receipt.SchemaVersion},
                    {"packageId", receipt.PackageId},
                    {"version", receipt.Version},
                    {"archiveSha256", receipt.ArchiveSha256},
                    {"executableCodeFingerprint", receipt.ExecutableCodeFingerprint.empty()
                                                      ? Json(nullptr)
                                                      : Json(receipt.ExecutableCodeFingerprint)},
                    {"executableCodeApproved", receipt.ExecutableCodeApproved},
                    {"entries", std::move(entries)}};
        }

        [[nodiscard]] std::string ExecutableCodeFingerprint(const AssetPackageManifest& manifest)
        {
            if (manifest.ManagedAssemblies.empty())
                return {};
            std::string canonical;
            for (const auto& assembly : manifest.ManagedAssemblies)
            {
                canonical += assembly.Name + '\n' + PathText(assembly.DefinitionPath) + '\n' +
                             std::to_string(static_cast<unsigned int>(assembly.Scope)) + '\n';
            }
            for (const auto& file : manifest.Files)
            {
                if (file.Path.extension() == ".cs" ||
                    std::ranges::any_of(manifest.ManagedAssemblies,
                                        [&](const auto& assembly) { return assembly.DefinitionPath == file.Path; }))
                {
                    canonical += PathText(file.Path) + '\n' + file.Sha256 + '\n';
                }
            }
            return Detail::DigestToString(Detail::Sha256(std::as_bytes(std::span(canonical.data(), canonical.size()))));
        }

        [[nodiscard]] ProjectAssetImportResolution ResolutionFor(const ProjectAssetImportRequest& request,
                                                                 const std::filesystem::path& path)
        {
            const auto text = PathText(path);
            const auto found = std::ranges::find_if(request.Decisions, [&](const auto& decision)
                                                    { return PathText(decision.Path) == text; });
            return found == request.Decisions.end() ? ProjectAssetImportResolution::Unresolved : found->Resolution;
        }

        [[nodiscard]] bool Compatible(const AssetPackageManifest& manifest,
                                      const ProjectAssetPackageImporterSpecification& specification,
                                      std::string& reason)
        {
            const auto& compatibility = manifest.Compatibility;
            auto versionRange = ">=" + compatibility.MinimumEngineVersion;
            if (compatibility.MaximumEngineVersion)
                versionRange += " <=" + *compatibility.MaximumEngineVersion;
            if (!AssetPackageVersionSatisfies(specification.EngineVersion, versionRange))
            {
                reason = "The package does not support Kéire " + specification.EngineVersion + ".";
                return false;
            }
            const auto contains = [](const auto& values, const std::string& value)
            { return values.empty() || std::ranges::find(values, value) != values.end(); };
            if (!contains(compatibility.Platforms, specification.Platform) ||
                !contains(compatibility.Architectures, specification.Architecture))
            {
                reason = "The package does not support this platform or architecture.";
                return false;
            }
            for (const auto& renderer : compatibility.RendererCapabilities)
            {
                if (std::ranges::find(specification.RendererCapabilities, renderer) ==
                    specification.RendererCapabilities.end())
                {
                    reason = "The project renderer does not provide required capability '" + renderer + "'.";
                    return false;
                }
            }
            return true;
        }

        void RaiseMinimumEngineVersion(const std::filesystem::path& projectRoot)
        {
            const auto descriptor = projectRoot / "ProjectSettings" / "Project.keireproject";
            auto document = Json::parse(Detail::ReadTextFile(descriptor, MaximumReceiptBytes));
            const auto minimum = document.at("minimumEngineVersion").get<std::string>();
            if (!AssetPackageVersionSatisfies(minimum, ">=" + std::string(PackageMinimumEngineVersion)))
            {
                document["minimumEngineVersion"] = PackageMinimumEngineVersion;
                Detail::WriteTextFileAtomically(descriptor, document.dump(2) + '\n');
            }
        }

        void RemoveNoThrow(const std::filesystem::path& path) noexcept
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }

        [[nodiscard]] std::string NewOperationId() { return AssetId::Generate().ToString(); }

        [[nodiscard]] std::string MetadataAssetId(const std::filesystem::path& path)
        {
            try
            {
                const auto document = Json::parse(Detail::ReadTextFile(path, 2U * 1024U * 1024U));
                return document.value("id", std::string{});
            }
            catch (const std::exception&)
            {
                return {};
            }
        }
    } // namespace

    class ProjectAssetPackageImporter::Impl final
    {
      public:
        explicit Impl(ProjectAssetPackageImporterSpecification specification) : Specification(std::move(specification))
        {
            if (Specification.ProjectRoot.empty() || Specification.EngineVersion.empty() ||
                Specification.Platform.empty() || Specification.Architecture.empty())
            {
                throw std::invalid_argument("Project asset package importer specification is incomplete.");
            }
            Specification.ProjectRoot = std::filesystem::absolute(Specification.ProjectRoot).lexically_normal();
        }

        void Report(const ProjectAssetImportState state, const std::string_view operationId,
                    const std::string_view packageId, const std::filesystem::path& path = {},
                    const std::uint64_t completed = 0, const std::uint64_t total = 0,
                    const std::string_view message = {}) const
        {
            if (Specification.Events)
            {
                Specification.Events({state, std::string(operationId), std::string(packageId), path, completed, total,
                                      std::string(message)});
            }
        }

        [[nodiscard]] std::filesystem::path TransactionsRoot() const
        {
            return Specification.ProjectRoot / "Library" / "PackageImports";
        }

        [[nodiscard]] std::optional<ProjectAssetImportReceipt> LoadReceipt(const std::string_view packageId) const
        {
            const auto path = ProjectAssetPackageImporter::ReceiptPath(Specification.ProjectRoot, packageId);
            if (!std::filesystem::is_regular_file(path))
                return std::nullopt;
            return DecodeProjectAssetImportReceipt(Detail::ReadTextFile(path, MaximumReceiptBytes));
        }

        ProjectAssetPackageImporterSpecification Specification;
        mutable std::mutex Mutex;
    };

    std::string EncodeProjectAssetImportReceipt(const ProjectAssetImportReceipt& receipt)
    {
        return ReceiptJson(NormalizeReceipt(receipt)).dump(2) + '\n';
    }

    ProjectAssetImportReceipt DecodeProjectAssetImportReceipt(const std::string_view document)
    {
        const auto json = Json::parse(document);
        if (!json.is_object() || json.size() != 7U || !json.contains("schemaVersion") || !json.contains("packageId") ||
            !json.contains("version") || !json.contains("archiveSha256") || !json.contains("entries") ||
            !json.contains("executableCodeFingerprint") || !json.contains("executableCodeApproved") ||
            !json.at("entries").is_array())
        {
            throw std::invalid_argument("Project asset import receipt document is invalid.");
        }
        ProjectAssetImportReceipt result{.SchemaVersion = json.at("schemaVersion").get<std::uint32_t>(),
                                         .PackageId = json.at("packageId").get<std::string>(),
                                         .Version = json.at("version").get<std::string>(),
                                         .ArchiveSha256 = json.at("archiveSha256").get<std::string>(),
                                         .ExecutableCodeApproved = json.at("executableCodeApproved").get<bool>()};
        if (!json.at("executableCodeFingerprint").is_null())
            result.ExecutableCodeFingerprint = json.at("executableCodeFingerprint").get<std::string>();
        for (const auto& entry : json.at("entries"))
        {
            if (!entry.is_object() || entry.size() != 3U)
                throw std::invalid_argument("Project asset import receipt entry is invalid.");
            result.Entries.push_back({.ProjectPath = Detail::PathFromUtf8(entry.at("path").get<std::string>()),
                                      .PackageSha256 = entry.at("packageSha256").get<std::string>(),
                                      .Owned = entry.at("owned").get<bool>()});
        }
        return NormalizeReceipt(std::move(result));
    }

    ProjectAssetPackageImporter::ProjectAssetPackageImporter(ProjectAssetPackageImporterSpecification specification)
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
    }

    ProjectAssetPackageImporter::~ProjectAssetPackageImporter() = default;

    std::filesystem::path ProjectAssetPackageImporter::ReceiptPath(const std::filesystem::path& projectRoot,
                                                                   const std::string_view packageId)
    {
        if (!IsPackageId(packageId))
            throw std::invalid_argument("Asset package ID is invalid.");
        return projectRoot / "ProjectSettings" / "Packages" / "Imported" /
               Detail::PathFromUtf8(std::string(packageId) + ".keireimport");
    }

    std::optional<ProjectAssetImportReceipt>
    ProjectAssetPackageImporter::Receipt(const std::string_view packageId) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->LoadReceipt(packageId);
    }

    ProjectAssetImportPlan ProjectAssetPackageImporter::Preflight(const ProjectAssetImportRequest& request) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Report(ProjectAssetImportState::Preflight, {}, {}, request.Archive);
        AssetPackageVerification verification{.RequireSignature = request.RequireMarketplaceSignature,
                                              .ExpectedArchiveSizeBytes = request.ExpectedArchiveSizeBytes,
                                              .ExpectedArchiveSha256 = request.ExpectedArchiveSha256,
                                              .VerifySignature = m_Impl->Specification.VerifyMarketplaceSignature};
        ProjectAssetImportPlan result;
        result.Package = InspectAssetPackageArchive(request.Archive, verification);
        const auto& manifest = result.Package.Manifest;
        if (manifest.InstallKind != AssetPackageInstallKind::AssetImport)
            throw std::invalid_argument("Only AssetImport packages can be imported into project Assets.");

        std::string incompatibility;
        if (!Compatible(manifest, m_Impl->Specification, incompatibility))
        {
            result.Conflicts.push_back({ProjectAssetImportConflictKind::Compatibility, {}, std::move(incompatibility)});
        }
        result.ContainsExecutableCode = !manifest.ManagedAssemblies.empty();
        const auto previous = m_Impl->LoadReceipt(manifest.PackageId);
        const auto executableFingerprint = ExecutableCodeFingerprint(manifest);
        const bool previouslyApproved = previous && previous->ExecutableCodeApproved &&
                                        previous->ExecutableCodeFingerprint == executableFingerprint;
        if (result.ContainsExecutableCode && !request.AllowExecutableCode && !previouslyApproved)
        {
            result.Conflicts.push_back({ProjectAssetImportConflictKind::ExecutableCodeConsent,
                                        {},
                                        "This package contains C# assemblies and requires explicit code consent."});
        }

        std::unordered_map<std::string, const AssetPackageAsset*> assets;
        for (const auto& asset : manifest.Assets)
            assets.emplace(asset.Id.ToString(), &asset);
        std::set<std::string> selected;
        std::set<std::string> requested;
        for (const auto& id : request.SelectedAssets)
            requested.insert(id.ToString());
        std::function<void(const std::string&)> include = [&](const std::string& id)
        {
            if (!selected.insert(id).second)
                return;
            const auto found = assets.find(id);
            if (found == assets.end())
            {
                result.Conflicts.push_back({ProjectAssetImportConflictKind::MissingAssetDependency,
                                            {},
                                            "Selected asset dependency '" + id + "' is not in the package."});
                return;
            }
            for (const auto& dependency : found->second->Dependencies)
                include(dependency.ToString());
        };
        if (request.SelectedAssets.empty())
        {
            for (const auto& asset : manifest.Assets)
                include(asset.Id.ToString());
        }
        else
        {
            for (const auto& id : requested)
                include(id);
        }
        for (const auto& id : selected)
            result.ResolvedAssets.push_back(AssetId::Parse(id));

        std::set<std::string> includedFiles;
        if (manifest.Assets.empty() || request.SelectedAssets.empty())
        {
            for (const auto& file : manifest.Files)
                includedFiles.insert(PathText(file.Path));
        }
        else
        {
            for (const auto& id : selected)
            {
                const auto found = assets.find(id);
                if (found == assets.end())
                    continue;
                includedFiles.insert(PathText(found->second->SourcePath));
                includedFiles.insert(PathText(found->second->MetadataPath));
            }
        }
        if (!manifest.ManagedAssemblies.empty())
        {
            for (const auto& assembly : manifest.ManagedAssemblies)
                includedFiles.insert(PathText(assembly.DefinitionPath));
            for (const auto& file : manifest.Files)
                if (file.Path.extension() == ".cs")
                    includedFiles.insert(PathText(file.Path));
        }

        std::unordered_map<std::string, ProjectAssetImportReceiptEntry> previousEntries;
        if (previous)
            for (const auto& entry : previous->Entries)
                previousEntries.emplace(PathText(entry.ProjectPath), entry);

        for (const auto& file : manifest.Files)
        {
            if (!includedFiles.contains(PathText(file.Path)))
                continue;
            ProjectAssetImportPlanEntry entry{
                .PackagePath = file.Path, .ProjectPath = ProjectAssetPath(file.Path), .IncomingSha256 = file.Sha256};
            entry.RequiredDependency = std::ranges::any_of(selected, [&](const std::string& id)
                                                           { return !requested.empty() && !requested.contains(id); });
            const auto destination = Confined(m_Impl->Specification.ProjectRoot, entry.ProjectPath);
            const auto exists = std::filesystem::is_regular_file(destination);
            if (exists)
                entry.LocalSha256 = HashFile(destination);
            const auto prior = previousEntries.find(PathText(entry.ProjectPath));
            const auto resolution = ResolutionFor(request, entry.ProjectPath);
            if (!exists)
                entry.Disposition = ProjectAssetImportDisposition::Install;
            else if (entry.LocalSha256 == entry.IncomingSha256)
                entry.Disposition = ProjectAssetImportDisposition::ReuseIdentical;
            else if (resolution == ProjectAssetImportResolution::Replace)
                entry.Disposition = ProjectAssetImportDisposition::Replace;
            else if (resolution == ProjectAssetImportResolution::KeepLocal)
                entry.Disposition = ProjectAssetImportDisposition::KeepLocal;
            else if (prior != previousEntries.end() && entry.LocalSha256 == prior->second.PackageSha256)
                entry.Disposition = ProjectAssetImportDisposition::Replace;
            else if (prior != previousEntries.end() && entry.IncomingSha256 == prior->second.PackageSha256)
                entry.Disposition = ProjectAssetImportDisposition::KeepLocal;
            else
            {
                entry.Disposition = ProjectAssetImportDisposition::Conflict;
                result.Conflicts.push_back(
                    {prior == previousEntries.end() ? ProjectAssetImportConflictKind::Path
                                                    : ProjectAssetImportConflictKind::ModifiedLocalFile,
                     entry.ProjectPath,
                     prior == previousEntries.end()
                         ? "A different project file already exists at this import path."
                         : "The project file and incoming package file both changed; choose Replace or Keep Local."});
            }
            result.Entries.push_back(std::move(entry));
        }

        std::unordered_map<std::string, std::filesystem::path> destinationsByAsset;
        for (const auto& asset : manifest.Assets)
        {
            if (selected.contains(asset.Id.ToString()))
                destinationsByAsset.emplace(asset.Id.ToString(), ProjectAssetPath(asset.MetadataPath));
        }
        const auto assetsRoot = m_Impl->Specification.ProjectRoot / "Assets";
        std::error_code error;
        if (!destinationsByAsset.empty() && std::filesystem::is_directory(assetsRoot))
        {
            for (std::filesystem::recursive_directory_iterator
                     iterator(assetsRoot, std::filesystem::directory_options::skip_permission_denied, error),
                 end;
                 !error && iterator != end; iterator.increment(error))
            {
                if (!iterator->is_regular_file(error) || iterator->path().extension() != ".keiremeta")
                    continue;
                const auto id = MetadataAssetId(iterator->path());
                const auto incoming = destinationsByAsset.find(id);
                if (incoming == destinationsByAsset.end())
                    continue;
                const auto relative = iterator->path().lexically_relative(m_Impl->Specification.ProjectRoot);
                if (relative != incoming->second)
                {
                    result.Conflicts.push_back({ProjectAssetImportConflictKind::AssetIdentity, incoming->second,
                                                "AssetId '" + id + "' already exists at " + PathText(relative) + "."});
                }
            }
        }
        return result;
    }

    ProjectAssetImportResult ProjectAssetPackageImporter::Import(const ProjectAssetImportRequest& request)
    {
        auto plan = Preflight(request);
        if (!plan.Valid())
            throw std::runtime_error("Asset package import preflight has unresolved conflicts.");

        std::scoped_lock lock(m_Impl->Mutex);
        const auto operationId = NewOperationId();
        const auto& manifest = plan.Package.Manifest;
        const auto transaction = m_Impl->TransactionsRoot() / Detail::PathFromUtf8(operationId);
        const auto extraction = transaction / "payload";
        const auto journalPath = transaction / "journal.keirejson";
        Json journal{
            {"schemaVersion", 1}, {"state", "staging"}, {"packageId", manifest.PackageId}, {"entries", Json::array()}};
        bool publicationStarted = false;
        ProjectAssetImportResult result;
        result.Receipt = {.PackageId = manifest.PackageId,
                          .Version = manifest.Version,
                          .ArchiveSha256 = plan.Package.ArchiveSha256,
                          .ExecutableCodeFingerprint = ExecutableCodeFingerprint(manifest),
                          .ExecutableCodeApproved = !manifest.ManagedAssemblies.empty()};
        const auto previous = m_Impl->LoadReceipt(manifest.PackageId);
        std::unordered_map<std::string, ProjectAssetImportReceiptEntry> priorEntries;
        if (previous)
            for (const auto& entry : previous->Entries)
                priorEntries.emplace(PathText(entry.ProjectPath), entry);

        try
        {
            std::filesystem::create_directories(transaction);
            m_Impl->Report(ProjectAssetImportState::Extracting, operationId, manifest.PackageId, request.Archive);
            static_cast<void>(ExtractAssetPackageToStaging(
                {.Archive = request.Archive,
                 .AllowedStagingParent = transaction,
                 .StagingRoot = extraction,
                 .Verification = {.RequireSignature = request.RequireMarketplaceSignature,
                                  .ExpectedArchiveSizeBytes = request.ExpectedArchiveSizeBytes,
                                  .ExpectedArchiveSha256 = request.ExpectedArchiveSha256,
                                  .VerifySignature = m_Impl->Specification.VerifyMarketplaceSignature}}));

            for (std::size_t index = 0; index < plan.Entries.size(); ++index)
            {
                const auto& entry = plan.Entries[index];
                const auto destination = Confined(m_Impl->Specification.ProjectRoot, entry.ProjectPath);
                const bool existed = std::filesystem::is_regular_file(destination);
                if (existed && (entry.Disposition == ProjectAssetImportDisposition::Install ||
                                entry.Disposition == ProjectAssetImportDisposition::Replace))
                {
                    Detail::WriteFileAtomically(transaction / "before" / Detail::PathFromUtf8(std::to_string(index)),
                                                ReadBytes(destination));
                }
                journal["entries"].push_back(
                    {{"path", PathText(entry.ProjectPath)},
                     {"existed", existed},
                     {"mutated", entry.Disposition == ProjectAssetImportDisposition::Install ||
                                     entry.Disposition == ProjectAssetImportDisposition::Replace},
                     {"backup", existed ? std::to_string(index) : std::string{}}});
            }
            Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');
            journal["state"] = "publishing";
            Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');
            publicationStarted = true;

            for (std::size_t index = 0; index < plan.Entries.size(); ++index)
            {
                const auto& entry = plan.Entries[index];
                const auto destination = Confined(m_Impl->Specification.ProjectRoot, entry.ProjectPath);
                const auto prior = priorEntries.find(PathText(entry.ProjectPath));
                bool owned = prior != priorEntries.end() ? prior->second.Owned : true;
                if (entry.Disposition == ProjectAssetImportDisposition::Install ||
                    entry.Disposition == ProjectAssetImportDisposition::Replace)
                {
                    Detail::WriteFileAtomically(destination, ReadBytes(Confined(extraction, entry.PackagePath)));
                    result.Written.push_back(entry.ProjectPath);
                    owned = true;
                }
                else if (entry.Disposition == ProjectAssetImportDisposition::ReuseIdentical)
                {
                    result.Reused.push_back(entry.ProjectPath);
                    if (prior == priorEntries.end())
                        owned = false;
                }
                else
                {
                    result.Retained.push_back(entry.ProjectPath);
                    if (prior == priorEntries.end())
                        owned = false;
                }
                result.Receipt.Entries.push_back(
                    {.ProjectPath = entry.ProjectPath, .PackageSha256 = entry.IncomingSha256, .Owned = owned});
                m_Impl->Report(ProjectAssetImportState::Publishing, operationId, manifest.PackageId, entry.ProjectPath,
                               index + 1U, plan.Entries.size());
            }
            RaiseMinimumEngineVersion(m_Impl->Specification.ProjectRoot);
            Detail::WriteTextFileAtomically(ReceiptPath(m_Impl->Specification.ProjectRoot, manifest.PackageId),
                                            EncodeProjectAssetImportReceipt(result.Receipt));
            journal["state"] = "committed";
            Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');
            RemoveNoThrow(transaction);
            m_Impl->Report(ProjectAssetImportState::Completed, operationId, manifest.PackageId, {}, plan.Entries.size(),
                           plan.Entries.size(), "Asset package import completed.");
            return result;
        }
        catch (...)
        {
            if (publicationStarted)
            {
                m_Impl->Report(ProjectAssetImportState::RollingBack, operationId, manifest.PackageId);
                for (std::size_t index = plan.Entries.size(); index > 0U; --index)
                {
                    const auto& entry = plan.Entries[index - 1U];
                    if (entry.Disposition != ProjectAssetImportDisposition::Install &&
                        entry.Disposition != ProjectAssetImportDisposition::Replace)
                        continue;
                    const auto destination = Confined(m_Impl->Specification.ProjectRoot, entry.ProjectPath);
                    const auto backup = transaction / "before" / Detail::PathFromUtf8(std::to_string(index - 1U));
                    if (std::filesystem::is_regular_file(backup))
                        Detail::WriteFileAtomically(destination, ReadBytes(backup));
                    else
                        RemoveNoThrow(destination);
                }
            }
            RemoveNoThrow(transaction);
            m_Impl->Report(ProjectAssetImportState::Failed, operationId, manifest.PackageId);
            throw;
        }
    }

    ProjectAssetRemovalResult ProjectAssetPackageImporter::Remove(const std::string_view packageId)
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto receipt = m_Impl->LoadReceipt(packageId);
        if (!receipt)
            throw std::invalid_argument("The asset package is not imported into this project.");
        const auto operationId = NewOperationId();
        const auto transaction = m_Impl->TransactionsRoot() / Detail::PathFromUtf8(operationId);
        const auto journalPath = transaction / "journal.keirejson";
        const auto receiptPath = ReceiptPath(m_Impl->Specification.ProjectRoot, packageId);
        Json journal{{"schemaVersion", 1}, {"state", "staging"}, {"packageId", packageId}, {"entries", Json::array()}};
        ProjectAssetRemovalResult result;
        std::vector<std::filesystem::path> removals;
        for (const auto& entry : receipt->Entries)
        {
            const auto destination = Confined(m_Impl->Specification.ProjectRoot, entry.ProjectPath);
            if (!std::filesystem::is_regular_file(destination))
                continue;
            if (entry.Owned && HashFile(destination) == entry.PackageSha256)
            {
                removals.push_back(entry.ProjectPath);
                result.Removed.push_back(entry.ProjectPath);
            }
            else
                result.RetainedModified.push_back(entry.ProjectPath);
        }
        try
        {
            std::filesystem::create_directories(transaction / "before");
            for (std::size_t index = 0; index < removals.size(); ++index)
            {
                const auto destination = Confined(m_Impl->Specification.ProjectRoot, removals[index]);
                Detail::WriteFileAtomically(transaction / "before" / Detail::PathFromUtf8(std::to_string(index)),
                                            ReadBytes(destination));
                journal["entries"].push_back({{"path", PathText(removals[index])},
                                              {"existed", true},
                                              {"mutated", true},
                                              {"backup", std::to_string(index)}});
            }
            const auto receiptIndex = removals.size();
            Detail::WriteFileAtomically(transaction / "before" / Detail::PathFromUtf8(std::to_string(receiptIndex)),
                                        ReadBytes(receiptPath));
            journal["entries"].push_back(
                {{"path", PathText(receiptPath.lexically_relative(m_Impl->Specification.ProjectRoot))},
                 {"existed", true},
                 {"mutated", true},
                 {"backup", std::to_string(receiptIndex)}});
            Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');
            journal["state"] = "publishing";
            Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');
            for (const auto& relative : removals)
                RemoveNoThrow(Confined(m_Impl->Specification.ProjectRoot, relative));
            RemoveNoThrow(receiptPath);
            journal["state"] = "committed";
            Detail::WriteTextFileAtomically(journalPath, journal.dump(2) + '\n');
            RemoveNoThrow(transaction);
            return result;
        }
        catch (...)
        {
            for (std::size_t index = 0; index < removals.size(); ++index)
            {
                const auto backup = transaction / "before" / Detail::PathFromUtf8(std::to_string(index));
                if (std::filesystem::is_regular_file(backup))
                    Detail::WriteFileAtomically(Confined(m_Impl->Specification.ProjectRoot, removals[index]),
                                                ReadBytes(backup));
            }
            const auto receiptBackup = transaction / "before" / Detail::PathFromUtf8(std::to_string(removals.size()));
            if (std::filesystem::is_regular_file(receiptBackup))
                Detail::WriteFileAtomically(receiptPath, ReadBytes(receiptBackup));
            RemoveNoThrow(transaction);
            throw;
        }
    }

    ProjectAssetImportRecoveryResult ProjectAssetPackageImporter::RecoverInterruptedOperations()
    {
        std::scoped_lock lock(m_Impl->Mutex);
        ProjectAssetImportRecoveryResult result;
        const auto root = m_Impl->TransactionsRoot();
        std::error_code error;
        if (!std::filesystem::is_directory(root, error))
            return result;
        for (std::filesystem::directory_iterator iterator(root, error), end; !error && iterator != end;
             iterator.increment(error))
        {
            if (!iterator->is_directory(error))
                continue;
            const auto journalPath = iterator->path() / "journal.keirejson";
            try
            {
                const auto journal = Json::parse(Detail::ReadTextFile(journalPath, MaximumReceiptBytes));
                const auto state = journal.at("state").get<std::string>();
                if (state == "publishing")
                {
                    const auto& entries = journal.at("entries");
                    for (std::size_t index = entries.size(); index > 0U; --index)
                    {
                        const auto& entry = entries.at(index - 1U);
                        if (!entry.at("mutated").get<bool>())
                            continue;
                        const auto destination = Confined(m_Impl->Specification.ProjectRoot,
                                                          Detail::PathFromUtf8(entry.at("path").get<std::string>()));
                        if (entry.at("existed").get<bool>())
                        {
                            const auto backup = iterator->path() / "before" /
                                                Detail::PathFromUtf8(entry.at("backup").get<std::string>());
                            Detail::WriteFileAtomically(destination, ReadBytes(backup));
                        }
                        else
                            RemoveNoThrow(destination);
                    }
                    ++result.RecoveredOperations;
                }
                RemoveNoThrow(iterator->path());
            }
            catch (const std::exception& exception)
            {
                result.Diagnostics.push_back(PathText(iterator->path()) + ": " + exception.what());
            }
        }
        return result;
    }
} // namespace Keire
