#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPicker.h"
#include "KeireClient/Editor/AuthoringWidgets.h"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace KeireEditor
{
    class AnimatorControllerDocument;
    class SceneDocument;
    struct AnimatorControllerPreviewState;

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
        void OpenClip(Keire::AssetId clip, std::string name);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        IAnimatorControllerPanelController& m_Controller;
        StableNodeGraphCanvas m_GraphCanvas;
        AssetPicker m_AddAnimationPicker;
        std::map<std::string, AssetPicker> m_AssetPickers;
        Keire::UiPanelRegistration m_Registration;
        std::unique_ptr<AnimatorControllerPreviewState> m_Preview;
        std::unique_ptr<AnimatorControllerDocument> m_ClipPreviewDocument;
        std::optional<NodeGraphContextRequest> m_GraphContext;
        std::string m_SelectedTransition;
        std::string m_GraphLayer;
        std::string m_GraphSubgraph;
        std::string m_Message;
        bool m_FocusGraph = true;
    };
} // namespace KeireEditor
