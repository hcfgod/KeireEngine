#include "KeireClient/Editor/SceneCameraController.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>

namespace KeireEditor
{
    Keire::RenderCamera SceneCameraController::RenderCamera(const float aspect) const
    {
        Keire::RenderCamera camera;
        camera.View = ViewMatrix();
        camera.Projection = ProjectionMatrix(aspect);
        return camera;
    }

    SceneFocusShortcutAction SceneCameraController::ApplyFocusShortcut(const Keire::EntityId selection,
                                                                       const Keire::TimeStep timestamp) noexcept
    {
        const std::array selectionSet{selection};
        return selection ? ApplyFocusShortcut(std::span<const Keire::EntityId>(selectionSet), timestamp)
                         : ApplyFocusShortcut(std::span<const Keire::EntityId>{}, timestamp);
    }

    SceneFocusShortcutAction SceneCameraController::ApplyFocusShortcut(const std::span<const Keire::EntityId> selection,
                                                                       const Keire::TimeStep timestamp) noexcept
    {
        constexpr double doublePressSeconds = 0.35;
        if (selection.empty())
        {
            m_LastFocusShortcutSelection.clear();
            m_LastFocusShortcutSeconds = -1.0;
            return SceneFocusShortcutAction::None;
        }
        const auto now = timestamp.Seconds();
        const bool doublePress = std::ranges::equal(selection, m_LastFocusShortcutSelection) &&
                                 m_LastFocusShortcutSeconds >= 0.0 && now >= m_LastFocusShortcutSeconds &&
                                 now - m_LastFocusShortcutSeconds <= doublePressSeconds;
        m_LastFocusShortcutSelection.assign(selection.begin(), selection.end());
        m_LastFocusShortcutSeconds = now;
        if (!doublePress)
            return SceneFocusShortcutAction::Frame;

        SetLockedEntities(selection);
        m_LastFocusShortcutSelection.clear();
        m_LastFocusShortcutSeconds = -1.0;
        return SceneFocusShortcutAction::Lock;
    }

    Keire::EntityId SceneCameraController::LockedEntity() const noexcept
    {
        return m_LockedEntities.empty() ? Keire::EntityId{} : m_LockedEntities.back();
    }

    bool SceneCameraController::LockedTo(const std::span<const Keire::EntityId> selection) const noexcept
    {
        return std::ranges::equal(selection, m_LockedEntities);
    }

    void SceneCameraController::SetLockedEntity(const Keire::EntityId entity) noexcept
    {
        if (entity)
            SetLockedEntities(std::span(&entity, 1));
        else
            SetLockedEntities({});
    }

    void SceneCameraController::SetLockedEntities(const std::span<const Keire::EntityId> entities) noexcept
    {
        m_LockedEntities.assign(entities.begin(), entities.end());
        m_Dirty = true;
    }

    void SceneCameraController::FollowFrame(const Keire::Vector3 center, const float radius, const float aspectRatio,
                                            const float deltaSeconds)
    {
        const auto original = m_Camera.State();
        m_Camera.Frame(center, radius, 60.0F, aspectRatio);
        const auto target = m_Camera.State();
        auto state = original;
        const float blend = 1.0F - std::exp(-10.0F * std::clamp(deltaSeconds, 0.0F, 0.25F));
        state.Focus.X += (target.Focus.X - state.Focus.X) * blend;
        state.Focus.Y += (target.Focus.Y - state.Focus.Y) * blend;
        state.Focus.Z += (target.Focus.Z - state.Focus.Z) * blend;
        state.Distance += (target.Distance - state.Distance) * blend;
        state.OrthographicSize += (target.OrthographicSize - state.OrthographicSize) * blend;
        m_Camera.SetState(state);
        m_Dirty = true;
    }

    bool SceneCameraController::Load(const std::filesystem::path& path) noexcept
    {
        try
        {
            std::ifstream input(path);
            std::uint32_t version = 0;
            Keire::Detail::EditorCameraState state;
            if (!(input >> version >> state.Focus.X >> state.Focus.Y >> state.Focus.Z >> state.YawDegrees >>
                  state.PitchDegrees >> state.Distance) ||
                (version < 1 || version > 3))
                return false;
            std::vector<Keire::EntityId> locked;
            if (version >= 2)
            {
                std::uint32_t projection = 0;
                if (!(input >> state.OrthographicSize >> state.MoveSpeed >> projection) || projection > 1)
                    return false;
                state.Projection = static_cast<Keire::Detail::EditorCameraProjection>(projection);
                if (version == 2)
                {
                    std::string lockedText;
                    if (!(input >> lockedText))
                        return false;
                    if (lockedText != "-")
                        locked.push_back(Keire::EntityId::Parse(lockedText));
                }
                else
                {
                    std::size_t count = 0;
                    if (!(input >> count) || count > 4096)
                        return false;
                    for (std::size_t index = 0; index < count; ++index)
                    {
                        std::string entity;
                        if (!(input >> entity))
                            return false;
                        locked.push_back(Keire::EntityId::Parse(entity));
                    }
                }
            }
            m_Camera.SetState(state);
            m_LockedEntities = std::move(locked);
            m_Dirty = false;
            return true;
        }
        catch (...)
        {
            m_LockedEntities.clear();
            return false;
        }
    }

    bool SceneCameraController::Save(const std::filesystem::path& path) noexcept
    {
        if (!m_Dirty)
            return true;
        try
        {
            std::ostringstream output;
            output.precision(9);
            const auto& state = m_Camera.State();
            output << "3\n"
                   << state.Focus.X << ' ' << state.Focus.Y << ' ' << state.Focus.Z << '\n'
                   << state.YawDegrees << ' ' << state.PitchDegrees << ' ' << state.Distance << '\n'
                   << state.OrthographicSize << ' ' << state.MoveSpeed << ' '
                   << static_cast<std::uint32_t>(state.Projection) << ' ' << m_LockedEntities.size() << '\n';
            for (const auto entity : m_LockedEntities)
                output << entity.ToString() << '\n';
            std::filesystem::create_directories(path.parent_path());
            Keire::Detail::WriteTextFileAtomically(path, output.str());
            m_Dirty = false;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace KeireEditor
