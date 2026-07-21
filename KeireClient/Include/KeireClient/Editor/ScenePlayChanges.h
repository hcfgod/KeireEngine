#pragma once

#include "Keire/Core.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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

    struct ScenePlayChangePath
    {
        ScenePlayChangeKind Kind = ScenePlayChangeKind::ComponentProperty;
        Keire::AssetId Entity;
        Keire::ComponentTypeId Component;
        std::string Property;

        [[nodiscard]] bool operator==(const ScenePlayChangePath&) const noexcept = default;
    };

    struct ScenePlayChangePathHash
    {
        [[nodiscard]] std::size_t operator()(const ScenePlayChangePath& path) const noexcept;
    };

    class ScenePlayChangeTracker final
    {
      public:
        void RecordMutation(const Keire::SceneDefinition& before, const Keire::SceneDefinition& after);
        void Clear() noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] ScenePlayChangeOrigin Origin(const ScenePlayChangePath& path, std::string_view finalValue) const;

      private:
        std::unordered_map<ScenePlayChangePath, std::string, ScenePlayChangePathHash> m_AuthoredValues;
    };

    struct ScenePlayChange
    {
        std::size_t Id = 0;
        ScenePlayChangeKind Kind = ScenePlayChangeKind::ComponentProperty;
        ScenePlayChangeOrigin Origin = ScenePlayChangeOrigin::Runtime;
        Keire::AssetId Entity;
        Keire::ComponentTypeId Component;
        Keire::AssetId RequiredParent;
        std::string Property;
        std::string EntityName;
        std::string ComponentName;
        std::string Label;
        std::string Before;
        std::string After;
        bool Selected = false;
        bool Locked = false;
        bool CanKeepAtRoot = false;
        bool KeepAtRoot = false;
        std::string LockReason;
        std::string Conflict;
    };

    class ScenePlayChangeSet final
    {
      public:
        ScenePlayChangeSet(Keire::Ref<Keire::Scene> editingScene, Keire::Ref<Keire::Scene> runtimeScene,
                           std::unordered_set<Keire::AssetId> editorTouchedEntities = {});
        ScenePlayChangeSet(Keire::Ref<Keire::Scene> editingScene, Keire::Ref<Keire::Scene> runtimeScene,
                           const ScenePlayChangeTracker& tracker);
        ~ScenePlayChangeSet();

        ScenePlayChangeSet(const ScenePlayChangeSet&) = delete;
        ScenePlayChangeSet& operator=(const ScenePlayChangeSet&) = delete;

        [[nodiscard]] std::span<const ScenePlayChange> Changes() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] bool HasSelectedChanges() const noexcept;
        void SetSelected(std::size_t id, bool selected);
        void SetAllSelected(bool selected);
        void KeepCreatedEntityAtRoot(Keire::AssetId entity, bool keepAtRoot);
        [[nodiscard]] Keire::SceneDefinition BuildAppliedDefinition() const;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireEditor
