#pragma once

#include "KeireClient/Editor/ScenePicker.h"

#include "Keire/Core.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace KeireEditor
{
    enum class SceneTool : std::uint8_t
    {
        View,
        Translate,
        Rotate,
        Scale
    };

    struct SceneToolSettings
    {
        Keire::Vector3 PositionSnap{0.5F, 0.5F, 0.5F};
        float RotationSnapDegrees = 15.0F;
        float ScaleSnap = 0.1F;
        bool Snapping = false;
        bool LocalSpace = true;
        bool ShowIcons = true;
        bool ShowCameraFrustums = true;
        bool ShowLightDirections = true;
    };

    class SceneGizmoController final
    {
      public:
        enum class Axis : std::uint8_t
        {
            None,
            X,
            Y,
            Z,
            Uniform
        };

        using BeginUndo = std::function<void(std::string_view)>;

        void DrawToolbar(Keire::UiFrame& ui);
        [[nodiscard]] Keire::EntityId UpdateAndDraw(Keire::UiFrame& ui, const Keire::Ref<Keire::Scene>& scene,
                                                    Keire::EntityId selected, const Keire::RenderCamera& camera,
                                                    Keire::UiItemRect viewport, bool allowManipulation,
                                                    BeginUndo beginUndo, MeshBoundsResolver resolveMeshBounds = {});

        void Load(const std::filesystem::path& projectRoot);
        void Save(const std::filesystem::path& projectRoot) const noexcept;

        [[nodiscard]] SceneTool ActiveTool() const noexcept { return m_Tool; }
        [[nodiscard]] const SceneToolSettings& Settings() const noexcept { return m_Settings; }

      private:
        struct DragState
        {
            Axis ActiveAxis = Axis::None;
            Keire::UiPosition StartPointer;
            Keire::Vector3 InitialPosition;
            Keire::Vector3 InitialEuler;
            Keire::Vector3 InitialScale{1.0F, 1.0F, 1.0F};
            Keire::Vector3 WorldAxis;
            Keire::UiPosition ScreenAxis;
            float ScreenLength = 1.0F;
            float WorldLength = 1.0F;
            bool UndoRecorded = false;
        };

        SceneToolSettings m_Settings;
        DragState m_Drag;
        SceneTool m_Tool = SceneTool::Translate;
    };
} // namespace KeireEditor
