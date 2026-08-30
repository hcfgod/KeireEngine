#pragma once

#include "Keire/Assets/AssetSystem.h"
#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Ui/UiToolkit.h"
#include "KeireInternal/Scenes/ScenePresentationCanvasProjectionInternal.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    class ScenePresentationUiDocumentStore final
    {
      public:
        ScenePresentationUiDocumentStore(Ref<AssetSystem> assets, Ref<RuntimeUiTree> tree,
                                         std::size_t maximumUiElements);
        ~ScenePresentationUiDocumentStore();

        ScenePresentationUiDocumentStore(const ScenePresentationUiDocumentStore&) = delete;
        ScenePresentationUiDocumentStore& operator=(const ScenePresentationUiDocumentStore&) = delete;

        void BeginSynchronization() noexcept;
        void Synchronize(const Entity& entity, const Ref<UiDocumentComponent>& component,
                         std::map<EntityId, RuntimeUiElementId>& uiNodes,
                         std::map<std::uint64_t, EntityId>& nodeEntities, ScenePresentationCanvasProjection& projection,
                         std::vector<UiDocumentPanelProjection>& projections, std::set<EntityId>& seenUi);
        void EndSynchronization(std::map<EntityId, RuntimeUiElementId>& uiNodes,
                                std::map<std::uint64_t, EntityId>& nodeEntities);
        void Clear(std::map<EntityId, RuntimeUiElementId>& uiNodes,
                   std::map<std::uint64_t, EntityId>& nodeEntities) noexcept;

        [[nodiscard]] std::optional<ScenePresentationUiDocumentElement> Root(EntityId document) const;
        [[nodiscard]] std::optional<ScenePresentationUiDocumentElement> Find(EntityId document, AssetId stableId) const;
        [[nodiscard]] std::optional<ScenePresentationUiDocumentElement> Find(EntityId document,
                                                                             std::string_view name) const;
        [[nodiscard]] bool Alive(EntityId document, std::uint64_t generation, std::uint64_t element) const noexcept;
        [[nodiscard]] std::optional<std::string> ReadText(EntityId document, std::uint64_t generation,
                                                          std::uint64_t element) const noexcept;
        [[nodiscard]] bool SetText(EntityId document, std::uint64_t generation, std::uint64_t element,
                                   std::string_view text) noexcept;
        [[nodiscard]] std::optional<float> ReadValue(EntityId document, std::uint64_t generation,
                                                     std::uint64_t element) const noexcept;
        [[nodiscard]] bool SetValue(EntityId document, std::uint64_t generation, std::uint64_t element,
                                    float value) noexcept;
        [[nodiscard]] std::optional<bool> ReadFlag(EntityId document, std::uint64_t generation, std::uint64_t element,
                                                   ScenePresentationUiDocumentFlag property) const noexcept;
        [[nodiscard]] bool SetFlag(EntityId document, std::uint64_t generation, std::uint64_t element,
                                   ScenePresentationUiDocumentFlag property, bool value) noexcept;
        [[nodiscard]] bool ConsumeEvent(EntityId document, std::uint64_t generation, std::uint64_t element,
                                        RuntimeUiEventType type, std::deque<RuntimeUiEvent>& events) const noexcept;
        [[nodiscard]] bool Focus(EntityId document, std::uint64_t generation, std::uint64_t element) noexcept;
        [[nodiscard]] Ref<Ui::VisualElement> Visual(EntityId document, AssetId stableId) const noexcept;
        void SetBindingSource(EntityId document, Ref<UiDocumentBindingSource> source);
        [[nodiscard]] bool DispatchEvent(const RuntimeUiEvent& event);
        void Update(float deltaSeconds);
        void SynchronizeInteractionStates();
        [[nodiscard]] std::optional<ScenePresentationUiDocumentDebugSnapshot>
        DebugSnapshot(EntityId document, const std::deque<RuntimeUiEvent>& deferredEvents) const;
        [[nodiscard]] std::optional<ScenePresentationUiDocumentHit> Hit(EntityId document,
                                                                        RuntimeUiElementId element) const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire::Detail
