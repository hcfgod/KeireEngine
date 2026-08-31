#pragma once

#include "Keire/Api.h"
#include "Keire/Audio/AudioSystem.h"
#include "Keire/ECS/Component.h"
#include "Keire/Ref.h"
#include "Keire/Ui/RuntimeUi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace Keire
{
    class AssetSystem;
    enum class CanvasRenderMode : std::uint8_t
    {
        ScreenSpaceOverlay,
        ScreenSpaceCamera,
        WorldSpace
    };
    struct RenderCamera;
    class Scene;
    class RenderView;
    struct RuntimeUiRenderSubmission;
    class UiFrame;
    class UiDocumentBindingSource;
    namespace Ui
    {
        class VisualElement;
    }

    enum class AudioSourcePlaybackState : std::uint8_t
    {
        Stopped,
        Playing,
        Paused
    };

    struct AudioSourcePlaybackInfo
    {
        AudioSourcePlaybackState State = AudioSourcePlaybackState::Stopped;
        float PositionSeconds = 0.0F;
        float DurationSeconds = 0.0F;
    };

    struct ScenePresentationRuntimeStatistics
    {
        RuntimeUiStatistics Ui;
        AudioSystemStatistics Audio;
        std::size_t TrackedUiEntities = 0;
        std::size_t TrackedAudioSources = 0;
        std::size_t ActiveAudioSources = 0;
        std::size_t PendingAudioAssets = 0;
        std::size_t ActiveReverbZones = 0;
        bool HasAudioListener = false;
        bool UsingPrimaryCameraListener = false;
        std::uint64_t SynchronizationCount = 0;
        float UiSynchronizationMilliseconds = 0.0F;
        float AudioSynchronizationMilliseconds = 0.0F;
        float SynchronizationMilliseconds = 0.0F;
    };

    struct ScenePresentationAudioCheckpoint
    {
        EntityId Entity;
        AssetId Clip;
        AudioSourcePlaybackState State = AudioSourcePlaybackState::Stopped;
        std::uint64_t Frame = 0;
        bool ManualPlayRequested = false;
        bool PlayOnAwakeConsumed = false;
    };

    struct ScenePresentationUiEventCheckpoint
    {
        RuntimeUiEventType Type = RuntimeUiEventType::Click;
        EntityId Target;
        float PointerX = 0.0F;
        float PointerY = 0.0F;
        RuntimeUiPointerButton Button = RuntimeUiPointerButton::Primary;
    };

    struct ScenePresentationCheckpoint
    {
        EntityId FocusedEntity;
        std::vector<ScenePresentationAudioCheckpoint> AudioSources;
        std::vector<ScenePresentationUiEventCheckpoint> PendingUiEvents;
    };

    struct ScenePresentationCanvasGeometry
    {
        EntityId Canvas;
        CanvasRenderMode RenderMode{};
        std::array<Vector2, 4> ViewportCorners{};
        float PlaneDistance = 0.0F;
        bool Visible = false;
    };

    struct ScenePresentationUiGeometry
    {
        EntityId Entity;
        EntityId Canvas;
        std::array<Vector2, 4> ViewportCorners{};
        bool Visible = false;
    };

    struct ScenePresentationUiDocumentElement
    {
        std::uint64_t DocumentGeneration = 0;
        std::uint64_t Element = 0;
        AssetId StableId;
        RuntimeUiElementType Type = RuntimeUiElementType::Panel;
    };

    enum class ScenePresentationUiDocumentFlag : std::uint8_t
    {
        Interactable,
        Checked,
        Focused,
        Enabled
    };

    struct ScenePresentationUiDocumentDebugState
    {
        RuntimeUiElementType Type = RuntimeUiElementType::Panel;
        std::optional<AssetId> Parent;
        RuntimeUiStyle Style;
        RuntimeUiContent Content;
        RuntimeUiControlState Control;
        RuntimeUiRect Rect;
        RuntimeUiRect ClipRect;
        float LayoutScale = 1.0F;
        bool Visible = true;
        bool Enabled = true;
        bool Interactable = false;
        bool Focused = false;
        bool Hovered = false;
        bool Pressed = false;
        RuntimeUiDirtyReason DirtyReasons = RuntimeUiDirtyReason::None;
    };

    struct ScenePresentationUiDocumentSelectorTrace
    {
        AssetId StableId;
        std::string Selector;
        std::uint32_t Specificity = 0;
        std::size_t SourceOrder = 0;
        std::vector<std::string> AppliedProperties;
    };

    struct ScenePresentationUiDocumentEventRoute
    {
        std::uint64_t Sequence = 0;
        RuntimeUiEventType Type = RuntimeUiEventType::Click;
        RuntimeUiEventPhase Phase = RuntimeUiEventPhase::Target;
        std::optional<AssetId> Target;
        std::optional<AssetId> CurrentTarget;
        float PointerX = 0.0F;
        float PointerY = 0.0F;
        RuntimeUiPointerButton Button = RuntimeUiPointerButton::Primary;
    };

    struct ScenePresentationUiDocumentDebugElement
    {
        AssetId StableId;
        ScenePresentationUiDocumentDebugState State;
    };

    struct ScenePresentationUiDocumentDebugEvent
    {
        RuntimeUiEventType Type = RuntimeUiEventType::Click;
        std::optional<AssetId> Target;
        float PointerX = 0.0F;
        float PointerY = 0.0F;
        RuntimeUiPointerButton Button = RuntimeUiPointerButton::Primary;
    };

    struct ScenePresentationUiDocumentDebugSnapshot
    {
        EntityId Document;
        AssetId VisualTree;
        std::uint64_t DocumentGeneration = 0;
        std::vector<ScenePresentationUiDocumentDebugElement> Elements;
        std::optional<AssetId> Focused;
        std::vector<ScenePresentationUiDocumentDebugEvent> PendingTargetEvents;
        std::vector<ScenePresentationUiDocumentSelectorTrace> SelectorTrace;
        std::vector<ScenePresentationUiDocumentEventRoute> EventRouteHistory;
        RuntimeUiStatistics Statistics;
    };

    struct ScenePresentationUiDocumentHit
    {
        EntityId Document;
        AssetId StableId;
        std::uint64_t DocumentGeneration = 0;
        ScenePresentationUiDocumentDebugState State;
    };

    class KEIRE_API ScenePresentationRuntime final : public RefCounted
    {
      public:
        ScenePresentationRuntime(Ref<AssetSystem> assets, Ref<AudioSystem> audio,
                                 std::size_t maximumUiElements = 16'384);
        ~ScenePresentationRuntime() override;

        ScenePresentationRuntime(const ScenePresentationRuntime&) = delete;
        ScenePresentationRuntime& operator=(const ScenePresentationRuntime&) = delete;

        void Synchronize(Ref<Scene> scene, float viewportWidth, float viewportHeight, bool playing,
                         RuntimeUiInsets safeArea = {});
        void Synchronize(Ref<Scene> scene, float viewportWidth, float viewportHeight, bool playing,
                         RuntimeUiInsets safeArea, const RenderCamera* viewportCamera);
        void AdvanceUi(float deltaSeconds);
        void SetDefaultMixer(AssetId mixer) noexcept;
        [[nodiscard]] AssetId DefaultMixer() const noexcept;
        void Clear() noexcept;

        [[nodiscard]] bool Play(EntityId source);
        [[nodiscard]] bool Pause(EntityId source);
        [[nodiscard]] bool Resume(EntityId source);
        [[nodiscard]] bool Stop(EntityId source);
        [[nodiscard]] bool Seek(EntityId source, float positionSeconds);
        [[nodiscard]] AudioSourcePlaybackInfo Playback(EntityId source) const;
        [[nodiscard]] bool SetFocus(EntityId entity);
        [[nodiscard]] bool ConsumeClick(EntityId entity);
        [[nodiscard]] bool ConsumeUiEvent(EntityId entity, RuntimeUiEventType type);
        void PointerMove(float x, float y);
        void PointerLeave();
        bool PointerButton(float x, float y, RuntimeUiPointerButton button, bool pressed);
        bool CancelPointerButton(RuntimeUiPointerButton button) noexcept;
        bool PointerWheel(float x, float y, float horizontal, float vertical);
        void TextInput(std::string_view text);
        [[nodiscard]] bool KeyInput(RuntimeUiKey key);
        [[nodiscard]] bool TextInputFocused() const noexcept;
        [[nodiscard]] EntityId FocusedUiEntity() const noexcept;
        [[nodiscard]] EntityId HitTestUiEntity(float x, float y) const noexcept;
        [[nodiscard]] EntityId HitTestCanvasEntity(float x, float y) const noexcept;
        [[nodiscard]] std::optional<ScenePresentationCanvasGeometry> CanvasGeometry(EntityId canvas) const noexcept;
        [[nodiscard]] std::optional<ScenePresentationUiGeometry> UiGeometry(EntityId entity) const noexcept;
        [[nodiscard]] std::optional<ScenePresentationUiDocumentElement> UiDocumentRoot(EntityId document) const;
        [[nodiscard]] std::optional<ScenePresentationUiDocumentElement> FindUiDocumentElement(EntityId document,
                                                                                              AssetId stableId) const;
        [[nodiscard]] std::optional<ScenePresentationUiDocumentElement>
        FindUiDocumentElement(EntityId document, std::string_view name) const;
        [[nodiscard]] bool UiDocumentElementAlive(EntityId document, std::uint64_t documentGeneration,
                                                  std::uint64_t element) const noexcept;
        [[nodiscard]] std::optional<std::string> ReadUiDocumentElementText(EntityId document,
                                                                           std::uint64_t documentGeneration,
                                                                           std::uint64_t element) const noexcept;
        [[nodiscard]] bool SetUiDocumentElementText(EntityId document, std::uint64_t documentGeneration,
                                                    std::uint64_t element, std::string_view text) noexcept;
        [[nodiscard]] std::optional<float> ReadUiDocumentElementValue(EntityId document,
                                                                      std::uint64_t documentGeneration,
                                                                      std::uint64_t element) const noexcept;
        [[nodiscard]] bool SetUiDocumentElementValue(EntityId document, std::uint64_t documentGeneration,
                                                     std::uint64_t element, float value) noexcept;
        [[nodiscard]] std::optional<bool>
        ReadUiDocumentElementFlag(EntityId document, std::uint64_t documentGeneration, std::uint64_t element,
                                  ScenePresentationUiDocumentFlag property) const noexcept;
        [[nodiscard]] bool SetUiDocumentElementFlag(EntityId document, std::uint64_t documentGeneration,
                                                    std::uint64_t element, ScenePresentationUiDocumentFlag property,
                                                    bool value) noexcept;
        [[nodiscard]] bool ConsumeUiDocumentElementEvent(EntityId document, std::uint64_t documentGeneration,
                                                         std::uint64_t element, RuntimeUiEventType type) noexcept;
        [[nodiscard]] bool FocusUiDocumentElement(EntityId document, std::uint64_t documentGeneration,
                                                  std::uint64_t element) noexcept;
        [[nodiscard]] Ref<Ui::VisualElement> UiDocumentVisualElement(EntityId document,
                                                                     AssetId stableId) const noexcept;
        void SetUiDocumentBindingSource(EntityId document, Ref<UiDocumentBindingSource> source);
        [[nodiscard]] std::optional<ScenePresentationUiDocumentDebugSnapshot>
        UiDocumentDebugSnapshot(EntityId document) const;
        [[nodiscard]] std::optional<ScenePresentationUiDocumentHit> HitTestUiDocument(float x, float y) const noexcept;
        void Navigate(RuntimeUiNavigation navigation);
        [[nodiscard]] bool PollUiEvent(RuntimeUiEvent& event);
        [[nodiscard]] ScenePresentationCheckpoint CaptureCheckpoint() const;
        void RestoreCheckpoint(const ScenePresentationCheckpoint& checkpoint);

        [[nodiscard]] Ref<RuntimeUiTree> Ui() const noexcept;
        [[nodiscard]] std::vector<RuntimeUiRenderSubmission> UiRenderSubmissions(const Ref<RenderView>& view) const;
        [[nodiscard]] ScenePresentationRuntimeStatistics Statistics() const;
        void Draw(UiFrame& ui, float offsetX = 0.0F, float offsetY = 0.0F) const;
        void DrawScreenUi(UiFrame& ui, float offsetX = 0.0F, float offsetY = 0.0F) const;
        void DrawScreenUi(UiFrame& ui, const Ref<RenderView>& view, float offsetX = 0.0F, float offsetY = 0.0F) const;
        void DrawWorldUi(UiFrame& ui, float offsetX = 0.0F, float offsetY = 0.0F) const;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
