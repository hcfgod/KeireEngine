#pragma once

#include "KeireClient/Editor/ScenePicker.h"

#include "Keire/Core.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    enum class SceneTool : std::uint8_t
    {
        View,
        Translate,
        Rotate,
        Scale
    };

    enum class SceneTransformAxis : std::uint8_t
    {
        X,
        Y,
        Z,
        Uniform
    };

    struct SceneTransformTarget
    {
        Keire::Ref<Keire::TransformComponent> Transform;
        Keire::Matrix4 InitialWorld;
        Keire::Vector3 InitialPosition;
        Keire::Quaternion InitialRotation;
        Keire::Vector3 InitialScale{1.0F, 1.0F, 1.0F};
    };

    class SceneTransformGroup final
    {
      public:
        [[nodiscard]] static std::vector<SceneTransformTarget> Capture(const Keire::Ref<Keire::Scene>& scene,
                                                                       std::span<const Keire::AssetId> selections,
                                                                       Keire::EntityId primary);
        static void Restore(std::span<const SceneTransformTarget> targets);
        static void Apply(std::span<const SceneTransformTarget> targets, SceneTool tool, SceneTransformAxis axis,
                          float amount, Keire::Vector3 worldAxis, Keire::Vector3 pivot,
                          Keire::Quaternion pivotRotation);
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
        bool ShowPhysicsGizmos = true;
        bool EditColliders = false;
        Keire::GpuOcclusionDebugView OcclusionDebugView = Keire::GpuOcclusionDebugView::None;
        std::uint32_t OcclusionDebugMip = 0;
        bool ShowOcclusionMetadata = false;
    };

    struct SceneGizmoResult
    {
        Keire::EntityId Selection;
        bool SelectionActivated = false;
        bool PointerConsumed = false;
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

        [[nodiscard]] Keire::UiItemRect DrawOverlayToolbar(Keire::UiFrame& ui, Keire::UiItemRect viewport,
                                                           std::uint32_t occlusionPyramidMipCount = 0);
        [[nodiscard]] SceneGizmoResult UpdateAndDraw(Keire::UiFrame& ui, const Keire::Ref<Keire::Scene>& scene,
                                                     Keire::EntityId selected, const Keire::RenderCamera& camera,
                                                     Keire::UiItemRect viewport, bool allowManipulation,
                                                     bool pointerBlocked, const BeginUndo& beginUndo,
                                                     const MeshBoundsResolver& resolveMeshBounds = {},
                                                     std::span<const Keire::AssetId> selections = {});

        void Load(const std::filesystem::path& projectRoot);
        void Save(const std::filesystem::path& projectRoot) const noexcept;
        [[nodiscard]] bool ApplyToolShortcut(Keire::UiKey key) noexcept;
        void SetSnapping(bool enabled) noexcept { m_Settings.Snapping = enabled; }
        void SetShowCameraFrustums(bool enabled) noexcept { m_Settings.ShowCameraFrustums = enabled; }
        void SetShowLightDirections(bool enabled) noexcept { m_Settings.ShowLightDirections = enabled; }
        void SetShowPhysicsGizmos(bool enabled) noexcept { m_Settings.ShowPhysicsGizmos = enabled; }
        void SetColliderEditing(bool enabled) noexcept { m_Settings.EditColliders = enabled; }
        void SetOcclusionDebugView(Keire::GpuOcclusionDebugView view) noexcept
        {
            m_Settings.OcclusionDebugView = view;
            if (view != Keire::GpuOcclusionDebugView::HierarchicalDepth)
                m_Settings.OcclusionDebugMip = 0;
        }
        void SetOcclusionDebugMip(std::uint32_t mip) noexcept { m_Settings.OcclusionDebugMip = mip; }
        void ClampOcclusionDebugMip(std::uint32_t availableMipCount) noexcept
        {
            if (m_Settings.OcclusionDebugView != Keire::GpuOcclusionDebugView::HierarchicalDepth ||
                availableMipCount == 0U)
                m_Settings.OcclusionDebugMip = 0;
            else
                m_Settings.OcclusionDebugMip = std::min(m_Settings.OcclusionDebugMip, availableMipCount - 1U);
        }
        void SetShowOcclusionMetadata(bool enabled) noexcept { m_Settings.ShowOcclusionMetadata = enabled; }

        [[nodiscard]] SceneTool ActiveTool() const noexcept { return m_Tool; }
        [[nodiscard]] const SceneToolSettings& Settings() const noexcept { return m_Settings; }

      private:
        struct DragState
        {
            Axis ActiveAxis = Axis::None;
            Keire::UiPosition StartPointer;
            Keire::Vector3 WorldAxis;
            Keire::Vector3 Pivot;
            Keire::Quaternion PivotRotation;
            Keire::UiPosition ScreenAxis;
            float ScreenLength = 1.0F;
            float WorldLength = 1.0F;
            bool UndoRecorded = false;
            std::vector<SceneTransformTarget> Targets;
        };

        enum class ColliderHandle : std::uint8_t
        {
            None,
            BoxX,
            BoxY,
            BoxZ,
            SphereRadius,
            CapsuleRadius,
            CapsuleHeight
        };

        struct ColliderDragState
        {
            Keire::Ref<Keire::ColliderComponent> Collider;
            ColliderHandle Handle = ColliderHandle::None;
            Keire::UiPosition StartPointer;
            Keire::UiPosition ScreenAxis;
            float ScreenPixelsPerUnit = 1.0F;
            Keire::Vector3 InitialCenter;
            Keire::Vector3 InitialHalfExtent{0.5F, 0.5F, 0.5F};
            float InitialRadius = 0.5F;
            float InitialHeight = 1.0F;
            bool UndoRecorded = false;
        };

        SceneToolSettings m_Settings;
        DragState m_Drag;
        ColliderDragState m_ColliderDrag;
        SceneTool m_Tool = SceneTool::Translate;
    };
} // namespace KeireEditor
