#include "KeireClient/Editor/ViewportAssetDropRouter.h"

#include <stdexcept>

namespace KeireEditor
{
    void ViewportAssetDropRouter::Route(const Keire::AssetTypeId type, const Keire::AssetId asset,
                                        const Keire::EntityId target, IViewportAssetDropCommands& commands) const
    {
        if (!asset)
            throw std::invalid_argument("A viewport asset drop requires a valid asset ID.");
        if (type == Keire::SceneAsset::StaticType())
            commands.OpenDroppedScene(asset);
        else if (type == Keire::InputActionAsset::StaticType())
            commands.OpenDroppedInputActions(asset);
        else if (type == Keire::PrefabAsset::StaticType())
            commands.InstantiateDroppedPrefab(asset);
        else if (type == Keire::MeshAsset::StaticType())
            commands.CreateDroppedMeshEntity(asset);
        else if (type == Keire::MaterialAsset::StaticType())
        {
            if (!target)
                throw std::invalid_argument("Drop a material over a rendered scene entity.");
            commands.AssignDroppedMaterial(target, asset);
        }
        else
            throw std::invalid_argument("The dropped asset type has no Scene viewport action.");
    }
} // namespace KeireEditor
