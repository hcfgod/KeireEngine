#include "KeireHubRuntime/BuildSupportPlanning.h"

#include "KeireHubRuntime/PackageResolver.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <ranges>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError InvalidSelection(std::string message, std::string affected = {})
        {
            return {.Code = HubErrorCode::InvalidArgument,
                    .Message = std::move(message),
                    .AffectedItem = std::move(affected)};
        }

        [[nodiscard]] bool IsPlatform(const std::string_view value) noexcept
        {
            return value == "windows" || value == "linux" || value == "macos";
        }

        [[nodiscard]] bool IsArchitecture(const std::string_view value) noexcept
        {
            return value == "x86_64" || value == "arm64";
        }

        [[nodiscard]] bool ValidFilter(const BuildSupportFilter& filter) noexcept
        {
            return (!filter.Platform || IsPlatform(*filter.Platform)) &&
                   (!filter.Architecture || IsArchitecture(*filter.Architecture));
        }

        [[nodiscard]] bool IsConfinedTo(const std::filesystem::path& root, const std::filesystem::path& candidate)
        {
            if (root.empty() || candidate.empty() || !root.is_absolute() || !candidate.is_absolute())
                return false;
            const auto relative = candidate.lexically_normal().lexically_relative(root.lexically_normal());
            return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
        }

        [[nodiscard]] bool ValidEditor(const BuildSupportEditorTarget& editor)
        {
            return !editor.InstallationId.empty() && editor.InstallationId.size() <= 256 &&
                   static_cast<bool>(SemanticVersion::Parse(editor.EngineVersion)) && editor.Healthy &&
                   IsConfinedTo(editor.Root, editor.AssetToolEntrypoint);
        }

        [[nodiscard]] HubStatus ValidateEditorAvailability(const BuildSupportEditorTarget& editor)
        {
            if (editor.Running)
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::InstallationBusy,
                     .Message = "Close the selected editor before changing its Build Support components.",
                     .Retryable = true,
                     .AffectedItem = editor.InstallationId});
            }
            if (editor.HasActiveTask)
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::InstallationBusy,
                     .Message = "Wait for the selected editor's active installation task to finish.",
                     .Retryable = true,
                     .AffectedItem = editor.InstallationId});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] bool ValidComponent(const BuildSupportComponent& component)
        {
            const bool unsafeId = std::ranges::any_of(
                component.Id, [](const unsigned char character)
                { return character < 0x20U || character == 0x7fU || character == '/' || character == '\\'; });
            return !component.Id.empty() && component.Id.size() <= 256 && !unsafeId &&
                   static_cast<bool>(SemanticVersion::Parse(component.EngineVersion)) &&
                   IsPlatform(component.Platform) && IsArchitecture(component.Architecture);
        }

        [[nodiscard]] std::string PathText(const std::filesystem::path& value)
        {
            const auto bytes = value.generic_u8string();
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }

        [[nodiscard]] bool HasBuildSupportExtension(const std::filesystem::path& package)
        {
            auto extension = PathText(package.extension());
            std::ranges::transform(extension, extension.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return extension == ".keireplayersupport";
        }

        [[nodiscard]] HubStatus ValidateSelection(const BuildSupportSelection& selection)
        {
            if (!ValidEditor(selection.Editor))
            {
                return HubStatus::Failure(InvalidSelection(
                    "Select a healthy editor with a typed Asset Tool entrypoint.", selection.Editor.InstallationId));
            }
            if (const auto availability = ValidateEditorAvailability(selection.Editor); !availability)
                return availability;
            if (!ValidFilter(selection.Filter))
            {
                return HubStatus::Failure(
                    InvalidSelection("The Build Support platform or architecture filter is invalid."));
            }
            return HubStatus::Success();
        }
    } // namespace

    HubResult<BuildSupportSelection> SelectBuildSupportEditor(const std::span<const BuildSupportEditorTarget> editors,
                                                              const std::string_view installationId,
                                                              BuildSupportFilter filter)
    {
        if (installationId.empty() || !ValidFilter(filter))
        {
            return HubResult<BuildSupportSelection>::Failure(
                InvalidSelection("The Build Support editor selection is invalid.", std::string(installationId)));
        }
        const auto editor = std::ranges::find(editors, installationId, &BuildSupportEditorTarget::InstallationId);
        if (editor == editors.end())
        {
            return HubResult<BuildSupportSelection>::Failure(
                {.Code = HubErrorCode::NotFound,
                 .Message = "The selected editor installation is no longer available.",
                 .AffectedItem = std::string(installationId)});
        }
        if (!ValidEditor(*editor))
        {
            return HubResult<BuildSupportSelection>::Failure(InvalidSelection(
                "Build Support requires a healthy editor with a typed Asset Tool entrypoint.", editor->InstallationId));
        }
        if (const auto availability = ValidateEditorAvailability(*editor); !availability)
            return HubResult<BuildSupportSelection>::Failure(availability.Error());
        return HubResult<BuildSupportSelection>::Success({.Editor = *editor, .Filter = std::move(filter)});
    }

    HubResult<BuildSupportSelection>
    SelectBuildSupportEditorForTarget(const std::span<const BuildSupportEditorTarget> editors,
                                      const std::string_view platform, const std::string_view architecture)
    {
        BuildSupportFilter filter{.Platform = std::string(platform), .Architecture = std::string(architecture)};
        if (!ValidFilter(filter))
        {
            return HubResult<BuildSupportSelection>::Failure(
                InvalidSelection("The requested Build Support target is invalid."));
        }
        std::vector<BuildSupportEditorTarget> compatible;
        std::ranges::copy_if(editors, std::back_inserter(compatible), [](const BuildSupportEditorTarget& editor)
                             { return ValidEditor(editor) && ValidateEditorAvailability(editor); });
        std::ranges::sort(compatible,
                          [](const auto& left, const auto& right)
                          {
                              auto leftVersion = SemanticVersion::Parse(left.EngineVersion);
                              auto rightVersion = SemanticVersion::Parse(right.EngineVersion);
                              if (leftVersion.Value() != rightVersion.Value())
                                  return leftVersion.Value() > rightVersion.Value();
                              return left.InstallationId < right.InstallationId;
                          });
        if (compatible.empty())
        {
            const auto busy =
                std::ranges::find_if(editors, [](const BuildSupportEditorTarget& editor)
                                     { return ValidEditor(editor) && !ValidateEditorAvailability(editor); });
            if (busy != editors.end())
                return HubResult<BuildSupportSelection>::Failure(ValidateEditorAvailability(*busy).Error());
            return HubResult<BuildSupportSelection>::Failure(
                {.Code = HubErrorCode::NotFound,
                 .Message = "No healthy editor with a typed Asset Tool can manage the requested Build Support.",
                 .AffectedItem = std::string(platform) + "/" + std::string(architecture)});
        }
        return HubResult<BuildSupportSelection>::Success(
            {.Editor = std::move(compatible.front()), .Filter = std::move(filter)});
    }

    std::vector<BuildSupportComponent>
    FilterBuildSupportComponents(const std::span<const BuildSupportComponent> components,
                                 const BuildSupportSelection& selection)
    {
        if (!ValidateSelection(selection))
            return {};
        std::vector<BuildSupportComponent> result;
        std::ranges::copy_if(
            components, std::back_inserter(result),
            [&](const BuildSupportComponent& component)
            {
                return ValidComponent(component) && component.EngineVersion == selection.Editor.EngineVersion &&
                       (!selection.Filter.Platform || component.Platform == *selection.Filter.Platform) &&
                       (!selection.Filter.Architecture || component.Architecture == *selection.Filter.Architecture);
            });
        std::ranges::sort(result,
                          [](const auto& left, const auto& right)
                          {
                              if (left.Platform != right.Platform)
                                  return left.Platform < right.Platform;
                              if (left.Architecture != right.Architecture)
                                  return left.Architecture < right.Architecture;
                              return left.Id < right.Id;
                          });
        return result;
    }

    std::size_t CountBuildSupportComponents(const std::span<const BuildSupportComponent> components,
                                            const std::string_view engineVersion)
    {
        return static_cast<std::size_t>(
            std::ranges::count_if(components, [&](const BuildSupportComponent& component)
                                  { return ValidComponent(component) && component.EngineVersion == engineVersion; }));
    }

    HubError BuildSupportInventoryFailure(std::string affectedItem, std::string technicalDetails)
    {
        return {.Code = HubErrorCode::PackageManifestInvalid,
                .Message = "Installed Build Support files are missing or corrupt.",
                .Retryable = true,
                .AffectedItem = std::move(affectedItem),
                .TechnicalDetails = std::move(technicalDetails),
                .LogReference = "build-support.inventory"};
    }

    std::string BuildSupportLiveStatusText(const BuildSupportOperationKind kind, const std::string_view state,
                                           const std::string_view phase, const std::string_view errorCode)
    {
        if (state == "failed")
        {
            if (errorCode == "build_support.inventory_invalid")
                return "Installed Build Support files are missing or corrupt.";
            if (errorCode == "build_support.install_failed")
                return "Build Support could not be installed. Verify the package and try again.";
            if (errorCode == "build_support.catalog_unavailable")
                return "The Build Support catalog could not be downloaded. Check the network connection and try "
                       "again.";
            if (errorCode == "build_support.download_install_failed")
            {
                return "Build Support could not be downloaded and installed. Check the connection and package, then "
                       "try again.";
            }
            return "The Build Support operation failed. See Hub logs for details.";
        }
        if (state == "succeeded")
        {
            if (kind == BuildSupportOperationKind::Remove)
                return "Build Support removal completed.";
            return kind == BuildSupportOperationKind::Repair ? "Build Support repair completed."
                                                             : "Build Support import completed.";
        }
        if (state != "running")
            return "Build Support status is unavailable.";
        if (phase == "start")
            return kind == BuildSupportOperationKind::Repair ? "Starting Build Support repair."
                                                             : "Starting Build Support import.";
        if (phase == "verify")
            return "Verifying the Build Support package.";
        if (phase == "install")
            return kind == BuildSupportOperationKind::Repair ? "Repairing Build Support." : "Installing Build Support.";
        if (phase == "remove" && kind == BuildSupportOperationKind::Remove)
            return "Removing Build Support.";
        if (phase == "complete")
            return kind == BuildSupportOperationKind::Repair ? "Completing Build Support repair."
                                                             : "Completing Build Support import.";
        return "Build Support operation is running.";
    }

    HubResult<BuildSupportCommandPlan> PlanBuildSupportImport(const BuildSupportSelection& selection,
                                                              const std::filesystem::path& package,
                                                              const std::filesystem::path& operationDirectory,
                                                              const BuildSupportComponent* repairComponent)
    {
        if (const auto status = ValidateSelection(selection); !status)
            return HubResult<BuildSupportCommandPlan>::Failure(status.Error());
        if (package.empty() || !package.is_absolute() || !HasBuildSupportExtension(package) ||
            operationDirectory.empty() || !operationDirectory.is_absolute() ||
            operationDirectory == operationDirectory.root_path())
        {
            return HubResult<BuildSupportCommandPlan>::Failure(
                InvalidSelection("Select a regular .keireplayersupport Build Support package."));
        }
        if (repairComponent &&
            (!ValidComponent(*repairComponent) || repairComponent->EngineVersion != selection.Editor.EngineVersion ||
             (selection.Filter.Platform && repairComponent->Platform != *selection.Filter.Platform) ||
             (selection.Filter.Architecture && repairComponent->Architecture != *selection.Filter.Architecture)))
        {
            return HubResult<BuildSupportCommandPlan>::Failure(
                InvalidSelection("The Build Support repair target does not match the selected editor and filter."));
        }
        BuildSupportCommandPlan result{.Kind = repairComponent ? BuildSupportOperationKind::Repair
                                                               : BuildSupportOperationKind::Import,
                                       .Executable = selection.Editor.AssetToolEntrypoint,
                                       .Arguments = {"install-player-support", "--input", PathText(package), "--status",
                                                     PathText(operationDirectory / "status.json"), "--cancel",
                                                     PathText(operationDirectory / "cancel")},
                                       .WorkingDirectory = package.parent_path(),
                                       .StatusPath = operationDirectory / "status.json",
                                       .CancelPath = operationDirectory / "cancel",
                                       .ComponentId = repairComponent ? repairComponent->Id : std::string{}};
        if (repairComponent)
        {
            result.Arguments.emplace_back("--pack-id");
            result.Arguments.push_back(repairComponent->Id);
        }
        if (selection.Filter.Platform)
        {
            result.Arguments.emplace_back("--expected-platform");
            result.Arguments.push_back(*selection.Filter.Platform);
        }
        if (selection.Filter.Architecture)
        {
            result.Arguments.emplace_back("--expected-architecture");
            result.Arguments.push_back(*selection.Filter.Architecture);
        }
        return HubResult<BuildSupportCommandPlan>::Success(std::move(result));
    }

    HubResult<BuildSupportCommandPlan> PlanBuildSupportRemoval(const BuildSupportSelection& selection,
                                                               const BuildSupportComponent& component)
    {
        if (const auto status = ValidateSelection(selection); !status)
            return HubResult<BuildSupportCommandPlan>::Failure(status.Error());
        if (!ValidComponent(component) || component.EngineVersion != selection.Editor.EngineVersion ||
            (selection.Filter.Platform && component.Platform != *selection.Filter.Platform) ||
            (selection.Filter.Architecture && component.Architecture != *selection.Filter.Architecture))
        {
            return HubResult<BuildSupportCommandPlan>::Failure(InvalidSelection(
                "The Build Support component is outside the selected editor or target filter.", component.Id));
        }
        return HubResult<BuildSupportCommandPlan>::Success(
            {.Kind = BuildSupportOperationKind::Remove,
             .Executable = selection.Editor.AssetToolEntrypoint,
             .Arguments = {"remove-player-support", "--engine-version", component.EngineVersion, "--pack-id",
                           component.Id},
             .WorkingDirectory = selection.Editor.AssetToolEntrypoint.parent_path(),
             .ComponentId = component.Id});
    }
} // namespace KeireHub
