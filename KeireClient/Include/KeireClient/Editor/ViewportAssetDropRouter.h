#pragma once

#include "Keire/Core.h"

namespace KeireEditor
{
    class IViewportAssetDropCommands
    {
      public:
        virtual ~IViewportAssetDropCommands() = default;
        virtual void OpenDroppedScene(Keire::AssetId asset) = 0;
        virtual void OpenDroppedInputActions(Keire::AssetId asset) = 0;
        virtual void InstantiateDroppedPrefab(Keire::AssetId asset) = 0;
        virtual void CreateDroppedMeshEntity(Keire::AssetId asset) = 0;
        virtual void AssignDroppedMaterial(Keire::EntityId entity, Keire::AssetId asset) = 0;
    };

    class ViewportAssetDropRouter final
    {
      public:
        void Route(Keire::AssetTypeId type, Keire::AssetId asset, Keire::EntityId target,
                   IViewportAssetDropCommands& commands) const;
    };
} // namespace KeireEditor
