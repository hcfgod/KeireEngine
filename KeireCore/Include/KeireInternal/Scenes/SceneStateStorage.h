#pragma once

#include "Keire/ECS/Components/TransformComponent.h"
#include "KeireInternal/SceneState.h"
#include "KeireInternal/Scenes/SceneHierarchyCache.h"
#include "KeireInternal/Scenes/SceneIdentityIndex.h"
#include "KeireInternal/Scenes/SceneSerialization.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <entt/entt.hpp>
#include <functional>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    class SceneState::Impl final
    {
      public:
        struct EntityRecord
        {
            EntityId Id;
            EntityId Parent;
            std::string Name;
            bool Active = true;
            std::uint32_t Layer = 0;
            std::vector<std::string> Tags;
            std::vector<Ref<Component>> Components;
            std::vector<SceneComponentDefinition> MissingComponents;
            mutable Matrix4 CachedWorld;
            mutable bool WorldDirty = true;
        };

        Impl(const AssetId asset, SceneDefinition definition, Ref<ComponentRegistry> components)
            : AssetValue(asset), ComponentsRegistry(std::move(components)), OwnerThread(std::this_thread::get_id())
        {
            if (!ComponentsRegistry)
                ComponentsRegistry = ComponentRegistry::CreateDefault();
            SceneAsset::Validate(definition);
            Name = std::move(definition.Name);
            PrefabInstances = std::move(definition.PrefabInstances);
            PrefabOverrides = std::move(definition.PrefabOverrides);
            Lighting = definition.Lighting;
            BakedLightingAsset = definition.BakedLighting;
            for (const auto& object : definition.Objects)
            {
                const auto native = Registry.create();
                EntityRecord record{EntityId(object.Id), EntityId(object.Parent),
                                    object.Name,         object.Active,
                                    object.Layer,        object.Tags};
                bool hasTransform = false;
                for (const auto& serialized : object.Components)
                {
                    const auto registration = ComponentsRegistry->Find(serialized.Type);
                    if (!registration)
                    {
                        record.MissingComponents.push_back(serialized);
                        continue;
                    }
                    auto component = registration->Factory();
                    auto values = DecodeComponentPropertyBag(serialized.Data, *registration);
                    if (serialized.SchemaVersion != registration->SchemaVersion)
                    {
                        if (!registration->Migrate)
                            throw std::invalid_argument("Component requires an unavailable schema migration.");
                        values = registration->Migrate(values, serialized.SchemaVersion);
                    }
                    registration->Deserialize(*component, values, registration->SchemaVersion);
                    component->ApplyEnabled(serialized.Enabled);
                    hasTransform |= serialized.Type == TransformComponent::StaticType();
                    record.Components.push_back(std::move(component));
                }
                if (!hasTransform)
                {
                    const auto registration = ComponentsRegistry->Find(TransformComponent::StaticType());
                    auto transform = registration->Factory();
                    registration->Deserialize(
                        *transform, DecodeComponentPropertyBag(EncodeLegacyTransform(object.Transform), *registration),
                        registration->SchemaVersion);
                    record.Components.insert(record.Components.begin(), std::move(transform));
                }
                Registry.emplace<EntityRecord>(native, std::move(record));
                Entities.emplace(EntityId(object.Id), native);
                Order.push_back(EntityId(object.Id));
            }
        }

        [[nodiscard]] EntityRecord* Find(const EntityId id) noexcept
        {
            const auto found = Entities.find(id);
            return found == Entities.end() ? nullptr : Registry.try_get<EntityRecord>(found->second);
        }

        [[nodiscard]] const EntityRecord* Find(const EntityId id) const noexcept
        {
            const auto found = Entities.find(id);
            return found == Entities.end() ? nullptr : Registry.try_get<EntityRecord>(found->second);
        }

        [[nodiscard]] EntityId ParentOf(const EntityId id) const noexcept
        {
            const auto* record = Find(id);
            return record ? record->Parent : EntityId{};
        }

        [[nodiscard]] const std::vector<EntityId>& HierarchyOrder() const
        {
            return Hierarchy.Ordered(Order, [this](const EntityId id) { return ParentOf(id); });
        }

        [[nodiscard]] bool DescendsFrom(EntityId candidate, const EntityId ancestor) const noexcept
        {
            while (candidate)
            {
                if (candidate == ancestor)
                    return true;
                const auto* current = Find(candidate);
                if (!current)
                    return false;
                candidate = current->Parent;
            }
            return false;
        }

        void MarkWorldDirty(const EntityId root)
        {
            if (const auto* record = Find(root); !record || record->WorldDirty)
                return;
            Hierarchy.VisitSubtree(
                root, Order, [this](const EntityId id) { return ParentOf(id); },
                [this](const EntityId id) { Find(id)->WorldDirty = true; });
        }

        [[nodiscard]] Ref<TransformComponent> Transform(const EntityId id) const noexcept
        {
            const auto* record = Find(id);
            if (!record)
                return {};
            const auto found = std::ranges::find_if(record->Components, [](const auto& component)
                                                    { return component->Type() == TransformComponent::StaticType(); });
            return found == record->Components.end() ? Ref<TransformComponent>{}
                                                     : DynamicRefCast<TransformComponent>(*found);
        }

        void IndexComponent(const EntityId owner, const Ref<Component>& component)
        {
            ComponentPools[component->Type()][owner].push_back(component);
            LifecycleComponentsDirty = true;
        }

        void UnindexComponent(const EntityId owner, const Ref<Component>& component)
        {
            const auto pool = ComponentPools.find(component->Type());
            if (pool == ComponentPools.end())
                return;
            const auto entity = pool->second.find(owner);
            if (entity == pool->second.end())
                return;
            std::erase(entity->second, component);
            if (entity->second.empty())
                pool->second.erase(entity);
            if (pool->second.empty())
                ComponentPools.erase(pool);
            LifecycleComponentsDirty = true;
        }

        void IndexIdentity(const EntityId id, const EntityRecord& record)
        {
            NameIndex[record.Name].insert(id);
            for (const auto& tag : record.Tags)
                TagIndex[tag].insert(id);
        }

        void UnindexIdentity(const EntityId id, const EntityRecord& record)
        {
            const auto name = NameIndex.find(record.Name);
            if (name != NameIndex.end())
            {
                name->second.erase(id);
                if (name->second.empty())
                    NameIndex.erase(name);
            }
            for (const auto& tag : record.Tags)
            {
                const auto found = TagIndex.find(tag);
                if (found == TagIndex.end())
                    continue;
                found->second.erase(id);
                if (found->second.empty())
                    TagIndex.erase(found);
            }
        }

        [[nodiscard]] const std::vector<Ref<Component>>& LifecycleComponents() const
        {
            if (!LifecycleComponentsDirty)
                return CachedLifecycleComponents;
            CachedLifecycleComponents.clear();
            for (const auto id : HierarchyOrder())
                if (const auto* record = Find(id))
                    CachedLifecycleComponents.insert(CachedLifecycleComponents.end(), record->Components.begin(),
                                                     record->Components.end());
            std::ranges::stable_sort(CachedLifecycleComponents,
                                     [&](const auto& left, const auto& right)
                                     {
                                         return ComponentsRegistry->Find(left->Type())->ExecutionOrder <
                                                ComponentsRegistry->Find(right->Type())->ExecutionOrder;
                                     });
            LifecycleComponentsDirty = false;
            return CachedLifecycleComponents;
        }

        template <typename Callback> void Traverse(Callback&& callback)
        {
            ++TraversalDepth;
            try
            {
                std::forward<Callback>(callback)();
            }
            catch (...)
            {
                --TraversalDepth;
                throw;
            }
            --TraversalDepth;
        }

        AssetId AssetValue;
        std::string Name;
        std::vector<PrefabInstanceDefinition> PrefabInstances;
        std::vector<PrefabOverrideDefinition> PrefabOverrides;
        LightingBakeSettings Lighting;
        AssetId BakedLightingAsset;
        Ref<ComponentRegistry> ComponentsRegistry;
        std::thread::id OwnerThread;
        entt::registry Registry;
        std::unordered_map<EntityId, entt::entity> Entities;
        SceneIdentityIndex NameIndex;
        SceneIdentityIndex TagIndex;
        std::unordered_map<ComponentTypeId, std::unordered_map<EntityId, std::vector<Ref<Component>>>> ComponentPools;
        std::vector<EntityId> Order;
        SceneHierarchyCache Hierarchy;
        WeakRef<SceneState> Self;
        std::vector<std::function<void()>> Deferred;
        std::set<EntityId> PendingDestroyedEntities;
        std::map<EntityId, double> DelayedDestruction;
        double DestructionTime = 0.0;
        std::set<std::pair<EntityId, ComponentTypeId>> PendingDestroyedComponents;
        mutable std::vector<Ref<Component>> CachedLifecycleComponents;
        std::size_t TraversalDepth = 0;
        mutable bool LifecycleComponentsDirty = true;
        bool Open = true;
        bool Dirty = false;
        bool Playing = false;
    };

} // namespace Keire::Detail
