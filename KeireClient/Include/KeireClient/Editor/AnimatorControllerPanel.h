#pragma once

#include "Keire/Core.h"

#include <memory>
#include <string>

namespace KeireEditor
{
    class AnimatorControllerDocument;
    class SceneDocument;

    class IAnimatorControllerPanelController
    {
      public:
        virtual ~IAnimatorControllerPanelController() = default;
        [[nodiscard]] virtual AnimatorControllerDocument& AnimatorControllerState() noexcept = 0;
        [[nodiscard]] virtual const Keire::UiThemeDefinition& AnimatorControllerTheme() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetDatabase> AnimatorControllerDatabase() const noexcept = 0;
        [[nodiscard]] virtual Keire::Ref<Keire::AssetSystem> AnimatorControllerAssets() const noexcept = 0;
        [[nodiscard]] virtual SceneDocument& AnimatorControllerSceneDocument() noexcept = 0;
        virtual void ActivateAnimatorControllerHistory() noexcept = 0;
        virtual void SaveAnimatorControllerDocument() = 0;
        virtual void ReloadAnimatorControllerDocument(Keire::AssetId asset) = 0;
        virtual void UndoAnimatorControllerEdit() = 0;
        virtual void RedoAnimatorControllerEdit() = 0;
        virtual void ReportAnimatorControllerError(std::string message) noexcept = 0;
    };

    class AnimatorControllerPanel final
    {
      public:
        explicit AnimatorControllerPanel(IAnimatorControllerPanelController& controller) noexcept;
        ~AnimatorControllerPanel();

        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui);
        void SetMessage(std::string message) { m_Message = std::move(message); }
        void ResetTransientState() noexcept;
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        struct PreviewState;

        IAnimatorControllerPanelController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        std::unique_ptr<PreviewState> m_Preview;
        std::string m_SelectedTransition;
        std::string m_Message;
    };
} // namespace KeireEditor
