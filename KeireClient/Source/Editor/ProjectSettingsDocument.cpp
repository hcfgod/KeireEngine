#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireInternal/Environment/EnvironmentLightingSettingsStore.h"
#include "KeireInternal/Environment/EnvironmentTimeSettingsStore.h"
#include "KeireInternal/ProjectFileTransaction.h"
#include "KeireInternal/ProjectSettingsSerialization.h"

#include <array>
#include <cmath>
#include <exception>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        constexpr std::size_t MaximumOriginalSettingsBytes = 1024U * 1024U;
        constexpr std::array SettingsNames{"Rendering.keiresettings", "Authoring.keiresettings",
                                           "EnvironmentTime.keiresettings", "EnvironmentLighting.keiresettings"};

        void Validate(const Keire::RenderEnvironmentSettings& settings)
        {
            Keire::ValidateRenderEnvironmentSettings(settings);
            const auto finite = [](const float value) { return std::isfinite(value); };
            if (!finite(settings.AmbientColor.Red) || !finite(settings.AmbientColor.Green) ||
                !finite(settings.AmbientColor.Blue) || !finite(settings.AmbientColor.Alpha) ||
                !finite(settings.AmbientIntensity) || !finite(settings.Exposure) ||
                !finite(settings.EnvironmentRotationDegrees) || !finite(settings.EnvironmentDiffuseIntensity) ||
                !finite(settings.EnvironmentSpecularIntensity) || !finite(settings.DirectionalShadowDistance) ||
                !finite(settings.DirectionalShadowSplitLambda))
                throw std::invalid_argument("Project rendering settings must be finite.");
            const auto validColor = [](const float value) { return value >= 0.0F && value <= 1.0F; };
            if (!validColor(settings.AmbientColor.Red) || !validColor(settings.AmbientColor.Green) ||
                !validColor(settings.AmbientColor.Blue) || !validColor(settings.AmbientColor.Alpha))
                throw std::invalid_argument("Ambient color channels must be between zero and one.");
            if (settings.AmbientIntensity < 0.0F || settings.AmbientIntensity > 16.0F)
                throw std::invalid_argument("Ambient intensity must be between zero and sixteen.");
            if (settings.Exposure < 0.01F || settings.Exposure > 16.0F)
                throw std::invalid_argument("Exposure must be between 0.01 and 16.0.");
            if (settings.EnvironmentDiffuseIntensity < 0.0F || settings.EnvironmentDiffuseIntensity > 16.0F ||
                settings.EnvironmentSpecularIntensity < 0.0F || settings.EnvironmentSpecularIntensity > 16.0F)
                throw std::invalid_argument("Environment intensities must be between zero and sixteen.");
            if (settings.DirectionalShadowDistance <= 0.0F || settings.DirectionalShadowDistance > 100'000.0F ||
                settings.DirectionalShadowCascadeCount < 1U || settings.DirectionalShadowCascadeCount > 4U ||
                settings.DirectionalShadowResolution < 256U || settings.DirectionalShadowResolution > 8192U ||
                (settings.DirectionalShadowResolution & (settings.DirectionalShadowResolution - 1U)) != 0U ||
                settings.DirectionalShadowSplitLambda < 0.0F || settings.DirectionalShadowSplitLambda > 1.0F)
                throw std::invalid_argument("Directional shadow settings are outside supported production limits.");
        }

        void RequireRecoveredSettings(const std::filesystem::path& root)
        {
            if (!root.empty() && std::filesystem::exists(root) && Keire::Detail::HasPendingProjectSettingsSave(root))
            {
                throw std::runtime_error("Reopen the project exclusively to recover its interrupted settings save.");
            }
        }
    } // namespace

    ProjectSettingsDocument::~ProjectSettingsDocument() { Close(); }

    void ProjectSettingsDocument::Open(std::filesystem::path projectRoot,
                                       const Keire::RenderEnvironmentSettings settings,
                                       Keire::Ref<Keire::UndoContext> undo)
    {
        OpenImpl(std::move(projectRoot), settings, std::nullopt, std::move(undo));
    }

    void ProjectSettingsDocument::Open(std::filesystem::path projectRoot,
                                       const Keire::RenderEnvironmentSettings settings,
                                       Keire::ProjectAuthoringSettings authoringSettings,
                                       Keire::Ref<Keire::UndoContext> undo)
    {
        OpenImpl(std::move(projectRoot), settings, std::move(authoringSettings), std::move(undo));
    }

    ProjectSettingsDocument::FileBaseline
    ProjectSettingsDocument::CaptureFiles(const std::filesystem::path& projectRoot)
    {
        FileBaseline baseline;
        if (!std::filesystem::exists(projectRoot))
        {
            return baseline;
        }
        const Keire::Detail::AnchoredFileSystem files(projectRoot);
        for (std::size_t index = 0; index < SettingsNames.size(); ++index)
        {
            const auto destination = std::filesystem::path("ProjectSettings") / SettingsNames[index];
            if (files.Exists(destination))
            {
                baseline[index] = files.Read(destination, MaximumOriginalSettingsBytes);
            }
        }
        return baseline;
    }

    void ProjectSettingsDocument::OpenImpl(std::filesystem::path projectRoot,
                                           const Keire::RenderEnvironmentSettings settings,
                                           std::optional<Keire::ProjectAuthoringSettings> suppliedAuthoring,
                                           Keire::Ref<Keire::UndoContext> undo)
    {
        if (projectRoot.empty())
            throw std::invalid_argument("ProjectSettingsDocument requires a project root.");
        RequireRecoveredSettings(projectRoot);
        auto baseline = CaptureFiles(projectRoot);
        auto authoringSettings =
            suppliedAuthoring ? std::move(*suppliedAuthoring) : Keire::LoadProjectAuthoringSettings(projectRoot);
        Validate(settings);
        Keire::ValidateProjectAuthoringSettings(authoringSettings);
        const auto environmentTime = Keire::Detail::EnvironmentTimeSettingsStore::Load(projectRoot);
        auto environmentLighting = Keire::Detail::EnvironmentLightingSettingsStore::Load(projectRoot);
        auto saved = Snapshot{settings, authoringSettings, environmentTime, environmentLighting};
        if (CaptureFiles(projectRoot) != baseline)
        {
            throw std::runtime_error("Project settings changed while opening. Retry opening the project.");
        }
        Close();
        m_ProjectRoot = std::move(projectRoot);
        m_Settings = settings;
        m_AuthoringSettings = std::move(authoringSettings);
        m_EnvironmentTime = environmentTime;
        m_EnvironmentLighting = std::move(environmentLighting);
        m_Undo = std::move(undo);
        m_SavedState = std::move(saved);
        m_FileBaseline = std::move(baseline);
    }

    void ProjectSettingsDocument::Close() noexcept
    {
        m_ProjectRoot.clear();
        m_Settings = {};
        m_AuthoringSettings = Keire::DefaultProjectAuthoringSettings();
        m_EnvironmentTime = {};
        m_EnvironmentLighting = Keire::Detail::EnvironmentLightingProfile{};
        m_EditBaseline.reset();
        m_SavedState.reset();
        m_FileBaseline = {};
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
            m_EditBaseline = Current();
        m_Settings = settings;
        RefreshDirty();
    }

    void ProjectSettingsDocument::UpdateAuthoring(Keire::ProjectAuthoringSettings settings)
    {
        if (!Opened())
            throw std::logic_error("ProjectSettingsDocument is closed.");
        Keire::ValidateProjectAuthoringSettings(settings);
        if (settings == m_AuthoringSettings)
            return;
        if (!m_EditBaseline)
            m_EditBaseline = Current();
        m_AuthoringSettings = std::move(settings);
        RefreshDirty();
    }

    void ProjectSettingsDocument::UpdateEnvironmentTime(const Keire::Detail::EnvironmentClockState settings)
    {
        if (!Opened())
        {
            throw std::logic_error("ProjectSettingsDocument is closed.");
        }
        (void)Keire::Detail::EnvironmentClock(settings);
        if (settings == m_EnvironmentTime)
        {
            return;
        }
        if (!m_EditBaseline)
        {
            m_EditBaseline = Current();
        }
        m_EnvironmentTime = settings;
        RefreshDirty();
    }

    void ProjectSettingsDocument::UpdateEnvironmentLighting(Keire::Detail::EnvironmentLightingProfile settings)
    {
        if (!Opened())
        {
            throw std::logic_error("ProjectSettingsDocument is closed.");
        }
        if (settings == m_EnvironmentLighting)
        {
            return;
        }
        if (!m_EditBaseline)
        {
            m_EditBaseline = Current();
        }
        m_EnvironmentLighting = std::move(settings);
        RefreshDirty();
    }

    void ProjectSettingsDocument::CommitEdit(const std::string_view name)
    {
        if (!m_EditBaseline)
            return;
        const auto before = *m_EditBaseline;
        const auto after = Current();
        if (before == after || !m_Undo || !m_Undo->IsOpen())
        {
            m_EditBaseline.reset();
            return;
        }
        m_Undo->RecordApplied(Keire::CreateUndoCommand(
            std::string(name), [this, after] { Assign(after); }, [this, before] { Assign(before); }, sizeof(before),
            [this] { return Opened(); }));
        m_EditBaseline.reset();
    }

    void ProjectSettingsDocument::CancelEdit() noexcept
    {
        if (!m_EditBaseline)
            return;
        Assign(std::move(*m_EditBaseline));
    }

    void ProjectSettingsDocument::Reset()
    {
        Update({});
        CommitEdit("Reset Project Settings");
    }

    void ProjectSettingsDocument::ResetAuthoring()
    {
        UpdateAuthoring(Keire::DefaultProjectAuthoringSettings());
        CommitEdit("Reset Authoring Settings");
    }

    void ProjectSettingsDocument::ResetEnvironmentTime()
    {
        UpdateEnvironmentTime({});
        CommitEdit("Reset Environment Time");
    }

    void ProjectSettingsDocument::ResetEnvironmentLighting()
    {
        UpdateEnvironmentLighting(Keire::Detail::EnvironmentLightingProfile{});
        CommitEdit("Reset Environment Lighting");
    }

    void ProjectSettingsDocument::Save()
    {
        if (!Opened())
            throw std::logic_error("ProjectSettingsDocument is closed.");
        CommitEdit();
        auto saved = Current();
        const std::array contents{Keire::Detail::SerializeRenderEnvironmentSettings(m_Settings),
                                  Keire::Detail::SerializeProjectAuthoringSettings(m_AuthoringSettings),
                                  Keire::Detail::EnvironmentTimeSettingsStore::Serialize(m_EnvironmentTime),
                                  Keire::Detail::EnvironmentLightingSettingsStore::Serialize(m_EnvironmentLighting)};
        std::filesystem::create_directories(m_ProjectRoot);
        // Recover an interrupted publication before comparing files with the last accepted baseline.
        // The document keeps the user's pending values while recovery restores the on-disk baseline.
        if (Keire::Detail::HasPendingProjectSettingsSave(m_ProjectRoot))
        {
            (void)Keire::Detail::RecoverProjectSettingsFiles(m_ProjectRoot);
        }
        const Keire::Detail::AnchoredFileSystem files(m_ProjectRoot);
        std::array<Keire::Detail::ProjectFileReplacement, SettingsNames.size()> replacements;
        FileBaseline nextBaseline;
        for (std::size_t index = 0; index < SettingsNames.size(); ++index)
        {
            try
            {
                auto& replacement = replacements[index];
                replacement.Destination = std::filesystem::path("ProjectSettings") / SettingsNames[index];
                if (files.Exists(replacement.Destination))
                {
                    replacement.ExpectedOriginal = files.Read(replacement.Destination, MaximumOriginalSettingsBytes);
                }
                if (replacement.ExpectedOriginal != m_FileBaseline[index])
                {
                    throw std::runtime_error(
                        "The file was changed, added, or removed externally since opening or the last successful save. "
                        "Preserve pending edits and reopen the project to review the external changes.");
                }
                const auto bytes = std::as_bytes(std::span(contents[index].data(), contents[index].size()));
                replacement.Contents.assign(bytes.begin(), bytes.end());
                nextBaseline[index] = replacement.Contents;
            }
            catch (const std::exception& error)
            {
                std::throw_with_nested(std::runtime_error(
                    "Could not save ProjectSettings/" + std::string(SettingsNames[index]) + ": " + error.what() +
                    " No settings files were published. Pending edits are retained; "
                    "resolve the file error and retry Save Settings before closing the project."));
            }
        }
        try
        {
            Keire::Detail::PublishProjectSettingsFiles(m_ProjectRoot, replacements);
        }
        catch (const std::exception& error)
        {
            std::throw_with_nested(std::runtime_error(
                std::string("Could not save Project Settings: ") + error.what() +
                " Pending edits are retained. Resolve the file error and retry Save Settings; "
                "if recovery is required, preserve your pending values before reopening the project exclusively."));
        }
        m_SavedState = std::move(saved);
        static_assert(std::is_nothrow_move_assignable_v<FileBaseline>);
        m_FileBaseline = std::move(nextBaseline);
        m_Dirty = false;
    }

    void ProjectSettingsDocument::Assign(Snapshot settings) noexcept
    {
        // Any snapshot copy completes at the call boundary before publishing document state.
        // Cancel moves its owned baseline, so its noexcept path performs no allocating copies.
        static_assert(std::is_nothrow_move_constructible_v<Snapshot>);
        static_assert(std::is_nothrow_move_assignable_v<Keire::ProjectAuthoringSettings>);
        static_assert(std::is_nothrow_move_assignable_v<Keire::Detail::EnvironmentLightingProfile>);
        m_Settings = settings.Rendering;
        m_AuthoringSettings = std::move(settings.Authoring);
        m_EnvironmentTime = settings.EnvironmentTime;
        m_EnvironmentLighting = std::move(settings.EnvironmentLighting);
        m_EditBaseline.reset();
        RefreshDirty();
    }

    void ProjectSettingsDocument::RefreshDirty() noexcept
    {
        m_Dirty = !m_SavedState || m_Settings != m_SavedState->Rendering ||
                  m_AuthoringSettings != m_SavedState->Authoring ||
                  m_EnvironmentTime != m_SavedState->EnvironmentTime ||
                  m_EnvironmentLighting != m_SavedState->EnvironmentLighting;
    }
} // namespace KeireEditor
