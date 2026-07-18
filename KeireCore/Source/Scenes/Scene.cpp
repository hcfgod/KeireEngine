#include "Keire/Scenes/Scene.h"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace Detail
    {
        class SceneState final : public RefCounted
        {
          public:
            SceneState(const AssetId asset, SceneDefinition definition)
                : OwnerThread(std::this_thread::get_id()), Asset(asset), Definition(std::move(definition))
            {
                SceneAsset::Validate(Definition);
            }

            void RequireOwner(const char* operation) const
            {
                if (std::this_thread::get_id() != OwnerThread)
                    throw std::logic_error(std::string("Scene::") + operation + " must run on the owner thread.");
            }

            [[nodiscard]] bool Contains(const AssetId id) const noexcept
            {
                return Open && std::ranges::find(Definition.Objects, id, &SceneObjectDefinition::Id) !=
                                   Definition.Objects.end();
            }

            [[nodiscard]] std::optional<SceneObjectDefinition> SnapshotObject(const AssetId id) const
            {
                RequireOwner("SceneObjectHandle::Snapshot");
                if (!Open)
                    return std::nullopt;
                const auto found = std::ranges::find(Definition.Objects, id, &SceneObjectDefinition::Id);
                return found == Definition.Objects.end() ? std::nullopt : std::optional<SceneObjectDefinition>(*found);
            }

            std::thread::id OwnerThread;
            AssetId Asset;
            SceneDefinition Definition;
            bool Open = true;
            bool Dirty = false;
        };
    } // namespace Detail

    namespace
    {
        [[nodiscard]] std::vector<SceneObjectDefinition>
        HierarchyOrder(const std::vector<SceneObjectDefinition>& objects)
        {
            std::unordered_map<AssetId, std::vector<SceneObjectDefinition>> children;
            std::vector<SceneObjectDefinition> roots;
            for (const auto& object : objects)
            {
                if (object.Parent)
                    children[object.Parent].push_back(object);
                else
                    roots.push_back(object);
            }
            std::vector<SceneObjectDefinition> result;
            result.reserve(objects.size());
            const auto append = [&](const auto& self, SceneObjectDefinition object) -> void
            {
                const auto id = object.Id;
                result.push_back(std::move(object));
                if (const auto found = children.find(id); found != children.end())
                {
                    for (auto child : found->second)
                        self(self, std::move(child));
                }
            };
            for (auto root : roots)
                append(append, std::move(root));
            return result;
        }

        [[nodiscard]] bool DescendsFrom(const std::vector<SceneObjectDefinition>& objects, AssetId candidate,
                                        const AssetId ancestor)
        {
            while (candidate)
            {
                if (candidate == ancestor)
                    return true;
                const auto found = std::ranges::find(objects, candidate, &SceneObjectDefinition::Id);
                if (found == objects.end())
                    return false;
                candidate = found->Parent;
            }
            return false;
        }
    } // namespace

    SceneObjectHandle::SceneObjectHandle(WeakRef<Detail::SceneState> state, const AssetId id) noexcept
        : m_State(std::move(state)), m_Id(id)
    {
    }

    SceneObjectHandle::operator bool() const noexcept
    {
        const auto state = m_State.Lock();
        return state && state->Contains(m_Id);
    }

    std::optional<SceneObjectDefinition> SceneObjectHandle::Snapshot() const
    {
        const auto state = m_State.Lock();
        return state ? state->SnapshotObject(m_Id) : std::nullopt;
    }

    class Scene::Impl final
    {
      public:
        Impl(const AssetId asset, SceneDefinition definition)
            : State(CreateRef<Detail::SceneState>(asset, std::move(definition)))
        {
        }

        void RequireOpen(const char* operation) const
        {
            State->RequireOwner(operation);
            if (!State->Open)
                throw std::logic_error(std::string("Scene::") + operation + " cannot run after Close.");
        }

        void Commit(std::vector<SceneObjectDefinition> objects)
        {
            auto definition = State->Definition;
            definition.Objects = std::move(objects);
            SceneAsset::Validate(definition);
            State->Definition = std::move(definition);
            State->Dirty = true;
        }

        Ref<Detail::SceneState> State;
    };

    Scene::Scene(const AssetId asset, SceneDefinition definition)
        : m_Impl(std::make_unique<Impl>(asset, std::move(definition)))
    {
    }

    Scene::~Scene() { Close(); }

    AssetId Scene::Asset() const noexcept { return m_Impl->State->Asset; }

    std::string Scene::Name() const
    {
        m_Impl->RequireOpen("Name");
        return m_Impl->State->Definition.Name;
    }

    void Scene::SetName(std::string name)
    {
        m_Impl->RequireOpen("SetName");
        auto definition = m_Impl->State->Definition;
        definition.Name = std::move(name);
        SceneAsset::Validate(definition);
        m_Impl->State->Definition = std::move(definition);
        m_Impl->State->Dirty = true;
    }

    bool Scene::IsOpen() const noexcept { return m_Impl->State->Open; }

    bool Scene::Dirty() const noexcept { return m_Impl->State->Dirty; }

    void Scene::MarkDirty() noexcept
    {
        if (m_Impl->State->Open && std::this_thread::get_id() == m_Impl->State->OwnerThread)
            m_Impl->State->Dirty = true;
    }

    void Scene::MarkSaved() noexcept
    {
        if (m_Impl->State->Open && std::this_thread::get_id() == m_Impl->State->OwnerThread)
            m_Impl->State->Dirty = false;
    }

    std::size_t Scene::ObjectCount() const noexcept
    {
        return m_Impl->State->Open ? m_Impl->State->Definition.Objects.size() : 0;
    }

    std::vector<SceneObjectDefinition> Scene::Objects() const
    {
        m_Impl->RequireOpen("Objects");
        return m_Impl->State->Definition.Objects;
    }

    SceneDefinition Scene::Snapshot() const
    {
        m_Impl->RequireOpen("Snapshot");
        return m_Impl->State->Definition;
    }

    SceneObjectHandle Scene::Find(const AssetId id) const noexcept
    {
        return m_Impl->State->Contains(id) ? SceneObjectHandle(WeakRef<Detail::SceneState>(m_Impl->State), id)
                                           : SceneObjectHandle{};
    }

    SceneObjectHandle Scene::CreateObject(std::string name, const AssetId parent)
    {
        m_Impl->RequireOpen("CreateObject");
        if (parent && !m_Impl->State->Contains(parent))
            throw std::invalid_argument("Scene object parent does not exist.");
        auto objects = m_Impl->State->Definition.Objects;
        SceneObjectDefinition object{AssetId::Generate(), parent, std::move(name)};
        objects.push_back(object);
        objects = HierarchyOrder(objects);
        m_Impl->Commit(std::move(objects));
        return SceneObjectHandle(WeakRef<Detail::SceneState>(m_Impl->State), object.Id);
    }

    SceneObjectHandle Scene::DuplicateObject(const AssetId id)
    {
        m_Impl->RequireOpen("DuplicateObject");
        auto objects = m_Impl->State->Definition.Objects;
        const auto found = std::ranges::find(objects, id, &SceneObjectDefinition::Id);
        if (found == objects.end())
            return {};
        std::unordered_map<AssetId, AssetId> remapped;
        std::vector<SceneObjectDefinition> copies;
        for (const auto& object : objects)
        {
            if (!DescendsFrom(objects, object.Id, id))
                continue;
            auto copy = object;
            const auto originalId = copy.Id;
            copy.Id = AssetId::Generate();
            if (originalId == id)
                copy.Name += " Copy";
            if (const auto parent = remapped.find(copy.Parent); parent != remapped.end())
                copy.Parent = parent->second;
            remapped.emplace(originalId, copy.Id);
            copies.push_back(std::move(copy));
        }
        const auto copyId = remapped.at(id);
        objects.insert(objects.end(), std::make_move_iterator(copies.begin()), std::make_move_iterator(copies.end()));
        objects = HierarchyOrder(objects);
        m_Impl->Commit(std::move(objects));
        return SceneObjectHandle(WeakRef<Detail::SceneState>(m_Impl->State), copyId);
    }

    bool Scene::DestroyObject(const AssetId id)
    {
        m_Impl->RequireOpen("DestroyObject");
        if (!m_Impl->State->Contains(id))
            return false;
        auto objects = m_Impl->State->Definition.Objects;
        std::unordered_set<AssetId> removed{id};
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& object : objects)
            {
                if (object.Parent && removed.contains(object.Parent) && removed.insert(object.Id).second)
                    changed = true;
            }
        }
        std::erase_if(objects, [&](const auto& object) { return removed.contains(object.Id); });
        m_Impl->Commit(std::move(objects));
        return true;
    }

    bool Scene::RenameObject(const AssetId id, std::string name)
    {
        m_Impl->RequireOpen("RenameObject");
        auto objects = m_Impl->State->Definition.Objects;
        const auto found = std::ranges::find(objects, id, &SceneObjectDefinition::Id);
        if (found == objects.end())
            return false;
        found->Name = std::move(name);
        m_Impl->Commit(std::move(objects));
        return true;
    }

    bool Scene::SetObjectActive(const AssetId id, const bool active)
    {
        m_Impl->RequireOpen("SetObjectActive");
        auto objects = m_Impl->State->Definition.Objects;
        const auto found = std::ranges::find(objects, id, &SceneObjectDefinition::Id);
        if (found == objects.end())
            return false;
        found->Active = active;
        m_Impl->Commit(std::move(objects));
        return true;
    }

    bool Scene::SetObjectTransform(const AssetId id, const SceneTransform transform)
    {
        m_Impl->RequireOpen("SetObjectTransform");
        auto objects = m_Impl->State->Definition.Objects;
        const auto found = std::ranges::find(objects, id, &SceneObjectDefinition::Id);
        if (found == objects.end())
            return false;
        found->Transform = transform;
        m_Impl->Commit(std::move(objects));
        return true;
    }

    bool Scene::ReparentObject(const AssetId id, const AssetId parent)
    {
        m_Impl->RequireOpen("ReparentObject");
        auto objects = m_Impl->State->Definition.Objects;
        const auto found = std::ranges::find(objects, id, &SceneObjectDefinition::Id);
        if (found == objects.end() ||
            (parent && std::ranges::find(objects, parent, &SceneObjectDefinition::Id) == objects.end()))
            return false;
        if (parent && DescendsFrom(objects, parent, id))
            throw std::invalid_argument("Scene object cannot be parented to itself or one of its descendants.");
        found->Parent = parent;
        objects = HierarchyOrder(objects);
        m_Impl->Commit(std::move(objects));
        return true;
    }

    void Scene::Close() noexcept
    {
        if (!m_Impl || !m_Impl->State->Open)
            return;
        m_Impl->State->Open = false;
        m_Impl->State->Definition = {};
    }

    std::optional<SceneObjectDefinition> Scene::SnapshotObject(const AssetId id) const
    {
        return m_Impl->State->SnapshotObject(id);
    }
} // namespace Keire
