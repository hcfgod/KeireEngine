#include "Keire/Scenes/ScenePresentationRuntime.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/RuntimeUiComponents.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <exception>
#include <limits>
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
            std::shared_ptr<const AudioClipData> LoadedClip;
            AudioVoiceId Voice;
            bool ManualPlayRequested = false;
            bool PlayOnAwakeConsumed = false;
            bool Paused = false;
        };

        struct AudioMixerState final
        {
            AssetHandle<AudioMixerAsset> Handle;
            AudioMixerRoutingId Routing;
            std::uint64_t Revision = 0;
            AssetId AppliedSnapshot;
            float AppliedWeight = -1.0F;
            float AppliedReverbSend = -1.0F;
        };

        Impl(Ref<AssetSystem> assets, Ref<AudioSystem> audio, const std::size_t maximumUiElements)
            : Assets(std::move(assets)), Audio(std::move(audio)), UiTree(CreateRef<RuntimeUiTree>(maximumUiElements))
        {
            if (!Assets)
                throw std::invalid_argument("ScenePresentationRuntime requires asset services.");
        }

        [[nodiscard]] RuntimeUiElementId EnsureUiNode(const Entity& entity, const RuntimeUiElementId parent)
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

        void ApplyUiState(const Entity& entity, const RuntimeUiElementId node)
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

        void TraverseUi(const Entity& entity, const RuntimeUiElementId parent)
        {
            RuntimeUiElementId effectiveParent = parent;
            if (entity.GetComponent<CanvasComponent>() || entity.GetComponent<RectTransformComponent>())
                effectiveParent = EnsureUiNode(entity, parent);
            for (const auto& child : entity.Children())
                TraverseUi(child, effectiveParent);
        }

        void SynchronizeUi(const Ref<Scene>& scene, const float viewportWidth, const float viewportHeight,
                           const RuntimeUiInsets safeArea)
        {
            SeenUi.clear();
            RuntimeUiCanvasSettings settings;
            bool settingsFound = false;
            for (const auto& entity : scene->Query<CanvasComponent>())
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

        [[nodiscard]] AudioPlaybackRequest PlaybackRequest(const Entity& entity, const AudioSourceComponent& source,
                                                           std::shared_ptr<const AudioClipData> clip,
                                                           const AudioMixerRoutingId mixerRouting) const
        {
            Vector3 position;
            if (const auto transform = entity.GetComponent<TransformComponent>())
                position = transform->WorldPosition();
            auto result = source.PlaybackRequest(std::move(clip), position);
            if (!result.Mixer)
                result.Mixer = DefaultMixer;
            result.MixerRouting = mixerRouting;
            if (result.Mixer && !mixerRouting)
                result.Mixer = {};
            return result;
        }

        [[nodiscard]] AudioMixerRoutingId EnsureMixer(const AssetId mixer)
        {
            if (!mixer)
                return {};
            SeenMixers.insert(mixer);
            auto& state = AudioMixers[mixer];
            if (!state.Handle)
                state.Handle = Assets->Load<AudioMixerAsset>(mixer, AssetPriority::High);
            const auto loaded = state.Handle.TryGetLoaded();
            if (!loaded)
                return {};
            const auto revision = state.Handle.Revision();
            if (!state.Routing)
            {
                state.Routing = Audio->RegisterMixer(mixer, loaded->Definition());
                state.Revision = revision;
            }
            else if (state.Revision != revision)
            {
                if (!Audio->UpdateMixer(state.Routing, loaded->Definition()))
                    state.Routing = Audio->RegisterMixer(mixer, loaded->Definition());
                state.Revision = revision;
                state.AppliedSnapshot = {};
                state.AppliedWeight = -1.0F;
                state.AppliedReverbSend = -1.0F;
            }
            return state.Routing;
        }

        [[nodiscard]] bool ApplyReverbZone(const AudioReverbZoneComponent& zone, const AssetId mixer,
                                           const float weight)
        {
            const auto routing = EnsureMixer(mixer);
            if (!routing)
                return false;
            auto& state = AudioMixers.at(mixer);
            if (state.AppliedSnapshot == zone.SnapshotId() && state.AppliedWeight == weight &&
                state.AppliedReverbSend == zone.ReverbSend())
            {
                return true;
            }
            const auto loaded = state.Handle.TryGetLoaded();
            if (!loaded)
                return false;
            try
            {
                auto definition = zone.SnapshotId()
                                      ? BlendAudioMixerSnapshot(loaded->Definition(), zone.SnapshotId(), weight)
                                      : loaded->Definition();
                std::set<AssetId> reverbBuses;
                for (const auto& bus : definition.Buses)
                    if (std::ranges::any_of(bus.Effects,
                                            [](const AudioMixerEffectDefinition& effect)
                                            {
                                                return !effect.Bypassed &&
                                                       (effect.Type == AudioGraphNodeType::AlgorithmicReverb ||
                                                        effect.Type == AudioGraphNodeType::ConvolutionReverb);
                                            }))
                        reverbBuses.insert(bus.Id);
                std::set<AssetId> sendReturnBuses;
                for (const auto& bus : definition.Buses)
                    for (const auto& send : bus.Sends)
                        if (reverbBuses.contains(send.DestinationBus))
                            sendReturnBuses.insert(send.DestinationBus);
                for (auto& bus : definition.Buses)
                {
                    for (auto& effect : bus.Effects)
                    {
                        if (effect.Bypassed || (effect.Type != AudioGraphNodeType::AlgorithmicReverb &&
                                                effect.Type != AudioGraphNodeType::ConvolutionReverb))
                            continue;
                        if (sendReturnBuses.contains(bus.Id))
                            continue;
                        if (effect.Type == AudioGraphNodeType::AlgorithmicReverb)
                        {
                            if (effect.Parameters.size() < 3U)
                            {
                                const std::array defaults{68.0F, 0.55F, 0.3F};
                                while (effect.Parameters.size() < defaults.size())
                                    effect.Parameters.push_back(defaults[effect.Parameters.size()]);
                            }
                            effect.Parameters[2] *= zone.ReverbSend() * weight;
                        }
                        else
                            for (auto& tap : effect.Parameters)
                                tap *= zone.ReverbSend() * weight;
                    }
                }
                for (auto& bus : definition.Buses)
                    for (auto& send : bus.Sends)
                        if (reverbBuses.contains(send.DestinationBus))
                            send.Gain *= zone.ReverbSend() * weight;
                if (!Audio->UpdateMixer(routing, definition))
                    return false;
            }
            catch (const std::exception&)
            {
                return false;
            }
            state.AppliedSnapshot = zone.SnapshotId();
            state.AppliedWeight = weight;
            state.AppliedReverbSend = zone.ReverbSend();
            return true;
        }

        void RestoreInactiveReverbZones(const std::set<AssetId>& activeMixers)
        {
            for (auto& [mixer, state] : AudioMixers)
            {
                if (activeMixers.contains(mixer) || state.AppliedWeight < 0.0F)
                    continue;
                if (const auto loaded = state.Handle.TryGetLoaded(); loaded && state.Routing)
                    (void)Audio->UpdateMixer(state.Routing, loaded->Definition());
                state.AppliedSnapshot = {};
                state.AppliedWeight = -1.0F;
                state.AppliedReverbSend = -1.0F;
            }
        }

        void RemoveMixer(AudioMixerState& state) noexcept
        {
            if (Audio && state.Routing)
            {
                try
                {
                    (void)Audio->UnregisterMixer(state.Routing);
                }
                catch (...)
                {
                }
            }
            state.Routing = {};
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
            state.Paused = false;
        }

        [[nodiscard]] ScenePresentationCheckpoint CaptureCheckpoint() const
        {
            ScenePresentationCheckpoint result;
            if (const auto focus = UiTree->Focus())
            {
                if (const auto found = NodeEntities.find(focus.Value()); found != NodeEntities.end())
                    result.FocusedEntity = found->second;
            }
            result.AudioSources.reserve(AudioSources.size());
            for (const auto& [entity, state] : AudioSources)
            {
                ScenePresentationAudioCheckpoint checkpoint;
                checkpoint.Entity = entity;
                checkpoint.Clip = state.Clip;
                checkpoint.ManualPlayRequested = state.ManualPlayRequested;
                checkpoint.PlayOnAwakeConsumed = state.PlayOnAwakeConsumed;
                if (Audio && state.Voice)
                {
                    if (const auto voice = Audio->Voice(state.Voice))
                    {
                        checkpoint.State =
                            voice->Paused ? AudioSourcePlaybackState::Paused : AudioSourcePlaybackState::Playing;
                        checkpoint.Frame = voice->Frame;
                    }
                }
                result.AudioSources.push_back(checkpoint);
            }
            const auto appendEvent = [&](const RuntimeUiEvent& event)
            {
                const auto target = NodeEntities.find(event.Target.Value());
                if (target != NodeEntities.end())
                {
                    result.PendingUiEvents.push_back(
                        {event.Type, target->second, event.PointerX, event.PointerY, event.Button});
                }
            };
            result.PendingUiEvents.reserve(DeferredUiEvents.size() + UiTree->Statistics().PendingEvents);
            for (const auto& event : DeferredUiEvents)
                appendEvent(event);
            for (const auto& event : UiTree->PendingEvents())
                appendEvent(event);
            return result;
        }

        void ApplyCheckpoint(const ScenePresentationCheckpoint& checkpoint)
        {
            if (checkpoint.AudioSources.size() != AudioSources.size())
                throw std::invalid_argument("Scene presentation audio checkpoint does not match the active scene.");
            std::set<EntityId> checkpointEntities;
            for (const auto& source : checkpoint.AudioSources)
            {
                const auto found = AudioSources.find(source.Entity);
                const auto entity = ActiveScene ? ActiveScene->FindEntity(source.Entity) : Entity{};
                const auto component =
                    entity ? entity.GetComponent<AudioSourceComponent>() : Ref<AudioSourceComponent>{};
                if (!checkpointEntities.insert(source.Entity).second || found == AudioSources.end() || !component ||
                    found->second.Clip != source.Clip || source.State > AudioSourcePlaybackState::Paused ||
                    (source.State != AudioSourcePlaybackState::Stopped &&
                     (!Audio || !found->second.LoadedClip || source.Frame > found->second.LoadedClip->Frames)))
                {
                    throw std::invalid_argument("Scene presentation audio checkpoint is invalid or stale.");
                }
            }
            if (checkpoint.FocusedEntity && !UiNodes.contains(checkpoint.FocusedEntity))
                throw std::invalid_argument("Scene presentation UI focus checkpoint is stale.");
            for (const auto& event : checkpoint.PendingUiEvents)
            {
                if (!UiNodes.contains(event.Target) || event.Type > RuntimeUiEventType::Cancel ||
                    event.Button > RuntimeUiPointerButton::Middle || !std::isfinite(event.PointerX) ||
                    !std::isfinite(event.PointerY))
                {
                    throw std::invalid_argument("Scene presentation UI event checkpoint is invalid or stale.");
                }
            }

            for (const auto& source : checkpoint.AudioSources)
            {
                auto& state = AudioSources.at(source.Entity);
                StopVoice(state);
                state.ManualPlayRequested = source.ManualPlayRequested;
                state.PlayOnAwakeConsumed = source.PlayOnAwakeConsumed;
                if (source.State == AudioSourcePlaybackState::Stopped)
                    continue;
                const auto entity = ActiveScene->FindEntity(source.Entity);
                const auto component = entity.GetComponent<AudioSourceComponent>();
                AudioMixerRoutingId routing;
                if (const auto mixer = AudioMixers.find(component->Mixer()); mixer != AudioMixers.end())
                    routing = mixer->second.Routing;
                state.Voice = Audio->Play(PlaybackRequest(entity, *component, state.LoadedClip, routing));
                if (!state.Voice || !Audio->Seek(state.Voice, source.Frame))
                    throw std::runtime_error("Scene presentation audio voice could not be restored.");
                if (source.State == AudioSourcePlaybackState::Paused)
                {
                    if (!Audio->Pause(state.Voice))
                        throw std::runtime_error("Scene presentation audio pause state could not be restored.");
                    state.Paused = true;
                }
            }

            const auto focus = checkpoint.FocusedEntity ? UiNodes.at(checkpoint.FocusedEntity) : RuntimeUiElementId{};
            if (!UiTree->SetFocus(focus))
                throw std::runtime_error("Scene presentation UI focus could not be restored.");
            UiTree->ReplacePendingEvents({});
            DeferredUiEvents.clear();
            for (const auto& event : checkpoint.PendingUiEvents)
            {
                DeferredUiEvents.push_back(
                    {event.Type, UiNodes.at(event.Target), event.PointerX, event.PointerY, event.Button});
            }
        }

        void SynchronizeAudio(const Ref<Scene>& scene, const bool playing)
        {
            if (!Audio)
            {
                AudioSources.clear();
                AudioMixers.clear();
                SeenAudio.clear();
                SeenMixers.clear();
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
            SeenMixers.clear();
            std::size_t pending = 0;
            for (const auto& entity : scene->Query<AudioSourceComponent>())
            {
                if (!entity)
                    continue;
                SeenAudio.insert(entity.Id());
                const auto source = entity.GetComponent<AudioSourceComponent>();
                const auto sourceMixer = source->Mixer() ? source->Mixer() : DefaultMixer;
                const auto mixerRouting = EnsureMixer(sourceMixer);
                if (sourceMixer && !mixerRouting)
                    ++pending;
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
                    if (playing && clip == nullptr && state.Voice)
                        state.ManualPlayRequested = true;
                    StopVoice(state);
                    continue;
                }
                const auto loadedClip = clip->Clip();
                if (state.LoadedClip != loadedClip)
                {
                    const bool resume = static_cast<bool>(state.Voice);
                    StopVoice(state);
                    state.LoadedClip = loadedClip;
                    state.ManualPlayRequested = state.ManualPlayRequested || resume;
                }
                auto specification = PlaybackRequest(entity, *source, state.LoadedClip, mixerRouting);
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
                    state.Paused = false;
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
            Vector3 listenerPosition;
            bool listenerFound = false;
            HasAudioListener = false;
            UsingPrimaryCameraListener = false;
            for (const auto& entity : scene->Query<AudioListenerComponent>())
            {
                const auto listener = entity.GetComponent<AudioListenerComponent>();
                if (!entity.ActiveInHierarchy() || !listener->Enabled() || !listener->Primary())
                    continue;
                AudioListenerState state;
                if (const auto transform = entity.GetComponent<TransformComponent>())
                {
                    state.Position = transform->WorldPosition();
                    state.Forward = Math::TransformDirection(transform->WorldMatrix(), {0.0F, 0.0F, -1.0F});
                    state.Up = Math::TransformDirection(transform->WorldMatrix(), {0.0F, 1.0F, 0.0F});
                }
                state.Gain = listener->Gain();
                Audio->SetListener(state);
                listenerPosition = state.Position;
                listenerFound = true;
                HasAudioListener = true;
                break;
            }
            if (!listenerFound)
            {
                Entity selectedCamera;
                auto selectedPriority = std::numeric_limits<std::int32_t>::min();
                for (const auto& entity : scene->Query<CameraComponent>())
                {
                    const auto camera = entity.GetComponent<CameraComponent>();
                    if (!entity.ActiveInHierarchy() || !camera->Enabled() || !camera->Primary() ||
                        camera->Priority() < selectedPriority)
                    {
                        continue;
                    }
                    selectedCamera = entity;
                    selectedPriority = camera->Priority();
                }
                if (selectedCamera)
                {
                    AudioListenerState state;
                    if (const auto transform = selectedCamera.GetComponent<TransformComponent>())
                    {
                        state.Position = transform->WorldPosition();
                        state.Forward = Math::TransformDirection(transform->WorldMatrix(), {0.0F, 0.0F, -1.0F});
                        state.Up = Math::TransformDirection(transform->WorldMatrix(), {0.0F, 1.0F, 0.0F});
                    }
                    Audio->SetListener(state);
                    listenerPosition = state.Position;
                    listenerFound = true;
                    HasAudioListener = true;
                    UsingPrimaryCameraListener = true;
                }
            }

            std::map<AssetId, std::pair<Ref<AudioReverbZoneComponent>, float>> activeZones;
            if (listenerFound)
            {
                for (const auto& entity : scene->Query<AudioReverbZoneComponent>())
                {
                    const auto zone = entity.GetComponent<AudioReverbZoneComponent>();
                    const auto transform = entity.GetComponent<TransformComponent>();
                    const auto zoneMixer = zone->Mixer() ? zone->Mixer() : DefaultMixer;
                    if (!entity.ActiveInHierarchy() || !zone->Enabled() || !zoneMixer || !transform)
                        continue;
                    const auto world = transform->WorldMatrix();
                    const auto localPosition = Math::TransformPoint(Math::Inverse(world), listenerPosition);
                    float outsideDistance = 0.0F;
                    if (zone->Shape() == AudioReverbZoneShape::Sphere)
                    {
                        const auto distance =
                            std::sqrt(localPosition.X * localPosition.X + localPosition.Y * localPosition.Y +
                                      localPosition.Z * localPosition.Z);
                        if (distance > zone->SphereRadius())
                        {
                            const auto scale = zone->SphereRadius() / distance;
                            const auto closest = Math::TransformPoint(
                                world, {localPosition.X * scale, localPosition.Y * scale, localPosition.Z * scale});
                            const Vector3 outside{listenerPosition.X - closest.X, listenerPosition.Y - closest.Y,
                                                  listenerPosition.Z - closest.Z};
                            outsideDistance =
                                std::sqrt(outside.X * outside.X + outside.Y * outside.Y + outside.Z * outside.Z);
                        }
                    }
                    else
                    {
                        const auto extent = zone->BoxHalfExtent();
                        const Vector3 closestLocal{std::clamp(localPosition.X, -extent.X, extent.X),
                                                   std::clamp(localPosition.Y, -extent.Y, extent.Y),
                                                   std::clamp(localPosition.Z, -extent.Z, extent.Z)};
                        const auto closest = Math::TransformPoint(world, closestLocal);
                        const Vector3 outside{listenerPosition.X - closest.X, listenerPosition.Y - closest.Y,
                                              listenerPosition.Z - closest.Z};
                        outsideDistance =
                            std::sqrt(outside.X * outside.X + outside.Y * outside.Y + outside.Z * outside.Z);
                    }
                    const float weight = zone->BlendDistance() == 0.0F
                                             ? (outsideDistance == 0.0F ? 1.0F : 0.0F)
                                             : std::clamp(1.0F - outsideDistance / zone->BlendDistance(), 0.0F, 1.0F);
                    auto found = activeZones.find(zoneMixer);
                    if (found == activeZones.end() || zone->Priority() > found->second.first->Priority() ||
                        (zone->Priority() == found->second.first->Priority() && weight > found->second.second))
                    {
                        activeZones.insert_or_assign(zoneMixer, std::pair{zone, weight});
                    }
                }
            }
            std::set<AssetId> activeReverbMixers;
            ActiveReverbZones = 0;
            for (const auto& [mixer, active] : activeZones)
            {
                SeenMixers.insert(mixer);
                if (ApplyReverbZone(*active.first, mixer, active.second))
                {
                    activeReverbMixers.insert(mixer);
                    if (active.second > 0.0F)
                        ++ActiveReverbZones;
                }
                else
                    ++PendingAudio;
            }
            RestoreInactiveReverbZones(activeReverbMixers);

            for (auto iterator = AudioMixers.begin(); iterator != AudioMixers.end();)
            {
                if (!SeenMixers.contains(iterator->first))
                {
                    RemoveMixer(iterator->second);
                    iterator = AudioMixers.erase(iterator);
                }
                else
                    ++iterator;
            }
        }

        Ref<AssetSystem> Assets;
        Ref<AudioSystem> Audio;
        Ref<RuntimeUiTree> UiTree;
        Ref<Scene> ActiveScene;
        AssetId DefaultMixer;
        std::map<EntityId, RuntimeUiElementId> UiNodes;
        std::map<std::uint64_t, EntityId> NodeEntities;
        std::set<EntityId> SeenUi;
        std::deque<RuntimeUiEvent> DeferredUiEvents;
        std::map<EntityId, AudioSourceState> AudioSources;
        std::map<AssetId, AudioMixerState> AudioMixers;
        std::set<EntityId> SeenAudio;
        std::set<AssetId> SeenMixers;
        std::size_t PendingAudio = 0;
        std::size_t ActiveReverbZones = 0;
        bool HasAudioListener = false;
        bool UsingPrimaryCameraListener = false;
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

    void ScenePresentationRuntime::SetDefaultMixer(const AssetId mixer) noexcept { m_Impl->DefaultMixer = mixer; }

    AssetId ScenePresentationRuntime::DefaultMixer() const noexcept { return m_Impl->DefaultMixer; }

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
        for (auto& [mixer, state] : m_Impl->AudioMixers)
        {
            (void)mixer;
            m_Impl->RemoveMixer(state);
        }
        m_Impl->AudioMixers.clear();
        m_Impl->UiNodes.clear();
        m_Impl->NodeEntities.clear();
        m_Impl->SeenUi.clear();
        m_Impl->DeferredUiEvents.clear();
        m_Impl->SeenAudio.clear();
        m_Impl->SeenMixers.clear();
        m_Impl->UiTree->Clear();
        m_Impl->ActiveScene.Reset();
        m_Impl->PendingAudio = 0;
        m_Impl->ActiveReverbZones = 0;
        m_Impl->HasAudioListener = false;
        m_Impl->UsingPrimaryCameraListener = false;
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

    bool ScenePresentationRuntime::Pause(const EntityId source)
    {
        const auto found = m_Impl->AudioSources.find(source);
        if (found == m_Impl->AudioSources.end() || !found->second.Voice || found->second.Paused)
            return found != m_Impl->AudioSources.end() && found->second.Paused;
        if (!m_Impl->Audio->Pause(found->second.Voice))
        {
            found->second.Voice = {};
            return false;
        }
        found->second.Paused = true;
        return true;
    }

    bool ScenePresentationRuntime::Resume(const EntityId source)
    {
        const auto found = m_Impl->AudioSources.find(source);
        if (found == m_Impl->AudioSources.end() || !found->second.Voice || !found->second.Paused)
            return false;
        if (!m_Impl->Audio->Pause(found->second.Voice, false))
        {
            found->second.Voice = {};
            found->second.Paused = false;
            return false;
        }
        found->second.Paused = false;
        return true;
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

    bool ScenePresentationRuntime::Seek(const EntityId source, const float positionSeconds)
    {
        if (!std::isfinite(positionSeconds) || positionSeconds < 0.0F)
            throw std::invalid_argument("Audio source seek position must be finite and non-negative.");
        const auto found = m_Impl->AudioSources.find(source);
        if (found == m_Impl->AudioSources.end() || !found->second.Voice || !found->second.LoadedClip)
            return false;
        const auto& clip = *found->second.LoadedClip;
        const auto duration =
            clip.SampleRate == 0 ? 0.0F : static_cast<float>(clip.Frames) / static_cast<float>(clip.SampleRate);
        if (positionSeconds > duration)
            throw std::out_of_range("Audio source seek position exceeds the clip duration.");
        const auto frame = static_cast<std::uint64_t>(std::min(
            static_cast<double>(clip.Frames), std::round(static_cast<double>(positionSeconds) * clip.SampleRate)));
        return m_Impl->Audio->Seek(found->second.Voice, frame);
    }

    AudioSourcePlaybackInfo ScenePresentationRuntime::Playback(const EntityId source) const
    {
        AudioSourcePlaybackInfo result;
        const auto found = m_Impl->AudioSources.find(source);
        if (found == m_Impl->AudioSources.end())
            return result;
        if (found->second.LoadedClip && found->second.LoadedClip->SampleRate != 0)
        {
            result.DurationSeconds = static_cast<float>(found->second.LoadedClip->Frames) /
                                     static_cast<float>(found->second.LoadedClip->SampleRate);
        }
        const auto voice = m_Impl->Audio && found->second.Voice ? m_Impl->Audio->Voice(found->second.Voice)
                                                                : std::optional<AudioVoiceInfo>{};
        if (!voice)
            return result;
        if (found->second.LoadedClip && found->second.LoadedClip->SampleRate != 0)
            result.PositionSeconds =
                static_cast<float>(voice->Frame) / static_cast<float>(found->second.LoadedClip->SampleRate);
        result.State = voice->Paused ? AudioSourcePlaybackState::Paused : AudioSourcePlaybackState::Playing;
        return result;
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
            event = m_Impl->DeferredUiEvents.front();
            m_Impl->DeferredUiEvents.pop_front();
            return true;
        }
        return m_Impl->UiTree->PollEvent(event);
    }

    ScenePresentationCheckpoint ScenePresentationRuntime::CaptureCheckpoint() const
    {
        return m_Impl->CaptureCheckpoint();
    }

    void ScenePresentationRuntime::RestoreCheckpoint(const ScenePresentationCheckpoint& checkpoint)
    {
        const auto rollback = m_Impl->CaptureCheckpoint();
        try
        {
            m_Impl->ApplyCheckpoint(checkpoint);
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            try
            {
                m_Impl->ApplyCheckpoint(rollback);
            }
            catch (...)
            {
            }
            std::rethrow_exception(failure);
        }
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
        result.ActiveAudioSources = static_cast<std::size_t>(
            std::ranges::count_if(m_Impl->AudioSources, [](const auto& item) { return bool(item.second.Voice); }));
        result.PendingAudioAssets = m_Impl->PendingAudio;
        result.ActiveReverbZones = m_Impl->ActiveReverbZones;
        result.HasAudioListener = m_Impl->HasAudioListener;
        result.UsingPrimaryCameraListener = m_Impl->UsingPrimaryCameraListener;
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
