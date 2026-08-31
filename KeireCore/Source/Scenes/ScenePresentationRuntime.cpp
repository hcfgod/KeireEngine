#include "Keire/Scenes/ScenePresentationRuntime.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Audio/AudioAssets.h"
#include "Keire/ECS/Components/AudioComponents.h"
#include "Keire/ECS/Components/CameraComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Ui.h"
#include "KeireInternal/Scenes/ScenePresentationCanvasProjectionInternal.h"
#include "KeireInternal/Scenes/ScenePresentationUiDocumentInternal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <optional>
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
            std::map<AssetId, AssetHandle<AudioClipAsset>> ImpulseHandles;
            std::map<AssetId, std::uint64_t> ImpulseRevisions;
            AudioMixerRoutingId Routing;
            std::uint64_t Revision = 0;
            AssetId AppliedSnapshot;
            float AppliedWeight = -1.0F;
            float AppliedReverbSend = -1.0F;
        };

        Impl(Ref<AssetSystem> assets, Ref<AudioSystem> audio, const std::size_t maximumUiElements)
            : Assets(std::move(assets)), Audio(std::move(audio)), UiTree(CreateRef<RuntimeUiTree>(maximumUiElements)),
              UiDocuments(Assets, UiTree, maximumUiElements)
        {
            if (!Assets)
                throw std::invalid_argument("ScenePresentationRuntime requires asset services.");
        }

        [[nodiscard]] std::optional<AudioMixerImpulseResponses>
        ResolveImpulseResponses(AudioMixerState& state, const AudioMixerDefinition& definition)
        {
            const auto dependencies = AudioMixerDependencies(definition);
            std::erase_if(state.ImpulseHandles, [&dependencies](const auto& entry)
                          { return !std::ranges::binary_search(dependencies, entry.first); });
            for (const auto dependency : dependencies)
                if (!state.ImpulseHandles.contains(dependency))
                    state.ImpulseHandles.emplace(dependency,
                                                 Assets->Load<AudioClipAsset>(dependency, AssetPriority::High));

            AudioMixerImpulseResponses result;
            for (auto& [id, handle] : state.ImpulseHandles)
            {
                const auto loaded = handle.TryGetLoaded();
                if (!loaded)
                    return std::nullopt;
                result.emplace(id, loaded->Clip());
            }
            return result;
        }

        [[nodiscard]] static std::map<AssetId, std::uint64_t> ImpulseResponseRevisions(const AudioMixerState& state)
        {
            std::map<AssetId, std::uint64_t> result;
            for (const auto& [id, handle] : state.ImpulseHandles)
                result.emplace(id, handle.Revision());
            return result;
        }

        void SynchronizeUi(const Ref<Scene>& scene, const float viewportWidth, const float viewportHeight,
                           const RuntimeUiInsets safeArea, const RenderCamera* viewportCamera)
        {
            SeenUi.clear();
            UiDocuments.BeginSynchronization();
            CanvasProjection.ResetAssignments();
            std::vector<Detail::UiDocumentPanelProjection> documentProjections;
            for (const auto& entity : scene->Query<UiDocumentComponent>())
            {
                if (!entity)
                    continue;
                UiDocuments.Synchronize(entity, entity.GetComponent<UiDocumentComponent>(), UiNodes, NodeEntities,
                                        CanvasProjection, documentProjections, SeenUi);
            }
            UiDocuments.EndSynchronization(UiNodes, NodeEntities);
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
            if (ActiveSlider && !UiTree->Exists(ActiveSlider))
                ActiveSlider = {};
            UiDocuments.SetStyleEvaluationContext(viewportWidth, viewportHeight);
            UiTree->Layout(viewportWidth, viewportHeight, safeArea);
            CanvasProjection.Rebuild(scene, UiNodes, documentProjections, viewportWidth, viewportHeight,
                                     viewportCamera);
        }

        [[nodiscard]] Entity UiEntity(const RuntimeUiElementId node) const
        {
            const auto found = NodeEntities.find(node.Value());
            return found != NodeEntities.end() && ActiveScene ? ActiveScene->FindEntity(found->second) : Entity{};
        }

        [[nodiscard]] RuntimeUiElementId FindRuntimeUiControl(RuntimeUiElementId node,
                                                              const RuntimeUiElementType type) const
        {
            while (const auto state = UiTree->State(node))
            {
                if (state->Type == type)
                    return node;
                node = state->Parent;
            }
            return {};
        }

        void DrainUiEvents()
        {
            RuntimeUiEvent event;
            while (UiTree->PollEvent(event))
            {
                bool defaultPrevented = false;
                try
                {
                    defaultPrevented = UiDocuments.DispatchEvent(event);
                }
                catch (...)
                {
                    DeferredUiEvents.push_back(event);
                    throw;
                }
                if (event.Type == RuntimeUiEventType::Click && !defaultPrevented)
                {
                    if (const auto state = UiTree->State(event.Target);
                        state && state->Type == RuntimeUiElementType::Toggle && state->Enabled && state->Interactable)
                    {
                        auto control = state->Control;
                        control.Checked = !control.Checked;
                        if (UiTree->SetControl(event.Target, control))
                            (void)UiTree->DispatchEvent({.Type = RuntimeUiEventType::ValueChanged,
                                                         .Target = event.Target,
                                                         .PointerX = event.PointerX,
                                                         .PointerY = event.PointerY,
                                                         .Button = event.Button});
                    }
                }
                DeferredUiEvents.push_back(std::move(event));
            }
            UiDocuments.SynchronizeInteractionStates();
        }

        void UpdateSlider(const float x, const float y)
        {
            const auto state = UiTree->State(ActiveSlider);
            if (!state || state->Type != RuntimeUiElementType::Slider || !state->Enabled || !state->Interactable ||
                state->Rect.Empty())
                return;
            float normalized = state->Control.Vertical ? 1.0F - (y - state->Rect.Y) / state->Rect.Height
                                                       : (x - state->Rect.X) / state->Rect.Width;
            if (state->Control.Reversed)
                normalized = 1.0F - normalized;
            auto control = state->Control;
            const float previous = control.Value;
            control.Value = control.Minimum + std::clamp(normalized, 0.0F, 1.0F) * (control.Maximum - control.Minimum);
            if (control.Value != previous && UiTree->SetControl(ActiveSlider, control))
                (void)UiTree->DispatchEvent({.Type = RuntimeUiEventType::ValueChanged, .Target = ActiveSlider});
        }
        [[nodiscard]] std::optional<Vector2> ActiveSliderPoint(const float x, const float y) const noexcept
        {
            if (!ActiveSlider)
                return std::nullopt;
            if (const auto canvas = CanvasProjection.ForNode(ActiveSlider))
                return Detail::MapCapturedViewportToCanvasLayout(*canvas, {x, y});
            return std::nullopt;
        }

        [[nodiscard]] bool ScrollAt(const RuntimeUiElementId hit, const float horizontal, const float vertical)
        {
            if (!hit)
                return false;
            const auto runtimeNode = FindRuntimeUiControl(hit, RuntimeUiElementType::ScrollView);
            const auto runtimeState = UiTree->State(runtimeNode);
            if (!runtimeState || !runtimeState->Enabled || !runtimeState->Interactable)
                return false;
            const float scale = std::max(runtimeState->LayoutScale, 0.0001F);
            const Vector2 maximum{
                std::max(0.0F, runtimeState->Control.ContentSize.X - runtimeState->Rect.Width / scale),
                std::max(0.0F, runtimeState->Control.ContentSize.Y - runtimeState->Rect.Height / scale)};
            auto style = runtimeState->Style;
            const auto previous = style.ContentOffset;
            style.ContentOffset = {
                std::clamp(previous.X - horizontal * runtimeState->Control.ScrollSensitivity, 0.0F, maximum.X),
                std::clamp(previous.Y - vertical * runtimeState->Control.ScrollSensitivity, 0.0F, maximum.Y)};
            if (style.ContentOffset == previous || !UiTree->SetStyle(runtimeNode, std::move(style)))
                return false;
            return UiTree->DispatchEvent({.Type = RuntimeUiEventType::ValueChanged, .Target = runtimeNode});
        }

        void AppendText(const std::string_view text)
        {
            const auto focus = UiTree->Focus();
            if (text.empty())
                return;
            const auto state = UiTree->State(focus);
            if (!state || state->Type != RuntimeUiElementType::InputField || !state->Enabled || !state->Interactable ||
                state->Content.Text.size() + text.size() > 1'048'576)
                return;
            auto content = state->Content;
            content.Text.append(text);
            if (UiTree->SetContent(focus, std::move(content)))
                (void)UiTree->DispatchEvent({.Type = RuntimeUiEventType::TextChanged, .Target = focus});
        }

        [[nodiscard]] bool InputKey(const RuntimeUiKey key)
        {
            const auto focus = UiTree->Focus();
            const auto state = UiTree->State(focus);
            if (!state || state->Type != RuntimeUiElementType::InputField || !state->Enabled || !state->Interactable)
                return false;
            if (key == RuntimeUiKey::Enter)
                return UiTree->DispatchEvent({.Type = RuntimeUiEventType::Submit, .Target = focus});
            if (key == RuntimeUiKey::Escape)
                return UiTree->DispatchEvent({.Type = RuntimeUiEventType::Cancel, .Target = focus});
            if (key != RuntimeUiKey::Backspace || state->Content.Text.empty())
                return true;
            auto content = state->Content;
            std::size_t start = content.Text.size() - 1;
            while (start > 0 && (static_cast<unsigned char>(content.Text[start]) & 0xc0U) == 0x80U)
                --start;
            content.Text.erase(start);
            return UiTree->SetContent(focus, std::move(content)) &&
                   UiTree->DispatchEvent({.Type = RuntimeUiEventType::TextChanged, .Target = focus});
        }

        [[nodiscard]] AudioPlaybackRequest PlaybackRequest(const Entity& entity, const AudioSourceComponent& source,
                                                           std::shared_ptr<const AudioClipData> clip,
                                                           const AudioMixerRoutingId mixerRouting) const
        {
            Vector3 position;
            if (const auto transform = entity.GetComponent<TransformComponent>())
                position = transform->PresentationWorldPosition();
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
            const auto impulseResponses = ResolveImpulseResponses(state, loaded->Definition());
            if (!impulseResponses)
                return {};
            const auto revision = state.Handle.Revision();
            const auto impulseRevisions = ImpulseResponseRevisions(state);
            if (!state.Routing)
            {
                state.Routing = Audio->RegisterMixer(mixer, loaded->Definition(), *impulseResponses);
                state.Revision = revision;
                state.ImpulseRevisions = impulseRevisions;
            }
            else if (state.Revision != revision || state.ImpulseRevisions != impulseRevisions)
            {
                if (!Audio->UpdateMixer(state.Routing, loaded->Definition(), *impulseResponses))
                    state.Routing = Audio->RegisterMixer(mixer, loaded->Definition(), *impulseResponses);
                state.Revision = revision;
                state.ImpulseRevisions = impulseRevisions;
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
                        {
                            if (effect.Parameters.empty())
                                effect.Parameters.push_back(1.0F);
                            effect.Parameters[0] *= zone.ReverbSend() * weight;
                        }
                    }
                }
                for (auto& bus : definition.Buses)
                    for (auto& send : bus.Sends)
                        if (reverbBuses.contains(send.DestinationBus))
                            send.Gain *= zone.ReverbSend() * weight;
                const auto impulseResponses = ResolveImpulseResponses(state, definition);
                if (!impulseResponses || !Audio->UpdateMixer(routing, definition, *impulseResponses))
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
                    if (const auto impulseResponses = ResolveImpulseResponses(state, loaded->Definition()))
                        (void)Audio->UpdateMixer(state.Routing, loaded->Definition(), *impulseResponses);
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
                if (!UiNodes.contains(event.Target) || event.Type > RuntimeUiEventType::TextChanged ||
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
                    state.Position = transform->PresentationWorldPosition();
                    state.Forward = Math::TransformDirection(transform->PresentationWorldMatrix(), {0.0F, 0.0F, -1.0F});
                    state.Up = Math::TransformDirection(transform->PresentationWorldMatrix(), {0.0F, 1.0F, 0.0F});
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
                        state.Position = transform->PresentationWorldPosition();
                        state.Forward =
                            Math::TransformDirection(transform->PresentationWorldMatrix(), {0.0F, 0.0F, -1.0F});
                        state.Up = Math::TransformDirection(transform->PresentationWorldMatrix(), {0.0F, 1.0F, 0.0F});
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
                    const auto world = transform->PresentationWorldMatrix();
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
        Detail::ScenePresentationUiDocumentStore UiDocuments;
        Detail::ScenePresentationCanvasProjection CanvasProjection;
        std::set<EntityId> SeenUi;
        std::deque<RuntimeUiEvent> DeferredUiEvents;
        RuntimeUiElementId ActiveSlider;
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
        Synchronize(std::move(scene), viewportWidth, viewportHeight, playing, safeArea, nullptr);
    }

    void ScenePresentationRuntime::Synchronize(Ref<Scene> scene, const float viewportWidth, const float viewportHeight,
                                               const bool playing, const RuntimeUiInsets safeArea,
                                               const RenderCamera* viewportCamera)
    {
        if (!scene || !scene->IsOpen())
        {
            Clear();
            return;
        }
        const auto synchronizationStarted = std::chrono::steady_clock::now();
        m_Impl->ActiveScene = std::move(scene);
        const auto uiStarted = std::chrono::steady_clock::now();
        m_Impl->SynchronizeUi(m_Impl->ActiveScene, viewportWidth, viewportHeight, safeArea, viewportCamera);
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
    void ScenePresentationRuntime::AdvanceUi(const float deltaSeconds) { m_Impl->UiDocuments.Update(deltaSeconds); }

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
        m_Impl->UiDocuments.Clear(m_Impl->UiNodes, m_Impl->NodeEntities);
        m_Impl->UiNodes.clear();
        m_Impl->NodeEntities.clear();
        m_Impl->CanvasProjection.Clear();
        m_Impl->SeenUi.clear();
        m_Impl->DeferredUiEvents.clear();
        m_Impl->ActiveSlider = {};
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
        return ConsumeUiEvent(entity, RuntimeUiEventType::Click);
    }

    bool ScenePresentationRuntime::ConsumeUiEvent(const EntityId entity, const RuntimeUiEventType type)
    {
        const auto found = m_Impl->UiNodes.find(entity);
        if (found == m_Impl->UiNodes.end())
            return false;
        m_Impl->DrainUiEvents();
        const auto target = found->second;
        const auto deferred = std::ranges::find_if(m_Impl->DeferredUiEvents, [target, type](const RuntimeUiEvent& event)
                                                   { return event.Type == type && event.Target == target; });
        if (deferred != m_Impl->DeferredUiEvents.end())
        {
            m_Impl->DeferredUiEvents.erase(deferred);
            return true;
        }
        return false;
    }

    void ScenePresentationRuntime::PointerMove(const float x, const float y)
    {
        const auto hit = m_Impl->CanvasProjection.ResolveHit(*m_Impl->UiTree, x, y);
        const auto point = hit ? hit->LayoutPoint : Vector2{x, y};
        m_Impl->UiTree->PointerMoveTo(hit ? hit->Node : RuntimeUiElementId{}, point.X, point.Y);
        if (const auto sliderPoint = m_Impl->ActiveSliderPoint(x, y))
            m_Impl->UpdateSlider(sliderPoint->X, sliderPoint->Y);
        m_Impl->DrainUiEvents();
    }

    void ScenePresentationRuntime::PointerLeave()
    {
        m_Impl->UiTree->PointerLeave();
        m_Impl->DrainUiEvents();
    }

    bool ScenePresentationRuntime::PointerButton(const float x, const float y, const RuntimeUiPointerButton button,
                                                 const bool pressed)
    {
        const auto hit = m_Impl->CanvasProjection.ResolveHit(*m_Impl->UiTree, x, y);
        const auto point = hit ? hit->LayoutPoint : Vector2{x, y};
        const bool handled =
            m_Impl->UiTree->PointerButtonTo(hit ? hit->Node : RuntimeUiElementId{}, point.X, point.Y, button, pressed);
        if (button == RuntimeUiPointerButton::Primary)
        {
            if (pressed)
            {
                if (hit)
                {
                    m_Impl->ActiveSlider = m_Impl->FindRuntimeUiControl(hit->Node, RuntimeUiElementType::Slider);
                }
            }
            if (const auto sliderPoint = m_Impl->ActiveSliderPoint(x, y))
                m_Impl->UpdateSlider(sliderPoint->X, sliderPoint->Y);
            if (!pressed)
                m_Impl->ActiveSlider = {};
        }
        m_Impl->DrainUiEvents();
        return handled;
    }

    bool ScenePresentationRuntime::CancelPointerButton(const RuntimeUiPointerButton button) noexcept
    {
        const bool handled = m_Impl->UiTree->CancelPointerButton(button);
        if (button == RuntimeUiPointerButton::Primary)
            m_Impl->ActiveSlider = {};
        try
        {
            m_Impl->DrainUiEvents();
        }
        catch (...)
        {
        }
        return handled;
    }

    bool ScenePresentationRuntime::PointerWheel(const float x, const float y, const float horizontal,
                                                const float vertical)
    {
        if (!std::isfinite(horizontal) || !std::isfinite(vertical))
            throw std::invalid_argument("Runtime UI pointer wheel must be finite.");
        const auto hit = m_Impl->CanvasProjection.ResolveHit(*m_Impl->UiTree, x, y);
        return hit && m_Impl->ScrollAt(hit->Node, horizontal, vertical);
    }

    void ScenePresentationRuntime::TextInput(const std::string_view text) { m_Impl->AppendText(text); }

    bool ScenePresentationRuntime::KeyInput(const RuntimeUiKey key) { return m_Impl->InputKey(key); }

    bool ScenePresentationRuntime::TextInputFocused() const noexcept
    {
        try
        {
            const auto state = m_Impl->UiTree->State(m_Impl->UiTree->Focus());
            return state && state->Type == RuntimeUiElementType::InputField && state->Visible && state->Enabled &&
                   state->Interactable;
        }
        catch (...)
        {
            return false;
        }
    }

    EntityId ScenePresentationRuntime::FocusedUiEntity() const noexcept
    {
        try
        {
            const auto entity = m_Impl->UiEntity(m_Impl->UiTree->Focus());
            return entity ? entity.Id() : EntityId{};
        }
        catch (...)
        {
            return {};
        }
    }

    EntityId ScenePresentationRuntime::HitTestUiEntity(const float x, const float y) const noexcept
    {
        try
        {
            const auto hit = m_Impl->CanvasProjection.ResolveHit(*m_Impl->UiTree, x, y);
            if (!hit)
                return {};
            const auto entity = m_Impl->UiEntity(hit->Node);
            return entity ? entity.Id() : EntityId{};
        }
        catch (...)
        {
            return {};
        }
    }

    EntityId ScenePresentationRuntime::HitTestCanvasEntity(const float x, const float y) const noexcept
    {
        return m_Impl->CanvasProjection.HitTestCanvas(x, y);
    }

    std::optional<ScenePresentationCanvasGeometry>
    ScenePresentationRuntime::CanvasGeometry(const EntityId canvas) const noexcept
    {
        return m_Impl->CanvasProjection.CanvasGeometry(canvas);
    }

    std::optional<ScenePresentationUiGeometry>
    ScenePresentationRuntime::UiGeometry(const EntityId entity) const noexcept
    {
        return m_Impl->CanvasProjection.UiGeometry(entity, *m_Impl->UiTree, m_Impl->UiNodes);
    }

    std::optional<ScenePresentationUiDocumentElement>
    ScenePresentationRuntime::UiDocumentRoot(const EntityId document) const
    {
        return m_Impl->UiDocuments.Root(document);
    }

    std::optional<ScenePresentationUiDocumentElement>
    ScenePresentationRuntime::FindUiDocumentElement(const EntityId document, const AssetId stableId) const
    {
        return m_Impl->UiDocuments.Find(document, stableId);
    }

    std::optional<ScenePresentationUiDocumentElement>
    ScenePresentationRuntime::FindUiDocumentElement(const EntityId document, const std::string_view name) const
    {
        return m_Impl->UiDocuments.Find(document, name);
    }

    bool ScenePresentationRuntime::UiDocumentElementAlive(const EntityId document,
                                                          const std::uint64_t documentGeneration,
                                                          const std::uint64_t element) const noexcept
    {
        return m_Impl->UiDocuments.Alive(document, documentGeneration, element);
    }

    std::optional<std::string>
    ScenePresentationRuntime::ReadUiDocumentElementText(const EntityId document, const std::uint64_t documentGeneration,
                                                        const std::uint64_t element) const noexcept
    {
        return m_Impl->UiDocuments.ReadText(document, documentGeneration, element);
    }

    bool ScenePresentationRuntime::SetUiDocumentElementText(const EntityId document,
                                                            const std::uint64_t documentGeneration,
                                                            const std::uint64_t element,
                                                            const std::string_view text) noexcept
    {
        return m_Impl->UiDocuments.SetText(document, documentGeneration, element, text);
    }

    std::optional<float> ScenePresentationRuntime::ReadUiDocumentElementValue(
        const EntityId document, const std::uint64_t documentGeneration, const std::uint64_t element) const noexcept
    {
        return m_Impl->UiDocuments.ReadValue(document, documentGeneration, element);
    }

    bool ScenePresentationRuntime::SetUiDocumentElementValue(const EntityId document,
                                                             const std::uint64_t documentGeneration,
                                                             const std::uint64_t element, const float value) noexcept
    {
        return m_Impl->UiDocuments.SetValue(document, documentGeneration, element, value);
    }

    std::optional<bool>
    ScenePresentationRuntime::ReadUiDocumentElementFlag(const EntityId document, const std::uint64_t documentGeneration,
                                                        const std::uint64_t element,
                                                        const ScenePresentationUiDocumentFlag property) const noexcept
    {
        return m_Impl->UiDocuments.ReadFlag(document, documentGeneration, element, property);
    }

    bool ScenePresentationRuntime::SetUiDocumentElementFlag(const EntityId document,
                                                            const std::uint64_t documentGeneration,
                                                            const std::uint64_t element,
                                                            const ScenePresentationUiDocumentFlag property,
                                                            const bool value) noexcept
    {
        return m_Impl->UiDocuments.SetFlag(document, documentGeneration, element, property, value);
    }

    bool ScenePresentationRuntime::ConsumeUiDocumentElementEvent(const EntityId document,
                                                                 const std::uint64_t documentGeneration,
                                                                 const std::uint64_t element,
                                                                 const RuntimeUiEventType type) noexcept
    {
        m_Impl->DrainUiEvents();
        return m_Impl->UiDocuments.ConsumeEvent(document, documentGeneration, element, type, m_Impl->DeferredUiEvents);
    }

    bool ScenePresentationRuntime::FocusUiDocumentElement(const EntityId document,
                                                          const std::uint64_t documentGeneration,
                                                          const std::uint64_t element) noexcept
    {
        return m_Impl->UiDocuments.Focus(document, documentGeneration, element);
    }
    Ref<Ui::VisualElement> ScenePresentationRuntime::UiDocumentVisualElement(const EntityId document,
                                                                             const AssetId stableId) const noexcept
    {
        return m_Impl->UiDocuments.Visual(document, stableId);
    }
    void ScenePresentationRuntime::SetUiDocumentBindingSource(const EntityId document,
                                                              Ref<UiDocumentBindingSource> source)
    {
        m_Impl->UiDocuments.SetBindingSource(document, std::move(source));
    }
    std::optional<ScenePresentationUiDocumentDebugSnapshot>
    ScenePresentationRuntime::UiDocumentDebugSnapshot(const EntityId document) const
    {
        return m_Impl->UiDocuments.DebugSnapshot(document, m_Impl->DeferredUiEvents);
    }

    std::optional<ScenePresentationUiDocumentHit>
    ScenePresentationRuntime::HitTestUiDocument(const float x, const float y) const noexcept
    {
        try
        {
            const auto hit = m_Impl->CanvasProjection.ResolveHit(*m_Impl->UiTree, x, y);
            if (!hit)
                return std::nullopt;
            const auto owner = m_Impl->NodeEntities.find(hit->Node.Value());
            if (owner == m_Impl->NodeEntities.end())
                return std::nullopt;
            return m_Impl->UiDocuments.Hit(owner->second, hit->Node);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void ScenePresentationRuntime::Navigate(const RuntimeUiNavigation navigation)
    {
        m_Impl->UiTree->Navigate(navigation);
        m_Impl->DrainUiEvents();
    }

    bool ScenePresentationRuntime::PollUiEvent(RuntimeUiEvent& event)
    {
        m_Impl->DrainUiEvents();
        if (!m_Impl->DeferredUiEvents.empty())
        {
            event = m_Impl->DeferredUiEvents.front();
            m_Impl->DeferredUiEvents.pop_front();
            return true;
        }
        return false;
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

    std::vector<RuntimeUiRenderSubmission>
    ScenePresentationRuntime::UiRenderSubmissions(const Ref<RenderView>& view) const
    {
        return m_Impl->CanvasProjection.RenderSubmissions(m_Impl->UiTree, view);
    }

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
        m_Impl->CanvasProjection.Draw(*m_Impl->UiTree, ui, offsetX, offsetY, true, true);
    }

    void ScenePresentationRuntime::DrawScreenUi(UiFrame& ui, const float offsetX, const float offsetY) const
    {
        m_Impl->CanvasProjection.Draw(*m_Impl->UiTree, ui, offsetX, offsetY, true, false);
    }

    void ScenePresentationRuntime::DrawScreenUi(UiFrame& ui, const Ref<RenderView>& view, const float offsetX,
                                                const float offsetY) const
    {
        m_Impl->CanvasProjection.Draw(*m_Impl->UiTree, ui, offsetX, offsetY, true, false, view);
    }

    void ScenePresentationRuntime::DrawWorldUi(UiFrame& ui, const float offsetX, const float offsetY) const
    {
        m_Impl->CanvasProjection.Draw(*m_Impl->UiTree, ui, offsetX, offsetY, false, true);
    }
} // namespace Keire
