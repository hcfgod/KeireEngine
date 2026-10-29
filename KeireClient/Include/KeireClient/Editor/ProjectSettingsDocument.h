#pragma once

#include "Keire/Core.h"
#include "KeireInternal/Environment/EnvironmentClock.h"
#include "KeireInternal/Environment/EnvironmentLightingProfile.h"
#include "KeireInternal/Spatial/PlanetProjectSettings.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    class ProjectSettingsDocument final
    {
      public:
        ~ProjectSettingsDocument();
        void Open(std::filesystem::path projectRoot, Keire::RenderEnvironmentSettings settings,
                  Keire::Ref<Keire::UndoContext> undo = {});
        void Open(std::filesystem::path projectRoot, Keire::RenderEnvironmentSettings settings,
                  Keire::ProjectAuthoringSettings authoringSettings, Keire::Ref<Keire::UndoContext> undo = {});
        void Close() noexcept;

        [[nodiscard]] const Keire::RenderEnvironmentSettings& Settings() const noexcept { return m_Settings; }
        [[nodiscard]] const Keire::ProjectAuthoringSettings& AuthoringSettings() const noexcept
        {
            return m_AuthoringSettings;
        }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }

        [[nodiscard]] Keire::Detail::EnvironmentClockState EnvironmentTime() const noexcept
        {
            return m_EnvironmentTime;
        }

        [[nodiscard]] bool Opened() const noexcept { return !m_ProjectRoot.empty(); }
        [[nodiscard]] const Keire::Detail::EnvironmentLightingProfile& EnvironmentLighting() const noexcept
        {
            return m_EnvironmentLighting;
        }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Undo; }

        [[nodiscard]] const Keire::Detail::PlanetProjectSettings& Planets() const noexcept { return m_Planets; }

        void Update(Keire::RenderEnvironmentSettings settings);
        void UpdateAuthoring(Keire::ProjectAuthoringSettings settings);
        void UpdateEnvironmentTime(Keire::Detail::EnvironmentClockState settings);
        void UpdateEnvironmentLighting(Keire::Detail::EnvironmentLightingProfile settings);
        void UpdatePlanets(Keire::Detail::PlanetProjectSettings settings);
        void CommitEdit(std::string_view name = "Edit Project Settings");
        void CancelEdit() noexcept;
        void Reset();
        void ResetAuthoring();
        void ResetEnvironmentTime();
        void ResetEnvironmentLighting();
        void ResetPlanets();
        void Save();

      private:
        using FileBaseline = std::array<std::optional<std::vector<std::byte>>, 5>;

        void OpenImpl(std::filesystem::path projectRoot, Keire::RenderEnvironmentSettings settings,
                      std::optional<Keire::ProjectAuthoringSettings> authoringSettings,
                      Keire::Ref<Keire::UndoContext> undo);
        [[nodiscard]] static FileBaseline CaptureFiles(const std::filesystem::path& projectRoot);

        struct Snapshot
        {
            Keire::RenderEnvironmentSettings Rendering;
            Keire::ProjectAuthoringSettings Authoring;
            Keire::Detail::EnvironmentClockState EnvironmentTime;
            Keire::Detail::EnvironmentLightingProfile EnvironmentLighting;
            Keire::Detail::PlanetProjectSettings Planets;

            [[nodiscard]] bool operator==(const Snapshot&) const = default;
        };

        [[nodiscard]] Snapshot Current() const
        {
            return {m_Settings, m_AuthoringSettings, m_EnvironmentTime, m_EnvironmentLighting, m_Planets};
        }
        void Assign(Snapshot settings) noexcept;
        void RefreshDirty() noexcept;

        std::filesystem::path m_ProjectRoot;
        Keire::RenderEnvironmentSettings m_Settings;
        Keire::ProjectAuthoringSettings m_AuthoringSettings = Keire::DefaultProjectAuthoringSettings();
        Keire::Detail::EnvironmentClockState m_EnvironmentTime;
        Keire::Detail::EnvironmentLightingProfile m_EnvironmentLighting;
        Keire::Detail::PlanetProjectSettings m_Planets;
        std::optional<Snapshot> m_EditBaseline;
        std::optional<Snapshot> m_SavedState;
        FileBaseline m_FileBaseline;
        Keire::Ref<Keire::UndoContext> m_Undo;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
