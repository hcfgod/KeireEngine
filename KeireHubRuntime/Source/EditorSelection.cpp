#include "KeireHubRuntime/EditorSelection.h"

#include "KeireHubRuntime/PackageResolver.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace KeireHub
{
    namespace
    {
        struct Candidate final
        {
            const EditorInstallation* Editor = nullptr;
            SemanticVersion Version;
        };

        [[nodiscard]] std::optional<SemanticVersion> ParseOptional(const std::string& value)
        {
            if (value.empty())
                return std::nullopt;
            auto parsed = SemanticVersion::Parse(value);
            return parsed ? std::optional(std::move(parsed).Value()) : std::nullopt;
        }
    } // namespace

    HubResult<EditorInstallation> SelectCompatibleEditor(const std::span<const EditorInstallation> editors,
                                                         const EditorSelectionRequest& request)
    {
        const auto lastSaved = ParseOptional(request.LastSavedVersion);
        const auto minimum = ParseOptional(request.MinimumVersion);
        if ((!request.LastSavedVersion.empty() && !lastSaved) || (!request.MinimumVersion.empty() && !minimum) ||
            request.ProjectSchema == 0)
        {
            return HubResult<EditorInstallation>::Failure({.Code = HubErrorCode::InvalidArgument,
                                                           .Message = "The project compatibility metadata is invalid.",
                                                           .AffectedItem = request.LastSavedVersion});
        }
        std::vector<Candidate> compatible;
        compatible.reserve(editors.size());
        for (const auto& editor : editors)
        {
            auto version = SemanticVersion::Parse(editor.Version);
            if (!version || editor.Health != InstallationHealth::Healthy || editor.Entrypoints.empty() ||
                request.ProjectSchema < editor.MinimumProjectSchema ||
                request.ProjectSchema > editor.MaximumProjectSchema || (minimum && version.Value() < *minimum) ||
                (lastSaved && version.Value() < *lastSaved))
                continue;
            compatible.push_back({&editor, std::move(version).Value()});
        }
        if (compatible.empty())
        {
            return HubResult<EditorInstallation>::Failure(
                {.Code = HubErrorCode::NotFound,
                 .Message = "No installed editor can safely open this project.",
                 .Retryable = true,
                 .AffectedItem = request.LastSavedVersion,
                 .TechnicalDetails =
                     "Install or locate an editor whose manifest supports the project schema and minimum version."});
        }
        if (!request.PreferredInstallationId.empty())
        {
            const auto preferred = std::ranges::find_if(
                compatible, [&](const auto& value) { return value.Editor->Id == request.PreferredInstallationId; });
            if (preferred != compatible.end())
                return HubResult<EditorInstallation>::Success(*preferred->Editor);
        }
        if (lastSaved)
        {
            const auto exact = std::ranges::find(compatible, *lastSaved, &Candidate::Version);
            if (exact != compatible.end())
                return HubResult<EditorInstallation>::Success(*exact->Editor);
        }
        const auto selected = std::ranges::min_element(compatible, {}, &Candidate::Version);
        return HubResult<EditorInstallation>::Success(*selected->Editor);
    }
} // namespace KeireHub
