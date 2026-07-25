#pragma once

#include "Keire/Core.h"

#include <span>
#include <string>

namespace KeireEditor
{
    struct PrefabSourceUpdate
    {
        Keire::PrefabDefinition Prefab;
        Keire::SceneDefinition Scene;
    };

    [[nodiscard]] Keire::PrefabDefinition CreatePrefabFromSelection(const Keire::SceneDefinition& scene,
                                                                    std::span<const Keire::AssetId> roots,
                                                                    std::string name);
    [[nodiscard]] Keire::PrefabDefinition CreatePrefabVariant(Keire::AssetId basePrefab, std::string name,
                                                              std::vector<Keire::PrefabOverrideDefinition> overrides);
    [[nodiscard]] Keire::PrefabInstanceDefinition InstantiatePrefab(Keire::SceneDefinition& scene,
                                                                    Keire::AssetId prefab,
                                                                    const Keire::SceneDefinition& composed,
                                                                    Keire::AssetId parent = {});
    [[nodiscard]] Keire::PrefabInstanceDefinition ConnectPrefabInstance(Keire::SceneDefinition& scene,
                                                                        Keire::AssetId prefab,
                                                                        const Keire::SceneDefinition& source,
                                                                        Keire::AssetId instanceRoot);
    [[nodiscard]] bool RevertPrefabInstance(Keire::SceneDefinition& scene, Keire::AssetId instanceRoot,
                                            const Keire::SceneDefinition& composed);
    [[nodiscard]] bool UnpackPrefab(Keire::SceneDefinition& scene, Keire::AssetId instanceRoot,
                                    bool completely = false);
    [[nodiscard]] Keire::PrefabDefinition
    UpdatePrefabFromEditingScene(const Keire::PrefabDefinition& source, const Keire::SceneDefinition& edited,
                                 const Keire::SceneDefinition* composedBase = nullptr);
    [[nodiscard]] PrefabSourceUpdate ApplyPrefabInstanceToSource(const Keire::SceneDefinition& scene,
                                                                 Keire::AssetId instanceRoot,
                                                                 const Keire::PrefabDefinition& source,
                                                                 const Keire::SceneDefinition& composedSource,
                                                                 const Keire::SceneDefinition* composedBase = nullptr);
} // namespace KeireEditor
