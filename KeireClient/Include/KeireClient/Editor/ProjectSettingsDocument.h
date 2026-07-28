#pragma once

#include "Keire/Core.h"

#include <filesystem>
#include <optional>
#include <string_view>

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
        [[nodiscard]] bool Opened() const noexcept { return !m_ProjectRoot.empty(); }

        void Update(Keire::RenderEnvironmentSettings settings);
        void UpdateAuthoring(Keire::ProjectAuthoringSettings settings);
        void CommitEdit(std::string_view name = "Edit Project Settings");
        void CancelEdit() noexcept;
        void Reset();
        void ResetAuthoring();
        void Save();

      private:
        struct Snapshot
        {
            Keire::RenderEnvironmentSettings Rendering;
            Keire::ProjectAuthoringSettings Authoring;

            [[nodiscard]] bool operator==(const Snapshot&) const = default;
        };

        [[nodiscard]] Snapshot Current() const { return {m_Settings, m_AuthoringSettings}; }
        void Assign(const Snapshot& settings) noexcept;

        std::filesystem::path m_ProjectRoot;
        Keire::RenderEnvironmentSettings m_Settings;
        Keire::ProjectAuthoringSettings m_AuthoringSettings = Keire::DefaultProjectAuthoringSettings();
        std::optional<Snapshot> m_EditBaseline;
        Keire::Ref<Keire::UndoContext> m_Undo;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
