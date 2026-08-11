#include "Keire/Scenes/Scene.h"

#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "KeireInternal/SceneState.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] Json EncodeProperty(const ComponentPropertyValue& value)
        {
            return std::visit(
                [](const auto& current) -> Json
                {
                    using T = std::remove_cvref_t<decltype(current)>;
                    if constexpr (std::same_as<T, Vector2>)
                        return Json::array({current.X, current.Y});
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json::array({current.X, current.Y, current.Z});
                    else if constexpr (std::same_as<T, Vector4>)
                        return Json::array({current.X, current.Y, current.Z, current.W});
                    else if constexpr (std::same_as<T, Quaternion>)
                        return Json::array({current.X, current.Y, current.Z, current.W});
                    else if constexpr (std::same_as<T, Color>)
                        return Json::array({current.Red, current.Green, current.Blue, current.Alpha});
                    else if constexpr (std::same_as<T, AssetId> || std::same_as<T, EntityId>)
                        return current ? Json(current.ToString()) : Json(nullptr);
                    else if constexpr (std::same_as<T, ComponentEventValue>)
                    {
                        Json listeners = Json::array();
                        for (const auto& listener : current.Listeners)
                        {
                            listeners.push_back(
                                {{"enabled", listener.Enabled},
                                 {"target", listener.Target ? Json(listener.Target.ToString()) : Json(nullptr)},
                                 {"component",
                                  listener.Component ? Json(listener.Component.ToString()) : Json(nullptr)},
                                 {"method", listener.Method}});
                        }
                        return listeners;
                    }
                    else if constexpr (std::same_as<T, Curve1D>)
                    {
                        Json keys = Json::array();
                        for (const auto& key : current.Keys())
                        {
                            keys.push_back({{"time", key.Time},
                                            {"value", key.Value},
                                            {"inTangent", key.InTangent},
                                            {"outTangent", key.OutTangent},
                                            {"interpolation", static_cast<std::uint8_t>(key.Interpolation)}});
                        }
                        return keys;
                    }
                    else if constexpr (std::same_as<T, ColorGradient>)
                    {
                        Json keys = Json::array();
                        for (const auto& key : current.Keys())
                        {
                            keys.push_back(
                                {{"time", key.Time},
                                 {"color", {key.Value.Red, key.Value.Green, key.Value.Blue, key.Value.Alpha}}});
                        }
                        return Json{{"interpolation", static_cast<std::uint8_t>(current.Interpolation())},
                                    {"keys", std::move(keys)}};
                    }
                    else
                        return Json(current);
                },
                value);
        }

        [[nodiscard]] ComponentPropertyValue DecodeProperty(const Json& value, const ComponentPropertyKind kind)
        {
            const auto requireArray = [&](const std::size_t size)
            {
                if (!value.is_array() || value.size() != size)
                    throw std::invalid_argument("Component vector property has an invalid shape.");
            };
            switch (kind)
            {
            case ComponentPropertyKind::Boolean:
                return value.get<bool>();
            case ComponentPropertyKind::Integer:
                return value.get<std::int64_t>();
            case ComponentPropertyKind::Scalar:
                return value.get<double>();
            case ComponentPropertyKind::Text:
                return value.get<std::string>();
            case ComponentPropertyKind::Vector2:
                requireArray(2);
                return Vector2{value[0].get<float>(), value[1].get<float>()};
            case ComponentPropertyKind::Vector3:
                requireArray(3);
                return Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            case ComponentPropertyKind::Vector4:
                requireArray(4);
                return Vector4{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                               value[3].get<float>()};
            case ComponentPropertyKind::Quaternion:
                requireArray(4);
                return Quaternion{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                                  value[3].get<float>()};
            case ComponentPropertyKind::Color:
                requireArray(4);
                return Color{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                             value[3].get<float>()};
            case ComponentPropertyKind::Asset:
                return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
            case ComponentPropertyKind::Entity:
                return value.is_null() ? EntityId{} : EntityId::Parse(value.get<std::string>());
            case ComponentPropertyKind::Event:
            {
                if (!value.is_array())
                    throw std::runtime_error("Component event property must be an array.");
                ComponentEventValue result;
                result.Listeners.reserve(value.size());
                for (const auto& serialized : value)
                {
                    if (!serialized.is_object())
                        throw std::runtime_error("Component event listener must be an object.");
                    ComponentEventListener listener;
                    listener.Enabled = serialized.value("enabled", true);
                    if (const auto found = serialized.find("target"); found != serialized.end() && !found->is_null())
                        listener.Target = EntityId::Parse(found->get<std::string>());
                    if (const auto found = serialized.find("component"); found != serialized.end() && !found->is_null())
                    {
                        listener.Component = ComponentTypeId::Parse(found->get<std::string>());
                    }
                    listener.Method = serialized.value("method", std::string{});
                    result.Listeners.push_back(std::move(listener));
                }
                return result;
            }
            case ComponentPropertyKind::Curve:
            {
                if (!value.is_array())
                    throw std::runtime_error("Component curve property must be an array.");
                std::vector<CurveKey> keys;
                keys.reserve(value.size());
                for (const auto& serialized : value)
                {
                    if (!serialized.is_object())
                        throw std::runtime_error("Component curve key must be an object.");
                    const auto interpolation = serialized.value("interpolation", std::uint8_t{1});
                    if (interpolation > static_cast<std::uint8_t>(CurveInterpolation::Cubic))
                        throw std::runtime_error("Component curve interpolation is invalid.");
                    keys.push_back({serialized.at("time").get<float>(), serialized.at("value").get<float>(),
                                    serialized.value("inTangent", 0.0F), serialized.value("outTangent", 0.0F),
                                    static_cast<CurveInterpolation>(interpolation)});
                }
                return Curve1D(std::move(keys));
            }
            case ComponentPropertyKind::Gradient:
            {
                if (!value.is_object())
                    throw std::runtime_error("Component gradient property must be an object.");
                const auto interpolation = value.value("interpolation", std::uint8_t{1});
                if (interpolation > static_cast<std::uint8_t>(GradientInterpolation::Linear))
                    throw std::runtime_error("Component gradient interpolation is invalid.");
                const auto& serializedKeys = value.at("keys");
                if (!serializedKeys.is_array())
                    throw std::runtime_error("Component gradient keys must be an array.");
                std::vector<ColorGradientKey> keys;
                keys.reserve(serializedKeys.size());
                for (const auto& serialized : serializedKeys)
                {
                    if (!serialized.is_object())
                        throw std::runtime_error("Component gradient key must be an object.");
                    const auto& color = serialized.at("color");
                    if (!color.is_array() || color.size() != 4)
                        throw std::runtime_error("Component gradient color must contain four channels.");
                    keys.push_back(
                        {serialized.at("time").get<float>(),
                         {color[0].get<float>(), color[1].get<float>(), color[2].get<float>(), color[3].get<float>()}});
                }
                return ColorGradient(std::move(keys), static_cast<GradientInterpolation>(interpolation));
            }
            }
            throw std::invalid_argument("Unsupported component property kind.");
        }

        [[nodiscard]] std::string EncodeBag(const ComponentPropertyBag& bag)
        {
            Json object = Json::object();
            for (const auto& [key, value] : bag)
                object[key] = EncodeProperty(value);
            return object.dump();
        }

        [[nodiscard]] ComponentPropertyBag DecodeBag(const std::string_view data,
                                                     const ComponentRegistration& registration)
        {
            const auto object = Json::parse(data);
            if (!object.is_object())
                throw std::invalid_argument("Component data must be a JSON object.");
            ComponentPropertyBag result;
            for (const auto& property : registration.Properties)
            {
                if (const auto found = object.find(property.Key); found != object.end())
                    result.emplace(property.Key, DecodeProperty(*found, property.Kind));
            }
            if (const auto found = object.find("managedState"); found != object.end())
            {
                if (!found->is_string())
                    throw std::invalid_argument("Managed component state must be text.");
                result.insert_or_assign("managedState", found->get<std::string>());
            }
            return result;
        }

        [[nodiscard]] std::string LegacyTransformData(const SceneTransform& transform)
        {
            return EncodeBag(
                {{"position", transform.Position}, {"rotation", transform.Rotation}, {"scale", transform.Scale}});
        }
    } // namespace

    namespace Detail
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
                    EntityRecord record{EntityId(object.Id), EntityId(object.Parent), object.Name, object.Active,
                                        object.Layer};
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
                        auto values = DecodeBag(serialized.Data, *registration);
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
                        registration->Deserialize(*transform,
                                                  DecodeBag(LegacyTransformData(object.Transform), *registration),
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

            [[nodiscard]] std::vector<EntityId> HierarchyOrder() const
            {
                std::unordered_map<EntityId, std::vector<EntityId>> children;
                std::vector<EntityId> roots;
                for (const auto id : Order)
                {
                    const auto* record = Find(id);
                    if (!record)
                        continue;
                    if (record->Parent)
                        children[record->Parent].push_back(id);
                    else
                        roots.push_back(id);
                }
                std::vector<EntityId> result;
                result.reserve(Entities.size());
                const auto append = [&](const auto& self, const EntityId id) -> void
                {
                    result.push_back(id);
                    if (const auto found = children.find(id); found != children.end())
                        for (const auto child : found->second)
                            self(self, child);
                };
                for (const auto root : roots)
                    append(append, root);
                return result;
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
                if (auto* record = Find(root))
                    record->WorldDirty = true;
                for (const auto id : Order)
                    if (id != root && DescendsFrom(id, root))
                        if (auto* child = Find(id))
                            child->WorldDirty = true;
            }

            [[nodiscard]] Ref<TransformComponent> Transform(const EntityId id) const noexcept
            {
                const auto* record = Find(id);
                if (!record)
                    return {};
                const auto found =
                    std::ranges::find_if(record->Components, [](const auto& component)
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
            std::unordered_map<ComponentTypeId, std::unordered_map<EntityId, std::vector<Ref<Component>>>>
                ComponentPools;
            std::vector<EntityId> Order;
            WeakRef<SceneState> Self;
            std::vector<std::function<void()>> Deferred;
            mutable std::vector<Ref<Component>> CachedLifecycleComponents;
            std::size_t TraversalDepth = 0;
            mutable bool LifecycleComponentsDirty = true;
            bool Open = true;
            bool Dirty = false;
            bool Playing = false;
        };

        SceneState::SceneState(const AssetId asset, SceneDefinition definition, Ref<ComponentRegistry> components)
            : m_Impl(std::make_unique<Impl>(asset, std::move(definition), std::move(components)))
        {
        }

        SceneState::~SceneState() { Close(); }

        void SceneState::Initialize(WeakRef<SceneState> self)
        {
            m_Impl->Self = std::move(self);
            for (const auto id : m_Impl->Order)
                if (auto* record = m_Impl->Find(id))
                    for (const auto& component : record->Components)
                    {
                        component->Attach(m_Impl->Self, id);
                        m_Impl->IndexComponent(id, component);
                    }
            for (const auto id : m_Impl->Order)
                SynchronizeEntityLayer(id);
        }

        void SceneState::RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != m_Impl->OwnerThread)
                throw std::logic_error(std::string("Scene::") + operation + " must run on the owner thread.");
            if (!m_Impl->Open)
                throw std::logic_error(std::string("Scene::") + operation + " cannot run after Close.");
        }

        AssetId SceneState::Asset() const noexcept { return m_Impl->AssetValue; }
        bool SceneState::IsOpen() const noexcept { return m_Impl->Open; }
        bool SceneState::Dirty() const noexcept { return m_Impl->Dirty; }

        void SceneState::MarkDirty() noexcept
        {
            if (m_Impl->Open && std::this_thread::get_id() == m_Impl->OwnerThread)
                m_Impl->Dirty = true;
        }

        void SceneState::MarkSaved() noexcept
        {
            if (m_Impl->Open && std::this_thread::get_id() == m_Impl->OwnerThread)
                m_Impl->Dirty = false;
        }

        std::string SceneState::Name() const
        {
            RequireOwner("Name");
            return m_Impl->Name;
        }

        void SceneState::SetName(std::string name)
        {
            RequireOwner("SetName");
            if (name.empty() || name.size() > 256)
                throw std::invalid_argument("Scene name is empty or exceeds 256 UTF-8 bytes.");
            m_Impl->Name = std::move(name);
            m_Impl->Dirty = true;
        }

        bool SceneState::Contains(const EntityId id) const noexcept
        {
            return m_Impl->Open && m_Impl->Entities.contains(id);
        }

        std::size_t SceneState::Count() const noexcept { return m_Impl->Open ? m_Impl->Entities.size() : 0; }

        std::optional<SceneObjectDefinition> SceneState::SnapshotObject(const EntityId id) const
        {
            RequireOwner("SnapshotObject");
            const auto definition = Snapshot();
            const auto found = std::ranges::find(definition.Objects, id.Value(), &SceneObjectDefinition::Id);
            return found == definition.Objects.end() ? std::nullopt : std::optional<SceneObjectDefinition>(*found);
        }

        SceneDefinition SceneState::Snapshot() const
        {
            RequireOwner("Snapshot");
            SceneDefinition result{.SchemaVersion = CurrentSceneSchemaVersion,
                                   .Name = m_Impl->Name,
                                   .PrefabInstances = m_Impl->PrefabInstances,
                                   .PrefabOverrides = m_Impl->PrefabOverrides,
                                   .Lighting = m_Impl->Lighting,
                                   .BakedLighting = m_Impl->BakedLightingAsset};
            result.Objects.reserve(m_Impl->Entities.size());
            for (const auto id : m_Impl->HierarchyOrder())
            {
                const auto* record = m_Impl->Find(id);
                SceneObjectDefinition object{id.Value(), record->Parent.Value(), record->Name, record->Active};
                object.Layer = record->Layer;
                object.Components.reserve(record->Components.size() + record->MissingComponents.size());
                for (const auto& component : record->Components)
                {
                    const auto registration = m_Impl->ComponentsRegistry->Find(component->Type());
                    object.Components.push_back({component->Type(), registration->SchemaVersion, component->Enabled(),
                                                 EncodeBag(registration->Serialize(*component))});
                    if (component->Type() == TransformComponent::StaticType())
                    {
                        const auto transform = DynamicRefCast<TransformComponent>(component);
                        object.Transform = {transform->LocalPosition(), transform->LocalRotation(),
                                            transform->LocalScale()};
                    }
                }
                object.Components.insert(object.Components.end(), record->MissingComponents.begin(),
                                         record->MissingComponents.end());
                result.Objects.push_back(std::move(object));
            }
            return result;
        }

        LightingBakeSettings SceneState::LightingBakeConfiguration() const
        {
            RequireOwner("LightingBakeConfiguration");
            return m_Impl->Lighting;
        }

        void SceneState::SetLightingBakeConfiguration(const LightingBakeSettings settings)
        {
            RequireOwner("SetLightingBakeConfiguration");
            auto definition = Snapshot();
            definition.Lighting = settings;
            SceneAsset::Validate(definition);
            m_Impl->Lighting = settings;
            m_Impl->Dirty = true;
        }

        AssetId SceneState::BakedLighting() const
        {
            RequireOwner("BakedLighting");
            return m_Impl->BakedLightingAsset;
        }

        void SceneState::SetBakedLighting(const AssetId asset)
        {
            RequireOwner("SetBakedLighting");
            m_Impl->BakedLightingAsset = asset;
            m_Impl->Dirty = true;
        }

        SceneHierarchySnapshot SceneState::HierarchySnapshot() const
        {
            RequireOwner("HierarchySnapshot");
            SceneHierarchySnapshot result{.PrefabInstances = m_Impl->PrefabInstances};
            result.Objects.reserve(m_Impl->Entities.size());
            for (const auto id : m_Impl->HierarchyOrder())
            {
                const auto* record = m_Impl->Find(id);
                auto& object =
                    result.Objects.emplace_back(id.Value(), record->Parent.Value(), record->Name, record->Active);
                object.Layer = record->Layer;
            }
            return result;
        }

        std::vector<Entity> SceneState::Entities() const
        {
            RequireOwner("Entities");
            std::vector<Entity> result;
            result.reserve(m_Impl->Entities.size());
            for (const auto id : m_Impl->HierarchyOrder())
                result.push_back(Entity(m_Impl->Self, id));
            return result;
        }

        Entity SceneState::Find(const EntityId id) const noexcept
        {
            return Contains(id) ? Entity(m_Impl->Self, id) : Entity{};
        }

        Entity SceneState::Create(std::string name, const EntityId parent)
        {
            RequireOwner("CreateEntity");
            if (name.empty() || name.size() > 256)
                throw std::invalid_argument("Entity name is empty or exceeds 256 UTF-8 bytes.");
            if (parent && !Contains(parent))
                throw std::invalid_argument("Entity parent does not exist.");
            const auto id = EntityId::Generate();
            const auto transformRegistration = m_Impl->ComponentsRegistry->Find(TransformComponent::StaticType());
            auto transform = transformRegistration->Factory();
            auto commit = [this, id, parent, name = std::move(name), transform]() mutable
            {
                const auto native = m_Impl->Registry.create();
                typename Impl::EntityRecord record{id, parent, std::move(name), true};
                transform->Attach(m_Impl->Self, id);
                record.Components.push_back(transform);
                m_Impl->Registry.emplace<typename Impl::EntityRecord>(native, std::move(record));
                m_Impl->Entities.emplace(id, native);
                m_Impl->IndexComponent(id, transform);
                m_Impl->Order.push_back(id);
                m_Impl->Dirty = true;
                if (m_Impl->Playing)
                {
                    transform->InvokeAwake();
                    if (ActiveInHierarchy(id) && transform->Enabled())
                        transform->InvokeEnable();
                }
            };
            if (m_Impl->TraversalDepth)
                m_Impl->Deferred.push_back(commit);
            else
                commit();
            return Entity(m_Impl->Self, id);
        }

        Entity SceneState::Duplicate(const EntityId id)
        {
            RequireOwner("DuplicateEntity");
            if (!Contains(id))
                return {};
            const auto snapshot = Snapshot();
            std::unordered_map<EntityId, EntityId> remapped;
            EntityId rootCopy;
            for (const auto& object : snapshot.Objects)
            {
                const EntityId original(object.Id);
                if (!m_Impl->DescendsFrom(original, id))
                    continue;
                const auto copy =
                    Create(original == id ? object.Name + " Copy" : object.Name,
                           remapped.contains(EntityId(object.Parent)) ? remapped.at(EntityId(object.Parent))
                                                                      : EntityId(object.Parent));
                if (original == id)
                    rootCopy = copy.Id();
                remapped.emplace(original, copy.Id());
                SetActive(copy.Id(), object.Active);
                SetEntityLayer(copy.Id(), object.Layer);
                auto transform = copy.GetComponent<TransformComponent>();
                transform->SetLocalPosition(object.Transform.Position);
                transform->SetLocalRotation(object.Transform.Rotation);
                transform->SetLocalScale(object.Transform.Scale);
                for (const auto& serialized : object.Components)
                {
                    if (serialized.Type == TransformComponent::StaticType())
                        continue;
                    const auto registration = m_Impl->ComponentsRegistry->Find(serialized.Type);
                    if (!registration)
                    {
                        m_Impl->Find(copy.Id())->MissingComponents.push_back(serialized);
                        continue;
                    }
                    auto component = AddComponent(copy.Id(), serialized.Type);
                    registration->Deserialize(*component, DecodeBag(serialized.Data, *registration),
                                              serialized.SchemaVersion);
                    SetComponentEnabled(*component, serialized.Enabled);
                }
                // The entity field is canonical. Legacy component payloads may still carry an older layer bit.
                SetEntityLayer(copy.Id(), object.Layer);
            }
            return Find(rootCopy);
        }

        bool SceneState::Destroy(const EntityId id)
        {
            RequireOwner("DestroyEntity");
            if (!Contains(id))
                return false;
            const auto commit = [this, id]
            {
                std::vector<EntityId> removed;
                for (const auto candidate : m_Impl->Order)
                    if (m_Impl->DescendsFrom(candidate, id))
                        removed.push_back(candidate);
                const auto removes = [&](const AssetId object)
                { return std::ranges::find(removed, EntityId(object)) != removed.end(); };
                std::erase_if(m_Impl->PrefabInstances,
                              [&](PrefabInstanceDefinition& instance)
                              {
                                  if (removes(instance.Root))
                                      return true;
                                  std::vector<AssetId> removedSources;
                                  std::erase_if(instance.Objects,
                                                [&](const PrefabObjectMapping& mapping)
                                                {
                                                    if (!removes(mapping.Instance))
                                                        return false;
                                                    removedSources.push_back(mapping.Source);
                                                    return true;
                                                });
                                  std::erase_if(instance.Overrides,
                                                [&](const PrefabOverrideDefinition& overrideValue)
                                                {
                                                    return std::ranges::find(removedSources, overrideValue.Object) !=
                                                           removedSources.end();
                                                });
                                  return instance.Objects.empty();
                              });
                std::erase_if(m_Impl->PrefabOverrides, [&](const PrefabOverrideDefinition& overrideValue)
                              { return removes(overrideValue.Object); });
                std::ranges::reverse(removed);
                for (const auto current : removed)
                {
                    auto* record = m_Impl->Find(current);
                    for (const auto& component : record->Components)
                    {
                        component->InvokeDestroy();
                        m_Impl->UnindexComponent(current, component);
                    }
                    const auto native = m_Impl->Entities.at(current);
                    m_Impl->Registry.destroy(native);
                    m_Impl->Entities.erase(current);
                    std::erase(m_Impl->Order, current);
                }
                m_Impl->Dirty = true;
            };
            if (m_Impl->TraversalDepth)
                m_Impl->Deferred.push_back(commit);
            else
                commit();
            return true;
        }

        std::vector<Entity> SceneState::Query(const ComponentTypeId type) const
        {
            RequireOwner("Query");
            if (!type)
                throw std::invalid_argument("Component query type ID must not be empty.");
            const auto pool = m_Impl->ComponentPools.find(type);
            if (pool == m_Impl->ComponentPools.end())
                return {};
            std::vector<Entity> result;
            result.reserve(pool->second.size());
            for (const auto id : m_Impl->HierarchyOrder())
                if (pool->second.contains(id))
                    result.push_back(Entity(m_Impl->Self, id));
            return result;
        }

        std::string SceneState::EntityName(const EntityId id) const
        {
            RequireOwner("EntityName");
            const auto* record = m_Impl->Find(id);
            if (!record)
                throw std::logic_error("Entity is stale.");
            return record->Name;
        }

        void SceneState::SetEntityName(const EntityId id, std::string name)
        {
            RequireOwner("SetEntityName");
            if (name.empty() || name.size() > 256)
                throw std::invalid_argument("Entity name is empty or exceeds 256 UTF-8 bytes.");
            auto* record = m_Impl->Find(id);
            if (!record)
                throw std::logic_error("Entity is stale.");
            record->Name = std::move(name);
            m_Impl->Dirty = true;
        }

        std::uint32_t SceneState::EntityLayer(const EntityId id) const
        {
            RequireOwner("EntityLayer");
            const auto* record = m_Impl->Find(id);
            if (!record)
                throw std::logic_error("Entity is stale.");
            return record->Layer;
        }

        void SceneState::SynchronizeEntityLayer(const EntityId id)
        {
            auto* record = m_Impl->Find(id);
            if (!record)
                return;
            const auto collisionLayer = EntityLayerBit(record->Layer);
            for (const auto& component : record->Components)
            {
                if (component->Type() == ColliderComponent::StaticType())
                    DynamicRefCast<ColliderComponent>(component)->ApplyEntityLayer(collisionLayer);
                else if (component->Type() == CharacterControllerComponent::StaticType())
                    DynamicRefCast<CharacterControllerComponent>(component)->ApplyEntityLayer(collisionLayer);
            }
        }

        void SceneState::SetEntityLayer(const EntityId id, const std::uint32_t layer)
        {
            RequireOwner("SetEntityLayer");
            if (!IsValidEntityLayer(layer))
                throw std::invalid_argument("Entity layer must be between 0 and 31.");
            auto* record = m_Impl->Find(id);
            if (!record)
                throw std::logic_error("Entity is stale.");
            if (record->Layer == layer)
                return;
            record->Layer = layer;
            SynchronizeEntityLayer(id);
            m_Impl->Dirty = true;
        }

        bool SceneState::ActiveSelf(const EntityId id) const
        {
            RequireOwner("ActiveSelf");
            const auto* record = m_Impl->Find(id);
            return record && record->Active;
        }

        bool SceneState::ActiveInHierarchy(EntityId id) const
        {
            RequireOwner("ActiveInHierarchy");
            while (id)
            {
                const auto* record = m_Impl->Find(id);
                if (!record || !record->Active)
                    return false;
                id = record->Parent;
            }
            return true;
        }

        void SceneState::SetActive(const EntityId id, const bool active)
        {
            RequireOwner("SetActive");
            auto* record = m_Impl->Find(id);
            if (!record)
                throw std::logic_error("Entity is stale.");
            if (record->Active == active)
                return;
            record->Active = active;
            m_Impl->Dirty = true;
            if (m_Impl->Playing)
            {
                for (const auto candidate : m_Impl->HierarchyOrder())
                {
                    if (!m_Impl->DescendsFrom(candidate, id))
                        continue;
                    const bool shouldEnable = ActiveInHierarchy(candidate);
                    for (const auto& component : m_Impl->Find(candidate)->Components)
                    {
                        if (shouldEnable && component->Enabled())
                            component->InvokeEnable();
                        else
                            component->InvokeDisable();
                    }
                }
            }
        }

        Entity SceneState::Parent(const EntityId id) const noexcept
        {
            const auto* record = m_Impl->Find(id);
            return record && record->Parent ? Entity(m_Impl->Self, record->Parent) : Entity{};
        }

        std::vector<Entity> SceneState::Children(const EntityId id) const
        {
            RequireOwner("Children");
            std::vector<Entity> result;
            for (const auto candidate : m_Impl->Order)
                if (const auto* record = m_Impl->Find(candidate); record && record->Parent == id)
                    result.push_back(Entity(m_Impl->Self, candidate));
            return result;
        }

        void SceneState::SetParent(const EntityId id, const EntityId parent, const bool preserveWorldTransform)
        {
            RequireOwner("SetParent");
            auto* record = m_Impl->Find(id);
            if (!record || (parent && !Contains(parent)))
                throw std::invalid_argument("Entity or requested parent does not exist.");
            if (parent && m_Impl->DescendsFrom(parent, id))
                throw std::invalid_argument("An entity cannot be parented to itself or one of its descendants.");
            if (record->Parent == parent)
                return;
            const auto previousWorld = WorldMatrix(id);
            const auto previousParent = record->Parent;
            record->Parent = parent;
            m_Impl->LifecycleComponentsDirty = true;
            m_Impl->MarkWorldDirty(id);
            if (preserveWorldTransform)
            {
                const auto local =
                    parent ? Math::Multiply(Math::Inverse(WorldMatrix(parent)), previousWorld) : previousWorld;
                Vector3 position;
                Quaternion rotation;
                Vector3 scale;
                if (!Math::DecomposeTransform(local, position, rotation, scale))
                {
                    record->Parent = previousParent;
                    m_Impl->MarkWorldDirty(id);
                    throw std::invalid_argument("Reparenting would produce a non-decomposable local transform.");
                }
                const auto transform = m_Impl->Transform(id);
                transform->SetLocalPosition(position);
                transform->SetLocalRotation(rotation);
                transform->SetLocalScale(scale);
            }
            m_Impl->Dirty = true;
        }

        void SceneState::Move(const EntityId id, const EntityId parent, const EntityId beforeSibling,
                              const bool preserveWorldTransform)
        {
            RequireOwner("Move");
            const auto* record = m_Impl->Find(id);
            const auto* sibling = beforeSibling ? m_Impl->Find(beforeSibling) : nullptr;
            if (!record || (parent && !Contains(parent)) || (beforeSibling && !sibling))
                throw std::invalid_argument("Entity, requested parent, or sibling does not exist.");
            if (beforeSibling == id)
                return;
            if (sibling && sibling->Parent != parent)
                throw std::invalid_argument("The insertion sibling must belong to the requested parent.");
            if (parent && m_Impl->DescendsFrom(parent, id))
                throw std::invalid_argument("An entity cannot be parented to itself or one of its descendants.");

            auto reordered = m_Impl->Order;
            std::erase(reordered, id);
            const auto insertion = beforeSibling ? std::ranges::find(reordered, beforeSibling) : reordered.end();
            reordered.insert(insertion, id);

            SetParent(id, parent, preserveWorldTransform);
            m_Impl->Order.swap(reordered);
            m_Impl->LifecycleComponentsDirty = true;
            m_Impl->Dirty = true;
        }

        Ref<Component> SceneState::AddComponent(const EntityId id, const ComponentTypeId type)
        {
            RequireOwner("AddComponent");
            auto* record = m_Impl->Find(id);
            if (!record)
                throw std::logic_error("Entity is stale.");
            const auto registration = m_Impl->ComponentsRegistry->Find(type);
            if (!registration)
                throw std::invalid_argument("Component type is not registered with this scene.");
            if (!registration->AllowMultiple && std::ranges::any_of(record->Components, [&](const auto& component)
                                                                    { return component->Type() == type; }))
                throw std::invalid_argument("Entity already has this single-instance component type.");
            for (const auto dependency : registration->RequiredComponents)
                if (!GetComponent(id, dependency))
                    (void)AddComponent(id, dependency);
            auto component = registration->Factory();
            const auto commit = [this, id, component]
            {
                auto* target = m_Impl->Find(id);
                if (!target)
                    return;
                component->Attach(m_Impl->Self, id);
                target->Components.push_back(component);
                m_Impl->IndexComponent(id, component);
                SynchronizeEntityLayer(id);
                m_Impl->Dirty = true;
                if (m_Impl->Playing)
                {
                    component->InvokeAwake();
                    if (component->Enabled() && ActiveInHierarchy(id))
                        component->InvokeEnable();
                }
            };
            if (m_Impl->TraversalDepth)
                m_Impl->Deferred.push_back(commit);
            else
                commit();
            return component;
        }

        Ref<Component> SceneState::GetComponent(const EntityId id, const ComponentTypeId type) const noexcept
        {
            const auto* record = m_Impl->Find(id);
            if (!record)
                return {};
            const auto found = std::ranges::find_if(record->Components,
                                                    [&](const auto& component) { return component->Type() == type; });
            return found == record->Components.end() ? Ref<Component>{} : *found;
        }

        std::vector<Ref<Component>> SceneState::GetComponents(const EntityId id, const ComponentTypeId type) const
        {
            RequireOwner("GetComponents");
            const auto* record = m_Impl->Find(id);
            if (!record)
                return {};
            if (!type)
                return record->Components;
            std::vector<Ref<Component>> result;
            std::ranges::copy_if(record->Components, std::back_inserter(result),
                                 [&](const auto& component) { return component->Type() == type; });
            return result;
        }

        bool SceneState::RemoveComponent(const EntityId id, const ComponentTypeId type)
        {
            RequireOwner("RemoveComponent");
            auto* record = m_Impl->Find(id);
            if (!record)
                return false;
            const auto registration = m_Impl->ComponentsRegistry->Find(type);
            if (!registration || !registration->Removable)
                return false;
            const auto dependent = std::ranges::find_if(
                record->Components,
                [&](const auto& component)
                {
                    const auto current = m_Impl->ComponentsRegistry->Find(component->Type());
                    return std::ranges::find(current->RequiredComponents, type) != current->RequiredComponents.end();
                });
            if (dependent != record->Components.end())
                throw std::logic_error("Component is required by another attached component.");
            const auto found = std::ranges::find_if(record->Components,
                                                    [&](const auto& component) { return component->Type() == type; });
            if (found == record->Components.end())
                return false;
            const auto component = *found;
            const auto commit = [this, id, component]
            {
                if (auto* target = m_Impl->Find(id))
                {
                    component->InvokeDestroy();
                    m_Impl->UnindexComponent(id, component);
                    std::erase(target->Components, component);
                    m_Impl->Dirty = true;
                }
            };
            if (m_Impl->TraversalDepth)
                m_Impl->Deferred.push_back(commit);
            else
                commit();
            return true;
        }

        void SceneState::SetComponentEnabled(Component& component, const bool enabled)
        {
            RequireOwner("SetComponentEnabled");
            if (!component.IsAttached() || component.Enabled() == enabled)
                return;
            component.ApplyEnabled(enabled);
            m_Impl->Dirty = true;
            if (m_Impl->Playing)
            {
                if (enabled && ActiveInHierarchy(component.Owner().Id()))
                    component.InvokeEnable();
                else
                    component.InvokeDisable();
            }
        }

        void SceneState::ComponentChanged(const Component& component)
        {
            RequireOwner("ComponentChanged");
            if (!component.IsAttached())
                return;
            m_Impl->Dirty = true;
            if (component.Type() == TransformComponent::StaticType())
                m_Impl->MarkWorldDirty(component.Owner().Id());
        }

        Matrix4 SceneState::WorldMatrix(const EntityId id) const
        {
            RequireOwner("WorldMatrix");
            auto* record = m_Impl->Find(id);
            if (!record)
                throw std::logic_error("Entity is stale.");
            if (!record->WorldDirty)
                return record->CachedWorld;
            const auto transform = m_Impl->Transform(id);
            record->CachedWorld = record->Parent ? Math::Multiply(WorldMatrix(record->Parent), transform->LocalMatrix())
                                                 : transform->LocalMatrix();
            record->WorldDirty = false;
            return record->CachedWorld;
        }

        void SceneState::BeginPlay()
        {
            RequireOwner("BeginPlay");
            if (m_Impl->Playing)
                return;
            m_Impl->Playing = true;
            m_Impl->Traverse(
                [&]
                {
                    const auto& components = m_Impl->LifecycleComponents();
                    for (const auto& component : components)
                        component->InvokeAwake();
                    for (const auto& component : components)
                        if (component->Enabled() && ActiveInHierarchy(component->Owner().Id()))
                            component->InvokeEnable();
                });
            FlushDeferred();
        }

        void SceneState::FixedUpdate(const float deltaSeconds)
        {
            RequireOwner("FixedUpdate");
            if (!m_Impl->Playing)
                return;
            m_Impl->Traverse(
                [&]
                {
                    for (const auto& component : m_Impl->LifecycleComponents())
                    {
                        if (component->LifecycleActive())
                            component->InvokeStart();
                        component->InvokeFixedUpdate(deltaSeconds);
                    }
                });
            FlushDeferred();
        }

        void SceneState::Update(const float deltaSeconds)
        {
            RequireOwner("Update");
            if (!m_Impl->Playing)
                return;
            m_Impl->Traverse(
                [&]
                {
                    for (const auto& component : m_Impl->LifecycleComponents())
                    {
                        if (component->LifecycleActive())
                            component->InvokeStart();
                        component->InvokeUpdate(deltaSeconds);
                    }
                });
            FlushDeferred();
        }

        void SceneState::LateUpdate()
        {
            RequireOwner("LateUpdate");
            if (!m_Impl->Playing)
                return;
            m_Impl->Traverse(
                [&]
                {
                    for (const auto& component : m_Impl->LifecycleComponents())
                        component->InvokeLateUpdate();
                });
            FlushDeferred();
        }

        void SceneState::DispatchAnimationEvent(const EntityId entity, const AnimationEventMessage& event)
        {
            RequireOwner("DispatchAnimationEvent");
            if (!m_Impl->Playing || !entity || event.Name.empty() || event.Name.size() > 256 ||
                event.Text.size() > 4096 || !std::isfinite(event.NormalizedTime) || !std::isfinite(event.Scalar))
            {
                throw std::invalid_argument("Animation event dispatch arguments are invalid.");
            }
            auto* record = m_Impl->Find(entity);
            if (!record || !ActiveInHierarchy(entity))
                return;
            m_Impl->Traverse(
                [&]
                {
                    for (const auto& component : record->Components)
                        component->InvokeAnimationEvent(event);
                });
            FlushDeferred();
        }

        void SceneState::DispatchAnimatorIk(const EntityId entity, const AnimationIkMessage& context)
        {
            RequireOwner("DispatchAnimatorIk");
            if (!m_Impl->Playing || !entity || !std::isfinite(context.LayerWeight) || context.LayerWeight < 0.0F ||
                context.LayerWeight > 1.0F)
            {
                throw std::invalid_argument("Animator IK dispatch arguments are invalid.");
            }
            auto* record = m_Impl->Find(entity);
            if (!record || !ActiveInHierarchy(entity))
                return;
            m_Impl->Traverse(
                [&]
                {
                    for (const auto& component : record->Components)
                        component->InvokeAnimatorIk(context);
                });
            FlushDeferred();
        }

        void SceneState::DispatchPhysicsContact(const EntityId entity, const PhysicsContactPhase phase,
                                                const PhysicsContactMessage& contact)
        {
            RequireOwner("DispatchPhysicsContact");
            if (!m_Impl->Playing || !entity || !contact.Other || !Math::IsFinite(contact.Point) ||
                !Math::IsFinite(contact.Normal) || !std::isfinite(contact.Impulse) || contact.Impulse < 0.0F)
            {
                throw std::invalid_argument("Physics contact dispatch arguments are invalid.");
            }
            auto* record = m_Impl->Find(entity);
            if (!record || !ActiveInHierarchy(entity))
                return;
            m_Impl->Traverse(
                [&]
                {
                    for (const auto& component : record->Components)
                        component->InvokePhysicsContact(phase, contact);
                });
            FlushDeferred();
        }

        void SceneState::EndPlay() noexcept
        {
            if (!m_Impl || !m_Impl->Playing)
                return;
            try
            {
                for (const auto& component : m_Impl->LifecycleComponents())
                    component->InvokeDisable();
            }
            catch (...)
            {
            }
            m_Impl->Playing = false;
        }

        void SceneState::FlushDeferred()
        {
            RequireOwner("FlushDeferred");
            if (m_Impl->TraversalDepth)
                return;
            auto operations = std::exchange(m_Impl->Deferred, {});
            for (auto& operation : operations)
                operation();
        }

        void SceneState::Close() noexcept
        {
            if (!m_Impl || !m_Impl->Open)
                return;
            EndPlay();
            try
            {
                for (const auto id : m_Impl->HierarchyOrder())
                    if (auto* record = m_Impl->Find(id))
                        for (const auto& component : record->Components)
                            try
                            {
                                component->InvokeDestroy();
                            }
                            catch (...)
                            {
                                component->Detach();
                            }
            }
            catch (...)
            {
            }
            m_Impl->Deferred.clear();
            m_Impl->Registry.clear();
            m_Impl->Entities.clear();
            m_Impl->ComponentPools.clear();
            m_Impl->Order.clear();
            m_Impl->Open = false;
            m_Impl->Self.Reset();
        }

        Ref<ComponentRegistry> SceneState::Components() const noexcept { return m_Impl->ComponentsRegistry; }
    } // namespace Detail

    SceneObjectHandle::SceneObjectHandle(WeakRef<Detail::SceneState> state, const AssetId id) noexcept
        : m_State(std::move(state)), m_Id(id)
    {
    }

    SceneObjectHandle::operator bool() const noexcept
    {
        const auto state = m_State.Lock();
        return state && state->Contains(EntityId(m_Id));
    }

    std::optional<SceneObjectDefinition> SceneObjectHandle::Snapshot() const
    {
        const auto state = m_State.Lock();
        return state ? state->SnapshotObject(EntityId(m_Id)) : std::nullopt;
    }

    class Scene::Impl final
    {
      public:
        Impl(const AssetId asset, SceneDefinition definition, Ref<ComponentRegistry> components)
            : State(CreateRef<Detail::SceneState>(asset, std::move(definition), std::move(components)))
        {
            State->Initialize(State);
        }

        Ref<Detail::SceneState> State;
    };

    Scene::Scene(const AssetId asset, SceneDefinition definition, Ref<ComponentRegistry> components)
        : m_Impl(std::make_unique<Impl>(asset, std::move(definition), std::move(components)))
    {
    }

    Scene::~Scene() { Close(); }
    AssetId Scene::Asset() const noexcept { return m_Impl->State->Asset(); }
    std::string Scene::Name() const { return m_Impl->State->Name(); }
    void Scene::SetName(std::string name) { m_Impl->State->SetName(std::move(name)); }
    bool Scene::IsOpen() const noexcept { return m_Impl->State->IsOpen(); }
    bool Scene::Dirty() const noexcept { return m_Impl->State->Dirty(); }
    void Scene::MarkDirty() noexcept { m_Impl->State->MarkDirty(); }
    void Scene::MarkSaved() noexcept { m_Impl->State->MarkSaved(); }
    std::size_t Scene::ObjectCount() const noexcept { return m_Impl->State->Count(); }
    std::vector<SceneObjectDefinition> Scene::Objects() const { return m_Impl->State->Snapshot().Objects; }
    SceneHierarchySnapshot Scene::HierarchySnapshot() const { return m_Impl->State->HierarchySnapshot(); }
    SceneDefinition Scene::Snapshot() const { return m_Impl->State->Snapshot(); }
    LightingBakeSettings Scene::LightingBakeConfiguration() const { return m_Impl->State->LightingBakeConfiguration(); }
    void Scene::SetLightingBakeConfiguration(const LightingBakeSettings settings)
    {
        m_Impl->State->SetLightingBakeConfiguration(settings);
    }
    AssetId Scene::BakedLighting() const { return m_Impl->State->BakedLighting(); }
    void Scene::SetBakedLighting(const AssetId asset) { m_Impl->State->SetBakedLighting(asset); }

    SceneObjectHandle Scene::Find(const AssetId id) const noexcept
    {
        return m_Impl->State->Contains(EntityId(id)) ? SceneObjectHandle(m_Impl->State, id) : SceneObjectHandle{};
    }

    SceneObjectHandle Scene::CreateObject(std::string name, const AssetId parent)
    {
        const auto entity = m_Impl->State->Create(std::move(name), EntityId(parent));
        return SceneObjectHandle(m_Impl->State, entity.Id().Value());
    }

    SceneObjectHandle Scene::DuplicateObject(const AssetId id)
    {
        const auto entity = m_Impl->State->Duplicate(EntityId(id));
        return entity ? SceneObjectHandle(m_Impl->State, entity.Id().Value()) : SceneObjectHandle{};
    }

    bool Scene::DestroyObject(const AssetId id) { return m_Impl->State->Destroy(EntityId(id)); }
    bool Scene::RenameObject(const AssetId id, std::string name)
    {
        if (!m_Impl->State->Contains(EntityId(id)))
            return false;
        m_Impl->State->SetEntityName(EntityId(id), std::move(name));
        return true;
    }
    bool Scene::SetObjectActive(const AssetId id, const bool active)
    {
        if (!m_Impl->State->Contains(EntityId(id)))
            return false;
        m_Impl->State->SetActive(EntityId(id), active);
        return true;
    }
    bool Scene::SetObjectLayer(const AssetId id, const std::uint32_t layer)
    {
        if (!m_Impl->State->Contains(EntityId(id)))
            return false;
        m_Impl->State->SetEntityLayer(EntityId(id), layer);
        return true;
    }
    bool Scene::SetObjectTransform(const AssetId id, const SceneTransform transform)
    {
        auto entity = m_Impl->State->Find(EntityId(id));
        if (!entity)
            return false;
        const auto component = entity.GetComponent<TransformComponent>();
        component->SetLocalPosition(transform.Position);
        component->SetLocalRotation(transform.Rotation);
        component->SetLocalScale(transform.Scale);
        return true;
    }
    bool Scene::ReparentObject(const AssetId id, const AssetId parent)
    {
        auto entity = m_Impl->State->Find(EntityId(id));
        if (!entity || (parent && !m_Impl->State->Contains(EntityId(parent))))
            return false;
        entity.SetParent(m_Impl->State->Find(EntityId(parent)), true);
        return true;
    }

    std::vector<Entity> Scene::Entities() const { return m_Impl->State->Entities(); }
    Entity Scene::FindEntity(const EntityId id) const noexcept { return m_Impl->State->Find(id); }
    Entity Scene::CreateEntity(std::string name, const Entity parent)
    {
        return m_Impl->State->Create(std::move(name), parent.Id());
    }
    Entity Scene::DuplicateEntity(const EntityId id) { return m_Impl->State->Duplicate(id); }
    bool Scene::DestroyEntity(const EntityId id) { return m_Impl->State->Destroy(id); }
    void Scene::MoveEntity(const EntityId id, const EntityId parent, const EntityId beforeSibling,
                           const bool preserveWorldTransform)
    {
        m_Impl->State->Move(id, parent, beforeSibling, preserveWorldTransform);
    }
    std::vector<Entity> Scene::Query(const ComponentTypeId type) const { return m_Impl->State->Query(type); }
    Ref<ComponentRegistry> Scene::Components() const noexcept { return m_Impl->State->Components(); }
    void Scene::BeginPlay() { m_Impl->State->BeginPlay(); }
    void Scene::FixedUpdate(const float deltaSeconds) { m_Impl->State->FixedUpdate(deltaSeconds); }
    void Scene::Update(const float deltaSeconds) { m_Impl->State->Update(deltaSeconds); }
    void Scene::LateUpdate() { m_Impl->State->LateUpdate(); }
    void Scene::DispatchAnimationEvent(const EntityId entity, const AnimationEventMessage& event)
    {
        m_Impl->State->DispatchAnimationEvent(entity, event);
    }
    void Scene::DispatchAnimatorIk(const EntityId entity, const AnimationIkMessage& context)
    {
        m_Impl->State->DispatchAnimatorIk(entity, context);
    }
    void Scene::DispatchPhysicsContact(const EntityId entity, const PhysicsContactPhase phase,
                                       const PhysicsContactMessage& contact)
    {
        m_Impl->State->DispatchPhysicsContact(entity, phase, contact);
    }
    void Scene::EndPlay() noexcept { m_Impl->State->EndPlay(); }
    void Scene::Close() noexcept { m_Impl->State->Close(); }

    std::optional<SceneObjectDefinition> Scene::SnapshotObject(const AssetId id) const
    {
        return m_Impl->State->SnapshotObject(EntityId(id));
    }
} // namespace Keire
