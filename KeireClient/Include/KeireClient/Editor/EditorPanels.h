#pragma once

#include "Keire/Ui.h"

namespace KeireEditor
{
    class ISceneViewportController
    {
      public:
        virtual ~ISceneViewportController() = default;
        virtual void DrawSceneContent(Keire::UiFrame& ui) = 0;
    };

    class IHierarchyController
    {
      public:
        virtual ~IHierarchyController() = default;
        virtual void DrawHierarchyContent(Keire::UiFrame& ui) = 0;
    };

    class IInspectorController
    {
      public:
        virtual ~IInspectorController() = default;
        virtual void DrawInspectorContent(Keire::UiFrame& ui) = 0;
    };

    class IInputActionsController
    {
      public:
        virtual ~IInputActionsController() = default;
        virtual void DrawInputActionsContent(Keire::UiFrame& ui) = 0;
    };

    class IProjectSettingsController
    {
      public:
        virtual ~IProjectSettingsController() = default;
        virtual void DrawProjectSettingsContent(Keire::UiFrame& ui) = 0;
    };

    class SceneViewportPanel final
    {
      public:
        explicit SceneViewportPanel(ISceneViewportController& controller) noexcept : m_Controller(controller) {}
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.scene", "Scene"});
        }
        void Draw(Keire::UiFrame& ui)
        {
            if (auto panel = ui.BeginPanel(m_Registration); panel)
                m_Controller.DrawSceneContent(ui);
        }
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        ISceneViewportController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
    };

    class HierarchyPanel final
    {
      public:
        explicit HierarchyPanel(IHierarchyController& controller) noexcept : m_Controller(controller) {}
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.hierarchy", "Hierarchy"});
        }
        void Draw(Keire::UiFrame& ui)
        {
            if (auto panel = ui.BeginPanel(m_Registration); panel)
                m_Controller.DrawHierarchyContent(ui);
        }
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        IHierarchyController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
    };

    class InspectorPanel final
    {
      public:
        explicit InspectorPanel(IInspectorController& controller) noexcept : m_Controller(controller) {}
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.inspector", "Inspector"});
        }
        void Draw(Keire::UiFrame& ui)
        {
            if (auto panel = ui.BeginPanel(m_Registration); panel)
                m_Controller.DrawInspectorContent(ui);
        }
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        IInspectorController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
    };

    class InputActionsPanel final
    {
      public:
        explicit InputActionsPanel(IInputActionsController& controller) noexcept : m_Controller(controller) {}
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.input-actions", "Input Actions", false});
        }
        void Draw(Keire::UiFrame& ui)
        {
            if (auto panel = ui.BeginPanel(m_Registration); panel)
                m_Controller.DrawInputActionsContent(ui);
        }
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        IInputActionsController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
    };

    class ProjectSettingsPanel final
    {
      public:
        explicit ProjectSettingsPanel(IProjectSettingsController& controller) noexcept : m_Controller(controller) {}
        void Attach(Keire::UiWorkspace& workspace)
        {
            m_Registration = workspace.RegisterPanel({"editor.project-settings", "Project Settings", false});
        }
        void Draw(Keire::UiFrame& ui)
        {
            if (auto panel = ui.BeginPanel(m_Registration); panel)
                m_Controller.DrawProjectSettingsContent(ui);
        }
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        IProjectSettingsController& m_Controller;
        Keire::UiPanelRegistration m_Registration;
    };
} // namespace KeireEditor
