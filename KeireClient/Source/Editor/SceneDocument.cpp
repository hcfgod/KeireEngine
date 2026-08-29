#include "KeireClient/Editor/SceneDocument.h"

#include "KeireClient/Editor/InspectorTransformUndo.h"

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/CharacterControllerComponent.h"
#include "Keire/ECS/Components/ColliderComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] bool ValidTransformValue(const InspectorTransformProperty property,
                                               const InspectorTransformValue& value)
        {
            switch (property)
            {
            case InspectorTransformProperty::Position:
            case InspectorTransformProperty::Scale:
                return std::holds_alternative<Keire::Vector3>(value);
            case InspectorTransformProperty::Rotation:
                return std::holds_alternative<Keire::Quaternion>(value);
            }
            return false;
        }

        class SceneDocumentEditGeneration final : public Keire::RefCounted
        {
        };

        class InspectorTransformUndoCommand final : public Keire::UndoCommand
        {
          public:
            InspectorTransformUndoCommand(const Keire::AssetId scene, const bool playMode, InspectorTransformEdit edit,
                                          InspectorTransformApply apply, Keire::UndoAvailability available)
                : m_Scene(scene), m_PlayMode(playMode), m_Entity(edit.Entity), m_Property(edit.Property),
                  m_Before(std::move(edit.Before)), m_After(std::move(edit.After)), m_Name(std::move(edit.Name)),
                  m_MergeKey(std::move(edit.MergeKey)), m_Scope(edit.Scope), m_HasScope(static_cast<bool>(edit.Scope)),
                  m_Apply(std::move(apply)), m_Available(std::move(available))
            {
            }

            [[nodiscard]] std::string_view Name() const noexcept override { return m_Name; }
            [[nodiscard]] std::size_t EstimatedBytes() const noexcept override
            {
                return sizeof(*this) + m_Name.size() + m_MergeKey.size();
            }
            [[nodiscard]] bool Available() const noexcept override
            {
                try
                {
                    return !m_Available || m_Available();
                }
                catch (...)
                {
                    return false;
                }
            }
            void Redo() override { m_Apply(m_Entity, m_Property, m_After); }
            void Undo() override { m_Apply(m_Entity, m_Property, m_Before); }
            [[nodiscard]] bool TryMerge(const Keire::UndoCommand& newer) override
            {
                const auto* command = dynamic_cast<const InspectorTransformUndoCommand*>(&newer);
                if (!command || m_MergeKey.empty() || command->m_MergeKey != m_MergeKey ||
                    command->m_Scene != m_Scene || command->m_PlayMode != m_PlayMode || command->m_Entity != m_Entity ||
                    command->m_Property != m_Property || command->m_HasScope != m_HasScope)
                {
                    return false;
                }
                if (m_HasScope)
                {
                    const auto scope = m_Scope.Lock();
                    const auto newerScope = command->m_Scope.Lock();
                    if (!scope || !newerScope || scope != newerScope)
                        return false;
                }
                m_After = command->m_After;
                return true;
            }

          private:
            Keire::AssetId m_Scene;
            bool m_PlayMode = false;
            Keire::EntityId m_Entity;
            InspectorTransformProperty m_Property = InspectorTransformProperty::Position;
            InspectorTransformValue m_Before;
            InspectorTransformValue m_After;
            std::string m_Name;
            std::string m_MergeKey;
            Keire::WeakRef<Keire::RefCounted> m_Scope;
            bool m_HasScope = false;
            InspectorTransformApply m_Apply;
            Keire::UndoAvailability m_Available;
        };

        void FitColliderToBuiltinMesh(const Keire::Ref<Keire::ColliderComponent>& collider,
                                      const Keire::BuiltinMesh mesh, const Keire::AssetId meshAsset)
        {
            switch (mesh)
            {
            case Keire::BuiltinMesh::Cube:
                collider->SetShape(Keire::ColliderShape::Box);
                collider->SetHalfExtent({0.5F, 0.5F, 0.5F});
                break;
            case Keire::BuiltinMesh::Sphere:
                collider->SetShape(Keire::ColliderShape::Sphere);
                collider->SetRadius(0.5F);
                break;
            case Keire::BuiltinMesh::Capsule:
                collider->SetShape(Keire::ColliderShape::Capsule);
                collider->SetRadius(0.25F);
                collider->SetHeight(1.0F);
                break;
            case Keire::BuiltinMesh::Cylinder:
            case Keire::BuiltinMesh::Cone:
                collider->SetShape(Keire::ColliderShape::ConvexMesh);
                collider->SetCollisionMesh(meshAsset);
                break;
            case Keire::BuiltinMesh::Plane:
            case Keire::BuiltinMesh::Quad:
            case Keire::BuiltinMesh::Torus:
                collider->SetShape(Keire::ColliderShape::TriangleMesh);
                collider->SetCollisionMesh(meshAsset);
                break;
            case Keire::BuiltinMesh::Error:
                break;
            }
        }

        void FitNewPhysicsComponentToBuiltinMesh(const Keire::Entity& entity,
                                                 const Keire::Ref<Keire::Component>& component)
        {
            const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
            if (!renderer)
                return;
            const auto mesh = Keire::MeshAsset::BuiltinKind(renderer->Mesh());
            if (!mesh || *mesh == Keire::BuiltinMesh::Error)
                return;
            if (const auto collider = Keire::DynamicRefCast<Keire::ColliderComponent>(component))
            {
                FitColliderToBuiltinMesh(collider, *mesh, renderer->Mesh());
                return;
            }
            if (const auto controller = Keire::DynamicRefCast<Keire::CharacterControllerComponent>(component))
            {
                if (*mesh == Keire::BuiltinMesh::Capsule)
                    controller->ConfigureCapsule(0.25F, 1.0F, 0.3F, 0.05F);
                else if (*mesh == Keire::BuiltinMesh::Sphere)
                    controller->ConfigureCapsule(0.5F, 1.0F, 0.3F, 0.05F);
            }
        }

        [[nodiscard]] Keire::Ref<Keire::Component> ComponentAtOrdinal(const Keire::Entity& entity,
                                                                      const Keire::ComponentTypeId type,
                                                                      const std::size_t requested) noexcept
        {
            std::size_t ordinal = 0;
            for (const auto& component : entity.GetComponents())
            {
                if (!component || component->Type() != type)
                    continue;
                if (ordinal++ == requested)
                    return component;
            }
            return {};
        }
    } // namespace

    Keire::Ref<Keire::RefCounted> InspectorTransformSceneScope::Identity() const noexcept
    {
        if (PlayMode)
            return Keire::Ref<Keire::RefCounted>(PlaySession.Lock());
        return EditGeneration.Lock();
    }

    InspectorTransformSceneScope CaptureInspectorTransformSceneScope(const SceneDocument& document)
    {
        InspectorTransformSceneScope result{.Asset = document.Asset()};
        if (const auto session = document.PlaySession())
        {
            result.PlayMode = true;
            result.PlaySession = session;
        }
        else
        {
            result.EditHistory = document.History();
            result.EditGeneration = document.EditGeneration();
        }
        return result;
    }

    Keire::Ref<Keire::Scene> ResolveInspectorTransformScene(const SceneDocument& document,
                                                            const InspectorTransformSceneScope& scope) noexcept
    {
        if (document.Asset() != scope.Asset)
            return {};
        if (scope.PlayMode)
        {
            const auto expectedSession = scope.PlaySession.Lock();
            if (!expectedSession || document.PlaySession() != expectedSession)
                return {};
            return expectedSession->RuntimeScene();
        }
        if (document.PlaySession())
            return {};
        const auto expectedGeneration = scope.EditGeneration.Lock();
        if (!expectedGeneration || document.EditGeneration() != expectedGeneration)
            return {};
        const auto expectedHistory = scope.EditHistory.Lock();
        if (expectedHistory ? document.History() != expectedHistory : static_cast<bool>(document.History()))
            return {};
        return document.EditingScene();
    }

    InspectorTransformEdit MakeInspectorTransformEdit(const Keire::EntityId entity,
                                                      const InspectorTransformProperty property,
                                                      InspectorTransformValue before, InspectorTransformValue after,
                                                      const std::uint64_t editSerial)
    {
        std::string name;
        std::string propertyName;
        switch (property)
        {
        case InspectorTransformProperty::Position:
            name = "Change Position";
            propertyName = "position";
            break;
        case InspectorTransformProperty::Rotation:
            name = "Change Rotation";
            propertyName = "rotation";
            break;
        case InspectorTransformProperty::Scale:
            name = "Change Scale";
            propertyName = "scale";
            break;
        }
        return {.Entity = entity,
                .Property = property,
                .Before = std::move(before),
                .After = std::move(after),
                .Name = std::move(name),
                .MergeKey = "transform." + propertyName + "." + entity.ToString() + "." + std::to_string(editSerial)};
    }

    std::unique_ptr<Keire::UndoCommand>
    CreateInspectorTransformUndoCommand(const Keire::AssetId scene, const bool playMode, InspectorTransformEdit edit,
                                        InspectorTransformApply apply, Keire::UndoAvailability available)
    {
        if (!edit.Entity)
            throw std::invalid_argument("A Transform undo edit requires an entity.");
        if (edit.Name.empty())
            throw std::invalid_argument("A Transform undo edit requires a name.");
        if (!ValidTransformValue(edit.Property, edit.Before) || !ValidTransformValue(edit.Property, edit.After))
            throw std::invalid_argument("A Transform undo edit value does not match its property.");
        if (!apply)
            throw std::invalid_argument("A Transform undo edit requires an apply operation.");
        return std::make_unique<InspectorTransformUndoCommand>(scene, playMode, std::move(edit), std::move(apply),
                                                               std::move(available));
    }

    Keire::Ref<Keire::Scene> SceneDocument::ActiveScene() const noexcept
    {
        if (m_PlaySession && m_PlaySession->State() != Keire::ScenePlayState::Stopped)
        {
            if (const auto runtime = m_PlaySession->RuntimeScene(); runtime && runtime->IsOpen())
                return runtime;
        }
        return m_Scene && m_Scene->IsOpen() ? m_Scene : Keire::Ref<Keire::Scene>{};
    }

    bool SceneDocument::IsSelected(const Keire::AssetId entity) const noexcept
    {
        return std::ranges::find(m_Selections, entity) != m_Selections.end();
    }

    void SceneDocument::Select(const Keire::AssetId selection) noexcept { Select(selection, false); }

    void SceneDocument::Select(const Keire::AssetId selection, const bool additive) noexcept
    {
        const auto scene = ActiveScene();
        if (!selection || !scene || !scene->FindEntity(Keire::EntityId(selection)))
        {
            if (!additive)
                ClearSelection();
            return;
        }
        const auto found = std::ranges::find(m_Selections, selection);
        if (additive && found != m_Selections.end())
        {
            m_Selections.erase(found);
            m_Selection = m_Selections.empty() ? Keire::AssetId{} : m_Selections.back();
            return;
        }
        if (!additive)
            m_Selections.clear();
        if (std::ranges::find(m_Selections, selection) == m_Selections.end())
            m_Selections.push_back(selection);
        m_Selection = selection;
    }

    void SceneDocument::SetSelections(const std::span<const Keire::AssetId> selections) noexcept
    {
        m_Selections.clear();
        const auto scene = ActiveScene();
        if (!scene)
        {
            m_Selection = {};
            return;
        }
        for (const auto selection : selections)
        {
            if (selection && scene->FindEntity(Keire::EntityId(selection)) &&
                std::ranges::find(m_Selections, selection) == m_Selections.end())
                m_Selections.push_back(selection);
        }
        m_Selection = m_Selections.empty() ? Keire::AssetId{} : m_Selections.back();
    }

    void SceneDocument::SynchronizeSelection() noexcept
    {
        const auto scene = ActiveScene();
        if (!scene || !m_Selection)
        {
            ClearSelection();
            return;
        }
        std::erase_if(m_Selections,
                      [&scene](const auto entity) { return !scene->FindEntity(Keire::EntityId(entity)); });
        if (!scene->FindEntity(Keire::EntityId(m_Selection)))
            m_Selection = m_Selections.empty() ? Keire::AssetId{} : m_Selections.back();
        else if (std::ranges::find(m_Selections, m_Selection) == m_Selections.end())
            m_Selections = {m_Selection};
    }

    void SceneDocument::ClearSelection() noexcept
    {
        m_Selection = {};
        m_Selections.clear();
    }

    bool SceneDocument::IsValidEntityName(const std::string_view name) noexcept
    {
        return !name.empty() && name.size() <= MaximumEntityNameBytes;
    }

    void SceneDocument::RenameEntity(const Keire::EntityId entity, std::string name)
    {
        if (!IsValidEntityName(name))
            throw std::invalid_argument("Entity name is empty or exceeds 256 UTF-8 bytes.");
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target)
            throw std::invalid_argument("Cannot rename an entity outside the active scene.");
        target.SetName(std::move(name));
    }

    Keire::EntityId SceneDocument::CreateEntity(std::string name, const Keire::EntityId parent,
                                                const Keire::ComponentTypeId component)
    {
        const auto scene = ActiveScene();
        const auto parentEntity = parent && scene ? scene->FindEntity(parent) : Keire::Entity{};
        if (!scene || (parent && !parentEntity))
            throw std::invalid_argument("Cannot create an entity outside the active scene.");
        auto created = scene->CreateEntity(std::move(name), parentEntity);
        if (component)
            (void)created.AddComponent(component);
        return created.Id();
    }

    Keire::EntityId SceneDocument::DuplicateEntity(const Keire::EntityId entity)
    {
        const auto scene = ActiveScene();
        const auto source = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!source)
            throw std::invalid_argument("Cannot duplicate an entity outside the active scene.");
        return scene->DuplicateEntity(entity).Id();
    }

    void SceneDocument::DeleteEntity(const Keire::EntityId entity)
    {
        const auto scene = ActiveScene();
        if (!scene || !scene->DestroyEntity(entity))
            throw std::invalid_argument("Cannot delete an entity outside the active scene.");
        if (m_Selection == entity.Value())
            m_Selection = {};
        SynchronizeSelection();
    }

    void SceneDocument::SetEntityActive(const Keire::EntityId entity, const bool active)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target)
            throw std::invalid_argument("Cannot activate an entity outside the active scene.");
        target.SetActive(active);
    }

    void SceneDocument::SetEntitiesActive(const std::span<const Keire::AssetId> entities, const bool active)
    {
        const auto scene = ActiveScene();
        std::vector<Keire::Entity> targets;
        targets.reserve(entities.size());
        for (const auto entity : entities)
        {
            auto target = scene ? scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
            if (!target)
                throw std::invalid_argument("Cannot activate an entity outside the active scene.");
            targets.push_back(std::move(target));
        }
        for (auto& target : targets)
            target.SetActive(active);
    }

    void SceneDocument::SetEntityLayer(const Keire::EntityId entity, const std::uint32_t layer)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target)
            throw std::invalid_argument("Cannot change the layer of an entity outside the active scene.");
        target.SetLayer(layer);
    }

    void SceneDocument::SetEntitiesLayer(const std::span<const Keire::AssetId> entities, const std::uint32_t layer)
    {
        if (!Keire::IsValidEntityLayer(layer))
            throw std::invalid_argument("Entity layer must be between 0 and 31.");
        const auto scene = ActiveScene();
        std::vector<Keire::Entity> targets;
        targets.reserve(entities.size());
        for (const auto entity : entities)
        {
            auto target = scene ? scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
            if (!target)
                throw std::invalid_argument("Cannot change the layer of an entity outside the active scene.");
            targets.push_back(std::move(target));
        }
        for (auto& target : targets)
            target.SetLayer(layer);
    }

    void SceneDocument::SetEntityTags(const Keire::EntityId entity, std::vector<std::string> tags)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target)
            throw std::invalid_argument("Cannot change tags on an entity outside the active scene.");
        target.SetTags(std::move(tags));
    }

    void SceneDocument::ReparentEntity(const Keire::EntityId entity, const Keire::EntityId parent,
                                       const bool keepWorldTransform)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto newParent = parent && scene ? scene->FindEntity(parent) : Keire::Entity{};
        if (!target || (parent && !newParent))
            throw std::invalid_argument("Cannot reparent entities outside the active scene.");
        target.SetParent(newParent, keepWorldTransform);
    }

    void SceneDocument::MoveEntity(const Keire::EntityId entity, const Keire::EntityId parent,
                                   const Keire::EntityId beforeSibling, const bool keepWorldTransform)
    {
        const std::array entities{entity};
        (void)MoveEntities(entities, parent, beforeSibling, keepWorldTransform);
    }

    std::vector<Keire::EntityId> SceneDocument::MoveEntities(const std::span<const Keire::EntityId> entities,
                                                             const Keire::EntityId parent,
                                                             const Keire::EntityId beforeSibling,
                                                             const bool keepWorldTransform)
    {
        const auto scene = ActiveScene();
        if (!scene || (parent && !scene->FindEntity(parent)) || (beforeSibling && !scene->FindEntity(beforeSibling)))
            throw std::invalid_argument("Cannot move entities outside the active scene.");

        std::vector<Keire::EntityId> requested;
        requested.reserve(entities.size());
        for (const auto entity : entities)
        {
            if (!entity || !scene->FindEntity(entity))
                throw std::invalid_argument("Cannot move entities outside the active scene.");
            if (std::ranges::find(requested, entity) == requested.end())
                requested.push_back(entity);
        }
        if (requested.empty())
            return {};
        if (beforeSibling && std::ranges::find(requested, beforeSibling) != requested.end())
            throw std::invalid_argument("The insertion target cannot be part of the moved selection.");

        if (beforeSibling)
        {
            const auto siblingParent = scene->FindEntity(beforeSibling).Parent();
            if ((parent && (!siblingParent || siblingParent.Id() != parent)) || (!parent && siblingParent))
                throw std::invalid_argument("The insertion sibling must belong to the requested parent.");
        }

        for (auto destination = parent ? scene->FindEntity(parent) : Keire::Entity{}; destination;
             destination = destination.Parent())
        {
            if (std::ranges::find(requested, destination.Id()) != requested.end())
                throw std::invalid_argument("Entities cannot be moved below themselves or their descendants.");
        }

        std::vector<Keire::EntityId> roots;
        roots.reserve(requested.size());
        for (const auto entity : requested)
        {
            bool selectedAncestor = false;
            for (auto ancestor = scene->FindEntity(entity).Parent(); ancestor; ancestor = ancestor.Parent())
            {
                if (std::ranges::find(requested, ancestor.Id()) != requested.end())
                {
                    selectedAncestor = true;
                    break;
                }
            }
            if (!selectedAncestor)
                roots.push_back(entity);
        }

        Keire::Matrix4 destinationInverse;
        if (parent && keepWorldTransform)
        {
            const auto parentTransform = scene->FindEntity(parent).GetComponent<Keire::TransformComponent>();
            if (!parentTransform)
                throw std::invalid_argument("The destination parent has no Transform.");
            destinationInverse = Keire::Math::Inverse(parentTransform->WorldMatrix());
        }
        if (keepWorldTransform)
        {
            for (const auto entity : roots)
            {
                const auto transform = scene->FindEntity(entity).GetComponent<Keire::TransformComponent>();
                if (!transform)
                    throw std::invalid_argument("Every moved entity must have a Transform.");
                const auto local = parent ? Keire::Math::Multiply(destinationInverse, transform->WorldMatrix())
                                          : transform->WorldMatrix();
                Keire::Vector3 position;
                Keire::Quaternion rotation;
                Keire::Vector3 scale;
                if (!Keire::Math::DecomposeTransform(local, position, rotation, scale))
                    throw std::invalid_argument("Moving the selection would produce a non-decomposable Transform.");
            }
        }

        for (const auto entity : roots)
            scene->MoveEntity(entity, parent, beforeSibling, keepWorldTransform);
        return roots;
    }

    void SceneDocument::SetTransform(const Keire::EntityId entity, const TransformValues& values)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto transform = target ? target.GetComponent<Keire::TransformComponent>() : nullptr;
        if (!transform)
            throw std::invalid_argument("Transform editing requires an entity in the active scene.");
        if (values.Position && !Keire::Math::IsFinite(*values.Position))
            throw std::invalid_argument("Transform position must be finite.");
        if (values.EulerDegrees && !Keire::Math::IsFinite(*values.EulerDegrees))
            throw std::invalid_argument("Transform Euler angles must be finite.");
        if (values.Scale && !Keire::TransformComponent::IsValidLocalScale(*values.Scale))
            throw std::invalid_argument("Transform scale axes must be finite with a magnitude of at least 0.000001.");
        if (values.Position)
            transform->SetLocalPosition(*values.Position);
        if (values.EulerDegrees)
            transform->SetLocalEulerAngles(*values.EulerDegrees);
        if (values.Scale)
            transform->SetLocalScale(*values.Scale);
    }

    void SceneDocument::SetTransforms(const std::span<const Keire::AssetId> entities, const TransformValues& values)
    {
        if (values.Position && !Keire::Math::IsFinite(*values.Position))
            throw std::invalid_argument("Transform position must be finite.");
        if (values.EulerDegrees && !Keire::Math::IsFinite(*values.EulerDegrees))
            throw std::invalid_argument("Transform Euler angles must be finite.");
        if (values.Scale && !Keire::TransformComponent::IsValidLocalScale(*values.Scale))
            throw std::invalid_argument("Transform scale axes must be finite with a magnitude of at least 0.000001.");

        const auto scene = ActiveScene();
        std::vector<Keire::Ref<Keire::TransformComponent>> transforms;
        transforms.reserve(entities.size());
        for (const auto entity : entities)
        {
            const auto target = scene ? scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
            const auto transform = target ? target.GetComponent<Keire::TransformComponent>() : nullptr;
            if (!transform)
                throw std::invalid_argument("Transform editing requires entities in the active scene.");
            transforms.push_back(transform);
        }
        for (const auto& transform : transforms)
        {
            if (values.Position)
                transform->SetLocalPosition(*values.Position);
            if (values.EulerDegrees)
                transform->SetLocalEulerAngles(*values.EulerDegrees);
            if (values.Scale)
                transform->SetLocalScale(*values.Scale);
        }
    }

    Keire::Ref<Keire::Component> SceneDocument::AddComponent(const Keire::EntityId entity,
                                                             const Keire::ComponentTypeId type)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target)
            throw std::invalid_argument("Cannot add a component outside the active scene.");
        auto component = target.AddComponent(type);
        if (component)
            FitNewPhysicsComponentToBuiltinMesh(target, component);
        return component;
    }

    void SceneDocument::RemoveComponent(const Keire::EntityId entity, const Keire::ComponentTypeId type)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target || !target.RemoveComponent(type))
            throw std::invalid_argument("Cannot remove that component from the active scene entity.");
    }

    void SceneDocument::RemoveComponent(const Keire::EntityId entity, const Keire::Ref<Keire::Component>& component)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target || !target.RemoveComponent(component))
            throw std::invalid_argument("Cannot remove that component from the active scene entity.");
    }

    void SceneDocument::MoveComponentBefore(const Keire::EntityId entity, const Keire::Ref<Keire::Component>& component,
                                            const Keire::Ref<Keire::Component>& before)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target)
            throw std::invalid_argument("Cannot reorder components outside the active scene.");
        target.MoveComponentBefore(component, before);
    }

    void SceneDocument::SetComponentEnabled(const Keire::EntityId entity, const Keire::ComponentTypeId type,
                                            const bool enabled)
    {
        const auto scene = ActiveScene();
        const auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto component = target ? target.GetComponent(type) : nullptr;
        if (!component)
            throw std::invalid_argument("Cannot edit a component outside the active scene.");
        component->SetEnabled(enabled);
    }

    void SceneDocument::SetComponentsEnabled(const std::span<const Keire::AssetId> entities,
                                             const Keire::ComponentTypeId type, const bool enabled,
                                             const std::size_t ordinal)
    {
        const auto scene = ActiveScene();
        std::vector<Keire::Ref<Keire::Component>> components;
        components.reserve(entities.size());
        for (const auto entity : entities)
        {
            const auto target = scene ? scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
            const auto component = target ? ComponentAtOrdinal(target, type, ordinal) : nullptr;
            if (!component)
                throw std::invalid_argument("Multi-edit requires a common component on every selected entity.");
            components.push_back(component);
        }
        for (const auto& component : components)
            component->SetEnabled(enabled);
    }

    void SceneDocument::SetComponentValues(const Keire::EntityId entity, const Keire::Ref<Keire::Component>& component,
                                           const Keire::ComponentPropertyBag& values)
    {
        const auto scene = ActiveScene();
        const auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto components = target ? target.GetComponents() : std::vector<Keire::Ref<Keire::Component>>{};
        const auto registration = component && scene ? scene->Components()->Find(component->Type()) : std::nullopt;
        if (!target || std::ranges::find(components, component) == components.end() || !registration)
            throw std::invalid_argument("Cannot edit a component outside the active scene.");
        auto validation = registration->Factory();
        registration->Deserialize(*validation, values, registration->SchemaVersion);
        registration->Deserialize(*component, values, registration->SchemaVersion);
        scene->MarkDirty();
    }

    void SceneDocument::ResetComponent(const Keire::EntityId entity, const Keire::ComponentTypeId type)
    {
        const auto scene = ActiveScene();
        const auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto component = target ? target.GetComponent(type) : nullptr;
        const auto registration = scene ? scene->Components()->Find(type) : std::nullopt;
        if (!component || !registration)
            throw std::invalid_argument("Cannot reset a component outside the active scene.");
        const auto defaults = registration->Factory();
        if (!defaults)
            throw std::runtime_error("The component factory returned null while resetting the component.");
        registration->Deserialize(*component, registration->Serialize(*defaults), registration->SchemaVersion);
    }

    void SceneDocument::ResetComponents(const std::span<const Keire::AssetId> entities,
                                        const Keire::ComponentTypeId type, const std::size_t ordinal)
    {
        const auto scene = ActiveScene();
        const auto registration = scene ? scene->Components()->Find(type) : std::nullopt;
        if (!registration)
            throw std::invalid_argument("Cannot reset components outside the active scene.");
        const auto defaults = registration->Factory();
        if (!defaults)
            throw std::runtime_error("The component factory returned null while resetting components.");
        const auto values = registration->Serialize(*defaults);
        struct Candidate final
        {
            Keire::Ref<Keire::Component> Component;
            Keire::ComponentPropertyBag Original;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(entities.size());
        for (const auto entity : entities)
        {
            const auto target = scene->FindEntity(Keire::EntityId(entity));
            const auto component = target ? ComponentAtOrdinal(target, type, ordinal) : nullptr;
            if (!component)
                throw std::invalid_argument("Multi-edit requires a common component on every selected entity.");
            candidates.push_back({component, registration->Serialize(*component)});
        }

        std::size_t applied = 0;
        try
        {
            for (; applied < candidates.size(); ++applied)
                registration->Deserialize(*candidates[applied].Component, values, registration->SchemaVersion);
        }
        catch (...)
        {
            const auto rollbackCount = std::min(applied + 1U, candidates.size());
            for (std::size_t rollback = rollbackCount; rollback > 0; --rollback)
            {
                try
                {
                    registration->Deserialize(*candidates[rollback - 1U].Component, candidates[rollback - 1U].Original,
                                              registration->SchemaVersion);
                }
                catch (...)
                {
                }
            }
            throw;
        }
        if (!candidates.empty())
            scene->MarkDirty();
    }

    void SceneDocument::SetComponentProperty(const Keire::EntityId entity, const Keire::ComponentTypeId type,
                                             const std::string_view property, Keire::ComponentPropertyValue value)
    {
        const auto scene = ActiveScene();
        const auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto component = target ? target.GetComponent(type) : nullptr;
        const auto registration = scene ? scene->Components()->Find(type) : std::nullopt;
        if (!component || !registration)
            throw std::invalid_argument("Cannot edit a component outside the active scene.");
        if (std::ranges::find(registration->Properties, property, &Keire::ComponentProperty::Key) ==
            registration->Properties.end())
            throw std::invalid_argument("The component does not declare that property.");
        auto values = registration->Serialize(*component);
        values.insert_or_assign(std::string(property), std::move(value));
        auto validation = registration->Factory();
        if (!validation)
            throw std::runtime_error("The component factory returned null while validating an edit.");
        registration->Deserialize(*validation, values, registration->SchemaVersion);
        registration->Deserialize(*component, values, registration->SchemaVersion);
        scene->MarkDirty();
    }

    void SceneDocument::SetComponentsProperty(const std::span<const Keire::AssetId> entities,
                                              const Keire::ComponentTypeId type, const std::string_view property,
                                              const Keire::ComponentPropertyValue& value, const std::size_t ordinal)
    {
        SetComponentsProperties(entities, type, {{std::string(property), value}}, ordinal);
    }

    void SceneDocument::SetComponentsProperties(const std::span<const Keire::AssetId> entities,
                                                const Keire::ComponentTypeId type,
                                                const Keire::ComponentPropertyBag& updates, const std::size_t ordinal)
    {
        const auto scene = ActiveScene();
        const auto registration = scene ? scene->Components()->Find(type) : std::nullopt;
        if (!registration)
            throw std::invalid_argument("Cannot multi-edit components outside the active scene.");
        for (const auto& [property, value] : updates)
        {
            (void)value;
            if (std::ranges::find(registration->Properties, property, &Keire::ComponentProperty::Key) ==
                registration->Properties.end())
            {
                throw std::invalid_argument("The common component does not declare property '" + property + "'.");
            }
        }

        struct Candidate final
        {
            Keire::Ref<Keire::Component> Component;
            Keire::ComponentPropertyBag Original;
            Keire::ComponentPropertyBag Values;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(entities.size());
        for (const auto entity : entities)
        {
            const auto target = scene->FindEntity(Keire::EntityId(entity));
            const auto component = target ? ComponentAtOrdinal(target, type, ordinal) : nullptr;
            if (!component)
                throw std::invalid_argument("Multi-edit requires a common component on every selected entity.");
            auto original = registration->Serialize(*component);
            auto values = original;
            for (const auto& [property, value] : updates)
                values.insert_or_assign(property, value);
            const auto validation = registration->Factory();
            if (!validation)
                throw std::runtime_error("The component factory returned null while validating a multi-edit.");
            registration->Deserialize(*validation, values, registration->SchemaVersion);
            candidates.push_back({component, std::move(original), std::move(values)});
        }

        std::size_t applied = 0;
        try
        {
            for (; applied < candidates.size(); ++applied)
                registration->Deserialize(*candidates[applied].Component, candidates[applied].Values,
                                          registration->SchemaVersion);
        }
        catch (...)
        {
            const auto rollbackCount = std::min(applied + 1U, candidates.size());
            for (std::size_t rollback = rollbackCount; rollback > 0; --rollback)
            {
                try
                {
                    registration->Deserialize(*candidates[rollback - 1U].Component, candidates[rollback - 1U].Original,
                                              registration->SchemaVersion);
                }
                catch (...)
                {
                }
            }
            throw;
        }
        if (!candidates.empty())
            scene->MarkDirty();
    }

    void SceneDocument::SetMeshRendererMaterial(const Keire::EntityId entity, const std::size_t slot,
                                                const Keire::AssetId material)
    {
        const auto scene = ActiveScene();
        const auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto renderer = target ? target.GetComponent<Keire::MeshRendererComponent>() : nullptr;
        if (!renderer)
            throw std::invalid_argument("Material editing requires a Mesh Renderer in the active scene.");
        renderer->SetMaterial(slot, material);
        scene->MarkDirty();
    }

    void SceneDocument::SetMeshRenderersMaterial(const std::span<const Keire::AssetId> entities, const std::size_t slot,
                                                 const Keire::AssetId material)
    {
        const auto scene = ActiveScene();
        struct Candidate final
        {
            Keire::Ref<Keire::MeshRendererComponent> Renderer;
            std::vector<Keire::AssetId> OriginalMaterials;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(entities.size());
        for (const auto entity : entities)
        {
            const auto target = scene ? scene->FindEntity(Keire::EntityId(entity)) : Keire::Entity{};
            const auto renderer = target ? target.GetComponent<Keire::MeshRendererComponent>() : nullptr;
            if (!renderer)
                throw std::invalid_argument("Multi-edit requires a Mesh Renderer on every selected entity.");
            candidates.push_back({renderer, {renderer->Materials().begin(), renderer->Materials().end()}});
        }

        std::size_t applied = 0;
        try
        {
            for (; applied < candidates.size(); ++applied)
                candidates[applied].Renderer->SetMaterial(slot, material);
        }
        catch (...)
        {
            const auto rollbackCount = std::min(applied + 1U, candidates.size());
            for (std::size_t rollback = rollbackCount; rollback > 0; --rollback)
            {
                try
                {
                    candidates[rollback - 1U].Renderer->SetMaterials(candidates[rollback - 1U].OriginalMaterials);
                }
                catch (...)
                {
                }
            }
            throw;
        }
        if (!candidates.empty())
            scene->MarkDirty();
    }

    void SceneDocument::Open(Keire::Ref<Keire::Scene> scene, const Keire::AssetId asset, std::filesystem::path source,
                             Keire::Ref<Keire::UndoContext> undo)
    {
        if (!scene)
            throw std::invalid_argument("SceneDocument::Open requires a scene.");
        Close();
        m_Scene = std::move(scene);
        if (!source.empty())
        {
            const auto sourceName = source.stem().string();
            if (!sourceName.empty() && m_Scene->Name() != sourceName)
            {
                const bool wasDirty = m_Scene->Dirty();
                m_Scene->SetName(sourceName);
                if (!wasDirty)
                    m_Scene->MarkSaved();
            }
        }
        m_EditGeneration = Keire::CreateRef<SceneDocumentEditGeneration>();
        m_Asset = asset ? asset : m_Scene->Asset();
        m_Source = std::move(source);
        m_Undo = std::move(undo);
    }

    void SceneDocument::ReplaceEditingScene(Keire::Ref<Keire::Scene> scene, const bool preserveSelection)
    {
        if (!scene)
            throw std::invalid_argument("SceneDocument::ReplaceEditingScene requires a scene.");
        // Viewport and panel callbacks can retain a scene Ref until the current UI frame completes. Releasing the
        // document's ownership lets the old scene close when its final alias is gone instead of invalidating it here.
        m_Scene = std::move(scene);
        if (preserveSelection)
            SynchronizeSelection();
        else
            ClearSelection();
    }

    void SceneDocument::SetLoadOperation(Keire::Ref<Keire::SceneLoadOperation> operation) noexcept
    {
        m_LoadOperation = std::move(operation);
    }

    void SceneDocument::SetSaveDialog(Keire::Ref<Keire::SaveFileDialogOperation> operation) noexcept
    {
        m_SaveDialog = std::move(operation);
    }

    Keire::Ref<Keire::SaveFileDialogOperation> SceneDocument::TakeSaveDialog() noexcept
    {
        auto operation = std::move(m_SaveDialog);
        m_SaveDialog.Reset();
        return operation;
    }

    void SceneDocument::SetUndoContext(Keire::Ref<Keire::UndoContext> undo) noexcept { m_Undo = std::move(undo); }

    void SceneDocument::SetIdentity(const Keire::AssetId asset, std::filesystem::path source)
    {
        m_Asset = asset;
        m_Source = std::move(source);
    }

    void SceneDocument::SetRecoveryPath(std::filesystem::path path) { m_RecoveryPath = std::move(path); }

    void SceneDocument::SetStatus(std::string status) { m_Status = std::move(status); }

    void SceneDocument::Save()
    {
        if (!m_Scene || m_Source.empty())
            throw std::logic_error("SceneDocument cannot save without an editing scene and source path.");
        const auto bytes = Keire::SceneAsset::Encode(m_Scene->Snapshot());
        const std::string contents(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::Detail::WriteTextFileAtomically(m_Source, contents);
        m_Scene->MarkSaved();
        DiscardRecovery();
    }

    bool SceneDocument::WriteRecovery()
    {
        if (!m_Scene || !m_Scene->Dirty() || m_RecoveryPath.empty())
            return false;
        const auto bytes = Keire::SceneAsset::Encode(m_Scene->Snapshot());
        const std::string contents(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::Detail::WriteTextFileAtomically(m_RecoveryPath, contents);
        m_RecoveryAvailable = true;
        m_RecoverySeconds = 0.0;
        return true;
    }

    void SceneDocument::RestoreRecovery()
    {
        if (!m_Scene || m_RecoveryPath.empty() || !std::filesystem::is_regular_file(m_RecoveryPath))
            throw std::logic_error("SceneDocument has no recovery snapshot to restore.");
        const auto source = Keire::Detail::ReadTextFile(m_RecoveryPath, std::size_t{64U} * 1024U * 1024U);
        const auto bytes = std::as_bytes(std::span(source.data(), source.size()));
        auto restored = Keire::CreateRef<Keire::Scene>(m_Asset, Keire::SceneAsset::Decode(bytes)->Definition(),
                                                       m_Scene->Components());
        restored->MarkDirty();
        ReplaceEditingScene(std::move(restored), true);
        m_RecoveryAvailable = false;
        m_RecoverySeconds = 0.0;
    }

    void SceneDocument::DiscardRecovery() noexcept
    {
        if (!m_RecoveryPath.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(m_RecoveryPath, ignored);
        }
        m_RecoveryAvailable = false;
        m_RecoverySeconds = 0.0;
    }

    void SceneDocument::AdvanceRecovery(const double seconds) noexcept
    {
        if (seconds > 0.0)
            m_RecoverySeconds += seconds;
    }

    void SceneDocument::BeginPlay(Keire::Ref<Keire::UndoContext> playUndo, Keire::Ref<Keire::AssetSystem> assets,
                                  Keire::Ref<Keire::AudioSystem> audio, Keire::Ref<Keire::PhysicsSystem> physics,
                                  const Keire::AssetId defaultMixer,
                                  const Keire::Ref<Keire::SceneRuntimeWorld>& runtimeWorld)
    {
        if (!m_Scene)
            throw std::logic_error("SceneDocument cannot enter Play without an editing scene.");
        if (m_PlaySession && m_PlaySession->State() != Keire::ScenePlayState::Stopped)
            throw std::logic_error("SceneDocument is already in Play.");
        m_PlaySession = Keire::CreateRef<Keire::SceneRuntimeSession>(m_Scene, std::move(assets), std::move(audio),
                                                                     std::move(physics));
        if (const auto presentation = m_PlaySession->Presentation())
            presentation->SetDefaultMixer(defaultMixer);
        m_PlayUndo = std::move(playUndo);
        m_PlaySession->Play();
        if (runtimeWorld)
            (void)runtimeWorld->Adopt(m_PlaySession);
        SynchronizeSelection();
    }

    void SceneDocument::SetPlaySession(Keire::Ref<Keire::SceneRuntimeSession> session)
    {
        if (!m_PlaySession || !session)
            throw std::logic_error("SceneDocument can only replace an active Play session.");
        m_PlaySession = std::move(session);
        SynchronizeSelection();
    }

    void SceneDocument::EndPlay() noexcept
    {
        if (m_PlaySession)
            m_PlaySession->Stop();
        m_PlaySession.Reset();
        if (m_PlayUndo)
            m_PlayUndo->Close();
        m_PlayUndo.Reset();
        SynchronizeSelection();
    }

    void SceneDocument::Close() noexcept
    {
        if (m_PlaySession)
            m_PlaySession->Stop();
        m_PlaySession.Reset();
        if (m_PlayUndo)
            m_PlayUndo->Close();
        m_PlayUndo.Reset();
        m_LoadOperation.Reset();
        m_SaveDialog.Reset();
        if (m_Scene)
            m_Scene->Close();
        m_Scene.Reset();
        m_EditGeneration.Reset();
        m_Undo.Reset();
        m_Asset = {};
        ClearSelection();
        m_Source.clear();
        m_RecoveryPath.clear();
        m_Status.clear();
        m_RecoverySeconds = 0.0;
        m_RecoveryAvailable = false;
    }
} // namespace KeireEditor
