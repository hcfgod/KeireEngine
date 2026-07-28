#pragma once

#include "Keire/Core.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace KeireEditor
{
    class AudioMixerDocument;

    class IAudioMixerPanelController
    {
      public:
        virtual ~IAudioMixerPanelController() = default;
        [[nodiscard]] virtual AudioMixerDocument& AudioMixerState() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& AudioMixerTheme() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> AudioMixerDatabase() const noexcept = 0;
        [[nodiscard]] virtual std::string_view AudioMixerPreviewDiagnostic() const noexcept = 0;
        virtual void ActivateAudioMixerHistory() noexcept = 0;
        virtual void SaveAudioMixerDocument() = 0;
        virtual void DiscardAudioMixerDocument() = 0;
        virtual void ReloadAudioMixerDocument(Keire::AssetId asset) = 0;
        virtual void UndoAudioMixerEdit() = 0;
        virtual void RedoAudioMixerEdit() = 0;
        virtual void StopAudioMixerPreview() noexcept = 0;
        virtual void ReportAudioMixerError(std::string message) noexcept = 0;
    };

    class AudioMixerPanel final
    {
      public:
        explicit AudioMixerPanel(IAudioMixerPanelController& controller) noexcept : m_Controller(controller) {}

        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui);
        void SetMessage(std::string message) { m_Message = std::move(message); }
        void ResetTransientState() noexcept;
        void StopTransientPreview() noexcept;
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        [[nodiscard]] bool ApplyEdit(std::string_view name,
                                     const std::function<void(Keire::AudioMixerDefinition&)>& operation);
        void DrawRouting(Keire::UiFrame& ui);
        void DrawSelectedBus(Keire::UiFrame& ui);
        void DrawSnapshots(Keire::UiFrame& ui);
        void DrawDucking(Keire::UiFrame& ui);

        IAudioMixerPanelController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        Keire::AssetId m_SelectedSnapshot;
        Keire::AssetId m_SelectedDucking;
        std::string m_Message;
        bool m_WasVisible = false;
    };
} // namespace KeireEditor
