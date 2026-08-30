#pragma once

#include "Keire/Assets/Asset.h"

#include <filesystem>

namespace Keire
{
    class AssetDatabase;
}

namespace Keire::Detail
{
    struct StarterProjectUiAssets final
    {
        AssetId VisualTree;
        AssetId PanelSettings;
        AssetId WorldVisualTree;
        AssetId WorldPanelSettings;
        AssetId RenderTextureVisualTree;
        AssetId RenderTexturePanelSettings;
    };

    [[nodiscard]] StarterProjectUiAssets CreateStarterProjectUiAssets(AssetDatabase& database);
    void WriteStarterProjectUiScript(const std::filesystem::path& projectRoot);
} // namespace Keire::Detail
