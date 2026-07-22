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
        void SetState(const Keire::Detail::EditorCameraState& state) { m_Camera.SetState(state); }
        [[nodiscard]] Keire::Matrix4 ViewMatrix() const noexcept { return m_Camera.ViewMatrix(); }
        [[nodiscard]] Keire::Matrix4 ProjectionMatrix(float aspect) const { return m_Camera.ProjectionMatrix(aspect); }
        [[nodiscard]] bool Update(const Keire::Detail::EditorCameraInput& input) { return m_Camera.Update(input); }
        void ToggleProjection() noexcept { m_Camera.ToggleProjection(); }
        void Snap(Keire::Detail::EditorCameraAxis axis) noexcept { m_Camera.Snap(axis); }
        void SetFocus(Keire::Vector3 focus) { m_Camera.SetFocus(focus); }
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
        [[nodiscard]] bool Capturing() const noexcept { return m_Capturing; }
        void SetCapturing(bool capturing) noexcept { m_Capturing = capturing; }
        void MarkDirty() noexcept { m_Dirty = true; }

        [[nodiscard]] bool Load(const std::filesystem::path& path) noexcept;
        [[nodiscard]] bool Save(const std::filesystem::path& path) noexcept;

      private:
        Keire::Detail::EditorCameraController m_Camera;
        std::vector<Keire::EntityId> m_LockedEntities;
        std::vector<Keire::EntityId> m_LastFocusShortcutSelection;
        double m_LastFocusShortcutSeconds = -1.0;
        bool m_Capturing = false;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
