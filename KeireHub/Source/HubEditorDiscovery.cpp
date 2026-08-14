#include "KeireHub/HubEditorDiscovery.h"

#include "KeireHubRuntime/EditorInstallationManager.h"

#include "Keire/BuildInfo.h"
#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <stdexcept>

#ifndef KEIRE_EDITOR_TARGET
#define KEIRE_EDITOR_TARGET "KeireClient"
#endif

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string Lower(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const char character)
                                   { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
            return value;
        }

        [[nodiscard]] bool IsConfined(const std::filesystem::path& root, const std::filesystem::path& path)
        {
            const auto relative = path.lexically_normal();
            if (relative.empty() || relative.is_absolute())
                return false;
            for (const auto& component : relative)
            {
                if (component == "..")
                    return false;
            }
            std::error_code error;
            const auto canonical = std::filesystem::weakly_canonical(root / relative, error);
            if (error)
                return false;
            const auto mismatch = std::mismatch(root.begin(), root.end(), canonical.begin(), canonical.end());
            return mismatch.first == root.end();
        }
    } // namespace

    HubResult<EditorInstallation> InspectExternalEditor(std::filesystem::path selected, std::string installationId)
    {
        try
        {
            auto resolvedRoot = ResolveExternalEditorPackageRoot(selected);
            if (!resolvedRoot)
                throw std::runtime_error(resolvedRoot.Error().TechnicalDetails);
            auto root = std::move(resolvedRoot).Value();
            const auto manifestPath = root / "editor-package.json";
            const auto manifestBytes = Keire::Detail::ReadTextFile(manifestPath, std::size_t{4U} * 1024U * 1024U);
            const auto manifest = nlohmann::json::parse(manifestBytes);
            const auto schema = manifest.at("schemaVersion").get<std::uint32_t>();
            if (schema != 1 && schema != 2)
                throw std::runtime_error("The selected editor package uses an unsupported manifest schema.");
            auto calculatedFingerprint = ComputeEditorPackageManifestFingerprint(manifestBytes);
            if (!calculatedFingerprint)
                throw std::runtime_error(calculatedFingerprint.Error().Message);
            if (schema == 2 && manifest.at("manifestFingerprint").get<std::string>() != calculatedFingerprint.Value())
                throw std::runtime_error("The selected editor package manifest fingerprint is invalid.");
            const auto platform = Lower(manifest.value("platform", std::string(Keire::GetBuildInfo().Platform)));
            const auto architecture =
                Lower(manifest.value("architecture", std::string(Keire::GetBuildInfo().Architecture)));
            if (Lower(platform) != Lower(std::string(Keire::GetBuildInfo().Platform)) ||
                Lower(architecture) != Lower(std::string(Keire::GetBuildInfo().Architecture)))
            {
                throw std::runtime_error("The selected editor package is not compatible with this host.");
            }

            std::filesystem::path editorRelative = std::filesystem::path("bin") / KEIRE_EDITOR_TARGET;
            std::filesystem::path assetToolRelative;
#if defined(_WIN32)
            editorRelative += ".exe";
#endif
            std::vector<std::filesystem::path> entrypoints;
            if (schema == 2)
            {
                const auto& values = manifest.at("entrypoints");
                if (!values.is_object() || !values.contains("editor"))
                    throw std::runtime_error("The selected editor package has no declared editor entrypoint.");
                editorRelative = Keire::Detail::PathFromUtf8(values.at("editor").get<std::string>());
                entrypoints.push_back(editorRelative);
                for (const auto& [name, value] : values.items())
                {
                    (void)name;
                    if (!value.is_string())
                        throw std::runtime_error("The selected editor package has an invalid entrypoint.");
                    const auto relative = Keire::Detail::PathFromUtf8(value.get<std::string>());
                    if (!IsConfined(root, relative) || !std::filesystem::is_regular_file(root / relative))
                        throw std::runtime_error("The selected editor package has a missing or unsafe entrypoint.");
                    if (name == "assetTool")
                        assetToolRelative = relative;
                    if (name != "editor")
                        entrypoints.push_back(relative);
                }
            }
            else
            {
                entrypoints.push_back(editorRelative);
            }
            const auto entrypoint = root / editorRelative;
            if (!std::filesystem::is_regular_file(entrypoint))
                throw std::runtime_error("The selected editor package is missing its editor entrypoint.");
            std::uint32_t minimumProjectSchema = 1;
            std::uint32_t maximumProjectSchema = 1;
            if (schema == 2)
            {
                const auto& projectSchema = manifest.at("projectSchema");
                minimumProjectSchema = projectSchema.at("minimum").get<std::uint32_t>();
                maximumProjectSchema = projectSchema.at("maximum").get<std::uint32_t>();
                if (minimumProjectSchema == 0 || maximumProjectSchema < minimumProjectSchema)
                    throw std::runtime_error("The selected editor package has an invalid project schema range.");
            }
            return HubResult<EditorInstallation>::Success(
                {.Id = std::move(installationId),
                 .Version = manifest.value("version", std::string("Unknown")),
                 .Channel = manifest.value("channel", std::string("Stable")),
                 .Platform = platform,
                 .Architecture = architecture,
                 .Root = std::move(root),
                 .Ownership = InstallationOwnership::External,
                 .ManifestFingerprint = manifest.value("manifestFingerprint", calculatedFingerprint.Value()),
                 .Entrypoints = std::move(entrypoints),
                 .EditorEntrypoint = std::move(editorRelative),
                 .AssetToolEntrypoint = std::move(assetToolRelative),
                 .BundledDotnetSdk = manifest.value("bundledDotnetSdk", std::string{}),
                 .MinimumProjectSchema = minimumProjectSchema,
                 .MaximumProjectSchema = maximumProjectSchema,
                 .InstalledSizeBytes = manifest.value("installedSizeBytes", 0ULL),
                 .Health = InstallationHealth::VerificationRequired});
        }
        catch (const std::exception& error)
        {
            return HubResult<EditorInstallation>::Failure({.Code = HubErrorCode::InvalidData,
                                                           .Message = "The selected editor package is invalid.",
                                                           .AffectedItem = selected.filename().string(),
                                                           .TechnicalDetails = error.what()});
        }
    }

    HubResult<EditorInstallation> RegisterExternalEditor(HubController& controller, std::filesystem::path selected,
                                                         std::string installationId)
    {
        auto result = InspectExternalEditor(std::move(selected), std::move(installationId));
        if (!result)
            return result;
        auto installation = std::move(result).Value();
        const auto existing = controller.Installations().Snapshot();
        if (std::ranges::find(*existing, installation.Id, &EditorInstallation::Id) != existing->end())
        {
            return HubResult<EditorInstallation>::Failure(
                {.Code = HubErrorCode::DuplicateIdentifier,
                 .Message = "That editor installation identifier is already in use.",
                 .AffectedItem = installation.Id});
        }
        if (std::ranges::find(*existing, installation.Root, &EditorInstallation::Root) != existing->end())
        {
            return HubResult<EditorInstallation>::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                                           .Message = "That editor installation is already registered.",
                                                           .AffectedItem = installation.Id});
        }
        if (const auto status = controller.Installations().Upsert(installation); !status)
            return HubResult<EditorInstallation>::Failure(status.Error());
        return HubResult<EditorInstallation>::Success(std::move(installation));
    }

    HubResult<std::optional<EditorInstallation>>
    RegisterPackagedEditorIfPresent(HubController& controller, const std::filesystem::path& packageRoot)
    {
        std::error_code error;
        const auto manifest = packageRoot / "editor-package.json";
        if (packageRoot.empty() || !std::filesystem::is_regular_file(manifest, error) || error)
            return HubResult<std::optional<EditorInstallation>>::Success(std::nullopt);

        auto inspected = InspectExternalEditor(packageRoot, "packaged-editor-candidate");
        if (!inspected)
            return HubResult<std::optional<EditorInstallation>>::Failure(inspected.Error());
        auto installation = std::move(inspected).Value();
        installation.Id = "packaged-" + installation.ManifestFingerprint;

        const auto existing = controller.Installations().Snapshot();
        if (const auto found = std::ranges::find(*existing, installation.Root, &EditorInstallation::Root);
            found != existing->end())
        {
            if (found->Ownership != InstallationOwnership::External)
            {
                return HubResult<std::optional<EditorInstallation>>::Failure(
                    {.Code = HubErrorCode::UnsafeInstallRoot,
                     .Message = "The packaged editor location belongs to a managed installation.",
                     .AffectedItem = found->Id});
            }
            if (found->ManifestFingerprint != installation.ManifestFingerprint)
            {
                installation.Id = found->Id;
                if (const auto status = controller.Installations().Upsert(installation); !status)
                    return HubResult<std::optional<EditorInstallation>>::Failure(status.Error());
                return HubResult<std::optional<EditorInstallation>>::Success(std::move(installation));
            }
            return HubResult<std::optional<EditorInstallation>>::Success(*found);
        }
        if (const auto status = controller.Installations().Upsert(installation); !status)
            return HubResult<std::optional<EditorInstallation>>::Failure(status.Error());
        return HubResult<std::optional<EditorInstallation>>::Success(std::move(installation));
    }
} // namespace KeireHub
