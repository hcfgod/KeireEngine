#include "Keire/Scenes/ScenePresentationRuntime.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/RuntimeUiComponents.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Ui.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace Keire
{
    class ScenePresentationRuntime::Impl final
    {
      public:
        struct AudioSourceState final
        {
            AssetId Clip;
            AssetHandle<AudioClipAsset> Handle;
            AudioVoiceId Voice;
            bool ManualPlayRequested = false;
            bool PlayOnAwakeConsumed = false;
        };

        Impl(Ref<AssetSystem> assets, Ref<AudioSystem> audio, const std::size_t maximumUiElements)
            : Assets(std::move(assets)), Audio(std::move(audio)), UiTree(CreateRef<RuntimeUiTree>(maximumUiElements))
        {
            if (!Assets)
                throw std::invalid_argument("ScenePresentationRuntime requires asset services.");
        }

        [[nodiscard]] RuntimeUiElementId EnsureUiNode(const Entity entity, const RuntimeUiElementId parent)
        {
            auto type = RuntimeUiElementType::Panel;
            if (entity.GetComponent<CanvasComponent>())
                type = RuntimeUiElementType::Canvas;
            else if (entity.GetComponent<UiButtonComponent>())
                type = RuntimeUiElementType::Button;
            else if (entity.GetComponent<UiTextComponent>())
                type = RuntimeUiElementType::Text;
            else if (entity.GetComponent<UiImageComponent>())
                type = RuntimeUiElementType::Image;
            if (const auto layout = entity.GetComponent<UiLayoutComponent>())
            {
                if (layout->Direction() == UiLayoutDirection::Horizontal)
                    type = RuntimeUiElementType::HorizontalLayout;
                else if (layout->Direction() == UiLayoutDirection::Grid)
                    type = RuntimeUiElementType::GridLayout;
                else
                    type = RuntimeUiElementType::VerticalLayout;
            }
            const auto found = UiNodes.find(entity.Id());
            RuntimeUiElementId node;
            if (found == UiNodes.end() || !UiTree->Exists(found->second))
            {
                node = UiTree->Create(type, parent);
                UiNodes.insert_or_assign(entity.Id(), node);
                NodeEntities.insert_or_assign(node.Value(), entity.Id());
            }
            else
            {
                node = found->second;
                (void)UiTree->SetParent(node, parent);
                (void)UiTree->SetType(node, type);
            }
            SeenUi.insert(entity.Id());
            ApplyUiState(entity, node);
            return node;
        }

        void ApplyUiState(const Entity entity, const RuntimeUiElementId node)
        {
            RuntimeUiStyle style;
            if (const auto rect = entity.GetComponent<RectTransformComponent>())
            {
                const auto parent = entity.Parent();
                const bool drivenByLayout = parent && parent.GetComponent<UiLayoutComponent>();
                style.Position = drivenByLayout ? RuntimeUiPositionMode::Flow : RuntimeUiPositionMode::Absolute;
                style.UseAnchors = !drivenByLayout;
                style.AnchorMinimum = rect->AnchorMinimum();
                style.AnchorMaximum = rect->AnchorMaximum();
                style.Pivot = rect->Pivot();
                style.AnchoredPosition = rect->AnchoredPosition();
                style.SizeDelta = rect->SizeDelta();
                style.LocalScale = rect->Scale();
                if (drivenByLayout)
                {
                    style.Width = std::max(0.0F, rect->SizeDelta().X);
                    style.Height = std::max(0.0F, rect->SizeDelta().Y);
                }
            }
            if (const auto image = entity.GetComponent<UiImageComponent>())
            {
                style.Background = image->Tint();
                style.CornerRadius = image->ImageType() == UiImageType::Sliced ? 12.0F : 0.0F;
            }
            if (const auto text = entity.GetComponent<UiTextComponent>())
            {
                style.Foreground = text->TextColor();
                style.FontSize = text->FontSize();
                const auto alignment = static_cast<std::uint8_t>(text->Alignment());
                const auto horizontal = alignment % 3U;
                const auto vertical = alignment / 3U;
                style.HorizontalAlignment =
                    horizontal == 0 ? RuntimeUiAlignment::Start
                                    : (horizontal == 1 ? RuntimeUiAlignment::Center : RuntimeUiAlignment::End);
                style.VerticalAlignment = vertical == 0
                                              ? RuntimeUiAlignment::Start
                                              : (vertical == 1 ? RuntimeUiAlignment::Center : RuntimeUiAlignment::End);
            }
            if (const auto button = entity.GetComponent<UiButtonComponent>())
            {
                style.Background = button->NormalColor();
                style.HoverBackground = button->HoverColor();
                style.PressedBackground = button->PressedColor();
                style.DisabledBackground = button->DisabledColor();
                style.CornerRadius = 10.0F;
            }
            if (const auto layout = entity.GetComponent<UiLayoutComponent>())
            {
                const auto padding = layout->Padding();
                style.Padding = {padding.X, padding.Y, padding.Z, padding.W};
                style.Gap = layout->Spacing();
                style.GridCellSize = layout->CellSize();
                style.ControlChildWidth = layout->ControlChildWidth();
                style.ControlChildHeight = layout->ControlChildHeight();
                style.ForceExpandWidth = layout->ForceExpandWidth();
                style.ForceExpandHeight = layout->ForceExpandHeight();
                const auto alignment = static_cast<std::uint32_t>(layout->Alignment());
                const auto horizontal = alignment % 3U;
                const auto vertical = alignment / 3U;
                style.ChildHorizontalAlignment =
                    horizontal == 0 ? RuntimeUiAlignment::Start
                                    : (horizontal == 1 ? RuntimeUiAlignment::Center : RuntimeUiAlignment::End);
                style.ChildVerticalAlignment =
                    vertical == 0 ? RuntimeUiAlignment::Start
                                  : (vertical == 1 ? RuntimeUiAlignment::Center : RuntimeUiAlignment::End);
            }
            if (const auto canvas = entity.GetComponent<CanvasComponent>())
                style.SortingOrder = canvas->SortingOrder();
            (void)UiTree->SetStyle(node, style);

            RuntimeUiContent content;
            if (const auto text = entity.GetComponent<UiTextComponent>())
            {
                content.Text = text->Text();
                content.Font = text->Font();
                content.AccessibilityLabel = text->Text();
            }
            if (const auto image = entity.GetComponent<UiImageComponent>())
                content.Image = image->Sprite();
            if (const auto button = entity.GetComponent<UiButtonComponent>(); button && !button->Action().empty())
                content.AccessibilityLabel = button->Action();
            (void)UiTree->SetContent(node, std::move(content));
            (void)UiTree->SetVisible(node, entity.ActiveInHierarchy());
            const auto button = entity.GetComponent<UiButtonComponent>();
            (void)UiTree->SetEnabled(node, !button || button->Interactable());
            (void)UiTree->SetInteractable(node, button && button->Interactable());
        }

        void TraverseUi(const Entity entity, const RuntimeUiElementId parent)
        {
            RuntimeUiElementId effectiveParent = parent;
            if (entity.GetComponent<CanvasComponent>() || entity.GetComponent<RectTransformComponent>())
                effectiveParent = EnsureUiNode(entity, parent);
            for (const auto child : entity.Children())
                TraverseUi(child, effectiveParent);
        }

        void SynchronizeUi(const Ref<Scene>& scene, const float viewportWidth, const float viewportHeight,
                           const RuntimeUiInsets safeArea)
        {
            SeenUi.clear();
            RuntimeUiCanvasSettings settings;
            bool settingsFound = false;
            for (const auto entity : scene->Query<CanvasComponent>())
            {
                if (!entity)
                    continue;
                bool nestedCanvas = false;
                for (auto ancestor = entity.Parent(); ancestor; ancestor = ancestor.Parent())
                {
                    if (ancestor.GetComponent<CanvasComponent>())
                    {
                        nestedCanvas = true;
                        break;
                    }
                }
                if (nestedCanvas)
                    continue;
                if (!settingsFound)
                {
                    const auto canvas = entity.GetComponent<CanvasComponent>();
                    const auto reference = canvas->ReferenceResolution();
                    settings.ReferenceWidth = reference.X;
                    settings.ReferenceHeight = reference.Y;
                    settings.ScaleMode = static_cast<RuntimeUiScaleMode>(canvas->ScaleMode());
                    settings.MatchWidthOrHeight = canvas->MatchWidthOrHeight();
                    settings.AccessibilityScale = canvas->AccessibilityScale();
                    settings.RespectSafeArea = canvas->RespectSafeArea();
                    settingsFound = true;
                }
                TraverseUi(entity, {});
            }
            for (auto iterator = UiNodes.begin(); iterator != UiNodes.end();)
            {
                if (!SeenUi.contains(iterator->first))
                {
                    NodeEntities.erase(iterator->second.Value());
                    (void)UiTree->Destroy(iterator->second);
                    iterator = UiNodes.erase(iterator);
                }
                else
                    ++iterator;
            }
            UiTree->Layout(viewportWidth, viewportHeight, safeArea, settings);
        }

        [[nodiscard]] AudioPlaybackRequest PlaybackRequest(const Entity entity, const AudioSourceComponent& source,
                                                           std::shared_ptr<const AudioClipData> clip) const
        {
            Vector3 position;
            if (const auto transform = entity.GetComponent<TransformComponent>())
                position = transform->WorldPosition();
            return source.PlaybackRequest(std::move(clip), position);
        }

        void StopVoice(AudioSourceState& state) noexcept
        {
            if (Audio && state.Voice)
            {
                try
                {
                    (void)Audio->Stop(state.Voice);
                }
                catch (...)
                {
                }
            }
            state.Voice = {};
        }

        void SynchronizeAudio(const Ref<Scene>& scene, const bool playing)
        {
            if (!Audio)
            {
                AudioSources.clear();
                SeenAudio.clear();
                PendingAudio = 0;
                WasPlaying = false;
                return;
            }
            if (playing && !WasPlaying)
            {
                for (auto& [entity, state] : AudioSources)
                {
                    (void)entity;
                    state.PlayOnAwakeConsumed = false;
                }
            }
            WasPlaying = playing;
            SeenAudio.clear();
            std::size_t pending = 0;
            for (const auto entity : scene->Query<AudioSourceComponent>())
            {
                if (!entity)
                    continue;
                SeenAudio.insert(entity.Id());
                const auto source = entity.GetComponent<AudioSourceComponent>();
                auto& state = AudioSources[entity.Id()];
                if (state.Clip != source->Clip())
                {
                    StopVoice(state);
                    state = {};
                    state.Clip = source->Clip();
                    if (state.Clip)
                        state.Handle = Assets->Load<AudioClipAsset>(state.Clip, AssetPriority::High);
                }
                const auto clip = state.Handle.TryGetLoaded();
                if (state.Clip && !clip)
                    ++pending;
                if (!playing || !entity.ActiveInHierarchy() || !source->Enabled() || !clip)
                {
                    StopVoice(state);
                    continue;
                }
                auto specification = PlaybackRequest(entity, *source, clip->Clip());
                if (state.Voice)
                {
                    if (Audio->SetVoice(state.Voice, specification))
                    {
                        state.ManualPlayRequested = false;
                        continue;
                    }
                    state.Voice = {};
                }
                const bool requested =
                    state.ManualPlayRequested || (source->PlayOnAwake() && !state.PlayOnAwakeConsumed);
                if (!requested)
                    continue;
                state.Voice = Audio->Play(std::move(specification));
                if (state.Voice)
                {
                    state.ManualPlayRequested = false;
                    state.PlayOnAwakeConsumed = state.PlayOnAwakeConsumed || source->PlayOnAwake();
                }
            }
            for (auto iterator = AudioSources.begin(); iterator != AudioSources.end();)
            {
                if (!SeenAudio.contains(iterator->first))
                {
                    StopVoice(iterator->second);
                    iterator = AudioSources.erase(iterator);
                }
                else
                    ++iterator;
            }
            PendingAudio = pending;

            for (const auto entity : scene->Query<AudioListenerComponent>())
            {
                const auto listener = entity.GetComponent<AudioListenerComponent>();
                if (!entity.ActiveInHierarchy() || !listener->Enabled() || !listener->Primary())
                    continue;
                AudioListenerState state;
                if (const auto transform = entity.GetComponent<TransformComponent>())
                    state.Position = transform->WorldPosition();
                Audio->SetListener(state);
                break;
            }
        }

        Ref<AssetSystem> Assets;
        Ref<AudioSystem> Audio;
        Ref<RuntimeUiTree> UiTree;
        Ref<Scene> ActiveScene;
        std::map<EntityId, RuntimeUiElementId> UiNodes;
        std::map<std::uint64_t, EntityId> NodeEntities;
        std::set<EntityId> SeenUi;
        std::deque<RuntimeUiEvent> DeferredUiEvents;
        std::map<EntityId, AudioSourceState> AudioSources;
        std::set<EntityId> SeenAudio;
        std::size_t PendingAudio = 0;
        bool WasPlaying = false;
        std::uint64_t SynchronizationCount = 0;
        float UiSynchronizationMilliseconds = 0.0F;
        float AudioSynchronizationMilliseconds = 0.0F;
        float SynchronizationMilliseconds = 0.0F;
    };

    ScenePresentationRuntime::ScenePresentationRuntime(Ref<AssetSystem> assets, Ref<AudioSystem> audio,
                                                       const std::size_t maximumUiElements)
        : m_Impl(std::make_unique<Impl>(std::move(assets), std::move(audio), maximumUiElements))
    {
    }

    ScenePresentationRuntime::~ScenePresentationRuntime() { Clear(); }

    void ScenePresentationRuntime::Synchronize(Ref<Scene> scene, const float viewportWidth, const float viewportHeight,
                                               const bool playing, const RuntimeUiInsets safeArea)
    {
        if (!scene || !scene->IsOpen())
        {
            Clear();
            return;
        }
        const auto synchronizationStarted = std::chrono::steady_clock::now();
        m_Impl->ActiveScene = std::move(scene);
        const auto uiStarted = std::chrono::steady_clock::now();
        m_Impl->SynchronizeUi(m_Impl->ActiveScene, viewportWidth, viewportHeight, safeArea);
        const auto audioStarted = std::chrono::steady_clock::now();
        m_Impl->SynchronizeAudio(m_Impl->ActiveScene, playing);
        const auto completed = std::chrono::steady_clock::now();
        m_Impl->UiSynchronizationMilliseconds =
            std::chrono::duration<float, std::milli>(audioStarted - uiStarted).count();
        m_Impl->AudioSynchronizationMilliseconds =
            std::chrono::duration<float, std::milli>(completed - audioStarted).count();
        m_Impl->SynchronizationMilliseconds =
            std::chrono::duration<float, std::milli>(completed - synchronizationStarted).count();
        ++m_Impl->SynchronizationCount;
    }

    void ScenePresentationRuntime::Clear() noexcept
    {
        if (!m_Impl)
            return;
        for (auto& [entity, state] : m_Impl->AudioSources)
        {
            (void)entity;
            m_Impl->StopVoice(state);
        }
        m_Impl->AudioSources.clear();
        m_Impl->UiNodes.clear();
        m_Impl->NodeEntities.clear();
        m_Impl->SeenUi.clear();
        m_Impl->DeferredUiEvents.clear();
        m_Impl->SeenAudio.clear();
        m_Impl->UiTree->Clear();
        m_Impl->ActiveScene.Reset();
        m_Impl->PendingAudio = 0;
        m_Impl->WasPlaying = false;
        m_Impl->SynchronizationCount = 0;
        m_Impl->UiSynchronizationMilliseconds = 0.0F;
        m_Impl->AudioSynchronizationMilliseconds = 0.0F;
        m_Impl->SynchronizationMilliseconds = 0.0F;
    }

    bool ScenePresentationRuntime::Play(const EntityId source)
    {
        if (const auto found = m_Impl->AudioSources.find(source); found != m_Impl->AudioSources.end())
        {
            found->second.ManualPlayRequested = true;
            return true;
        }
        return false;
    }

    bool ScenePresentationRuntime::Stop(const EntityId source)
    {
        if (const auto found = m_Impl->AudioSources.find(source); found != m_Impl->AudioSources.end())
        {
            found->second.ManualPlayRequested = false;
            found->second.PlayOnAwakeConsumed = true;
            m_Impl->StopVoice(found->second);
            return true;
        }
        return false;
    }

    bool ScenePresentationRuntime::SetFocus(const EntityId entity)
    {
        const auto found = m_Impl->UiNodes.find(entity);
        return found != m_Impl->UiNodes.end() && m_Impl->UiTree->SetFocus(found->second);
    }

    bool ScenePresentationRuntime::ConsumeClick(const EntityId entity)
    {
        const auto found = m_Impl->UiNodes.find(entity);
        if (found == m_Impl->UiNodes.end())
            return false;
        const auto target = found->second;
        const auto deferred =
            std::ranges::find_if(m_Impl->DeferredUiEvents, [target](const RuntimeUiEvent& event)
                                 { return event.Type == RuntimeUiEventType::Click && event.Target == target; });
        if (deferred != m_Impl->DeferredUiEvents.end())
        {
            m_Impl->DeferredUiEvents.erase(deferred);
            return true;
        }
        RuntimeUiEvent event;
        while (m_Impl->UiTree->PollEvent(event))
        {
            if (event.Type == RuntimeUiEventType::Click && event.Target == target)
                return true;
            m_Impl->DeferredUiEvents.push_back(event);
        }
        return false;
    }

    void ScenePresentationRuntime::PointerMove(const float x, const float y) { m_Impl->UiTree->PointerMove(x, y); }

    void ScenePresentationRuntime::PointerButton(const float x, const float y, const RuntimeUiPointerButton button,
                                                 const bool pressed)
    {
        m_Impl->UiTree->PointerButton(x, y, button, pressed);
    }

    void ScenePresentationRuntime::Navigate(const RuntimeUiNavigation navigation)
    {
        m_Impl->UiTree->Navigate(navigation);
    }

    bool ScenePresentationRuntime::PollUiEvent(RuntimeUiEvent& event)
    {
        if (!m_Impl->DeferredUiEvents.empty())
        {
            event = std::move(m_Impl->DeferredUiEvents.front());
            m_Impl->DeferredUiEvents.pop_front();
            return true;
        }
        return m_Impl->UiTree->PollEvent(event);
    }

    Ref<RuntimeUiTree> ScenePresentationRuntime::Ui() const noexcept { return m_Impl->UiTree; }

    ScenePresentationRuntimeStatistics ScenePresentationRuntime::Statistics() const
    {
        ScenePresentationRuntimeStatistics result;
        result.Ui = m_Impl->UiTree->Statistics();
        if (m_Impl->Audio)
            result.Audio = m_Impl->Audio->Statistics();
        result.TrackedUiEntities = m_Impl->UiNodes.size();
        result.TrackedAudioSources = m_Impl->AudioSources.size();
        result.ActiveAudioSources =
            std::ranges::count_if(m_Impl->AudioSources, [](const auto& item) { return bool(item.second.Voice); });
        result.PendingAudioAssets = m_Impl->PendingAudio;
        result.SynchronizationCount = m_Impl->SynchronizationCount;
        result.UiSynchronizationMilliseconds = m_Impl->UiSynchronizationMilliseconds;
        result.AudioSynchronizationMilliseconds = m_Impl->AudioSynchronizationMilliseconds;
        result.SynchronizationMilliseconds = m_Impl->SynchronizationMilliseconds;
        return result;
    }

    void ScenePresentationRuntime::Draw(UiFrame& ui, const float offsetX, const float offsetY) const
    {
        for (const auto& command : m_Impl->UiTree->DrawCommands())
        {
            if (command.Type == RuntimeUiDrawType::PushClip || command.Type == RuntimeUiDrawType::PopClip)
                continue;
            const auto clipped = command.Rect.Intersect(command.ClipRect);
            if (clipped.Empty())
                continue;
            const UiItemRect rectangle{{offsetX + clipped.X, offsetY + clipped.Y},
                                       {offsetX + clipped.X + clipped.Width, offsetY + clipped.Y + clipped.Height}};
            const UiColor color{command.ColorValue.Red, command.ColorValue.Green, command.ColorValue.Blue,
                                command.ColorValue.Alpha};
            switch (command.Type)
            {
            case RuntimeUiDrawType::Quad:
                ui.DrawFilledRectangle(rectangle, color, command.CornerRadius);
                if (command.BorderWidth > 0.0F)
                    ui.DrawRectangle(rectangle,
                                     {command.BorderColor.Red, command.BorderColor.Green, command.BorderColor.Blue,
                                      command.BorderColor.Alpha},
                                     command.BorderWidth, command.CornerRadius);
                break;
            case RuntimeUiDrawType::Image:
                ui.DrawFilledRectangle(rectangle, color, command.CornerRadius);
                break;
            case RuntimeUiDrawType::Text:
            {
                const auto measured = ui.MeasureText(command.Text, command.FontSize);
                float textX = command.Rect.X;
                float textY = command.Rect.Y;
                if (command.HorizontalAlignment == RuntimeUiAlignment::Center)
                    textX += (command.Rect.Width - measured.Width) * 0.5F;
                else if (command.HorizontalAlignment == RuntimeUiAlignment::End)
                    textX += command.Rect.Width - measured.Width;
                if (command.VerticalAlignment == RuntimeUiAlignment::Center)
                    textY += (command.Rect.Height - measured.Height) * 0.5F;
                else if (command.VerticalAlignment == RuntimeUiAlignment::End)
                    textY += command.Rect.Height - measured.Height;
                const UiItemRect textClip{{offsetX + command.ClipRect.X, offsetY + command.ClipRect.Y},
                                          {offsetX + command.ClipRect.X + command.ClipRect.Width,
                                           offsetY + command.ClipRect.Y + command.ClipRect.Height}};
                ui.DrawOverlayText({offsetX + textX, offsetY + textY}, color, command.Text, command.FontSize, textClip);
                break;
            }
            case RuntimeUiDrawType::PushClip:
            case RuntimeUiDrawType::PopClip:
                break;
            }
        }
    }
} // namespace Keire
