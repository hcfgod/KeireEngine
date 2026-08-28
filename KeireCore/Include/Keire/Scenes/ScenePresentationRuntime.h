#pragma once

#include "Keire/Api.h"
#include "Keire/Audio/AudioSystem.h"
#include "Keire/ECS/Component.h"
#include "Keire/Ref.h"
#include "Keire/Ui/RuntimeUi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace Keire
{
    class AssetSystem;
    class Scene;
    class UiFrame;

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
        void Navigate(RuntimeUiNavigation navigation);
        [[nodiscard]] bool PollUiEvent(RuntimeUiEvent& event);
        [[nodiscard]] ScenePresentationCheckpoint CaptureCheckpoint() const;
        void RestoreCheckpoint(const ScenePresentationCheckpoint& checkpoint);

        [[nodiscard]] Ref<RuntimeUiTree> Ui() const noexcept;
        [[nodiscard]] ScenePresentationRuntimeStatistics Statistics() const;
        void Draw(UiFrame& ui, float offsetX = 0.0F, float offsetY = 0.0F) const;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
