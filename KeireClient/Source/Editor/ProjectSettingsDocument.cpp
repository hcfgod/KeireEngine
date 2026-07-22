#include "KeireClient/Editor/ProjectSettingsDocument.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        void Validate(const Keire::RenderEnvironmentSettings& settings)
        {
            const auto finite = [](const float value) { return std::isfinite(value); };
            if (!finite(settings.AmbientColor.Red) || !finite(settings.AmbientColor.Green) ||
                !finite(settings.AmbientColor.Blue) || !finite(settings.AmbientColor.Alpha) ||
                !finite(settings.AmbientIntensity) || !finite(settings.Exposure))
                throw std::invalid_argument("Project rendering settings must be finite.");
            const auto validColor = [](const float value) { return value >= 0.0F && value <= 1.0F; };
            if (!validColor(settings.AmbientColor.Red) || !validColor(settings.AmbientColor.Green) ||
                !validColor(settings.AmbientColor.Blue) || !validColor(settings.AmbientColor.Alpha))
                throw std::invalid_argument("Ambient color channels must be between zero and one.");
            if (settings.AmbientIntensity < 0.0F || settings.AmbientIntensity > 16.0F)
                throw std::invalid_argument("Ambient intensity must be between zero and sixteen.");
            if (settings.Exposure < 0.01F || settings.Exposure > 16.0F)
                throw std::invalid_argument("Exposure must be between 0.01 and 16.0.");
        }
    } // namespace

    ProjectSettingsDocument::~ProjectSettingsDocument() { Close(); }

    void ProjectSettingsDocument::Open(std::filesystem::path projectRoot,
                                       const Keire::RenderEnvironmentSettings settings,
                                       Keire::Ref<Keire::UndoContext> undo)
    {
        if (projectRoot.empty())
            throw std::invalid_argument("ProjectSettingsDocument requires a project root.");
        Validate(settings);
        Close();
        m_ProjectRoot = std::move(projectRoot);
        m_Settings = settings;
        m_Undo = std::move(undo);
    }

    void ProjectSettingsDocument::Close() noexcept
    {
        m_ProjectRoot.clear();
        m_Settings = {};
        m_EditBaseline.reset();
        if (m_Undo && m_Undo->IsOpen())
            m_Undo->Close();
        m_Undo.Reset();
        m_Dirty = false;
    }

    void ProjectSettingsDocument::Update(const Keire::RenderEnvironmentSettings settings)
    {
        if (!Opened())
            throw std::logic_error("ProjectSettingsDocument is closed.");
        Validate(settings);
        if (settings == m_Settings)
            return;
        if (!m_EditBaseline)
            m_EditBaseline = m_Settings;
        m_Settings = settings;
        m_Dirty = true;
    }

    void ProjectSettingsDocument::CommitEdit(const std::string_view name)
    {
        if (!m_EditBaseline)
            return;
        const auto before = *m_EditBaseline;
        const auto after = m_Settings;
        m_EditBaseline.reset();
        if (before == after || !m_Undo || !m_Undo->IsOpen())
            return;
        m_Undo->RecordApplied(Keire::CreateUndoCommand(
            std::string(name), [this, after] { Assign(after); }, [this, before] { Assign(before); }, sizeof(before),
            [this] { return Opened(); }));
    }

    void ProjectSettingsDocument::CancelEdit() noexcept
    {
        if (!m_EditBaseline)
            return;
        m_Settings = *m_EditBaseline;
        m_EditBaseline.reset();
    }

    void ProjectSettingsDocument::Reset()
    {
        Update({});
        CommitEdit("Reset Project Settings");
    }

    void ProjectSettingsDocument::Save()
    {
        if (!Opened())
            throw std::logic_error("ProjectSettingsDocument is closed.");
        CommitEdit();
        Keire::SaveRenderEnvironmentSettings(m_ProjectRoot, m_Settings);
        m_Dirty = false;
    }

    void ProjectSettingsDocument::Assign(const Keire::RenderEnvironmentSettings& settings) noexcept
    {
        m_Settings = settings;
        m_EditBaseline.reset();
        m_Dirty = true;
    }
} // namespace KeireEditor
