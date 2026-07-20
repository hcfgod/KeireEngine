#pragma once

#include "Keire/Ui.h"

namespace KeireEditor
{
    class ISceneViewportController
    {
      public:
        virtual ~ISceneViewportController() = default;
        virtual void DrawScene(Keire::UiFrame& ui) = 0;
    };

    class IHierarchyController
    {
      public:
        virtual ~IHierarchyController() = default;
        virtual void DrawHierarchy(Keire::UiFrame& ui) = 0;
    };

    class IInspectorController
    {
      public:
        virtual ~IInspectorController() = default;
        virtual void DrawInspector(Keire::UiFrame& ui) = 0;
    };

    class IInputActionsController
    {
      public:
        virtual ~IInputActionsController() = default;
        virtual void DrawInputActionsEditor(Keire::UiFrame& ui) = 0;
    };

    class IProjectSettingsController
    {
      public:
        virtual ~IProjectSettingsController() = default;
        virtual void DrawProjectSettings(Keire::UiFrame& ui) = 0;
    };

    class SceneViewportPanel final
    {
      public:
        explicit SceneViewportPanel(ISceneViewportController& controller) noexcept : m_Controller(controller) {}
        void Draw(Keire::UiFrame& ui) { m_Controller.DrawScene(ui); }

      private:
        ISceneViewportController& m_Controller;
    };

    class HierarchyPanel final
    {
      public:
        explicit HierarchyPanel(IHierarchyController& controller) noexcept : m_Controller(controller) {}
        void Draw(Keire::UiFrame& ui) { m_Controller.DrawHierarchy(ui); }

      private:
        IHierarchyController& m_Controller;
    };

    class InspectorPanel final
    {
      public:
        explicit InspectorPanel(IInspectorController& controller) noexcept : m_Controller(controller) {}
        void Draw(Keire::UiFrame& ui) { m_Controller.DrawInspector(ui); }

      private:
        IInspectorController& m_Controller;
    };

    class InputActionsPanel final
    {
      public:
        explicit InputActionsPanel(IInputActionsController& controller) noexcept : m_Controller(controller) {}
        void Draw(Keire::UiFrame& ui) { m_Controller.DrawInputActionsEditor(ui); }

      private:
        IInputActionsController& m_Controller;
    };

    class ProjectSettingsPanel final
    {
      public:
        explicit ProjectSettingsPanel(IProjectSettingsController& controller) noexcept : m_Controller(controller) {}
        void Draw(Keire::UiFrame& ui) { m_Controller.DrawProjectSettings(ui); }

      private:
        IProjectSettingsController& m_Controller;
    };
} // namespace KeireEditor
