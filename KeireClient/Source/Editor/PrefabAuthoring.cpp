#include "KeireClient/Editor/PrefabAuthoring.h"

#include <algorithm>
#include <ranges>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool DescendsFrom(const Keire::SceneDefinition& scene, Keire::AssetId object,
                                        const Keire::AssetId ancestor)
        {
            while (object)
            {
                if (object == ancestor)
                    return true;
                const auto found = std::ranges::find(scene.Objects, object, &Keire::SceneObjectDefinition::Id);
                if (found == scene.Objects.end())
                    return false;
                object = found->Parent;
            }
            return false;
        }
    } // namespace

    Keire::PrefabDefinition CreatePrefabFromSelection(const Keire::SceneDefinition& scene,
                                                      const std::span<const Keire::AssetId> roots, std::string name)
    {
        Keire::SceneAsset::Validate(scene);
        if (roots.empty() || name.empty())
            throw std::invalid_argument("Prefab creation requires a name and at least one selected root.");
        std::set<Keire::AssetId> selectedRoots;
        for (const auto root : roots)
        {
            if (!root ||
                std::ranges::find(scene.Objects, root, &Keire::SceneObjectDefinition::Id) == scene.Objects.end())
                throw std::invalid_argument("Prefab selection contains an unavailable scene object.");
            selectedRoots.insert(root);
        }
        for (auto iterator = selectedRoots.begin(); iterator != selectedRoots.end();)
        {
            const bool nested =
                std::ranges::any_of(selectedRoots, [&](const auto candidate)
                                    { return candidate != *iterator && DescendsFrom(scene, *iterator, candidate); });
            if (nested)
                iterator = selectedRoots.erase(iterator);
            else
                ++iterator;
        }

        Keire::PrefabDefinition result;
        result.Template = Keire::SceneAsset::EmptyDefinition(std::move(name));
        for (const auto& object : scene.Objects)
        {
            const auto selected = std::ranges::find_if(selectedRoots, [&](const auto root)
                                                       { return DescendsFrom(scene, object.Id, root); });
            if (selected == selectedRoots.end())
                continue;
            auto copy = object;
            if (copy.Id == *selected)
                copy.Parent = {};
            result.Template.Objects.push_back(std::move(copy));
        }
        Keire::PrefabAsset::Validate(result);
        return result;
    }

    Keire::PrefabDefinition CreatePrefabVariant(const Keire::AssetId basePrefab, std::string name,
                                                std::vector<Keire::PrefabOverrideDefinition> overrides)
    {
        if (!basePrefab || name.empty())
            throw std::invalid_argument("Prefab variant requires a base prefab and name.");
        Keire::PrefabDefinition result;
        result.BasePrefab = basePrefab;
        result.Template = Keire::SceneAsset::EmptyDefinition(std::move(name));
        result.Template.PrefabOverrides = std::move(overrides);
        Keire::PrefabAsset::Validate(result);
        return result;
    }

    Keire::PrefabInstanceDefinition InstantiatePrefab(Keire::SceneDefinition& scene, const Keire::AssetId prefab,
                                                      const Keire::SceneDefinition& composed,
                                                      const Keire::AssetId parent)
    {
        Keire::SceneAsset::Validate(scene);
        Keire::SceneAsset::Validate(composed);
        if (!prefab || !composed.PrefabInstances.empty() || !composed.PrefabOverrides.empty())
            throw std::invalid_argument("Prefab instantiation requires a fully composed prefab.");
        if (parent &&
            std::ranges::find(scene.Objects, parent, &Keire::SceneObjectDefinition::Id) == scene.Objects.end())
            throw std::invalid_argument("Prefab instance parent is unavailable.");

        std::unordered_map<Keire::AssetId, Keire::AssetId> remapped;
        for (const auto& object : composed.Objects)
            remapped.emplace(object.Id, Keire::AssetId::Generate());

        auto replacement = scene;
        Keire::PrefabInstanceDefinition instance;
        instance.Prefab = prefab;
        for (const auto& object : composed.Objects)
        {
            auto copy = object;
            copy.Id = remapped.at(object.Id);
            copy.Parent = object.Parent ? remapped.at(object.Parent) : parent;
            replacement.Objects.push_back(std::move(copy));
            instance.Objects.push_back({object.Id, remapped.at(object.Id)});
            if (!object.Parent && !instance.Root)
                instance.Root = remapped.at(object.Id);
        }
        if (!instance.Root)
            throw std::invalid_argument("Prefab instantiation requires at least one root object.");
        replacement.PrefabInstances.push_back(instance);
        Keire::SceneAsset::Validate(replacement);
        scene = std::move(replacement);
        return instance;
    }

    bool UnpackPrefab(Keire::SceneDefinition& scene, const Keire::AssetId instanceRoot, const bool completely)
    {
        Keire::SceneAsset::Validate(scene);
        const auto found =
            std::ranges::find(scene.PrefabInstances, instanceRoot, &Keire::PrefabInstanceDefinition::Root);
        if (found == scene.PrefabInstances.end())
            return false;
        auto replacement = scene;
        std::erase_if(replacement.PrefabInstances,
                      [&](const Keire::PrefabInstanceDefinition& instance)
                      {
                          return instance.Root == instanceRoot ||
                                 (completely && DescendsFrom(scene, instance.Root, instanceRoot));
                      });
        Keire::SceneAsset::Validate(replacement);
        scene = std::move(replacement);
        return true;
    }

    bool RevertPrefabInstance(Keire::SceneDefinition& scene, const Keire::AssetId instanceRoot,
                              const Keire::SceneDefinition& composed)
    {
        Keire::SceneAsset::Validate(scene);
        Keire::SceneAsset::Validate(composed);
        if (!composed.PrefabInstances.empty() || !composed.PrefabOverrides.empty())
            throw std::invalid_argument("Prefab revert requires a fully composed prefab.");
        const auto found =
            std::ranges::find(scene.PrefabInstances, instanceRoot, &Keire::PrefabInstanceDefinition::Root);
        if (found == scene.PrefabInstances.end())
            return false;

        auto replacement = scene;
        auto instance =
            std::ranges::find(replacement.PrefabInstances, instanceRoot, &Keire::PrefabInstanceDefinition::Root);
        const auto currentRoot =
            std::ranges::find(replacement.Objects, instanceRoot, &Keire::SceneObjectDefinition::Id);
        if (currentRoot == replacement.Objects.end())
            throw std::invalid_argument("Prefab instance root is unavailable.");
        const auto externalParent = currentRoot->Parent;

        std::unordered_map<Keire::AssetId, Keire::AssetId> remapped;
        for (const auto& mapping : instance->Objects)
            remapped.emplace(mapping.Source, mapping.Instance);
        for (const auto& object : composed.Objects)
            if (!remapped.contains(object.Id))
                remapped.emplace(object.Id, Keire::AssetId::Generate());

        std::set<Keire::AssetId> removed;
        for (const auto& mapping : instance->Objects)
            removed.insert(mapping.Instance);
        for (const auto& overrideValue : instance->Overrides)
            if (overrideValue.Kind == Keire::PrefabOverrideKind::AddObject && overrideValue.AddedObject)
                removed.insert(overrideValue.AddedObject->Id);
        bool expanded = true;
        while (expanded)
        {
            expanded = false;
            for (const auto& object : replacement.Objects)
                if (removed.contains(object.Parent) && removed.insert(object.Id).second)
                    expanded = true;
        }
        std::erase_if(replacement.Objects,
                      [&](const Keire::SceneObjectDefinition& object) { return removed.contains(object.Id); });

        instance->Objects.clear();
        instance->Overrides.clear();
        instance->Root = {};
        for (const auto& object : composed.Objects)
        {
            auto copy = object;
            copy.Id = remapped.at(object.Id);
            copy.Parent = object.Parent ? remapped.at(object.Parent) : externalParent;
            replacement.Objects.push_back(std::move(copy));
            instance->Objects.push_back({object.Id, remapped.at(object.Id)});
            if (!object.Parent && !instance->Root)
                instance->Root = remapped.at(object.Id);
        }
        if (!instance->Root)
            throw std::invalid_argument("Prefab revert requires a composed root object.");
        Keire::SceneAsset::Validate(replacement);
        scene = std::move(replacement);
        return true;
    }
} // namespace KeireEditor
