#include "KeireHubRuntime/EditorInstallationRegistry.h"

#include "KeireHubRuntime/PackageResolver.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <set>
#include <span>
#include <string_view>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumRegistryBytes = std::size_t{64} * 1024 * 1024;
        constexpr std::size_t MaximumMarkerBytes = std::size_t{16} * 1024;
        constexpr std::size_t MaximumInstallations = 128;
        constexpr std::size_t MaximumEntrypoints = 16;

        [[nodiscard]] std::string_view ToString(const InstallationOwnership value) noexcept
        {
            return value == InstallationOwnership::Managed ? "managed" : "external";
        }

        [[nodiscard]] std::string_view ToString(const InstallationHealth value) noexcept
        {
            constexpr std::array names{"unknown", "healthy", "verificationRequired", "damaged", "missing"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] std::optional<InstallationHealth> ParseHealth(const std::string_view value) noexcept
        {
            constexpr std::array values{InstallationHealth::Unknown, InstallationHealth::Healthy,
                                        InstallationHealth::VerificationRequired, InstallationHealth::Damaged,
                                        InstallationHealth::Missing};
            for (const auto candidate : values)
            {
                if (ToString(candidate) == value)
                    return candidate;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string_view ToString(const PackageKind value) noexcept
        {
            constexpr std::array names{"hubInstaller", "editor",          "buildSupport",
                                       "template",     "learningContent", "toolchain"};
            const auto index = static_cast<std::size_t>(value);
            return index < names.size() ? names[index] : std::string_view{};
        }

        [[nodiscard]] std::optional<PackageKind> ParsePackageKind(const std::string_view value) noexcept
        {
            constexpr std::array values{PackageKind::HubInstaller,    PackageKind::Editor,
                                        PackageKind::BuildSupport,    PackageKind::Template,
                                        PackageKind::LearningContent, PackageKind::Toolchain};
            const auto found = std::ranges::find(values, value, [](const PackageKind kind) { return ToString(kind); });
            return found == values.end() ? std::nullopt : std::optional(*found);
        }

        [[nodiscard]] Detail::Json SerializePackages(const std::vector<InstalledPackageRecord>& packages)
        {
            Detail::Json result = Detail::Json::array();
            for (const auto& package : packages)
            {
                Detail::Json dependencies = Detail::Json::array();
                for (const auto& dependency : package.Dependencies)
                {
                    dependencies.push_back(
                        {{"packageId", dependency.PackageId}, {"versions", dependency.Versions.ToString()}});
                }
                Detail::Json files = Detail::Json::array();
                for (const auto& file : package.Files)
                {
                    files.push_back({{"path", Detail::PathToUtf8(file.Path)},
                                     {"sizeBytes", file.SizeBytes},
                                     {"sha256", file.Sha256},
                                     {"mode", file.Mode}});
                }
                result.push_back({{"id", package.Id},
                                  {"version", package.Version.ToString()},
                                  {"kind", ToString(package.Kind)},
                                  {"artifactSizeBytes", package.ArtifactSizeBytes},
                                  {"artifactSha256", package.ArtifactSha256},
                                  {"installedSizeBytes", package.InstalledSizeBytes},
                                  {"dependencies", std::move(dependencies)},
                                  {"files", std::move(files)},
                                  {"licenses", package.LicenseReferences}});
            }
            return result;
        }

        [[nodiscard]] std::vector<InstalledPackageRecord> ParsePackages(const Detail::Json& values)
        {
            if (!values.is_array() || values.size() > 256U)
                throw std::invalid_argument("Invalid installed package collection.");
            std::vector<InstalledPackageRecord> result;
            result.reserve(values.size());
            for (const auto& value : values)
            {
                auto version = SemanticVersion::Parse(value.at("version").get<std::string>());
                const auto kind = ParsePackageKind(value.at("kind").get<std::string>());
                if (!version || !kind)
                    throw std::invalid_argument("Invalid installed package identity.");
                InstalledPackageRecord package{
                    .Id = value.at("id").get<std::string>(),
                    .Version = std::move(version).Value(),
                    .Kind = *kind,
                    .ArtifactSizeBytes = value.at("artifactSizeBytes").get<std::uint64_t>(),
                    .ArtifactSha256 = value.at("artifactSha256").get<std::string>(),
                    .InstalledSizeBytes = value.at("installedSizeBytes").get<std::uint64_t>(),
                    .LicenseReferences = value.at("licenses").get<std::vector<std::string>>()};
                const auto& dependencies = value.at("dependencies");
                if (!dependencies.is_array() || dependencies.size() > 128U)
                    throw std::invalid_argument("Invalid installed package dependency collection.");
                for (const auto& dependency : dependencies)
                {
                    auto versions = VersionConstraint::Parse(dependency.at("versions").get<std::string>());
                    if (!versions)
                        throw std::invalid_argument("Invalid installed package dependency version.");
                    package.Dependencies.push_back({.PackageId = dependency.at("packageId").get<std::string>(),
                                                    .Versions = std::move(versions).Value()});
                }
                const auto& files = value.at("files");
                if (!files.is_array() || files.size() > 32768U)
                    throw std::invalid_argument("Invalid installed package file collection.");
                for (const auto& file : files)
                {
                    package.Files.push_back({.Path = Detail::PathFromUtf8(file.at("path").get<std::string>()),
                                             .SizeBytes = file.at("sizeBytes").get<std::uint64_t>(),
                                             .Sha256 = file.at("sha256").get<std::string>(),
                                             .Mode = file.at("mode").get<std::uint32_t>()});
                }
                result.push_back(std::move(package));
            }
            return result;
        }

        [[nodiscard]] std::string NormalizedPathKey(const std::filesystem::path& path)
        {
            auto key = Detail::PathToUtf8(path.lexically_normal());
#if defined(_WIN32)
            std::ranges::transform(key, key.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
            return key;
        }

        [[nodiscard]] std::string LowerFilename(const std::filesystem::path& path)
        {
            auto value = Detail::PathToUtf8(path.filename());
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        void NormalizeEntrypointRoles(EditorInstallation& installation)
        {
            if (installation.EditorEntrypoint.empty())
            {
                const auto found = std::ranges::find_if(installation.Entrypoints,
                                                        [](const auto& path)
                                                        {
                                                            const auto filename = LowerFilename(path);
                                                            return filename.find("keireclient") != std::string::npos ||
                                                                   filename == "editor" || filename == "editor.exe";
                                                        });
                installation.EditorEntrypoint =
                    found == installation.Entrypoints.end()
                        ? (installation.Entrypoints.empty() ? std::filesystem::path{}
                                                            : installation.Entrypoints.front())
                        : *found;
            }
            if (installation.AssetToolEntrypoint.empty())
            {
                const auto found =
                    std::ranges::find_if(installation.Entrypoints, [](const auto& path)
                                         { return LowerFilename(path).find("assettool") != std::string::npos; });
                if (found != installation.Entrypoints.end())
                    installation.AssetToolEntrypoint = *found;
            }
        }

        [[nodiscard]] bool IsPlatform(const std::string_view value) noexcept
        {
            return value == "windows" || value == "linux" || value == "macos";
        }

        [[nodiscard]] bool IsArchitecture(const std::string_view value) noexcept
        {
            return value == "x86_64" || value == "arm64";
        }

        [[nodiscard]] HubStatus ValidateMarker(const ManagedInstallMarker& marker)
        {
            if (!Detail::IsBoundedIdentifier(marker.InstallationId) || !Detail::IsSha256(marker.ManifestFingerprint) ||
                (!marker.ReceiptSha256.empty() && !Detail::IsSha256(marker.ReceiptSha256)) ||
                marker.Nonce.size() < 32 || marker.Nonce.size() > 256 ||
                !std::ranges::all_of(marker.Nonce, [](const unsigned char value) { return std::isxdigit(value) != 0; }))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The managed-install marker is invalid.",
                                           .AffectedItem = marker.InstallationId});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus Validate(const EditorInstallation& installation)
        {
            if (installation.Ownership < InstallationOwnership::Managed ||
                installation.Ownership > InstallationOwnership::External ||
                installation.Health < InstallationHealth::Unknown ||
                installation.Health > InstallationHealth::Missing || !Detail::IsBoundedIdentifier(installation.Id) ||
                !SemanticVersion::Parse(installation.Version) ||
                !Detail::IsBoundedIdentifier(installation.Channel, 64) || !IsPlatform(installation.Platform) ||
                !IsArchitecture(installation.Architecture) || Detail::PathToUtf8(installation.Root).empty() ||
                Detail::PathToUtf8(installation.Root).size() > 4096 ||
                !Detail::IsSha256(installation.ManifestFingerprint) || installation.Entrypoints.empty() ||
                installation.Entrypoints.size() > MaximumEntrypoints || installation.BundledDotnetSdk.size() > 64 ||
                installation.MinimumProjectSchema == 0 ||
                installation.MaximumProjectSchema < installation.MinimumProjectSchema)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The editor installation metadata is invalid.",
                                           .AffectedItem = installation.Id});
            }
            for (const auto& entrypoint : installation.Entrypoints)
            {
                if (!Detail::IsSafeRelativePath(entrypoint) || Detail::PathToUtf8(entrypoint).size() > 1024)
                {
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                               .Message = "An editor entrypoint escapes its installation root.",
                                               .AffectedItem = installation.Id});
                }
            }
            const auto hasEntrypoint = [&](const std::filesystem::path& role)
            {
                return !role.empty() &&
                       std::ranges::any_of(installation.Entrypoints, [&](const auto& value)
                                           { return NormalizedPathKey(value) == NormalizedPathKey(role); });
            };
            if (!hasEntrypoint(installation.EditorEntrypoint) ||
                (!installation.AssetToolEntrypoint.empty() && !hasEntrypoint(installation.AssetToolEntrypoint)))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "An editor entrypoint role is invalid.",
                                           .AffectedItem = installation.Id});
            }
            if (installation.Ownership == InstallationOwnership::Managed)
            {
                ManagedInstallMarker marker{installation.Id, installation.ManifestFingerprint, installation.MarkerNonce,
                                            installation.PackageReceiptSha256};
                if (const auto status = ValidateMarker(marker); !status)
                    return status;
            }
            else if (!installation.MarkerNonce.empty())
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "External editor installations cannot own managed markers.",
                                           .AffectedItem = installation.Id});
            }
            const bool hasReceipt = !installation.PackageTreeIdentity.empty() ||
                                    !installation.PackageReceiptSha256.empty() ||
                                    !installation.InstalledPackages.empty();
            if (hasReceipt)
            {
                PackageInstallReceipt receipt{.DocumentSha256 = installation.PackageReceiptSha256,
                                              .AggregateIdentitySha256 = installation.PackageTreeIdentity,
                                              .AggregateInstalledSizeBytes = installation.InstalledSizeBytes,
                                              .Packages = installation.InstalledPackages};
                if (const auto status = ValidatePackageInstallReceipt(receipt); !status)
                    return status;
                auto encoded = EncodePackageInstallReceipt(receipt);
                if (!encoded ||
                    Detail::Sha256Hex(std::as_bytes(std::span(encoded.Value()))) != installation.PackageReceiptSha256)
                {
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                               .Message = "The editor registration receipt binding is invalid.",
                                               .AffectedItem = installation.Id});
                }
            }
            return HubStatus::Success();
        }

        [[nodiscard]] HubStatus DuplicateBatchItem(const EditorInstallation& installation, const std::string_view field)
        {
            return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                       .Message = "The editor import contains a duplicate " + std::string(field) + ".",
                                       .AffectedItem = installation.Id});
        }

        [[nodiscard]] Detail::Json Serialize(const std::vector<EditorInstallation>& installations)
        {
            Detail::Json values = Detail::Json::array();
            for (const auto& installation : installations)
            {
                Detail::Json entrypoints = Detail::Json::array();
                for (const auto& entrypoint : installation.Entrypoints)
                    entrypoints.push_back(Detail::PathToUtf8(entrypoint));
                values.push_back({{"id", installation.Id},
                                  {"version", installation.Version},
                                  {"channel", installation.Channel},
                                  {"platform", installation.Platform},
                                  {"architecture", installation.Architecture},
                                  {"root", Detail::PathToUtf8(installation.Root)},
                                  {"ownership", ToString(installation.Ownership)},
                                  {"manifestFingerprint", installation.ManifestFingerprint},
                                  {"packageTreeIdentity", installation.PackageTreeIdentity},
                                  {"packageReceiptSha256", installation.PackageReceiptSha256},
                                  {"markerNonce", installation.MarkerNonce},
                                  {"installedPackages", SerializePackages(installation.InstalledPackages)},
                                  {"entrypoints", std::move(entrypoints)},
                                  {"editorEntrypoint", Detail::PathToUtf8(ResolveEditorEntrypoint(installation))},
                                  {"assetToolEntrypoint", Detail::PathToUtf8(ResolveAssetToolEntrypoint(installation))},
                                  {"bundledDotnetSdk", installation.BundledDotnetSdk},
                                  {"minimumProjectSchema", installation.MinimumProjectSchema},
                                  {"maximumProjectSchema", installation.MaximumProjectSchema},
                                  {"installedSizeBytes", installation.InstalledSizeBytes},
                                  {"lastVerified", installation.LastVerifiedUnixSeconds},
                                  {"health", ToString(installation.Health)}});
            }
            return {{"schemaVersion", EditorInstallationRegistry::CurrentSchemaVersion},
                    {"installations", std::move(values)}};
        }

        [[nodiscard]] HubResult<std::vector<EditorInstallation>> Parse(const Detail::Json& document)
        {
            try
            {
                if (document.at("schemaVersion").get<std::uint32_t>() !=
                    EditorInstallationRegistry::CurrentSchemaVersion)
                {
                    return HubResult<std::vector<EditorInstallation>>::Failure(
                        {.Code = HubErrorCode::UnsupportedSchema,
                         .Message = "This editor registry uses an unsupported schema.",
                         .AffectedItem = "installations"});
                }
                const auto& values = document.at("installations");
                if (!values.is_array() || values.size() > MaximumInstallations)
                    throw std::invalid_argument("Invalid installation collection.");
                std::vector<EditorInstallation> result;
                result.reserve(values.size());
                for (const auto& value : values)
                {
                    EditorInstallation installation;
                    installation.Id = value.at("id").get<std::string>();
                    installation.Version = value.at("version").get<std::string>();
                    installation.Channel = value.at("channel").get<std::string>();
                    installation.Platform = value.at("platform").get<std::string>();
                    installation.Architecture = value.at("architecture").get<std::string>();
                    installation.Root = Detail::PathFromUtf8(value.at("root").get<std::string>());
                    const auto ownership = value.at("ownership").get<std::string>();
                    if (ownership == "managed")
                        installation.Ownership = InstallationOwnership::Managed;
                    else if (ownership == "external")
                        installation.Ownership = InstallationOwnership::External;
                    else
                        throw std::invalid_argument("Unknown installation ownership.");
                    installation.ManifestFingerprint = value.at("manifestFingerprint").get<std::string>();
                    installation.PackageTreeIdentity = value.value("packageTreeIdentity", std::string{});
                    installation.PackageReceiptSha256 = value.value("packageReceiptSha256", std::string{});
                    installation.MarkerNonce = value.value("markerNonce", std::string{});
                    installation.InstalledPackages =
                        ParsePackages(value.value("installedPackages", Detail::Json::array()));
                    const auto& entrypoints = value.at("entrypoints");
                    if (!entrypoints.is_array() || entrypoints.size() > MaximumEntrypoints)
                        throw std::invalid_argument("Invalid entrypoint collection.");
                    for (const auto& entrypoint : entrypoints)
                        installation.Entrypoints.push_back(Detail::PathFromUtf8(entrypoint.get<std::string>()));
                    installation.EditorEntrypoint =
                        Detail::PathFromUtf8(value.value("editorEntrypoint", std::string{}));
                    installation.AssetToolEntrypoint =
                        Detail::PathFromUtf8(value.value("assetToolEntrypoint", std::string{}));
                    NormalizeEntrypointRoles(installation);
                    installation.BundledDotnetSdk = value.value("bundledDotnetSdk", std::string{});
                    installation.MinimumProjectSchema = value.at("minimumProjectSchema").get<std::uint32_t>();
                    installation.MaximumProjectSchema = value.at("maximumProjectSchema").get<std::uint32_t>();
                    installation.InstalledSizeBytes = value.value("installedSizeBytes", 0ULL);
                    installation.LastVerifiedUnixSeconds = value.value("lastVerified", 0ULL);
                    const auto health = ParseHealth(value.value("health", "unknown"));
                    if (!health)
                        throw std::invalid_argument("Unknown installation health.");
                    installation.Health = *health;
                    if (const auto status = Validate(installation); !status)
                        throw std::invalid_argument(status.Error().Message);
                    if (std::ranges::find(result, installation.Id, &EditorInstallation::Id) != result.end())
                        throw std::invalid_argument("Duplicate installation identity.");
                    result.push_back(std::move(installation));
                }
                return HubResult<std::vector<EditorInstallation>>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<std::vector<EditorInstallation>>::Failure(
                    {.Code = HubErrorCode::InvalidData,
                     .Message = "The editor-installation registry is malformed.",
                     .AffectedItem = "installations",
                     .TechnicalDetails = error.what()});
            }
        }
    } // namespace

    std::filesystem::path ResolveEditorEntrypoint(const EditorInstallation& installation)
    {
        auto normalized = installation;
        NormalizeEntrypointRoles(normalized);
        return normalized.EditorEntrypoint;
    }

    std::filesystem::path ResolveAssetToolEntrypoint(const EditorInstallation& installation)
    {
        auto normalized = installation;
        NormalizeEntrypointRoles(normalized);
        return normalized.AssetToolEntrypoint;
    }

    EditorInstallationRegistry::EditorInstallationRegistry(std::filesystem::path registryPath)
        : m_Path(std::move(registryPath)), m_Snapshot(std::make_shared<const std::vector<EditorInstallation>>())
    {
    }

    HubStatus EditorInstallationRegistry::Load()
    {
        if (!std::filesystem::exists(m_Path))
        {
            m_Snapshot = std::make_shared<const std::vector<EditorInstallation>>();
            return HubStatus::Success();
        }
        auto document = Detail::ReadJsonFile(m_Path, MaximumRegistryBytes);
        if (!document)
        {
            if (document.Error().Code == HubErrorCode::InvalidData)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(document.Error());
        }
        auto installations = Parse(document.Value());
        if (!installations)
        {
            if (installations.Error().Code != HubErrorCode::UnsupportedSchema)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(installations.Error());
        }
        std::ranges::sort(installations.Value(), {}, &EditorInstallation::Id);
        m_Snapshot = std::make_shared<const std::vector<EditorInstallation>>(std::move(installations).Value());
        return HubStatus::Success();
    }

    HubStatus EditorInstallationRegistry::Upsert(EditorInstallation installation)
    {
        NormalizeEntrypointRoles(installation);
        if (const auto status = Validate(installation); !status)
            return status;
        auto installations = *m_Snapshot;
        const auto rootKey = NormalizedPathKey(installation.Root);
        const auto rootOwner = std::ranges::find_if(
            installations, [&](const auto& existing)
            { return existing.Id != installation.Id && NormalizedPathKey(existing.Root) == rootKey; });
        if (rootOwner != installations.end())
            return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                       .Message = "This editor location is already registered.",
                                       .AffectedItem = rootOwner->Id});
        const auto found = std::ranges::find(installations, installation.Id, &EditorInstallation::Id);
        if (found == installations.end())
            installations.push_back(std::move(installation));
        else
            *found = std::move(installation);
        return Commit(std::move(installations));
    }

    HubStatus EditorInstallationRegistry::UpsertMany(const std::span<const EditorInstallation> incoming)
    {
        auto prepared = PrepareUpsertMany(incoming);
        if (!prepared)
            return HubStatus::Failure(prepared.Error());
        if (incoming.empty())
            return HubStatus::Success();
        return Commit(std::move(prepared).Value());
    }

    HubStatus EditorInstallationRegistry::UpdateHealth(const std::span<const EditorInstallationHealthUpdate> updates)
    {
        if (updates.empty())
            return HubStatus::Success();
        if (updates.size() > MaximumInstallations)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The editor health update exceeds the registry safety limit.",
                                       .AffectedItem = "installations"});
        }

        auto installations = *m_Snapshot;
        std::set<std::string> updatedIds;
        bool changed = false;
        for (const auto& update : updates)
        {
            if (!updatedIds.insert(update.InstallationId).second)
            {
                return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                           .Message = "The editor health update contains a duplicate identity.",
                                           .AffectedItem = update.InstallationId});
            }
            switch (update.Health)
            {
            case InstallationHealth::Unknown:
            case InstallationHealth::Healthy:
            case InstallationHealth::VerificationRequired:
            case InstallationHealth::Damaged:
            case InstallationHealth::Missing:
                break;
            default:
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The editor health update contains an invalid state.",
                                           .AffectedItem = update.InstallationId});
            }
            const auto found = std::ranges::find(installations, update.InstallationId, &EditorInstallation::Id);
            if (found == installations.end())
            {
                return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                           .Message = "The editor installation is no longer registered.",
                                           .AffectedItem = update.InstallationId});
            }
            changed = changed || found->Health != update.Health;
            found->Health = update.Health;
        }
        return changed ? Commit(std::move(installations)) : HubStatus::Success();
    }

    HubResult<std::vector<EditorInstallation>>
    EditorInstallationRegistry::PrepareUpsertMany(const std::span<const EditorInstallation> incoming) const
    {
        if (incoming.size() > MaximumInstallations)
        {
            return HubResult<std::vector<EditorInstallation>>::Failure(
                {.Code = HubErrorCode::InvalidArgument,
                 .Message = "The editor import exceeds the registry safety limit.",
                 .AffectedItem = "installations"});
        }

        std::vector<EditorInstallation> normalized;
        normalized.reserve(incoming.size());
        std::set<std::string, std::less<>> incomingIds;
        std::set<std::string, std::less<>> incomingRoots;
        for (const auto& incomingInstallation : incoming)
        {
            auto installation = incomingInstallation;
            NormalizeEntrypointRoles(installation);
            if (const auto status = Validate(installation); !status)
                return HubResult<std::vector<EditorInstallation>>::Failure(status.Error());
            if (!incomingIds.insert(installation.Id).second)
            {
                return HubResult<std::vector<EditorInstallation>>::Failure(
                    DuplicateBatchItem(installation, "identity").Error());
            }
            if (!incomingRoots.insert(NormalizedPathKey(installation.Root)).second)
            {
                return HubResult<std::vector<EditorInstallation>>::Failure(
                    DuplicateBatchItem(installation, "location").Error());
            }
            normalized.push_back(std::move(installation));
        }

        auto installations = *m_Snapshot;
        for (auto& installation : normalized)
        {
            const auto rootKey = NormalizedPathKey(installation.Root);
            const auto rootOwner = std::ranges::find_if(installations, [&](const EditorInstallation& existing)
                                                        { return NormalizedPathKey(existing.Root) == rootKey; });
            const auto idOwner = std::ranges::find(installations, installation.Id, &EditorInstallation::Id);
            if (rootOwner != installations.end())
            {
                if (idOwner != installations.end() && idOwner != rootOwner)
                {
                    return HubResult<std::vector<EditorInstallation>>::Failure(
                        DuplicateBatchItem(installation, "identity").Error());
                }
                continue;
            }
            if (idOwner != installations.end())
            {
                return HubResult<std::vector<EditorInstallation>>::Failure(
                    DuplicateBatchItem(installation, "identity").Error());
            }
            installations.push_back(std::move(installation));
        }
        if (installations.size() > MaximumInstallations)
        {
            return HubResult<std::vector<EditorInstallation>>::Failure(
                {.Code = HubErrorCode::InvalidArgument,
                 .Message = "The editor import would exceed the registry safety limit.",
                 .AffectedItem = "installations"});
        }
        return HubResult<std::vector<EditorInstallation>>::Success(std::move(installations));
    }

    HubStatus EditorInstallationRegistry::RemoveExternal(const std::string& installationId)
    {
        auto installations = *m_Snapshot;
        const auto found = std::ranges::find(installations, installationId, &EditorInstallation::Id);
        if (found == installations.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The editor installation is no longer registered.",
                                       .AffectedItem = installationId});
        if (found->Ownership != InstallationOwnership::External)
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "Managed editors must be verified before removal.",
                                       .AffectedItem = installationId});
        installations.erase(found);
        return Commit(std::move(installations));
    }

    HubStatus EditorInstallationRegistry::RemoveManagedRegistration(const std::string& installationId,
                                                                    const std::filesystem::path& expectedRoot)
    {
        if (auto status = CanMutateManagedInstall(installationId, expectedRoot); !status)
            return status;
        auto installations = *m_Snapshot;
        std::erase_if(installations, [&](const auto& value) { return value.Id == installationId; });
        return Commit(std::move(installations));
    }

    HubStatus EditorInstallationRegistry::RemoveMissingManagedRegistration(const std::string& installationId,
                                                                           const std::filesystem::path& expectedRoot)
    {
        auto installations = *m_Snapshot;
        const auto found = std::ranges::find(installations, installationId, &EditorInstallation::Id);
        if (found == installations.end())
        {
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The editor installation is no longer registered.",
                                       .AffectedItem = installationId});
        }
        if (found->Ownership != InstallationOwnership::Managed ||
            NormalizedPathKey(found->Root) != NormalizedPathKey(expectedRoot))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The editor location is not the registered managed installation.",
                                       .AffectedItem = installationId});
        }

        std::error_code error;
        const auto status = std::filesystem::symlink_status(expectedRoot, error);
        if ((!error && status.type() != std::filesystem::file_type::not_found) ||
            (error && error != std::errc::no_such_file_or_directory))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The editor root still exists, so its registration was preserved.",
                                       .AffectedItem = installationId,
                                       .TechnicalDetails = error.message()});
        }
        installations.erase(found);
        return Commit(std::move(installations));
    }

    HubStatus EditorInstallationRegistry::RemoveDeletedManagedRegistration(const ManagedInstallRemovalProof& proof)
    {
        const ManagedInstallMarker marker{.InstallationId = proof.InstallationId,
                                          .ManifestFingerprint = proof.ManifestFingerprint,
                                          .Nonce = proof.MarkerNonce,
                                          .ReceiptSha256 = proof.PackageReceiptSha256};
        if (!proof.Root.is_absolute() || !Detail::IsSha256(proof.PackageTreeIdentity) ||
            proof.PackageReceiptSha256.empty())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The completed managed-editor removal proof is invalid.",
                                       .AffectedItem = proof.InstallationId});
        }
        if (const auto status = ValidateMarker(marker); !status)
            return status;
        auto installations = *m_Snapshot;
        const auto found = std::ranges::find(installations, proof.InstallationId, &EditorInstallation::Id);
        if (found == installations.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The editor installation is no longer registered.",
                                       .AffectedItem = proof.InstallationId});
        if (found->Ownership != InstallationOwnership::Managed ||
            NormalizedPathKey(found->Root) != NormalizedPathKey(proof.Root) ||
            found->ManifestFingerprint != proof.ManifestFingerprint ||
            found->PackageTreeIdentity != proof.PackageTreeIdentity ||
            found->PackageReceiptSha256 != proof.PackageReceiptSha256 || found->MarkerNonce != proof.MarkerNonce)
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The completed removal does not match the registered editor.",
                                       .AffectedItem = proof.InstallationId});
        }
        std::error_code error;
        const auto status = std::filesystem::symlink_status(proof.Root, error);
        if ((!error && status.type() != std::filesystem::file_type::not_found) ||
            (error && error != std::errc::no_such_file_or_directory))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The editor root still exists, so its registration was preserved.",
                                       .AffectedItem = proof.InstallationId,
                                       .TechnicalDetails = error.message()});
        }
        installations.erase(found);
        return Commit(std::move(installations));
    }

    HubStatus EditorInstallationRegistry::CanMutateManagedInstall(const std::string& installationId,
                                                                  const std::filesystem::path& expectedRoot) const
    {
        const auto found = std::ranges::find(*m_Snapshot, installationId, &EditorInstallation::Id);
        if (found == m_Snapshot->end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The editor installation is no longer registered.",
                                       .AffectedItem = installationId});
        if (found->Ownership != InstallationOwnership::Managed ||
            NormalizedPathKey(found->Root) != NormalizedPathKey(expectedRoot))
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The editor location is not a verified managed installation.",
                                       .AffectedItem = installationId});
        }
        auto marker = ReadManagedMarker(expectedRoot);
        if (!marker || marker.Value().InstallationId != found->Id ||
            marker.Value().ManifestFingerprint != found->ManifestFingerprint ||
            marker.Value().Nonce != found->MarkerNonce || marker.Value().ReceiptSha256 != found->PackageReceiptSha256)
        {
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The managed-install marker does not match this editor.",
                                       .AffectedItem = installationId,
                                       .TechnicalDetails = marker ? "Marker fields do not match the registry."
                                                                  : marker.Error().TechnicalDetails});
        }
        return HubStatus::Success();
    }

    std::shared_ptr<const std::vector<EditorInstallation>> EditorInstallationRegistry::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    const std::filesystem::path& EditorInstallationRegistry::Path() const noexcept { return m_Path; }

    HubStatus EditorInstallationRegistry::WriteManagedMarker(const std::filesystem::path& root,
                                                             const ManagedInstallMarker& marker)
    {
        if (const auto status = ValidateMarker(marker); !status)
            return status;
        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || error)
            return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                       .Message = "The managed editor root does not exist.",
                                       .AffectedItem = Detail::PathToUtf8(root.filename())});
        const auto path = root / MarkerFileName;
        if (std::filesystem::exists(path))
        {
            auto existing = ReadManagedMarker(root);
            if (!existing || existing.Value().InstallationId != marker.InstallationId ||
                existing.Value().ManifestFingerprint != marker.ManifestFingerprint ||
                existing.Value().Nonce != marker.Nonce || existing.Value().ReceiptSha256 != marker.ReceiptSha256)
            {
                return HubStatus::Failure({.Code = HubErrorCode::UnsafeInstallRoot,
                                           .Message = "The editor root already has a different managed-install marker.",
                                           .AffectedItem = marker.InstallationId});
            }
            return HubStatus::Success();
        }
        return Detail::WriteJsonFileAtomically(path, {{"schemaVersion", 1},
                                                      {"installationId", marker.InstallationId},
                                                      {"manifestFingerprint", marker.ManifestFingerprint},
                                                      {"nonce", marker.Nonce},
                                                      {"receiptSha256", marker.ReceiptSha256}});
    }

    HubResult<ManagedInstallMarker> EditorInstallationRegistry::ReadManagedMarker(const std::filesystem::path& root)
    {
        auto document = Detail::ReadJsonFile(root / MarkerFileName, MaximumMarkerBytes);
        if (!document)
            return HubResult<ManagedInstallMarker>::Failure(document.Error());
        try
        {
            if (document.Value().at("schemaVersion").get<std::uint32_t>() != 1)
                throw std::invalid_argument("Unsupported marker schema.");
            ManagedInstallMarker marker{document.Value().at("installationId").get<std::string>(),
                                        document.Value().at("manifestFingerprint").get<std::string>(),
                                        document.Value().at("nonce").get<std::string>(),
                                        document.Value().value("receiptSha256", std::string{})};
            if (const auto status = ValidateMarker(marker); !status)
                return HubResult<ManagedInstallMarker>::Failure(status.Error());
            return HubResult<ManagedInstallMarker>::Success(std::move(marker));
        }
        catch (const std::exception& error)
        {
            return HubResult<ManagedInstallMarker>::Failure({.Code = HubErrorCode::InvalidData,
                                                             .Message = "The managed-install marker is malformed.",
                                                             .AffectedItem = Detail::PathToUtf8(root.filename()),
                                                             .TechnicalDetails = error.what()});
        }
    }

    HubStatus EditorInstallationRegistry::Commit(std::vector<EditorInstallation> installations)
    {
        std::ranges::sort(installations, {}, &EditorInstallation::Id);
        if (auto status = Detail::WriteJsonFileAtomically(m_Path, Serialize(installations)); !status)
            return status;
        m_Snapshot = std::make_shared<const std::vector<EditorInstallation>>(std::move(installations));
        return HubStatus::Success();
    }
} // namespace KeireHub
