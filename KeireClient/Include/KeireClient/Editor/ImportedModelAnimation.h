#pragma once

#include "Keire/Core.h"

#include <exception>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace KeireEditor
{
    struct ImportedModelAnimation final
    {
        Keire::AssetId Skeleton;
        Keire::AssetId Skin;
        Keire::AssetId Rig;

        [[nodiscard]] static ImportedModelAnimation Resolve(const std::span<const Keire::AssetId> ids,
                                                            const Keire::AssetSystem& assets)
        {
            std::vector<std::pair<Keire::AssetId, Keire::AssetTypeId>> typedAssets;
            for (const auto id : ids)
                if (const auto type = assets.TryGetType(id))
                    typedAssets.emplace_back(id, *type);
            return Resolve(typedAssets);
        }

        [[nodiscard]] static ImportedModelAnimation
        Resolve(const std::span<const std::pair<Keire::AssetId, Keire::AssetTypeId>> assets)
        {
            ImportedModelAnimation result;
            for (const auto& [id, type] : assets)
            {
                auto* destination = type == Keire::SkeletonAsset::StaticType()        ? &result.Skeleton
                                    : type == Keire::SkinnedMeshAsset::StaticType()   ? &result.Skin
                                    : type == Keire::RigDefinitionAsset::StaticType() ? &result.Rig
                                                                                      : nullptr;
                if (!destination)
                    continue;
                if (!id || (*destination && *destination != id))
                    throw std::invalid_argument(
                        "The model has ambiguous animation assets. Reimport it before placing it in a scene.");
                *destination = id;
            }
            if (result.Skin && !result.Skeleton)
                throw std::invalid_argument(
                    "The model's skin has no imported skeleton. Reimport the model before placing it in a scene.");
            return result;
        }

        // Resolve before creating the entity so malformed import metadata cannot leave a partial drop.
        void Apply(Keire::Entity entity) const
        {
            if (!Skin)
                return;
            if (!Skeleton)
                throw std::invalid_argument("An imported skin requires a skeleton.");
            if (entity.GetComponent<Keire::AnimatorComponent>())
                throw std::invalid_argument("Automatic model setup cannot replace an existing Animator.");
            const auto animator = entity.AddComponent<Keire::AnimatorComponent>();
            try
            {
                animator->SetSkeleton(Skeleton);
                animator->SetSkinnedMesh(Skin);
                animator->SetRigDefinition(Rig);
            }
            catch (...)
            {
                const auto failure = std::current_exception();
                try
                {
                    (void)entity.RemoveComponent(animator);
                }
                catch (...)
                {
                    // Cleanup must not replace the original setup failure.
                }
                std::rethrow_exception(failure);
            }
        }
    };
} // namespace KeireEditor
