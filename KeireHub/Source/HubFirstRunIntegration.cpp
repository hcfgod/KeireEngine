#include "KeireHub/HubFirstRunIntegration.h"

#include <utility>

namespace KeireHub
{
    namespace
    {
        void AddExistingDirectory(std::vector<std::filesystem::path>& roots, const std::filesystem::path& path)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (!error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status))
                roots.push_back(path);
        }
    } // namespace

    HubFirstRunDiscoveryRequest BuildHubFirstRunDiscoveryRequest(const HubSettings& settings,
                                                                 const std::filesystem::path& executable,
                                                                 std::string platform, std::string architecture)
    {
        HubFirstRunDiscoveryRequest request{.HostPlatform = std::move(platform),
                                            .HostArchitecture = std::move(architecture)};
        AddExistingDirectory(request.ProjectRoots, settings.DefaultProjectLocation);
        for (const auto& root : settings.ProjectDiscoveryRoots)
            AddExistingDirectory(request.ProjectRoots, root);
        AddExistingDirectory(request.EditorRoots, settings.DefaultEditorRoot);
        if (std::filesystem::is_regular_file(executable))
            request.PackagedOrCombinedAncestry = executable;
        return request;
    }

    void ApplyHubFirstRunSnapshot(const HubFirstRunWorkflowSnapshot& discovery, HubProductSnapshot& product)
    {
        product.FirstRunDiscoveryRunning = discovery.State == HubFirstRunWorkflowState::Running;
        product.FirstRunDiscoveryComplete = discovery.State == HubFirstRunWorkflowState::Completed;
        product.DiscoveredProjects = discovery.ProjectsFound;
        product.DiscoveredEditors = discovery.EditorsFound;
        product.DiscoveredProjectItems.clear();
        product.DiscoveredEditorItems.clear();
        if (discovery.Discovery)
        {
            product.DiscoveredProjectItems.reserve(discovery.Discovery->Projects.size());
            for (const auto& project : discovery.Discovery->Projects)
            {
                product.DiscoveredProjectItems.push_back({.Name = project.Name,
                                                          .Root = project.Root,
                                                          .Detail = "Project schema " +
                                                                    std::to_string(project.SchemaVersion) +
                                                                    " / editor " + project.LastSavedWithEngineVersion});
            }
            product.DiscoveredEditorItems.reserve(discovery.Discovery->Editors.size());
            for (const auto& editor : discovery.Discovery->Editors)
            {
                product.DiscoveredEditorItems.push_back(
                    {.Name = "Kéire Editor " + editor.Version,
                     .Root = editor.Root,
                     .Detail = editor.Channel + " / " + editor.Platform + " / " + editor.Architecture});
            }
        }
        product.FirstRunDiscoveryMessage = discovery.Message;
    }

    HubStatus ImportHubFirstRunSnapshot(const HubFirstRunWorkflowSnapshot& discovery, HubController& controller)
    {
        if (discovery.State != HubFirstRunWorkflowState::Completed || !discovery.Discovery || !discovery.PreparedImport)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "First-run discovery has not prepared an import.",
                                       .AffectedItem = "first-run-discovery"});
        }
        return CommitHubFirstRunImport(*discovery.PreparedImport, controller);
    }
} // namespace KeireHub
