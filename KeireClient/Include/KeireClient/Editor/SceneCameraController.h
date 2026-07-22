#pragma once

#include "Keire/Core.h"
#include "KeireInternal/EditorCameraController.h"

#include <filesystem>

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
        void SetState(const Keire::Detail::EditorCameraState& state) { m_Camera.SetState(state); }
        [[nodiscard]] Keire::Matrix4 ViewMatrix() const noexcept { return m_Camera.ViewMatrix(); }
        [[nodiscard]] Keire::Matrix4 ProjectionMatrix(float aspect) const { return m_Camera.ProjectionMatrix(aspect); }
        [[nodiscard]] bool Update(const Keire::Detail::EditorCameraInput& input) { return m_Camera.Update(input); }
        void ToggleProjection() noexcept { m_Camera.ToggleProjection(); }
        void Snap(Keire::Detail::EditorCameraAxis axis) noexcept { m_Camera.Snap(axis); }
        void SetFocus(Keire::Vector3 focus) { m_Camera.SetFocus(focus); }
        void Frame(Keire::Vector3 center, float radius) { m_Camera.Frame(center, radius); }
        [[nodiscard]] SceneFocusShortcutAction ApplyFocusShortcut(Keire::EntityId selection,
                                                                  Keire::TimeStep timestamp) noexcept;

        [[nodiscard]] Keire::EntityId LockedEntity() const noexcept { return m_LockedEntity; }
        void SetLockedEntity(Keire::EntityId entity) noexcept
        {
            m_LockedEntity = entity;
            m_Dirty = true;
        }
        [[nodiscard]] bool Capturing() const noexcept { return m_Capturing; }
        void SetCapturing(bool capturing) noexcept { m_Capturing = capturing; }
        void MarkDirty() noexcept { m_Dirty = true; }

        [[nodiscard]] bool Load(const std::filesystem::path& path) noexcept;
        [[nodiscard]] bool Save(const std::filesystem::path& path) noexcept;

      private:
        Keire::Detail::EditorCameraController m_Camera;
        Keire::EntityId m_LockedEntity;
        Keire::EntityId m_LastFocusShortcutEntity;
        double m_LastFocusShortcutSeconds = -1.0;
        bool m_Capturing = false;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
