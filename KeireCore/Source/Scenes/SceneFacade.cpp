#include "Keire/Scenes/Scene.h"

#include "Keire/ECS/Components/TransformComponent.h"
#include "KeireInternal/SceneState.h"

#include <utility>

namespace Keire
{
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
    Entity Scene::CreateEntity(std::string name, const Entity& parent)
    {
        return m_Impl->State->Create(std::move(name), parent.Id());
    }
    Entity Scene::DuplicateEntity(const EntityId id) { return m_Impl->State->Duplicate(id); }
    Entity Scene::InstantiatePrefab(const AssetId prefab, SceneDefinition definition, const Entity& parent,
                                    const Vector3 position, const Quaternion rotation, const bool active)
    {
        if (parent && parent.World() != m_Impl->State->Find(parent.Id()).World())
            throw std::invalid_argument("Prefab parent belongs to another scene.");
        return m_Impl->State->InstantiatePrefab(prefab, std::move(definition), parent.Id(), position, rotation, active);
    }
    bool Scene::DestroyEntity(const EntityId id) { return m_Impl->State->Destroy(id); }
    void Scene::MoveEntity(const EntityId id, const EntityId parent, const EntityId beforeSibling,
                           const bool preserveWorldTransform)
    {
        m_Impl->State->Move(id, parent, beforeSibling, preserveWorldTransform);
    }
    std::vector<Entity> Scene::Query(const ComponentTypeId type) const { return m_Impl->State->Query(type); }
    std::vector<Entity> Scene::QueryName(const std::string_view name) const { return m_Impl->State->QueryName(name); }
    std::vector<Entity> Scene::QueryTag(const std::string_view tag) const { return m_Impl->State->QueryTag(tag); }
    Ref<ComponentRegistry> Scene::Components() const noexcept { return m_Impl->State->Components(); }
    void Scene::BeginPlay() { m_Impl->State->BeginPlay(); }
    void Scene::FixedUpdate(const float deltaSeconds) { m_Impl->State->FixedUpdate(deltaSeconds); }
    void Scene::Update(const float deltaSeconds) { m_Impl->State->Update(deltaSeconds); }
    void Scene::LateUpdate() { m_Impl->State->LateUpdate(); }
    void Scene::DispatchAnimationEvent(const EntityId entity, const AnimationEventMessage& event)
    {
        m_Impl->State->DispatchAnimationEvent(entity, event);
    }
    void Scene::DispatchProceduralMotionEvent(const EntityId entity, const ProceduralMotionEvent& event)
    {
        m_Impl->State->DispatchProceduralMotionEvent(entity, event);
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
