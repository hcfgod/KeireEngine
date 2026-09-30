#pragma once

#include "KeireHub/HubProductUi.h"

#include "Keire/Ui.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    enum class HubCreateProjectAction
    {
        None,
        Browse,
        Create
    };

    struct HubCreateProjectRequest final
    {
        HubCreateProjectAction Action = HubCreateProjectAction::None;
        std::string TemplateId;
        std::string EditorId;
        std::string Name;
        std::filesystem::path ParentDirectory;
        bool OpenAfterCreation = true;
    };

    struct HubProjectEditorChoice final
    {
        std::size_t EditorIndex = 0;
        std::string Id;
        std::string PrimaryLabel;
        std::string RootLabel;

        [[nodiscard]] bool operator==(const HubProjectEditorChoice&) const noexcept = default;
    };

    [[nodiscard]] std::vector<HubProjectEditorChoice>
    BuildHubProjectEditorChoices(std::span<const HubEditorUiRecord> editors,
                                 const HubTemplateUiRecord& projectTemplate);
    [[nodiscard]] std::string SelectHubProjectEditorId(std::span<const HubProjectEditorChoice> choices,
                                                       std::string_view currentId);

    [[nodiscard]] HubCreateProjectRequest
    DrawHubCreateProjectDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, std::string& templateId,
                               std::string& editorId, std::string& projectName, std::string& projectLocation,
                               bool& openAfterCreation, bool folderDialogPending,
                               const std::filesystem::path& distributionRoot);
} // namespace KeireHub
