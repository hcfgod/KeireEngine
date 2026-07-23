#pragma once

#include "Keire/Core.h"

#include <span>
#include <string>

namespace KeireEditor
{
    [[nodiscard]] Keire::PrefabDefinition CreatePrefabFromSelection(const Keire::SceneDefinition& scene,
                                                                    std::span<const Keire::AssetId> roots,
                                                                    std::string name);
    [[nodiscard]] Keire::PrefabDefinition CreatePrefabVariant(Keire::AssetId basePrefab, std::string name,
                                                              std::vector<Keire::PrefabOverrideDefinition> overrides);
    [[nodiscard]] Keire::PrefabInstanceDefinition InstantiatePrefab(Keire::SceneDefinition& scene,
                                                                    Keire::AssetId prefab,
                                                                    const Keire::SceneDefinition& composed,
                                                                    Keire::AssetId parent = {});
    [[nodiscard]] bool RevertPrefabInstance(Keire::SceneDefinition& scene, Keire::AssetId instanceRoot,
                                            const Keire::SceneDefinition& composed);
    [[nodiscard]] bool UnpackPrefab(Keire::SceneDefinition& scene, Keire::AssetId instanceRoot,
                                    bool completely = false);
} // namespace KeireEditor
