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
        void Close() noexcept;

        [[nodiscard]] const Keire::RenderEnvironmentSettings& Settings() const noexcept { return m_Settings; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }
        [[nodiscard]] bool Opened() const noexcept { return !m_ProjectRoot.empty(); }

        void Update(Keire::RenderEnvironmentSettings settings);
        void CommitEdit(std::string_view name = "Edit Project Settings");
        void CancelEdit() noexcept;
        void Reset();
        void Save();

      private:
        void Assign(const Keire::RenderEnvironmentSettings& settings) noexcept;

        std::filesystem::path m_ProjectRoot;
        Keire::RenderEnvironmentSettings m_Settings;
        std::optional<Keire::RenderEnvironmentSettings> m_EditBaseline;
        Keire::Ref<Keire::UndoContext> m_Undo;
        bool m_Dirty = false;
    };
} // namespace KeireEditor
