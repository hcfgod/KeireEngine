#pragma once

#include "Keire/Core.h"
#include "KeireInternal/EditorCameraController.h"

#include <filesystem>
#include <span>
#include <vector>

namespace KeireEditor
{
    enum class SceneFocusShortcutAction : std::uint8_t
    {
        None,
        Frame,
        Lock
    };

    class SceneCameraController final
    {
      public:
        [[nodiscard]] const Keire::Detail::EditorCameraState& State() const noexcept { return m_Camera.State(); }
        void SetState(const Keire::Detail::EditorCameraState& state)
        {
            m_Camera.SetState(state);
            m_Dirty = true;
        }
        [[nodiscard]] Keire::Matrix4 ViewMatrix() const noexcept { return m_Camera.ViewMatrix(); }
        [[nodiscard]] Keire::Matrix4 ProjectionMatrix(float aspect) const;
        [[nodiscard]] Keire::RenderCamera RenderCamera(float aspect) const;
        [[nodiscard]] bool Update(const Keire::Detail::EditorCameraInput& input) { return m_Camera.Update(input); }
        void ToggleProjection() noexcept { m_Camera.ToggleProjection(); }
        void Snap(Keire::Detail::EditorCameraAxis axis) noexcept { m_Camera.Snap(axis); }
        void SetFocus(Keire::Vector3 focus) { m_Camera.SetFocus(focus); }
        void SetFocusPrecise(Keire::Vector3d focus) { m_Camera.SetFocusPrecise(focus); }
        void SetSurfaceNavigation(bool enabled)
        {
            m_Camera.SetSurfaceNavigation(enabled);
            m_Dirty = true;
        }

        void FollowSurfaceUp(Keire::Vector3 up)
        {
            m_Camera.FollowSurfaceUp(up);
            m_Dirty = m_Dirty || m_Camera.State().SurfaceNavigation;
        }

        void ReexpressReferenceFrame(Keire::Vector3d focusInTarget, const Keire::Detail::FrameRotation& sourceToTarget)
        {
            m_Camera.ReexpressReferenceFrame(focusInTarget, sourceToTarget);
            m_Dirty = true;
        }
        [[nodiscard]] Keire::Vector3d PreciseFocus() const noexcept { return m_Camera.State().Focus; }
        void FramePrecise(Keire::Vector3d center, float radius, float aspectRatio = 1.0F)
        {
            m_Camera.FramePrecise(center, radius, 60.0F, aspectRatio);
        }
        void FollowFramePrecise(Keire::Vector3d center, float radius, float aspectRatio, float deltaSeconds);
        void Frame(Keire::Vector3 center, float radius, float aspectRatio = 1.0F)
        {
            m_Camera.Frame(center, radius, 60.0F, aspectRatio);
        }
        void FollowFrame(Keire::Vector3 center, float radius, float aspectRatio, float deltaSeconds);
        [[nodiscard]] SceneFocusShortcutAction ApplyFocusShortcut(Keire::EntityId selection,
                                                                  Keire::TimeStep timestamp) noexcept;
        [[nodiscard]] SceneFocusShortcutAction ApplyFocusShortcut(std::span<const Keire::EntityId> selection,
                                                                  Keire::TimeStep timestamp) noexcept;

        [[nodiscard]] Keire::EntityId LockedEntity() const noexcept;
        [[nodiscard]] std::span<const Keire::EntityId> LockedEntities() const noexcept { return m_LockedEntities; }
        [[nodiscard]] bool LockedTo(std::span<const Keire::EntityId> selection) const noexcept;
        void SetLockedEntity(Keire::EntityId entity) noexcept;
        void SetLockedEntities(std::span<const Keire::EntityId> entities) noexcept;
        [[nodiscard]] bool Capturing() const noexcept
        {
            return m_NavigationMode != Keire::Detail::EditorCameraNavigationMode::None;
        }
        [[nodiscard]] Keire::Detail::EditorCameraNavigationMode NavigationMode() const noexcept
        {
            return m_NavigationMode;
        }
        void SetNavigationMode(const Keire::Detail::EditorCameraNavigationMode mode) noexcept
        {
            m_NavigationMode = mode;
        }
        void MarkDirty() noexcept { m_Dirty = true; }

        [[nodiscard]] bool Load(const std::filesystem::path& path) noexcept;
        [[nodiscard]] bool Save(const std::filesystem::path& path) noexcept;

      private:
        Keire::Detail::EditorCameraController m_Camera;
        std::vector<Keire::EntityId> m_LockedEntities;
        std::vector<Keire::EntityId> m_LastFocusShortcutSelection;
        double m_LastFocusShortcutSeconds = -1.0;
        Keire::Detail::EditorCameraNavigationMode m_NavigationMode = Keire::Detail::EditorCameraNavigationMode::None;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
