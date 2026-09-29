#pragma once

#include "Keire/Animation/RiggingSystem.h"

#include <filesystem>
#include <span>
#include <vector>

namespace KeireEditor::Detail
{
    [[nodiscard]] std::filesystem::path RetargetMappingPath(const std::filesystem::path& project, Keire::AssetId source,
                                                            Keire::AssetId target);
    void SaveRetargetMapping(const std::filesystem::path& project, Keire::AssetId source, Keire::AssetId target,
                             const Keire::SkeletonAsset& sourceSkeleton, const Keire::SkeletonAsset& targetSkeleton,
                             std::span<const Keire::AnimationRetargetOverride> mappings);
    [[nodiscard]] std::vector<Keire::AnimationRetargetOverride>
    LoadRetargetMapping(const std::filesystem::path& project, Keire::AssetId source, Keire::AssetId target,
                        const Keire::SkeletonAsset& sourceSkeleton, const Keire::SkeletonAsset& targetSkeleton);
} // namespace KeireEditor::Detail
