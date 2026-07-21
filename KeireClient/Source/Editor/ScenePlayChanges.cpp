#include "KeireClient/Editor/ScenePlayChanges.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        using ObjectMap = std::unordered_map<Keire::AssetId, const Keire::SceneObjectDefinition*>;

        [[nodiscard]] ObjectMap IndexObjects(const Keire::SceneDefinition& definition)
        {
            ObjectMap result;
            for (const auto& object : definition.Objects)
                result.emplace(object.Id, &object);
            return result;
        }

        [[nodiscard]] std::string DisplayValue(const Keire::ComponentPropertyValue& value)
        {
            return std::visit(
                [](const auto& typed) -> std::string
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, bool>)
                        return typed ? "true" : "false";
                    else if constexpr (std::same_as<T, std::string>)
                        return typed;
                    else if constexpr (std::same_as<T, std::int64_t>)
                        return std::to_string(typed);
                    else if constexpr (std::same_as<T, double>)
                    {
                        std::ostringstream stream;
                        stream.precision(6);
                        stream << typed;
                        return stream.str();
                    }
                    else if constexpr (std::same_as<T, Keire::AssetId> || std::same_as<T, Keire::EntityId>)
                        return typed ? typed.ToString() : "None";
                    else if constexpr (std::same_as<T, Keire::Vector2>)
                        return std::to_string(typed.X) + ", " + std::to_string(typed.Y);
                    else if constexpr (std::same_as<T, Keire::Vector3>)
                        return std::to_string(typed.X) + ", " + std::to_string(typed.Y) + ", " +
                               std::to_string(typed.Z);
                    else
                        return "Changed";
                },
                value);
        }

        [[nodiscard]] std::string ComponentDisplayName(const Keire::Ref<Keire::ComponentRegistry>& registry,
                                                       const Keire::ComponentTypeId type)
        {
            const auto registration = registry->Find(type);
            return registration ? registration->Name : type.ToString();
        }

        [[nodiscard]] const Keire::SceneComponentDefinition*
        FindComponentDefinition(const Keire::SceneObjectDefinition& object, const Keire::ComponentTypeId type)
        {
            const auto found = std::ranges::find(object.Components, type, &Keire::SceneComponentDefinition::Type);
            return found == object.Components.end() ? nullptr : &*found;
        }
    } // namespace

    class ScenePlayChangeSet::Impl final
    {
      public:
        struct Detail final
        {
            ScenePlayChange Public;
            std::optional<Keire::ComponentPropertyValue> Value;
            Keire::AssetId Parent;
        };

        Impl(Keire::Ref<Keire::Scene> editingScene, Keire::Ref<Keire::Scene> runtimeScene,
             std::unordered_set<Keire::AssetId> editorTouchedEntities)
            : Editing(std::move(editingScene)), Runtime(std::move(runtimeScene)),
              EditorTouched(std::move(editorTouchedEntities))
        {
            if (!Editing || !Runtime)
                throw std::invalid_argument("Play change tracking requires open edit and runtime scenes.");
            Registry = Editing->Components();
            if (!Registry)
                throw std::invalid_argument("Play change tracking requires a component registry.");
            Before = Editing->Snapshot();
            After = Runtime->Snapshot();
            Build();
        }

        [[nodiscard]] ScenePlayChangeOrigin Origin(const Keire::AssetId entity) const
        {
            return EditorTouched.contains(entity) ? ScenePlayChangeOrigin::Editor : ScenePlayChangeOrigin::Runtime;
        }

        void Add(ScenePlayChange change, std::optional<Keire::ComponentPropertyValue> value = {},
                 const Keire::AssetId parent = {})
        {
            change.Id = Details.size();
            change.Selected = change.Origin != ScenePlayChangeOrigin::Runtime;
            Details.push_back({std::move(change), std::move(value), parent});
        }

        void Build()
        {
            const auto beforeObjects = IndexObjects(Before);
            const auto afterObjects = IndexObjects(After);
            if (Before.Name != After.Name)
            {
                Add({.Kind = ScenePlayChangeKind::SceneName,
                     .Origin = ScenePlayChangeOrigin::Runtime,
                     .Label = "Scene name",
                     .Before = Before.Name,
                     .After = After.Name});
            }
            for (const auto& beforeObject : Before.Objects)
            {
                if (!afterObjects.contains(beforeObject.Id))
                {
                    Add({.Kind = ScenePlayChangeKind::DeleteEntity,
                         .Origin = Origin(beforeObject.Id),
                         .Entity = beforeObject.Id,
                         .EntityName = beforeObject.Name,
                         .Label = "Delete entity",
                         .Before = beforeObject.Name,
                         .After = "Deleted"});
                }
            }
            for (const auto& afterObject : After.Objects)
            {
                const auto beforeFound = beforeObjects.find(afterObject.Id);
                if (beforeFound == beforeObjects.end())
                {
                    Add({.Kind = ScenePlayChangeKind::CreateEntity,
                         .Origin = Origin(afterObject.Id),
                         .Entity = afterObject.Id,
                         .EntityName = afterObject.Name,
                         .Label = "Create entity",
                         .Before = "Missing",
                         .After = afterObject.Name},
                        {}, afterObject.Parent);
                    continue;
                }
                BuildEntity(*beforeFound->second, afterObject);
            }
            PublicChanges.reserve(Details.size());
            for (const auto& detail : Details)
                PublicChanges.push_back(detail.Public);
        }

        void BuildEntity(const Keire::SceneObjectDefinition& before, const Keire::SceneObjectDefinition& after)
        {
            const auto origin = Origin(after.Id);
            const auto addEntityChange = [&](const ScenePlayChangeKind kind, std::string label, std::string oldValue,
                                             std::string newValue, const Keire::AssetId parent = {})
            {
                Add({.Kind = kind,
                     .Origin = origin,
                     .Entity = after.Id,
                     .EntityName = after.Name,
                     .Label = std::move(label),
                     .Before = std::move(oldValue),
                     .After = std::move(newValue)},
                    {}, parent);
            };
            if (before.Name != after.Name)
                addEntityChange(ScenePlayChangeKind::EntityName, "Name", before.Name, after.Name);
            if (before.Active != after.Active)
                addEntityChange(ScenePlayChangeKind::EntityActive, "Active", before.Active ? "true" : "false",
                                after.Active ? "true" : "false");
            if (before.Parent != after.Parent)
                addEntityChange(ScenePlayChangeKind::EntityParent, "Parent",
                                before.Parent ? before.Parent.ToString() : "Root",
                                after.Parent ? after.Parent.ToString() : "Root", after.Parent);

            std::unordered_map<Keire::ComponentTypeId, const Keire::SceneComponentDefinition*> beforeComponents;
            std::unordered_map<Keire::ComponentTypeId, const Keire::SceneComponentDefinition*> afterComponents;
            for (const auto& component : before.Components)
                beforeComponents.emplace(component.Type, &component);
            for (const auto& component : after.Components)
                afterComponents.emplace(component.Type, &component);
            for (const auto& [type, component] : beforeComponents)
            {
                if (!afterComponents.contains(type))
                {
                    Add({.Kind = ScenePlayChangeKind::RemoveComponent,
                         .Origin = origin,
                         .Entity = after.Id,
                         .Component = type,
                         .EntityName = after.Name,
                         .ComponentName = ComponentDisplayName(Registry, type),
                         .Label = "Remove component",
                         .Before = component->Data,
                         .After = "Removed"});
                }
            }
            for (const auto& [type, afterComponent] : afterComponents)
            {
                const auto beforeFound = beforeComponents.find(type);
                if (beforeFound == beforeComponents.end())
                {
                    Add({.Kind = ScenePlayChangeKind::AddComponent,
                         .Origin = origin,
                         .Entity = after.Id,
                         .Component = type,
                         .EntityName = after.Name,
                         .ComponentName = ComponentDisplayName(Registry, type),
                         .Label = "Add component",
                         .Before = "Missing",
                         .After = afterComponent->Data});
                    continue;
                }
                BuildComponent(after, *beforeFound->second, *afterComponent, origin);
            }
        }

        void BuildComponent(const Keire::SceneObjectDefinition& object,
                            const Keire::SceneComponentDefinition& beforeDefinition,
                            const Keire::SceneComponentDefinition& afterDefinition, const ScenePlayChangeOrigin origin)
        {
            const auto registration = Registry->Find(afterDefinition.Type);
            if (!registration)
            {
                if (beforeDefinition.Enabled != afterDefinition.Enabled ||
                    beforeDefinition.Data != afterDefinition.Data)
                {
                    Add({.Kind = ScenePlayChangeKind::ReplaceUnknownComponent,
                         .Origin = origin,
                         .Entity = object.Id,
                         .Component = afterDefinition.Type,
                         .EntityName = object.Name,
                         .ComponentName = afterDefinition.Type.ToString(),
                         .Label = "Replace unavailable component data",
                         .Before = beforeDefinition.Data,
                         .After = afterDefinition.Data});
                }
                return;
            }
            if (beforeDefinition.Enabled != afterDefinition.Enabled)
            {
                Add({.Kind = ScenePlayChangeKind::ComponentEnabled,
                     .Origin = origin,
                     .Entity = object.Id,
                     .Component = afterDefinition.Type,
                     .EntityName = object.Name,
                     .ComponentName = registration->Name,
                     .Label = "Enabled",
                     .Before = beforeDefinition.Enabled ? "true" : "false",
                     .After = afterDefinition.Enabled ? "true" : "false"},
                    afterDefinition.Enabled);
            }
            const auto beforeEntity = Editing->FindEntity(Keire::EntityId(object.Id));
            const auto afterEntity = Runtime->FindEntity(Keire::EntityId(object.Id));
            const auto beforeComponent = beforeEntity.GetComponent(afterDefinition.Type);
            const auto afterComponent = afterEntity.GetComponent(afterDefinition.Type);
            if (!beforeComponent || !afterComponent)
                return;
            const auto beforeValues = registration->Serialize(*beforeComponent);
            const auto afterValues = registration->Serialize(*afterComponent);
            for (const auto& property : registration->Properties)
            {
                const auto beforeValue = beforeValues.find(property.Key);
                const auto afterValue = afterValues.find(property.Key);
                if (beforeValue == beforeValues.end() || afterValue == afterValues.end() ||
                    beforeValue->second == afterValue->second)
                    continue;
                Add({.Kind = ScenePlayChangeKind::ComponentProperty,
                     .Origin = origin,
                     .Entity = object.Id,
                     .Component = afterDefinition.Type,
                     .Property = property.Key,
                     .EntityName = object.Name,
                     .ComponentName = registration->Name,
                     .Label = property.DisplayName,
                     .Before = DisplayValue(beforeValue->second),
                     .After = DisplayValue(afterValue->second)},
                    afterValue->second);
            }
        }

        void SynchronizePublic() const
        {
            for (std::size_t index = 0; index < Details.size(); ++index)
                PublicChanges[index] = Details[index].Public;
        }

        Keire::Ref<Keire::Scene> Editing;
        Keire::Ref<Keire::Scene> Runtime;
        Keire::Ref<Keire::ComponentRegistry> Registry;
        Keire::SceneDefinition Before;
        Keire::SceneDefinition After;
        std::unordered_set<Keire::AssetId> EditorTouched;
        std::vector<Detail> Details;
        mutable std::vector<ScenePlayChange> PublicChanges;
    };

    ScenePlayChangeSet::ScenePlayChangeSet(Keire::Ref<Keire::Scene> editingScene, Keire::Ref<Keire::Scene> runtimeScene,
                                           std::unordered_set<Keire::AssetId> editorTouchedEntities)
        : m_Impl(std::make_unique<Impl>(std::move(editingScene), std::move(runtimeScene),
                                        std::move(editorTouchedEntities)))
    {
    }

    ScenePlayChangeSet::~ScenePlayChangeSet() = default;

    std::span<const ScenePlayChange> ScenePlayChangeSet::Changes() const noexcept
    {
        m_Impl->SynchronizePublic();
        return m_Impl->PublicChanges;
    }

    bool ScenePlayChangeSet::Empty() const noexcept { return m_Impl->Details.empty(); }

    bool ScenePlayChangeSet::HasSelectedChanges() const noexcept
    {
        return std::ranges::any_of(m_Impl->Details, [](const auto& detail) { return detail.Public.Selected; });
    }

    void ScenePlayChangeSet::SetSelected(const std::size_t id, const bool selected)
    {
        if (id >= m_Impl->Details.size())
            throw std::out_of_range("Play change ID is out of range.");
        auto& detail = m_Impl->Details[id];
        if (!detail.Public.Locked)
            detail.Public.Selected = selected;
    }

    void ScenePlayChangeSet::SetAllSelected(const bool selected)
    {
        for (auto& detail : m_Impl->Details)
            if (!detail.Public.Locked)
                detail.Public.Selected = selected;
    }

    Keire::SceneDefinition ScenePlayChangeSet::BuildAppliedDefinition() const
    {
        auto working = Keire::CreateRef<Keire::Scene>(m_Impl->Editing->Asset(), m_Impl->Before, m_Impl->Registry);
        std::unordered_set<Keire::AssetId> deleted;
        std::unordered_set<Keire::AssetId> created;
        std::vector<const Impl::Detail*> postponedParents;
        for (const auto& detail : m_Impl->Details)
        {
            if (!detail.Public.Selected)
                continue;
            const auto& change = detail.Public;
            if (change.Kind == ScenePlayChangeKind::SceneName)
            {
                working->SetName(m_Impl->After.Name);
                continue;
            }
            if (change.Kind == ScenePlayChangeKind::CreateEntity)
            {
                created.insert(change.Entity);
                continue;
            }
            if (change.Kind == ScenePlayChangeKind::DeleteEntity)
            {
                deleted.insert(change.Entity);
                continue;
            }
            auto entity = working->FindEntity(Keire::EntityId(change.Entity));
            if (!entity)
                continue;
            const auto runtimeEntity = m_Impl->Runtime->FindEntity(Keire::EntityId(change.Entity));
            switch (change.Kind)
            {
            case ScenePlayChangeKind::EntityName:
                entity.SetName(runtimeEntity.Name());
                break;
            case ScenePlayChangeKind::EntityActive:
                entity.SetActive(runtimeEntity.ActiveSelf());
                break;
            case ScenePlayChangeKind::EntityParent:
                postponedParents.push_back(&detail);
                break;
            case ScenePlayChangeKind::AddComponent:
            {
                auto component = entity.AddComponent(change.Component);
                const auto registration = m_Impl->Registry->Find(change.Component);
                const auto source = runtimeEntity.GetComponent(change.Component);
                if (registration && component && source)
                {
                    registration->Deserialize(*component, registration->Serialize(*source),
                                              registration->SchemaVersion);
                    component->SetEnabled(source->Enabled());
                }
                break;
            }
            case ScenePlayChangeKind::RemoveComponent:
                (void)entity.RemoveComponent(change.Component);
                break;
            case ScenePlayChangeKind::ComponentEnabled:
                if (const auto component = entity.GetComponent(change.Component); component && detail.Value)
                    component->SetEnabled(std::get<bool>(*detail.Value));
                break;
            case ScenePlayChangeKind::ComponentProperty:
            {
                const auto registration = m_Impl->Registry->Find(change.Component);
                const auto component = entity.GetComponent(change.Component);
                if (registration && component && detail.Value)
                {
                    auto values = registration->Serialize(*component);
                    values[change.Property] = *detail.Value;
                    registration->Deserialize(*component, values, registration->SchemaVersion);
                }
                break;
            }
            case ScenePlayChangeKind::ReplaceUnknownComponent:
            case ScenePlayChangeKind::SceneName:
            case ScenePlayChangeKind::CreateEntity:
            case ScenePlayChangeKind::DeleteEntity:
                break;
            }
        }

        auto result = working->Snapshot();
        const auto runtimeObjects = IndexObjects(m_Impl->After);
        std::erase_if(result.Objects, [&deleted](const auto& object) { return deleted.contains(object.Id); });
        for (const auto entity : created)
        {
            if (const auto found = runtimeObjects.find(entity); found != runtimeObjects.end())
                result.Objects.push_back(*found->second);
        }
        for (const auto* detail : postponedParents)
        {
            const auto found =
                std::ranges::find(result.Objects, detail->Public.Entity, &Keire::SceneObjectDefinition::Id);
            if (found != result.Objects.end())
                found->Parent = detail->Parent;
        }
        const auto resultObjects = IndexObjects(result);
        for (auto& object : result.Objects)
            if (object.Parent && !resultObjects.contains(object.Parent))
                object.Parent = {};
        Keire::SceneAsset::Validate(result);
        return result;
    }
} // namespace KeireEditor
