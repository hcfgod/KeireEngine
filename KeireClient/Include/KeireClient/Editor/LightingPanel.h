#pragma once

#include "Keire/Core.h"

#include <optional>
#include <string>

namespace KeireEditor
{
    class SceneDocument;

    class ILightingPanelController
    {
      public:
        virtual ~ILightingPanelController() = default;
        [[nodiscard]] virtual SceneDocument& LightingSceneDocument() noexcept = 0;
        [[nodiscard]] virtual bool LightingBakeBusy() const noexcept = 0;
        [[nodiscard]] virtual std::optional<Keire::AssetOperationProgress> LightingBakeProgress() const noexcept = 0;
        virtual void QueueLightingBake(bool force) = 0;
    };

    class LightingPanel final
    {
      public:
        explicit LightingPanel(ILightingPanelController& controller) noexcept : m_Controller(controller) {}

        void Attach(Keire::UiWorkspace& workspace);
        void Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        ILightingPanelController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
        std::string m_Error;
    };
} // namespace KeireEditor
