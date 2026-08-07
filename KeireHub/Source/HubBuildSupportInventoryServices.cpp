#include "KeireHub/HubBuildSupportInventoryWorkflow.h"

#include "Keire/Log.h"
#include "KeireInternal/Build/PlayerSupportPackage.h"
#include "KeireInternal/FileSystem.h"

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] BuildSupportComponent ToComponent(Keire::Detail::PlayerSupportPackageResult package)
        {
            const auto root = Keire::Detail::PlayerSupportStorageRoot() / package.Manifest.EngineVersion /
                              Keire::Detail::PathFromUtf8(package.Manifest.Id);
            std::string diagnostic;
            const bool healthy = Keire::Detail::ValidateInstalledPlayerSupportInventory(
                root, package.Manifest.EngineVersion, diagnostic);
            if (!healthy)
            {
                const auto failure = BuildSupportInventoryFailure(package.Manifest.Id, std::move(diagnostic));
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support validation failed [{}] for {}: {}",
                                   ToString(failure.Code), failure.AffectedItem, failure.TechnicalDetails);
                diagnostic = failure.Message;
            }
            return {.Id = std::move(package.Manifest.Id),
                    .EngineVersion = std::move(package.Manifest.EngineVersion),
                    .Platform = std::string(Keire::ToString(package.Manifest.Platform)),
                    .Architecture = std::string(Keire::ToString(package.Manifest.Architecture)),
                    .ArchiveSizeBytes = package.ArchiveSize,
                    .Healthy = healthy,
                    .Diagnostic = std::move(diagnostic)};
        }
    } // namespace

    HubBuildSupportInventoryServices CreateHubBuildSupportInventoryServices()
    {
        return {.Load = []
                {
                    try
                    {
                        std::vector<BuildSupportComponent> inventory;
                        for (auto& package : Keire::Detail::InstalledPlayerSupport())
                            inventory.push_back(ToComponent(std::move(package)));
                        return HubResult<std::vector<BuildSupportComponent>>::Success(std::move(inventory));
                    }
                    catch (const std::exception& error)
                    {
                        return HubResult<std::vector<BuildSupportComponent>>::Failure(
                            {.Code = HubErrorCode::IoRead,
                             .Message = "Build Support inventory could not be refreshed. See Hub logs for details.",
                             .Retryable = true,
                             .AffectedItem = "build-support",
                             .TechnicalDetails = error.what()});
                    }
                    catch (...)
                    {
                        return HubResult<std::vector<BuildSupportComponent>>::Failure(
                            {.Code = HubErrorCode::IoRead,
                             .Message = "Build Support inventory could not be refreshed. See Hub logs for details.",
                             .Retryable = true,
                             .AffectedItem = "build-support",
                             .TechnicalDetails = "The inventory service failed with a non-standard exception."});
                    }
                }};
    }
} // namespace KeireHub
