#pragma once

#include "KeireInternal/Environment/EnvironmentClock.h"
#include "KeireInternal/Environment/EnvironmentLightingEvaluation.h"

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace KeireEditor
{
    // Workspace-owned transient state. No document, scene, undo history or runtime world is modified here.
    class EnvironmentPreviewClock final
    {
      public:
        EnvironmentPreviewClock() : m_Owner(std::this_thread::get_id()) {}

        EnvironmentPreviewClock(const EnvironmentPreviewClock&) = delete;
        EnvironmentPreviewClock& operator=(const EnvironmentPreviewClock&) = delete;

        void Bind(const Keire::Detail::EnvironmentClockState authored)
        {
            RequireOwner();
            auto proposed = m_Clock;
            proposed.Restore(authored);
            proposed.Configure(authored.SecondsPerDay, 1.0, false);
            m_Clock = proposed;
            m_SimulationTime = 0.0;
            m_Authored = authored;
            m_Bound = true;
            m_Playing = false;
        }

        void SynchronizeAuthored(const Keire::Detail::EnvironmentClockState authored)
        {
            RequireOwner();
            if (!m_Bound || authored != m_Authored)
            {
                Bind(authored);
            }
        }

        void Unbind()
        {
            RequireOwner();
            m_Bound = false;
            m_Playing = false;
            m_LightingSource = {};
            m_LightingProfile.reset();
        }

        [[nodiscard]] Keire::AssetId LightingSource() const
        {
            RequireOwner();
            return m_LightingSource;
        }

        [[nodiscard]] Keire::EnvironmentLightingSnapshot
        CaptureLighting(const Keire::Detail::EnvironmentLightingProfile& profile, const Keire::AssetId source)
        {
            RequireBound();
            const auto clockRevision = m_Clock.DiscontinuityRevision();
            const bool newSource = source != m_LightingSource;
            const bool profileChanged = !m_LightingProfile || *m_LightingProfile != profile;
            auto revision = newSource ? std::uint64_t{1} : m_LightingRevision;
            if (!newSource && (profileChanged || clockRevision != m_LightingClockRevision))
            {
                if (revision == std::numeric_limits<std::uint64_t>::max())
                {
                    throw std::overflow_error("Environment preview lighting revision exhausted.");
                }
                ++revision;
            }
            auto result = Keire::Detail::EvaluateEnvironmentLighting(profile, m_Clock.State(), source, revision);
            std::optional<Keire::Detail::EnvironmentLightingProfile> proposed;
            if (profileChanged)
            {
                proposed = profile;
            }
            if (proposed)
            {
                m_LightingProfile = std::move(proposed);
            }
            m_LightingSource = source;
            m_LightingRevision = revision;
            m_LightingClockRevision = clockRevision;
            return result;
        }

        [[nodiscard]] bool Bound() const
        {
            RequireOwner();
            return m_Bound;
        }

        [[nodiscard]] bool Playing() const
        {
            RequireBound();
            return m_Playing;
        }

        void SetPlaying(const bool playing)
        {
            RequireBound();
            m_Playing = playing;
        }

        void SetSpeed(const double speed)
        {
            RequireBound();
            m_Clock.Configure(m_Clock.State().SecondsPerDay, speed, false);
        }

        void Seek(const std::int64_t day, const double fraction)
        {
            RequireBound();
            // Calendar scrubbing changes lighting/weather phase, not the independent local simulation epoch.
            // A signed calendar day must never be flattened into absolute double seconds for frame rotation.
            m_Clock.Seek(day, fraction);
        }

        void Reset()
        {
            RequireBound();
            Bind(m_Authored);
        }

        // The host supplies only this frame's elapsed time. Suppressed frames are discarded, never caught up.
        void Tick(const double elapsedSeconds, const bool editMode, const bool previewVisible)
        {
            RequireBound();
            if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0)
            {
                throw std::invalid_argument("Environment preview delta must be finite and nonnegative.");
            }
            if (m_Playing && editMode && previewVisible)
            {
                const double proposedTime = m_SimulationTime + elapsedSeconds;
                if (!std::isfinite(proposedTime))
                {
                    throw std::overflow_error("Environment preview simulation time exhausted.");
                }
                auto proposedClock = m_Clock;
                proposedClock.Advance(elapsedSeconds);
                m_Clock = proposedClock;
                m_SimulationTime = proposedTime;
            }
        }

        [[nodiscard]] double SimulationTime() const
        {
            RequireBound();
            return m_SimulationTime;
        }

        [[nodiscard]] Keire::Detail::EnvironmentClockState State() const
        {
            RequireBound();
            return m_Clock.State();
        }

        [[nodiscard]] std::uint64_t Revision() const
        {
            RequireBound();
            return m_Clock.DiscontinuityRevision();
        }

        // The caller must explicitly apply this through document history. Preview speed/playback are not authored.
        [[nodiscard]] Keire::Detail::EnvironmentClockState ApplyTimeCandidate() const
        {
            RequireBound();
            auto result = m_Authored;
            result.Day = m_Clock.State().Day;
            result.DayFraction = m_Clock.State().DayFraction;
            return result;
        }

      private:
        void RequireOwner() const
        {
            if (std::this_thread::get_id() != m_Owner)
            {
                throw std::logic_error("Environment preview requires its owner thread.");
            }
        }

        void RequireBound() const
        {
            RequireOwner();
            if (!m_Bound)
            {
                throw std::logic_error("Environment preview has no authored source.");
            }
        }

        const std::thread::id m_Owner;
        Keire::Detail::EnvironmentClock m_Clock;
        Keire::Detail::EnvironmentClockState m_Authored;
        Keire::AssetId m_LightingSource;
        std::optional<Keire::Detail::EnvironmentLightingProfile> m_LightingProfile;
        std::uint64_t m_LightingRevision = 1;
        std::uint64_t m_LightingClockRevision = 0;
        bool m_Bound = false;
        bool m_Playing = false;
        double m_SimulationTime = 0.0;
    };
} // namespace KeireEditor
