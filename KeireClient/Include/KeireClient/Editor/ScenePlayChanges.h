#pragma once

#include "Keire/Core.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace KeireEditor
{
    enum class ScenePlayChangeKind : std::uint8_t
    {
        SceneName,
        CreateEntity,
        DeleteEntity,
        EntityName,
        EntityActive,
        EntityParent,
        AddComponent,
        RemoveComponent,
        ComponentEnabled,
        ComponentProperty,
        ReplaceUnknownComponent
    };

    enum class ScenePlayChangeOrigin : std::uint8_t
    {
        Runtime,
        Editor,
        Mixed
    };

    struct ScenePlayChange
    {
        std::size_t Id = 0;
        ScenePlayChangeKind Kind = ScenePlayChangeKind::ComponentProperty;
        ScenePlayChangeOrigin Origin = ScenePlayChangeOrigin::Runtime;
        Keire::AssetId Entity;
        Keire::ComponentTypeId Component;
        std::string Property;
        std::string EntityName;
        std::string ComponentName;
        std::string Label;
        std::string Before;
        std::string After;
        bool Selected = false;
        bool Locked = false;
    };

    class ScenePlayChangeSet final
    {
      public:
        ScenePlayChangeSet(Keire::Ref<Keire::Scene> editingScene, Keire::Ref<Keire::Scene> runtimeScene,
                           std::unordered_set<Keire::AssetId> editorTouchedEntities = {});
        ~ScenePlayChangeSet();

        ScenePlayChangeSet(const ScenePlayChangeSet&) = delete;
        ScenePlayChangeSet& operator=(const ScenePlayChangeSet&) = delete;

        [[nodiscard]] std::span<const ScenePlayChange> Changes() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] bool HasSelectedChanges() const noexcept;
        void SetSelected(std::size_t id, bool selected);
        void SetAllSelected(bool selected);
        [[nodiscard]] Keire::SceneDefinition BuildAppliedDefinition() const;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireEditor
