#pragma once

#include "KeireHub/HubProductUi.h"

#include "Keire/Ui.h"

#include <string>

namespace KeireHub
{
    enum class HubOpenProjectAction
    {
        None,
        Browse,
        Open
    };

    [[nodiscard]] HubOpenProjectAction DrawHubOpenProjectDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot,
                                                                std::string& projectPath, bool folderDialogPending);
} // namespace KeireHub
