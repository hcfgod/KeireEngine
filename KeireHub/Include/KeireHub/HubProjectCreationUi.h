#pragma once

#include "KeireHub/HubProductUi.h"

#include "Keire/Ui.h"

#include <filesystem>
#include <string>

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

    [[nodiscard]] HubCreateProjectRequest
    DrawHubCreateProjectDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, std::string& templateId,
                               std::string& editorId, std::string& projectName, std::string& projectLocation,
                               bool& openAfterCreation, bool folderDialogPending);
} // namespace KeireHub
