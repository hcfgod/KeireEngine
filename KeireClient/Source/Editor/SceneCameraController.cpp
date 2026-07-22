#include "KeireClient/Editor/SceneCameraController.h"

#include "KeireInternal/FileSystem.h"

#include <fstream>
#include <sstream>
#include <string>

namespace KeireEditor
{
    SceneFocusShortcutAction SceneCameraController::ApplyFocusShortcut(const Keire::EntityId selection,
                                                                       const Keire::TimeStep timestamp) noexcept
    {
        constexpr double doublePressSeconds = 0.35;
        if (!selection)
        {
            m_LastFocusShortcutEntity = {};
            m_LastFocusShortcutSeconds = -1.0;
            return SceneFocusShortcutAction::None;
        }
        const auto now = timestamp.Seconds();
        const bool doublePress = selection == m_LastFocusShortcutEntity && m_LastFocusShortcutSeconds >= 0.0 &&
                                 now >= m_LastFocusShortcutSeconds &&
                                 now - m_LastFocusShortcutSeconds <= doublePressSeconds;
        m_LastFocusShortcutEntity = selection;
        m_LastFocusShortcutSeconds = now;
        if (!doublePress)
            return SceneFocusShortcutAction::Frame;

        SetLockedEntity(selection);
        m_LastFocusShortcutEntity = {};
        m_LastFocusShortcutSeconds = -1.0;
        return SceneFocusShortcutAction::Lock;
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
                (version != 1 && version != 2))
                return false;
            Keire::EntityId locked;
            if (version == 2)
            {
                std::uint32_t projection = 0;
                std::string lockedText;
                if (!(input >> state.OrthographicSize >> state.MoveSpeed >> projection >> lockedText) || projection > 1)
                    return false;
                state.Projection = static_cast<Keire::Detail::EditorCameraProjection>(projection);
                if (lockedText != "-")
                    locked = Keire::EntityId::Parse(lockedText);
            }
            m_Camera.SetState(state);
            m_LockedEntity = locked;
            m_Dirty = false;
            return true;
        }
        catch (...)
        {
            m_LockedEntity = {};
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
            output << "2\n"
                   << state.Focus.X << ' ' << state.Focus.Y << ' ' << state.Focus.Z << '\n'
                   << state.YawDegrees << ' ' << state.PitchDegrees << ' ' << state.Distance << '\n'
                   << state.OrthographicSize << ' ' << state.MoveSpeed << ' '
                   << static_cast<std::uint32_t>(state.Projection) << ' '
                   << (m_LockedEntity ? m_LockedEntity.ToString() : "-") << '\n';
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
