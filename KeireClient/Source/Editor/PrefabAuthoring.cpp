#include "KeireClient/Editor/PrefabAuthoring.h"

#include <algorithm>
#include <ranges>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

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
                                                      const Keire::AssetId parent,
                                                      const std::optional<Keire::Vector3> position)
    {
        Keire::SceneAsset::Validate(scene);
        Keire::SceneAsset::Validate(composed);
        if (!prefab || !composed.PrefabInstances.empty() || !composed.PrefabOverrides.empty())
            throw std::invalid_argument("Prefab instantiation requires a fully composed prefab.");
        if (parent &&
            std::ranges::find(scene.Objects, parent, &Keire::SceneObjectDefinition::Id) == scene.Objects.end())
            throw std::invalid_argument("Prefab instance parent is unavailable.");

        const auto root = std::ranges::find(composed.Objects, Keire::AssetId{}, &Keire::SceneObjectDefinition::Parent);
        if (root == composed.Objects.end())
            throw std::invalid_argument("Prefab instantiation requires at least one root object.");
        const auto offset = position ? Keire::Vector3{position->X - root->Transform.Position.X,
                                                      position->Y - root->Transform.Position.Y,
                                                      position->Z - root->Transform.Position.Z}
                                     : Keire::Vector3{};
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

            if (!object.Parent && position)
            {
                copy.Transform.Position = {copy.Transform.Position.X + offset.X, copy.Transform.Position.Y + offset.Y,
                                           copy.Transform.Position.Z + offset.Z};
                instance.Overrides.push_back({.Kind = Keire::PrefabOverrideKind::SetObjectTransform,
                                              .Object = object.Id,
                                              .Transform = copy.Transform});
            }

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

    Keire::PrefabInstanceDefinition ConnectPrefabInstance(Keire::SceneDefinition& scene, const Keire::AssetId prefab,
                                                          const Keire::SceneDefinition& source,
                                                          const Keire::AssetId instanceRoot)
    {
        Keire::SceneAsset::Validate(scene);
        Keire::SceneAsset::Validate(source);
        if (!prefab || !instanceRoot || !source.PrefabInstances.empty() || !source.PrefabOverrides.empty())
            throw std::invalid_argument("Prefab connection requires a flat prefab source and a valid instance root.");
        const auto root = std::ranges::find(source.Objects, instanceRoot, &Keire::SceneObjectDefinition::Id);
        if (root == source.Objects.end() || root->Parent)
            throw std::invalid_argument("Prefab connection root is unavailable or is not a source root.");

        Keire::PrefabInstanceDefinition instance;
        instance.Prefab = prefab;
        instance.Root = instanceRoot;
        for (const auto& object : source.Objects)
        {
            if (std::ranges::find(scene.Objects, object.Id, &Keire::SceneObjectDefinition::Id) == scene.Objects.end())
                throw std::invalid_argument("Prefab connection source does not match the scene hierarchy.");
            const bool alreadyConnected = std::ranges::any_of(
                scene.PrefabInstances,
                [&](const Keire::PrefabInstanceDefinition& existing)
                {
                    return std::ranges::any_of(existing.Objects, [&](const Keire::PrefabObjectMapping& mapping)
                                               { return mapping.Instance == object.Id; });
                });
            if (alreadyConnected)
                throw std::invalid_argument("Unpack the existing prefab instance before creating a new prefab.");
            instance.Objects.push_back({object.Id, object.Id});
        }

        auto replacement = scene;
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

        // Every source root has scene-owned placement, including additional roots in a multi-root prefab.
        std::unordered_map<Keire::AssetId, Keire::SceneObjectDefinition> rootPlacements;
        for (const auto& object : composed.Objects)
        {
            if (object.Parent)
            {
                continue;
            }
            const auto mapping = std::ranges::find(instance->Objects, object.Id, &Keire::PrefabObjectMapping::Source);
            if (mapping == instance->Objects.end())
            {
                continue;
            }
            const auto current =
                std::ranges::find(replacement.Objects, mapping->Instance, &Keire::SceneObjectDefinition::Id);
            if (current != replacement.Objects.end())
            {
                rootPlacements.emplace(object.Id, *current);
            }
        }

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
            if (const auto placement = rootPlacements.find(object.Id); placement != rootPlacements.end())
            {
                copy.Parent = placement->second.Parent;
                copy.Transform = placement->second.Transform;
                const auto currentTransform =
                    std::ranges::find(placement->second.Components, Keire::TransformComponent::StaticType(),
                                      &Keire::SceneComponentDefinition::Type);
                const auto sourceTransform = std::ranges::find(copy.Components, Keire::TransformComponent::StaticType(),
                                                               &Keire::SceneComponentDefinition::Type);
                if (currentTransform != placement->second.Components.end() && sourceTransform != copy.Components.end())
                {
                    sourceTransform->Data = currentTransform->Data;
                }
            }
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
    namespace
    {
        [[nodiscard]] const Keire::SceneObjectDefinition* FindPrefabObject(const Keire::SceneDefinition& scene,
                                                                           const Keire::AssetId id)
        {
            const auto found = std::ranges::find(scene.Objects, id, &Keire::SceneObjectDefinition::Id);
            return found == scene.Objects.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool SamePrefabComponent(const Keire::SceneComponentDefinition& left,
                                               const Keire::SceneComponentDefinition& right)
        {
            return left.Type == right.Type && left.SchemaVersion == right.SchemaVersion &&
                   left.Enabled == right.Enabled && left.Data == right.Data;
        }

        [[nodiscard]] std::vector<Keire::PrefabOverrideDefinition>
        ComputePrefabOverrides(const Keire::SceneDefinition& baseline, const Keire::SceneDefinition& edited)
        {
            std::vector<Keire::PrefabOverrideDefinition> result;
            std::set<Keire::AssetId> baselineIds;
            std::set<Keire::AssetId> editedIds;
            for (const auto& object : baseline.Objects)
                baselineIds.insert(object.Id);
            for (const auto& object : edited.Objects)
                editedIds.insert(object.Id);

            for (const auto& object : baseline.Objects)
            {
                if (editedIds.contains(object.Id) || (object.Parent && !editedIds.contains(object.Parent)))
                    continue;
                result.push_back({.Kind = Keire::PrefabOverrideKind::RemoveObject, .Object = object.Id});
            }

            for (const auto& object : edited.Objects)
            {
                if (!baselineIds.contains(object.Id))
                {
                    Keire::PrefabOverrideDefinition added;
                    added.Kind = Keire::PrefabOverrideKind::AddObject;
                    added.AddedObject = object;
                    result.push_back(std::move(added));
                    continue;
                }

                const auto* original = FindPrefabObject(baseline, object.Id);
                if (object.Parent != original->Parent)
                    throw std::invalid_argument("Prefab apply cannot reparent an existing source object.");
                if (object.Name != original->Name)
                    result.push_back(
                        {.Kind = Keire::PrefabOverrideKind::RenameObject, .Object = object.Id, .Name = object.Name});
                if (object.Active != original->Active)
                    result.push_back({.Kind = Keire::PrefabOverrideKind::SetObjectActive,
                                      .Object = object.Id,
                                      .Active = object.Active});
                if (object.Layer != original->Layer)
                    result.push_back({.Kind = Keire::PrefabOverrideKind::SetObjectLayer,
                                      .Object = object.Id,
                                      .Layer = object.Layer});
                if (object.Tags != original->Tags)
                    result.push_back(
                        {.Kind = Keire::PrefabOverrideKind::SetObjectTags, .Object = object.Id, .Tags = object.Tags});
                if (object.Transform != original->Transform)
                    result.push_back({.Kind = Keire::PrefabOverrideKind::SetObjectTransform,
                                      .Object = object.Id,
                                      .Transform = object.Transform});

                for (const auto& component : original->Components)
                {
                    const auto current =
                        std::ranges::find(object.Components, component.Type, &Keire::SceneComponentDefinition::Type);
                    if (current == object.Components.end())
                    {
                        if (component.Type == Keire::TransformComponent::StaticType())
                            throw std::invalid_argument("Prefab apply cannot remove the required Transform component.");
                        result.push_back({.Kind = Keire::PrefabOverrideKind::RemoveComponent,
                                          .Object = object.Id,
                                          .Component = component.Type});
                    }
                    else if (!SamePrefabComponent(component, *current) &&
                             component.Type != Keire::TransformComponent::StaticType())
                    {
                        result.push_back({.Kind = Keire::PrefabOverrideKind::RemoveComponent,
                                          .Object = object.Id,
                                          .Component = component.Type});
                        Keire::PrefabOverrideDefinition replacement;
                        replacement.Kind = Keire::PrefabOverrideKind::AddComponent;
                        replacement.Object = object.Id;
                        replacement.AddedComponent = *current;
                        result.push_back(std::move(replacement));
                    }
                }
                for (const auto& component : object.Components)
                {
                    if (std::ranges::find(original->Components, component.Type,
                                          &Keire::SceneComponentDefinition::Type) != original->Components.end())
                        continue;
                    Keire::PrefabOverrideDefinition added;
                    added.Kind = Keire::PrefabOverrideKind::AddComponent;
                    added.Object = object.Id;
                    added.AddedComponent = component;
                    result.push_back(std::move(added));
                }
            }
            return result;
        }
    } // namespace

    Keire::PrefabDefinition UpdatePrefabFromEditingScene(const Keire::PrefabDefinition& source,
                                                         const Keire::SceneDefinition& edited,
                                                         const Keire::SceneDefinition* composedBase)
    {
        auto result = source;
        if (!source.BasePrefab)
        {
            if (!source.Template.PrefabInstances.empty())
                throw std::invalid_argument(
                    "A prefab containing nested instances must be modified through a variant to preserve ownership.");
            result.Template = edited;
            result.Template.PrefabInstances.clear();
            result.Template.PrefabOverrides.clear();
        }
        else
        {
            if (!composedBase)
                throw std::invalid_argument("Prefab variant editing requires its composed base.");
            if (!source.Template.PrefabInstances.empty())
                throw std::invalid_argument(
                    "A variant with locally nested instances cannot be flattened during prefab editing.");
            result.Template = Keire::SceneAsset::EmptyDefinition(source.Template.Name);
            result.Template.PrefabOverrides = ComputePrefabOverrides(*composedBase, edited);
        }
        Keire::PrefabAsset::Validate(result);
        return result;
    }

    PrefabSourceUpdate ApplyPrefabInstanceToSource(const Keire::SceneDefinition& scene,
                                                   const Keire::AssetId instanceRoot,
                                                   const Keire::PrefabDefinition& source,
                                                   const Keire::SceneDefinition& composedSource,
                                                   const Keire::SceneDefinition* composedBase)
    {
        const auto instance =
            std::ranges::find(scene.PrefabInstances, instanceRoot, &Keire::PrefabInstanceDefinition::Root);
        if (instance == scene.PrefabInstances.end())
            throw std::invalid_argument("The selected entity is not a prefab instance root.");

        std::unordered_map<Keire::AssetId, Keire::AssetId> instanceToSource;
        std::unordered_map<Keire::AssetId, Keire::AssetId> sourceToInstance;
        for (const auto& mapping : instance->Objects)
        {
            if (!instanceToSource.emplace(mapping.Instance, mapping.Source).second ||
                !sourceToInstance.emplace(mapping.Source, mapping.Instance).second)
                throw std::invalid_argument("Prefab instance mappings are ambiguous.");
        }
        for (const auto& object : composedSource.Objects)
            if (!sourceToInstance.contains(object.Id))
                throw std::invalid_argument(
                    "The prefab source changed incompatibly; revert or recreate the instance before applying.");

        std::set<Keire::AssetId> members{instanceRoot};
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& object : scene.Objects)
                if (members.contains(object.Parent) && members.insert(object.Id).second)
                    changed = true;
        }

        Keire::SceneDefinition normalized = Keire::SceneAsset::EmptyDefinition(composedSource.Name);
        std::vector<Keire::PrefabObjectMapping> mappings;
        for (const auto& object : scene.Objects)
        {
            if (!members.contains(object.Id))
                continue;
            auto normalizedObject = object;
            const auto mapped = instanceToSource.find(object.Id);
            const auto sourceId = mapped == instanceToSource.end() ? object.Id : mapped->second;
            if (mapped == instanceToSource.end() && FindPrefabObject(composedSource, sourceId))
                throw std::invalid_argument("A new prefab object collides with an existing source object ID.");
            normalizedObject.Id = sourceId;
            if (object.Id == instanceRoot)
            {
                normalizedObject.Parent = {};
                const auto* sourceRoot = FindPrefabObject(composedSource, sourceId);
                if (!sourceRoot)
                    throw std::invalid_argument("Prefab instance root mapping is unavailable.");
                normalizedObject.Transform = sourceRoot->Transform;
                const auto sourceTransform =
                    std::ranges::find(sourceRoot->Components, Keire::TransformComponent::StaticType(),
                                      &Keire::SceneComponentDefinition::Type);
                const auto currentTransform =
                    std::ranges::find(normalizedObject.Components, Keire::TransformComponent::StaticType(),
                                      &Keire::SceneComponentDefinition::Type);
                if (sourceTransform != sourceRoot->Components.end() &&
                    currentTransform != normalizedObject.Components.end())
                    *currentTransform = *sourceTransform;
            }
            else if (const auto parent = instanceToSource.find(object.Parent); parent != instanceToSource.end())
            {
                normalizedObject.Parent = parent->second;
            }
            else if (members.contains(object.Parent))
            {
                normalizedObject.Parent = object.Parent;
            }
            else
            {
                throw std::invalid_argument("Prefab apply cannot move an instance object outside its instance root.");
            }
            normalized.Objects.push_back(std::move(normalizedObject));
            mappings.push_back({sourceId, object.Id});
        }

        auto updatedScene = scene;
        auto updatedInstance =
            std::ranges::find(updatedScene.PrefabInstances, instanceRoot, &Keire::PrefabInstanceDefinition::Root);
        updatedInstance->Objects = std::move(mappings);
        updatedInstance->Overrides.clear();
        auto updatedPrefab = UpdatePrefabFromEditingScene(source, normalized, composedBase);
        Keire::SceneAsset::Validate(updatedScene);
        return {std::move(updatedPrefab), std::move(updatedScene)};
    }
    std::vector<PrefabOverrideReviewEntry> ReviewPrefabInstance(const Keire::SceneDefinition& scene,
                                                                const Keire::AssetId instanceRoot,
                                                                const Keire::SceneDefinition& composedSource)
    {
        // Normalize using the same path as Apply so review cannot silently omit live component edits.
        Keire::PrefabDefinition source;
        source.Template = composedSource;
        const auto update = ApplyPrefabInstanceToSource(scene, instanceRoot, source, composedSource);
        const auto& edited = update.Prefab.Template;
        std::vector<PrefabOverrideReviewEntry> result;
        for (const auto& original : composedSource.Objects)
        {
            if (!FindPrefabObject(edited, original.Id))
            {
                result.push_back({original.Id, original.Name, {}, "Object", "Present", "Removed"});
            }
        }
        for (const auto& object : edited.Objects)
        {
            const auto* original = FindPrefabObject(composedSource, object.Id);
            if (!original)
            {
                result.push_back({object.Id, object.Name, {}, "Object", "Absent", "Added"});
                continue;
            }
            const auto add = [&](const Keire::ComponentTypeId component, std::string property,
                                 const nlohmann::json& before, const nlohmann::json& after)
            {
                if (before != after)
                {
                    result.push_back(
                        {object.Id, object.Name, component, std::move(property), before.dump(), after.dump()});
                }
            };
            add({}, "Name", original->Name, object.Name);
            add({}, "Active", original->Active, object.Active);
            add({}, "Layer", original->Layer, object.Layer);
            add({}, "Tags", original->Tags, object.Tags);
            const auto vectorValue = [](const Keire::Vector3& value)
            { return nlohmann::json::array({value.X, value.Y, value.Z}); };
            const auto rotationValue = [](const Keire::Quaternion& value)
            { return nlohmann::json::array({value.X, value.Y, value.Z, value.W}); };
            const auto transform = Keire::TransformComponent::StaticType();
            add(transform, "Position", vectorValue(original->Transform.Position),
                vectorValue(object.Transform.Position));
            add(transform, "Rotation", rotationValue(original->Transform.Rotation),
                rotationValue(object.Transform.Rotation));
            add(transform, "Scale", vectorValue(original->Transform.Scale), vectorValue(object.Transform.Scale));
            for (const auto& component : original->Components)
            {
                if (std::ranges::find(object.Components, component.Type, &Keire::SceneComponentDefinition::Type) ==
                    object.Components.end())
                {
                    result.push_back({object.Id, object.Name, component.Type, "Component", "Present", "Removed"});
                }
            }
            for (const auto& component : object.Components)
            {
                const auto previous =
                    std::ranges::find(original->Components, component.Type, &Keire::SceneComponentDefinition::Type);
                if (previous == original->Components.end())
                {
                    result.push_back({object.Id, object.Name, component.Type, "Component", "Absent", "Added"});
                    continue;
                }
                add(component.Type, "Enabled", previous->Enabled, component.Enabled);
                add(component.Type, "Schema version", previous->SchemaVersion, component.SchemaVersion);
                if (component.Type == transform)
                {
                    continue;
                }
                const auto before = nlohmann::json::parse(previous->Data, nullptr, false);
                const auto after = nlohmann::json::parse(component.Data, nullptr, false);
                if (!before.is_object() || !after.is_object())
                {
                    add(component.Type, "Data", previous->Data, component.Data);
                    continue;
                }
                std::set<std::string> properties;
                for (const auto& [key, value] : before.items())
                {
                    properties.insert(key);
                }
                for (const auto& [key, value] : after.items())
                {
                    properties.insert(key);
                }
                for (const auto& property : properties)
                {
                    if (!before.contains(property) || !after.contains(property))
                    {
                        result.push_back({object.Id, object.Name, component.Type, property,
                                          before.contains(property) ? before.at(property).dump() : "<absent>",
                                          after.contains(property) ? after.at(property).dump() : "<absent>"});
                        continue;
                    }
                    add(component.Type, property, before.at(property), after.at(property));
                }
            }
        }
        return result;
    }
} // namespace KeireEditor
