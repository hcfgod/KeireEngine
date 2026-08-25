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
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::size_t Utf8CodePointCount(const std::string_view value) noexcept
        {
            return static_cast<std::size_t>(std::ranges::count_if(
                value, [](const char byte) { return (static_cast<unsigned char>(byte) & 0xc0U) != 0x80U; }));
        }
    } // namespace

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
            : Assets(std::move(assets)), Audio(std::move(audio)), UiTree(CreateRef<RuntimeUiTree>(maximumUiElements))
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
            if (entity.GetComponent<UiSliderComponent>())
                type = RuntimeUiElementType::Slider;
            else if (entity.GetComponent<UiToggleComponent>())
                type = RuntimeUiElementType::Toggle;
            else if (entity.GetComponent<UiInputFieldComponent>())
                type = RuntimeUiElementType::InputField;
            else if (entity.GetComponent<UiScrollViewComponent>())
                type = RuntimeUiElementType::ScrollView;
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
            if (const auto slider = entity.GetComponent<UiSliderComponent>())
            {
                style.Background = {0.10F, 0.14F, 0.20F, 1.0F};
                style.HoverBackground = {0.13F, 0.19F, 0.27F, 1.0F};
                style.PressedBackground = {0.08F, 0.12F, 0.18F, 1.0F};
                style.DisabledBackground = {0.11F, 0.12F, 0.14F, 0.55F};
                style.Foreground = {0.12F, 0.72F, 0.96F, 1.0F};
                style.CornerRadius = 10.0F;
            }
            if (const auto toggle = entity.GetComponent<UiToggleComponent>())
            {
                style.Background = toggle->IsOn() ? toggle->OnColor() : toggle->OffColor();
                style.HoverBackground = toggle->IsOn() ? toggle->OnColor() : Color{0.22F, 0.27F, 0.34F, 1.0F};
                style.PressedBackground = {0.07F, 0.11F, 0.16F, 1.0F};
                style.DisabledBackground = {0.11F, 0.12F, 0.14F, 0.55F};
                style.Foreground = {0.94F, 0.98F, 1.0F, 1.0F};
                style.CornerRadius = 8.0F;
            }
            if (entity.GetComponent<UiInputFieldComponent>())
            {
                style.Background = {0.035F, 0.055F, 0.085F, 0.96F};
                style.HoverBackground = {0.055F, 0.09F, 0.13F, 1.0F};
                style.DisabledBackground = {0.07F, 0.08F, 0.10F, 0.55F};
                style.Foreground = {0.92F, 0.96F, 1.0F, 1.0F};
                style.Border = {0.18F, 0.48F, 0.72F, 0.9F};
                style.BorderWidth = 1.0F;
                style.CornerRadius = 8.0F;
                style.Padding = {12.0F, 8.0F, 12.0F, 8.0F};
                style.VerticalAlignment = RuntimeUiAlignment::Center;
            }
            if (const auto scroll = entity.GetComponent<UiScrollViewComponent>())
            {
                style.Background = {0.025F, 0.035F, 0.055F, 0.72F};
                style.Foreground = {0.20F, 0.66F, 0.88F, 0.82F};
                style.ContentOffset = scroll->Offset();
                style.ClipChildren = true;
                style.CornerRadius = 8.0F;
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
            if (const auto accessibility = entity.GetComponent<UiAccessibilityComponent>())
                style.NavigationOrder = accessibility->NavigationOrder();
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
            if (const auto input = entity.GetComponent<UiInputFieldComponent>())
            {
                content.Text = input->Text().empty() ? input->Placeholder() : input->Text();
                if (input->ContentType() == UiInputContentType::Password && !input->Text().empty())
                    content.Text.assign(Utf8CodePointCount(input->Text()), '*');
                content.AccessibilityLabel = input->Placeholder();
            }
            if (const auto accessibility = entity.GetComponent<UiAccessibilityComponent>())
            {
                if (!accessibility->Label().empty())
                    content.AccessibilityLabel = accessibility->Label();
                content.AccessibilityHint = accessibility->Hint();
                content.AccessibilityRole = static_cast<std::uint8_t>(accessibility->Role());
            }
            (void)UiTree->SetContent(node, std::move(content));

            RuntimeUiControlState control;
            if (const auto slider = entity.GetComponent<UiSliderComponent>())
            {
                control.Minimum = slider->Minimum();
                control.Maximum = slider->Maximum();
                control.Value = slider->Value();
                control.Vertical = slider->Direction() == UiSliderDirection::BottomToTop ||
                                   slider->Direction() == UiSliderDirection::TopToBottom;
                control.Reversed = slider->Direction() == UiSliderDirection::RightToLeft ||
                                   slider->Direction() == UiSliderDirection::TopToBottom;
            }
            if (const auto toggle = entity.GetComponent<UiToggleComponent>())
                control.Checked = toggle->IsOn();
            if (const auto scroll = entity.GetComponent<UiScrollViewComponent>())
                control.ContentSize = scroll->ContentSize();
            (void)UiTree->SetControl(node, control);
            (void)UiTree->SetVisible(node, entity.ActiveInHierarchy());
            const auto button = entity.GetComponent<UiButtonComponent>();
            const auto slider = entity.GetComponent<UiSliderComponent>();
            const auto toggle = entity.GetComponent<UiToggleComponent>();
            const auto input = entity.GetComponent<UiInputFieldComponent>();
            const auto scroll = entity.GetComponent<UiScrollViewComponent>();
            const bool hasControl = button || slider || toggle || input || scroll;
            const bool interactable = (button && button->Interactable()) || (slider && slider->Interactable()) ||
                                      (toggle && toggle->Interactable()) || (input && input->Interactable()) ||
                                      (scroll && scroll->Interactable());
            (void)UiTree->SetEnabled(node, !hasControl || interactable);
            (void)UiTree->SetInteractable(node, hasControl && interactable);
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
            if (ActiveSlider && !SeenUi.contains(ActiveSlider))
                ActiveSlider = {};
            UiTree->Layout(viewportWidth, viewportHeight, safeArea, settings);
        }

        [[nodiscard]] Entity UiEntity(const RuntimeUiElementId node) const
        {
            const auto found = NodeEntities.find(node.Value());
            return found != NodeEntities.end() && ActiveScene ? ActiveScene->FindEntity(found->second) : Entity{};
        }

        template <typename ComponentType>
        [[nodiscard]] std::pair<Entity, RuntimeUiElementId> FindUiControl(RuntimeUiElementId node) const
        {
            while (node)
            {
                const auto entity = UiEntity(node);
                if (entity && entity.GetComponent<ComponentType>())
                    return {entity, node};
                const auto state = UiTree->State(node);
                node = state ? state->Parent : RuntimeUiElementId{};
            }
            return {};
        }

        void QueueUiEvent(const EntityId entity, const RuntimeUiEventType type)
        {
            const auto found = UiNodes.find(entity);
            if (found != UiNodes.end())
                DeferredUiEvents.push_back({.Type = type, .Target = found->second});
        }

        void DrainUiEvents()
        {
            RuntimeUiEvent event;
            while (UiTree->PollEvent(event))
            {
                if (event.Type == RuntimeUiEventType::Click)
                {
                    const auto entity = UiEntity(event.Target);
                    if (const auto toggle =
                            entity ? entity.GetComponent<UiToggleComponent>() : Ref<UiToggleComponent>{};
                        toggle && toggle->Interactable())
                    {
                        toggle->SetIsOn(!toggle->IsOn());
                        DeferredUiEvents.push_back({.Type = RuntimeUiEventType::ValueChanged,
                                                    .Target = event.Target,
                                                    .PointerX = event.PointerX,
                                                    .PointerY = event.PointerY,
                                                    .Button = event.Button});
                    }
                }
                DeferredUiEvents.push_back(std::move(event));
            }
        }

        void UpdateSlider(const float x, const float y)
        {
            if (!ActiveSlider || !ActiveScene)
                return;
            const auto entity = ActiveScene->FindEntity(ActiveSlider);
            const auto slider = entity ? entity.GetComponent<UiSliderComponent>() : Ref<UiSliderComponent>{};
            const auto found = UiNodes.find(ActiveSlider);
            const auto state = found != UiNodes.end() ? UiTree->State(found->second) : std::nullopt;
            if (!slider || !slider->Interactable() || !state || state->Rect.Empty())
                return;
            float normalized = 0.0F;
            switch (slider->Direction())
            {
            case UiSliderDirection::LeftToRight:
                normalized = (x - state->Rect.X) / state->Rect.Width;
                break;
            case UiSliderDirection::RightToLeft:
                normalized = 1.0F - (x - state->Rect.X) / state->Rect.Width;
                break;
            case UiSliderDirection::BottomToTop:
                normalized = 1.0F - (y - state->Rect.Y) / state->Rect.Height;
                break;
            case UiSliderDirection::TopToBottom:
                normalized = (y - state->Rect.Y) / state->Rect.Height;
                break;
            }
            const float previous = slider->Value();
            slider->SetValue(slider->Minimum() +
                             std::clamp(normalized, 0.0F, 1.0F) * (slider->Maximum() - slider->Minimum()));
            if (slider->Value() != previous)
                QueueUiEvent(entity.Id(), RuntimeUiEventType::ValueChanged);
        }

        void ScrollAt(const float x, const float y, const float horizontal, const float vertical)
        {
            const auto hit = UiTree->HitTest(x, y);
            if (!hit)
                return;
            const auto [entity, node] = FindUiControl<UiScrollViewComponent>(*hit);
            const auto scroll = entity ? entity.GetComponent<UiScrollViewComponent>() : Ref<UiScrollViewComponent>{};
            const auto state = UiTree->State(node);
            if (!scroll || !scroll->Interactable() || !state)
                return;
            const float scale = std::max(UiTree->Statistics().Scale, 0.0001F);
            const auto content = scroll->ContentSize();
            const Vector2 maximum{std::max(0.0F, content.X - state->Rect.Width / scale),
                                  std::max(0.0F, content.Y - state->Rect.Height / scale)};
            const auto previous = scroll->Offset();
            const Vector2 candidate{
                scroll->Horizontal() ? std::clamp(previous.X - horizontal * scroll->Sensitivity(), 0.0F, maximum.X)
                                     : previous.X,
                scroll->Vertical() ? std::clamp(previous.Y - vertical * scroll->Sensitivity(), 0.0F, maximum.Y)
                                   : previous.Y};
            if (candidate == previous)
                return;
            scroll->SetOffset(candidate);
            QueueUiEvent(entity.Id(), RuntimeUiEventType::ValueChanged);
        }

        void AppendText(const std::string_view text)
        {
            const auto focus = UiTree->Focus();
            const auto entity = UiEntity(focus);
            const auto input = entity ? entity.GetComponent<UiInputFieldComponent>() : Ref<UiInputFieldComponent>{};
            if (!input || !input->Interactable() || text.empty())
                return;
            auto candidate = input->Text();
            if (candidate.size() + text.size() > input->CharacterLimit())
                return;
            candidate.append(text);
            try
            {
                input->SetText(std::move(candidate));
                QueueUiEvent(entity.Id(), RuntimeUiEventType::TextChanged);
            }
            catch (const std::invalid_argument&)
            {
            }
        }

        [[nodiscard]] bool InputKey(const RuntimeUiKey key)
        {
            const auto focus = UiTree->Focus();
            const auto entity = UiEntity(focus);
            const auto input = entity ? entity.GetComponent<UiInputFieldComponent>() : Ref<UiInputFieldComponent>{};
            if (!input || !input->Interactable())
                return false;
            if (key == RuntimeUiKey::Enter)
            {
                if (input->Multiline())
                    AppendText("\n");
                else
                    QueueUiEvent(entity.Id(), RuntimeUiEventType::Submit);
                return true;
            }
            if (key == RuntimeUiKey::Escape)
            {
                QueueUiEvent(entity.Id(), RuntimeUiEventType::Cancel);
                return true;
            }
            if (key != RuntimeUiKey::Backspace || input->Text().empty())
                return true;
            auto candidate = input->Text();
            std::size_t start = candidate.size() - 1;
            while (start > 0 && (static_cast<unsigned char>(candidate[start]) & 0xc0U) == 0x80U)
                --start;
            candidate.erase(start);
            input->SetText(std::move(candidate));
            QueueUiEvent(entity.Id(), RuntimeUiEventType::TextChanged);
            return true;
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
        std::set<EntityId> SeenUi;
        std::deque<RuntimeUiEvent> DeferredUiEvents;
        EntityId ActiveSlider;
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
        m_Impl->UiTree->PointerMove(x, y);
        m_Impl->UpdateSlider(x, y);
        m_Impl->DrainUiEvents();
    }

    void ScenePresentationRuntime::PointerButton(const float x, const float y, const RuntimeUiPointerButton button,
                                                 const bool pressed)
    {
        m_Impl->UiTree->PointerButton(x, y, button, pressed);
        if (button == RuntimeUiPointerButton::Primary)
        {
            if (pressed)
            {
                const auto hit = m_Impl->UiTree->HitTest(x, y);
                if (hit)
                {
                    const auto slider = m_Impl->FindUiControl<UiSliderComponent>(*hit);
                    m_Impl->ActiveSlider = slider.first ? slider.first.Id() : EntityId{};
                }
            }
            m_Impl->UpdateSlider(x, y);
            if (!pressed)
                m_Impl->ActiveSlider = {};
        }
        m_Impl->DrainUiEvents();
    }

    void ScenePresentationRuntime::PointerWheel(const float x, const float y, const float horizontal,
                                                const float vertical)
    {
        if (!std::isfinite(horizontal) || !std::isfinite(vertical))
            throw std::invalid_argument("Runtime UI pointer wheel must be finite.");
        m_Impl->ScrollAt(x, y, horizontal, vertical);
    }

    void ScenePresentationRuntime::TextInput(const std::string_view text) { m_Impl->AppendText(text); }

    bool ScenePresentationRuntime::KeyInput(const RuntimeUiKey key) { return m_Impl->InputKey(key); }

    bool ScenePresentationRuntime::TextInputFocused() const noexcept
    {
        try
        {
            const auto entity = m_Impl->UiEntity(m_Impl->UiTree->Focus());
            const auto input = entity ? entity.GetComponent<UiInputFieldComponent>() : Ref<UiInputFieldComponent>{};
            return input && input->Interactable();
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
