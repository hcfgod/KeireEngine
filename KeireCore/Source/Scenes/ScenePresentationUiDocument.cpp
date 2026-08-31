#include "KeireInternal/Scenes/ScenePresentationUiDocumentInternal.h"

#include "Keire/Scenes/Scene.h"
#include "Keire/Ui/UiElements.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace Keire::Detail
{
    namespace
    {
        void CollectTemplateReferences(const UiVisualElementDefinition& element, std::set<AssetId>& result)
        {
            if (element.Template)
                result.insert(element.Template);
            for (const auto& child : element.Children)
                CollectTemplateReferences(child, result);
        }

        [[nodiscard]] ScenePresentationUiDocumentDebugState MakeDebugState(const UiDocument& document,
                                                                           const RuntimeUiTree& tree,
                                                                           const RuntimeUiElementId element,
                                                                           const RuntimeUiElementState& state)
        {
            std::optional<AssetId> parent;
            if (state.Parent)
                if (const auto description = document.Describe(state.Parent))
                    parent = description->StableId;
            return {.Type = state.Type,
                    .Parent = parent,
                    .Style = state.Style,
                    .Content = state.Content,
                    .Control = state.Control,
                    .Rect = state.Rect,
                    .ClipRect = state.ClipRect,
                    .LayoutScale = state.LayoutScale,
                    .Visible = state.Visible,
                    .Enabled = state.Enabled,
                    .Interactable = state.Interactable,
                    .Focused = state.Focused,
                    .Hovered = state.Hovered,
                    .Pressed = state.Pressed,
                    .DirtyReasons = tree.DirtyReasons(element)};
        }
    } // namespace

    class ScenePresentationUiDocumentStore::Impl final
    {
      public:
        struct State final
        {
            AssetId VisualTree;
            AssetId PanelSettings;
            AssetHandle<UiVisualTreeAsset> VisualTreeHandle;
            AssetHandle<UiPanelSettingsAsset> PanelSettingsHandle;
            std::map<AssetId, AssetHandle<UiStyleSheetAsset>> StyleSheetHandles;
            std::map<AssetId, AssetHandle<UiVisualTreeAsset>> TemplateHandles;
            Ref<UiDocument> Instance;
            Ref<UiDocumentBindingSource> BindingSource;
            UiPanelSettingsDefinition Panel;
            std::uint64_t VisualTreeRevision = 0;
            std::uint64_t PanelSettingsRevision = 0;
            std::map<AssetId, std::uint64_t> StyleSheetRevisions;
            std::map<AssetId, std::uint64_t> TemplateRevisions;
            std::uint64_t Generation = 0;
            std::int32_t ComponentSortingOrder = 0;
            bool ReceivesInput = true;
        };

        Impl(Ref<AssetSystem> assets, Ref<RuntimeUiTree> tree, const std::size_t maximumUiElements)
            : Assets(std::move(assets)), Tree(std::move(tree)), MaximumUiElements(maximumUiElements)
        {
            if (!Assets || !Tree || MaximumUiElements == 0)
                throw std::invalid_argument("Scene UI Document store requires valid services and capacity.");
        }

        void RemoveInstance(const EntityId entity, State& state, std::map<EntityId, RuntimeUiElementId>& uiNodes,
                            std::map<std::uint64_t, EntityId>& nodeEntities)
        {
            if (!state.Instance)
                return;
            for (const auto node : state.Instance->Query({}))
            {
                const auto found = nodeEntities.find(node.Value());
                if (found != nodeEntities.end() && found->second == entity)
                    nodeEntities.erase(found);
            }
            if (const auto found = uiNodes.find(entity);
                found != uiNodes.end() && found->second == state.Instance->Root())
                uiNodes.erase(found);
            state.Instance.Reset();
        }

        void PublishProjection(const Entity& entity, State& state, std::map<EntityId, RuntimeUiElementId>& uiNodes,
                               std::map<std::uint64_t, EntityId>& nodeEntities,
                               ScenePresentationCanvasProjection& projection,
                               std::vector<UiDocumentPanelProjection>& projections)
        {
            if (!state.Instance)
                return;
            uiNodes.insert_or_assign(entity.Id(), state.Instance->Root());
            for (const auto node : state.Instance->Query({}))
            {
                nodeEntities.insert_or_assign(node.Value(), entity.Id());
                projection.Assign(node, state.Instance->Root());
            }
            if (entity.ActiveInHierarchy())
                projections.push_back({entity.Id(), state.Instance->Root(), state.Panel, state.ReceivesInput});
        }

        [[nodiscard]] std::uint64_t AllocateGeneration() noexcept
        {
            ++NextGeneration;
            if (NextGeneration == 0)
                ++NextGeneration;
            return NextGeneration;
        }

        [[nodiscard]] const State* FindState(const EntityId document, const std::uint64_t generation = 0) const noexcept
        {
            const auto found = Documents.find(document);
            if (found == Documents.end() || !found->second.Instance ||
                (generation != 0 && found->second.Generation != generation))
                return nullptr;
            return &found->second;
        }

        [[nodiscard]] State* FindState(const EntityId document, const std::uint64_t generation = 0) noexcept
        {
            return const_cast<State*>(std::as_const(*this).FindState(document, generation));
        }

        [[nodiscard]] std::optional<RuntimeUiElementId> Resolve(const EntityId document, const std::uint64_t generation,
                                                                const std::uint64_t element) const
        {
            const auto* state = FindState(document, generation);
            if (!state)
                return std::nullopt;
            const auto nodes = state->Instance->Query({});
            const auto found = std::ranges::find(nodes, element, &RuntimeUiElementId::Value);
            return found == nodes.end() ? std::nullopt : std::optional(*found);
        }

        [[nodiscard]] static std::optional<ScenePresentationUiDocumentElement>
        Describe(const State& state, const RuntimeUiElementId element)
        {
            const auto description = state.Instance->Describe(element);
            if (!description)
                return std::nullopt;
            return ScenePresentationUiDocumentElement{.DocumentGeneration = state.Generation,
                                                      .Element = element.Value(),
                                                      .StableId = description->StableId,
                                                      .Type = description->RuntimeType};
        }

        Ref<AssetSystem> Assets;
        Ref<RuntimeUiTree> Tree;
        std::size_t MaximumUiElements = 0;
        std::map<EntityId, State> Documents;
        std::set<EntityId> Seen;
        std::uint64_t NextGeneration = 0;
    };

    ScenePresentationUiDocumentStore::ScenePresentationUiDocumentStore(Ref<AssetSystem> assets, Ref<RuntimeUiTree> tree,
                                                                       const std::size_t maximumUiElements)
        : m_Impl(std::make_unique<Impl>(std::move(assets), std::move(tree), maximumUiElements))
    {
    }

    ScenePresentationUiDocumentStore::~ScenePresentationUiDocumentStore() = default;

    void ScenePresentationUiDocumentStore::BeginSynchronization() noexcept { m_Impl->Seen.clear(); }

    void ScenePresentationUiDocumentStore::Synchronize(const Entity& entity, const Ref<UiDocumentComponent>& component,
                                                       std::map<EntityId, RuntimeUiElementId>& uiNodes,
                                                       std::map<std::uint64_t, EntityId>& nodeEntities,
                                                       ScenePresentationCanvasProjection& projection,
                                                       std::vector<UiDocumentPanelProjection>& projections,
                                                       std::set<EntityId>& seenUi)
    {
        seenUi.insert(entity.Id());
        m_Impl->Seen.insert(entity.Id());
        auto& state = m_Impl->Documents[entity.Id()];
        state.ComponentSortingOrder = component->SortingOrder();
        state.ReceivesInput = component->ReceivesInput();
        if (!component->VisualTree())
        {
            m_Impl->RemoveInstance(entity.Id(), state, uiNodes, nodeEntities);
            state = {};
            return;
        }
        if (state.VisualTree != component->VisualTree())
        {
            state.VisualTree = component->VisualTree();
            state.VisualTreeHandle = m_Impl->Assets->Load<UiVisualTreeAsset>(state.VisualTree, AssetPriority::High);
        }
        if (state.PanelSettings != component->PanelSettings())
        {
            state.PanelSettings = component->PanelSettings();
            state.PanelSettingsHandle =
                state.PanelSettings
                    ? m_Impl->Assets->Load<UiPanelSettingsAsset>(state.PanelSettings, AssetPriority::High)
                    : AssetHandle<UiPanelSettingsAsset>{};
        }

        const auto visualTree = state.VisualTreeHandle.TryGetLoaded();
        const auto panel =
            state.PanelSettings ? state.PanelSettingsHandle.TryGetLoaded() : Ref<const UiPanelSettingsAsset>{};
        if (!visualTree || (state.PanelSettings && !panel))
        {
            m_Impl->PublishProjection(entity, state, uiNodes, nodeEntities, projection, projections);
            return;
        }

        const auto& styleIds = visualTree->Definition().StyleSheets;
        std::erase_if(state.StyleSheetHandles, [&styleIds](const auto& item)
                      { return std::ranges::find(styleIds, item.first) == styleIds.end(); });
        for (const auto style : styleIds)
            if (!state.StyleSheetHandles.contains(style))
                state.StyleSheetHandles.emplace(style,
                                                m_Impl->Assets->Load<UiStyleSheetAsset>(style, AssetPriority::High));
        std::vector<Ref<const UiStyleSheetAsset>> styleSheets;
        styleSheets.reserve(styleIds.size());
        std::map<AssetId, std::uint64_t> styleRevisions;
        for (const auto style : styleIds)
        {
            auto& handle = state.StyleSheetHandles.at(style);
            const auto loaded = handle.TryGetLoaded();
            if (!loaded)
            {
                m_Impl->PublishProjection(entity, state, uiNodes, nodeEntities, projection, projections);
                return;
            }
            styleSheets.push_back(loaded);
            styleRevisions.emplace(style, handle.Revision());
        }

        std::set<AssetId> pendingTemplates;
        CollectTemplateReferences(visualTree->Definition().Root, pendingTemplates);
        std::set<AssetId> visitedTemplates;
        std::map<AssetId, Ref<const UiVisualTreeAsset>> templateAssets;
        std::map<AssetId, std::uint64_t> templateRevisions;
        while (!pendingTemplates.empty())
        {
            const auto id = *pendingTemplates.begin();
            pendingTemplates.erase(pendingTemplates.begin());
            if (!visitedTemplates.insert(id).second)
                continue;
            if (visitedTemplates.size() > m_Impl->MaximumUiElements)
            {
                m_Impl->PublishProjection(entity, state, uiNodes, nodeEntities, projection, projections);
                return;
            }
            if (!state.TemplateHandles.contains(id))
                state.TemplateHandles.emplace(id, m_Impl->Assets->Load<UiVisualTreeAsset>(id, AssetPriority::High));
            auto& handle = state.TemplateHandles.at(id);
            const auto loaded = handle.TryGetLoaded();
            if (!loaded)
            {
                m_Impl->PublishProjection(entity, state, uiNodes, nodeEntities, projection, projections);
                return;
            }
            templateAssets.emplace(id, loaded);
            templateRevisions.emplace(id, handle.Revision());
            CollectTemplateReferences(loaded->Definition().Root, pendingTemplates);
        }
        std::erase_if(state.TemplateHandles,
                      [&visitedTemplates](const auto& item) { return !visitedTemplates.contains(item.first); });

        auto panelDefinition = panel ? panel->Definition() : UiPanelSettingsDefinition{};
        const auto combinedOrder = static_cast<std::int64_t>(panelDefinition.SortingOrder) +
                                   static_cast<std::int64_t>(state.ComponentSortingOrder);
        panelDefinition.SortingOrder = static_cast<std::int32_t>(
            std::clamp(combinedOrder, static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
                       static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
        const auto panelRevision = state.PanelSettings ? state.PanelSettingsHandle.Revision() : 0;
        const bool rebuild = !state.Instance || state.VisualTreeRevision != state.VisualTreeHandle.Revision() ||
                             state.PanelSettingsRevision != panelRevision ||
                             state.StyleSheetRevisions != styleRevisions ||
                             state.TemplateRevisions != templateRevisions;
        if (rebuild)
        {
            Ref<UiDocument> candidate;
            try
            {
                candidate = CreateRef<UiDocument>(visualTree, styleSheets, m_Impl->Tree, RuntimeUiElementId{},
                                                  [templates = std::move(templateAssets)](const AssetId id)
                                                  {
                                                      const auto found = templates.find(id);
                                                      return found == templates.end() ? Ref<const UiVisualTreeAsset>{}
                                                                                      : found->second;
                                                  });
                if (state.BindingSource)
                    candidate->SetBindingSource(state.BindingSource);
            }
            catch (...)
            {
                m_Impl->PublishProjection(entity, state, uiNodes, nodeEntities, projection, projections);
                return;
            }
            m_Impl->RemoveInstance(entity.Id(), state, uiNodes, nodeEntities);
            state.Instance = std::move(candidate);
            state.Generation = m_Impl->AllocateGeneration();
            state.VisualTreeRevision = state.VisualTreeHandle.Revision();
            state.PanelSettingsRevision = panelRevision;
            state.StyleSheetRevisions = std::move(styleRevisions);
            state.TemplateRevisions = std::move(templateRevisions);
        }

        if (!state.Instance)
            return;
        if (const auto rootState = m_Impl->Tree->State(state.Instance->Root()))
        {
            auto style = rootState->Style;
            style.SortingOrder = panelDefinition.SortingOrder;
            (void)m_Impl->Tree->SetStyle(state.Instance->Root(), style);
        }
        RuntimeUiCanvasSettings documentSettings;
        documentSettings.ScaleMode = panelDefinition.ScaleMode;
        documentSettings.ReferenceWidth = panelDefinition.ReferenceWidth;
        documentSettings.ReferenceHeight = panelDefinition.ReferenceHeight;
        documentSettings.MatchWidthOrHeight = panelDefinition.MatchWidthOrHeight;
        documentSettings.RespectSafeArea = panelDefinition.RespectSafeArea;
        if (!m_Impl->Tree->SetRootCanvasSettings(state.Instance->Root(), documentSettings))
            throw std::runtime_error("UI Document root rejected its Panel Settings layout contract.");
        state.Panel = panelDefinition;
        m_Impl->PublishProjection(entity, state, uiNodes, nodeEntities, projection, projections);
    }

    void ScenePresentationUiDocumentStore::EndSynchronization(std::map<EntityId, RuntimeUiElementId>& uiNodes,
                                                              std::map<std::uint64_t, EntityId>& nodeEntities)
    {
        for (auto iterator = m_Impl->Documents.begin(); iterator != m_Impl->Documents.end();)
        {
            if (!m_Impl->Seen.contains(iterator->first))
            {
                m_Impl->RemoveInstance(iterator->first, iterator->second, uiNodes, nodeEntities);
                iterator = m_Impl->Documents.erase(iterator);
            }
            else
                ++iterator;
        }
    }

    void ScenePresentationUiDocumentStore::SetStyleEvaluationContext(const float viewportWidth,
                                                                     const float viewportHeight, const float dpi)
    {
        const UiStyleEvaluationContext context{.Width = viewportWidth,
                                               .Height = viewportHeight,
                                               .Dpi = dpi,
                                               .Pointer = UiStylePointerPrecision::Fine,
                                               .Navigation = UiStyleNavigationMode::Pointer};
        for (auto& [entity, state] : m_Impl->Documents)
        {
            (void)entity;
            if (state.Instance)
                (void)state.Instance->SetStyleEvaluationContext(context);
        }
    }

    void ScenePresentationUiDocumentStore::Clear(std::map<EntityId, RuntimeUiElementId>& uiNodes,
                                                 std::map<std::uint64_t, EntityId>& nodeEntities) noexcept
    {
        try
        {
            for (auto& [entity, state] : m_Impl->Documents)
                m_Impl->RemoveInstance(entity, state, uiNodes, nodeEntities);
        }
        catch (...)
        {
        }
        m_Impl->Documents.clear();
        m_Impl->Seen.clear();
    }

    std::optional<ScenePresentationUiDocumentElement>
    ScenePresentationUiDocumentStore::Root(const EntityId document) const
    {
        const auto* state = m_Impl->FindState(document);
        return state ? m_Impl->Describe(*state, state->Instance->Root()) : std::nullopt;
    }

    std::optional<ScenePresentationUiDocumentElement>
    ScenePresentationUiDocumentStore::Find(const EntityId document, const AssetId stableId) const
    {
        const auto* state = m_Impl->FindState(document);
        const auto element = state ? state->Instance->Find(stableId) : std::nullopt;
        return element ? m_Impl->Describe(*state, *element) : std::nullopt;
    }

    std::optional<ScenePresentationUiDocumentElement>
    ScenePresentationUiDocumentStore::Find(const EntityId document, const std::string_view name) const
    {
        const auto* state = m_Impl->FindState(document);
        const auto element = state ? state->Instance->Find(name) : std::nullopt;
        return element ? m_Impl->Describe(*state, *element) : std::nullopt;
    }

    bool ScenePresentationUiDocumentStore::Alive(const EntityId document, const std::uint64_t generation,
                                                 const std::uint64_t element) const noexcept
    {
        try
        {
            return m_Impl->Resolve(document, generation, element).has_value();
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<std::string> ScenePresentationUiDocumentStore::ReadText(const EntityId document,
                                                                          const std::uint64_t generation,
                                                                          const std::uint64_t element) const noexcept
    {
        try
        {
            const auto runtime = m_Impl->Resolve(document, generation, element);
            const auto state = runtime ? m_Impl->Tree->State(*runtime) : std::nullopt;
            return state ? std::optional(state->Content.Text) : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ScenePresentationUiDocumentStore::SetText(const EntityId document, const std::uint64_t generation,
                                                   const std::uint64_t element, const std::string_view text) noexcept
    {
        try
        {
            const auto runtime = m_Impl->Resolve(document, generation, element);
            const auto state = runtime ? m_Impl->Tree->State(*runtime) : std::nullopt;
            if (!runtime || !state)
                return false;
            auto content = state->Content;
            content.Text = text;
            return m_Impl->Tree->SetContent(*runtime, std::move(content));
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<float> ScenePresentationUiDocumentStore::ReadValue(const EntityId document,
                                                                     const std::uint64_t generation,
                                                                     const std::uint64_t element) const noexcept
    {
        try
        {
            const auto runtime = m_Impl->Resolve(document, generation, element);
            const auto state = runtime ? m_Impl->Tree->State(*runtime) : std::nullopt;
            return state ? std::optional(state->Control.Value) : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ScenePresentationUiDocumentStore::SetValue(const EntityId document, const std::uint64_t generation,
                                                    const std::uint64_t element, const float value) noexcept
    {
        try
        {
            const auto runtime = m_Impl->Resolve(document, generation, element);
            const auto state = runtime ? m_Impl->Tree->State(*runtime) : std::nullopt;
            if (!runtime || !state || !std::isfinite(value))
                return false;
            auto control = state->Control;
            control.Value = std::clamp(value, std::min(control.Minimum, control.Maximum),
                                       std::max(control.Minimum, control.Maximum));
            return m_Impl->Tree->SetControl(*runtime, control);
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<bool>
    ScenePresentationUiDocumentStore::ReadFlag(const EntityId document, const std::uint64_t generation,
                                               const std::uint64_t element,
                                               const ScenePresentationUiDocumentFlag property) const noexcept
    {
        try
        {
            const auto runtime = m_Impl->Resolve(document, generation, element);
            const auto state = runtime ? m_Impl->Tree->State(*runtime) : std::nullopt;
            if (!runtime || !state)
                return std::nullopt;
            switch (property)
            {
            case ScenePresentationUiDocumentFlag::Interactable:
                return state->Interactable;
            case ScenePresentationUiDocumentFlag::Checked:
                return state->Control.Checked;
            case ScenePresentationUiDocumentFlag::Focused:
                return m_Impl->Tree->Focus() == *runtime;
            case ScenePresentationUiDocumentFlag::Enabled:
                return state->Enabled;
            }
        }
        catch (...)
        {
        }
        return std::nullopt;
    }

    bool ScenePresentationUiDocumentStore::SetFlag(const EntityId document, const std::uint64_t generation,
                                                   const std::uint64_t element,
                                                   const ScenePresentationUiDocumentFlag property,
                                                   const bool value) noexcept
    {
        try
        {
            const auto runtime = m_Impl->Resolve(document, generation, element);
            if (!runtime)
                return false;
            switch (property)
            {
            case ScenePresentationUiDocumentFlag::Interactable:
                return m_Impl->Tree->SetInteractable(*runtime, value);
            case ScenePresentationUiDocumentFlag::Checked:
            {
                const auto state = m_Impl->Tree->State(*runtime);
                if (!state)
                    return false;
                auto control = state->Control;
                control.Checked = value;
                return m_Impl->Tree->SetControl(*runtime, control);
            }
            case ScenePresentationUiDocumentFlag::Focused:
                return value && m_Impl->Tree->SetFocus(*runtime);
            case ScenePresentationUiDocumentFlag::Enabled:
                return m_Impl->Tree->SetEnabled(*runtime, value);
            }
        }
        catch (...)
        {
        }
        return false;
    }

    bool ScenePresentationUiDocumentStore::ConsumeEvent(const EntityId document, const std::uint64_t generation,
                                                        const std::uint64_t element, const RuntimeUiEventType type,
                                                        std::deque<RuntimeUiEvent>& events) const noexcept
    {
        try
        {
            const auto runtime = m_Impl->Resolve(document, generation, element);
            if (!runtime)
                return false;
            const auto found = std::ranges::find_if(events, [runtime, type](const RuntimeUiEvent& event)
                                                    { return event.Type == type && event.Target == *runtime; });
            if (found == events.end())
                return false;
            events.erase(found);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ScenePresentationUiDocumentStore::Focus(const EntityId document, const std::uint64_t generation,
                                                 const std::uint64_t element) noexcept
    {
        try
        {
            const auto runtime = m_Impl->Resolve(document, generation, element);
            return runtime && m_Impl->Tree->SetFocus(*runtime);
        }
        catch (...)
        {
            return false;
        }
    }

    Ref<Ui::VisualElement> ScenePresentationUiDocumentStore::Visual(const EntityId document,
                                                                    const AssetId stableId) const noexcept
    {
        try
        {
            const auto* state = m_Impl->FindState(document);
            return state ? state->Instance->Visual(stableId) : Ref<Ui::VisualElement>{};
        }
        catch (...)
        {
            return {};
        }
    }

    void ScenePresentationUiDocumentStore::SetBindingSource(const EntityId document,
                                                            Ref<UiDocumentBindingSource> source)
    {
        auto* state = m_Impl->FindState(document);
        if (!state)
            throw std::invalid_argument("UI Document binding source target is not presented.");
        state->Instance->SetBindingSource(source);
        state->BindingSource = std::move(source);
    }

    bool ScenePresentationUiDocumentStore::DispatchEvent(const RuntimeUiEvent& event)
    {
        for (auto& [entity, state] : m_Impl->Documents)
        {
            (void)entity;
            if (state.Instance && state.Instance->Describe(event.Target))
                return state.Instance->DispatchRuntimeEvent(event);
        }
        return false;
    }

    void ScenePresentationUiDocumentStore::SynchronizeInteractionStates()
    {
        for (auto& [entity, state] : m_Impl->Documents)
        {
            (void)entity;
            if (state.Instance)
                (void)state.Instance->SynchronizeInteractionStates();
        }
    }

    void ScenePresentationUiDocumentStore::Update(const float deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F)
            throw std::invalid_argument("UI Document update delta must be finite and non-negative.");
        for (auto& [entity, state] : m_Impl->Documents)
        {
            (void)entity;
            if (state.Instance)
            {
                (void)state.Instance->SynchronizeInteractionStates();
                state.Instance->UpdateBindings();
            }
        }
        (void)m_Impl->Tree->AdvanceTransitions(deltaSeconds);
    }

    std::optional<ScenePresentationUiDocumentDebugSnapshot>
    ScenePresentationUiDocumentStore::DebugSnapshot(const EntityId document,
                                                    const std::deque<RuntimeUiEvent>& deferredEvents) const
    {
        const auto* state = m_Impl->FindState(document);
        if (!state)
            return std::nullopt;
        ScenePresentationUiDocumentDebugSnapshot result;
        result.Document = document;
        result.VisualTree = state->VisualTree;
        result.DocumentGeneration = state->Generation;
        result.Statistics = m_Impl->Tree->Statistics();
        const auto focus = m_Impl->Tree->Focus();
        for (const auto element : state->Instance->Query({}))
        {
            const auto description = state->Instance->Describe(element);
            const auto runtimeState = m_Impl->Tree->State(element);
            if (!description || !runtimeState)
                continue;
            result.Elements.push_back(
                {description->StableId, MakeDebugState(*state->Instance, *m_Impl->Tree, element, *runtimeState)});
            if (element == focus)
                result.Focused = description->StableId;
            for (const auto& trace : state->Instance->ResolvedStyleTrace(element))
            {
                if (result.SelectorTrace.size() == 4'096)
                    break;
                result.SelectorTrace.push_back({.StableId = description->StableId,
                                                .Selector = trace.Selector,
                                                .Specificity = trace.Specificity,
                                                .SourceOrder = trace.SourceOrder,
                                                .AppliedProperties = trace.AppliedProperties});
            }
        }
        const auto appendEvent = [&](const RuntimeUiEvent& event)
        {
            const auto description = state->Instance->Describe(event.Target);
            if (!description)
                return;
            result.PendingTargetEvents.push_back({.Type = event.Type,
                                                  .Target = description->StableId,
                                                  .PointerX = event.PointerX,
                                                  .PointerY = event.PointerY,
                                                  .Button = event.Button});
        };
        for (const auto& event : deferredEvents)
            appendEvent(event);
        for (const auto& event : m_Impl->Tree->PendingEvents())
            appendEvent(event);
        for (const auto& route : m_Impl->Tree->EventRouteHistory())
        {
            if (result.EventRouteHistory.size() == 4'096)
                break;
            const auto target = state->Instance->Describe(route.Target);
            const auto current = state->Instance->Describe(route.CurrentTarget);
            if (!target || !current)
                continue;
            result.EventRouteHistory.push_back({.Sequence = route.Sequence,
                                                .Type = route.Type,
                                                .Phase = route.Phase,
                                                .Target = target->StableId,
                                                .CurrentTarget = current->StableId,
                                                .PointerX = route.PointerX,
                                                .PointerY = route.PointerY,
                                                .Button = route.Button});
        }
        return result;
    }

    std::optional<ScenePresentationUiDocumentHit>
    ScenePresentationUiDocumentStore::Hit(const EntityId document, const RuntimeUiElementId element) const noexcept
    {
        try
        {
            const auto* state = m_Impl->FindState(document);
            const auto description = state ? state->Instance->Describe(element) : std::nullopt;
            const auto runtimeState = description ? m_Impl->Tree->State(element) : std::nullopt;
            if (!state || !description || !runtimeState)
                return std::nullopt;
            return ScenePresentationUiDocumentHit{
                .Document = document,
                .StableId = description->StableId,
                .DocumentGeneration = state->Generation,
                .State = MakeDebugState(*state->Instance, *m_Impl->Tree, element, *runtimeState)};
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
} // namespace Keire::Detail
