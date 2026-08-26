#include "KeireInternal/Diagnostics/DiagnosticBundleUiInternal.h"

#include "Keire/Ui.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire::Internal
{
    namespace
    {
        [[nodiscard]] std::string SizeLabel(const std::uint64_t bytes)
        {
            constexpr std::uint64_t Kibibyte = 1024U;
            constexpr std::uint64_t Mebibyte = Kibibyte * 1024U;
            if (bytes >= Mebibyte)
                return std::to_string(bytes) + " B (" + std::to_string(bytes / Mebibyte) + " MiB)";
            if (bytes >= Kibibyte)
                return std::to_string(bytes) + " B (" + std::to_string(bytes / Kibibyte) + " KiB)";
            return std::to_string(bytes) + " B";
        }

        [[nodiscard]] bool SameSelection(const DiagnosticBundleSelection& first,
                                         const DiagnosticBundleSelection& second) noexcept
        {
            return first.IncludeLogs == second.IncludeLogs &&
                   first.IncludeProjectMetadata == second.IncludeProjectMetadata &&
                   first.IncludePackageVersions == second.IncludePackageVersions &&
                   first.IncludeCrashInformation == second.IncludeCrashInformation;
        }
    } // namespace

    void DiagnosticBundleDialogController::Open(DiagnosticBundleRequest request, std::string defaultName)
    {
        m_SaveDialog.Reset();
        m_Request = std::move(request);
        m_DefaultName = std::move(defaultName);
        m_Status.clear();
        m_Error.clear();
        m_Visible = true;
        m_OpenRequested = true;
        Rebuild();
    }

    void DiagnosticBundleDialogController::Draw(UiFrame& ui, WindowSystem& windows, const WindowId parent)
    {
        PollSaveDialog();
        if (std::exchange(m_OpenRequested, false))
            ui.OpenPopup("Collect Diagnostics");
        if (!m_Visible)
            return;

        ui.SetNextWindowSize({920.0F, 660.0F});
        if (auto dialog = ui.BeginPopupModal("Collect Diagnostics"); dialog)
        {
            ui.Text("Collect Diagnostics");
            ui.TextWrapped("Review the exact frozen archive inventory below. Diagnostics stay local until you save "
                           "and manually share the ZIP. Project names, paths, assets, documents, private package "
                           "contents, source URLs, credentials, environment data, and native dumps are excluded.");
            ui.Separator();

            auto selection = m_Request.Selection;
            bool changed = false;
            changed |= ui.Checkbox("Logs", selection.IncludeLogs);
            ui.SameLine();
            changed |= ui.Checkbox("Project metadata", selection.IncludeProjectMetadata);
            ui.SameLine();
            changed |= ui.Checkbox("Package versions", selection.IncludePackageVersions);
            ui.SameLine();
            changed |= ui.Checkbox("Crash information", selection.IncludeCrashInformation);
            if (changed)
                SetSelection(selection);

            if (!m_Error.empty())
                ui.TextColoredWrapped({0.94F, 0.30F, 0.30F, 1.0F}, m_Error);
            else if (!m_Status.empty())
                ui.TextColoredWrapped({0.28F, 0.72F, 0.45F, 1.0F}, m_Status);

            std::uint64_t archiveBytes = 0;
            std::uint64_t redactions = 0;
            if (m_Bundle)
            {
                archiveBytes = m_Bundle->ArchiveBytes().size();
                for (const auto& entry : m_Bundle->Preview())
                    redactions += entry.Redactions;
                ui.Text("Frozen archive: " + SizeLabel(archiveBytes) + "  •  " +
                        std::to_string(m_Bundle->Preview().size()) + " entries  •  " + std::to_string(redactions) +
                        " redactions  •  " + std::to_string(m_Bundle->Omissions().size()) + " omissions");

                if (auto child = ui.BeginChild("DiagnosticBundleInventory", {0.0F, 410.0F}, true); child)
                {
                    if (auto table = ui.BeginTable("DiagnosticBundleEntries", 5); table)
                    {
                        ui.TableSetupColumn("Archive path", UiTableColumnSizing::Stretch, 2.0F);
                        ui.TableSetupColumn("Section", UiTableColumnSizing::Fixed, 72.0F);
                        ui.TableSetupColumn("Size", UiTableColumnSizing::Fixed, 118.0F);
                        ui.TableSetupColumn("SHA-256", UiTableColumnSizing::Stretch, 2.2F);
                        ui.TableSetupColumn("Redactions", UiTableColumnSizing::Fixed, 86.0F);
                        ui.TableHeaderRow();
                        for (const auto& entry : m_Bundle->Preview())
                        {
                            ui.TableNextRow();
                            (void)ui.TableNextColumn();
                            ui.Text(entry.ArchivePath);
                            (void)ui.TableNextColumn();
                            ui.Text(DiagnosticBundleSectionName(entry.Section));
                            (void)ui.TableNextColumn();
                            ui.Text(SizeLabel(entry.SizeBytes));
                            (void)ui.TableNextColumn();
                            ui.Text(entry.Sha256);
                            (void)ui.TableNextColumn();
                            ui.Text(std::to_string(entry.Redactions));
                        }
                    }
                    if (!m_Bundle->Omissions().empty())
                    {
                        ui.Spacing();
                        ui.Text("Omitted sources");
                        if (auto table = ui.BeginTable("DiagnosticBundleOmissions", 2); table)
                        {
                            ui.TableSetupColumn("Archive path", UiTableColumnSizing::Stretch, 2.0F);
                            ui.TableSetupColumn("Reason", UiTableColumnSizing::Stretch, 1.0F);
                            ui.TableHeaderRow();
                            for (const auto& omission : m_Bundle->Omissions())
                            {
                                ui.TableNextRow();
                                (void)ui.TableNextColumn();
                                ui.Text(omission.ArchivePath);
                                (void)ui.TableNextColumn();
                                ui.Text(omission.Reason);
                            }
                        }
                    }
                }
            }

            if (auto disabled = ui.BeginDisabled(!m_Bundle || static_cast<bool>(m_SaveDialog)); disabled)
            {
                if (ui.Button(m_SaveDialog ? "Waiting for destination..." : "Save ZIP...", {180.0F, 34.0F}))
                {
                    SaveFileDialogSpecification specification;
                    specification.Title = "Save Kéire Diagnostic Bundle";
                    specification.DefaultName = m_DefaultName;
                    specification.FilterName = "ZIP archive";
                    specification.Extension = "zip";
                    m_SaveDialog = windows.ShowSaveFileDialog(parent, specification);
                    m_Status = "Choose where to save the frozen diagnostic bundle.";
                    m_Error.clear();
                }
            }
            ui.SameLine();
            if (ui.Button("Close", {100.0F, 34.0F}))
            {
                m_Visible = false;
                ui.CloseCurrentPopup();
            }
        }
    }

    void DiagnosticBundleDialogController::SetSelection(DiagnosticBundleSelection selection)
    {
        if (SameSelection(selection, m_Request.Selection))
            return;
        m_Request.Selection = selection;
        Rebuild();
    }

    void DiagnosticBundleDialogController::Shutdown() noexcept
    {
        m_SaveDialog.Reset();
        m_Bundle.reset();
        m_Request = {};
        m_DefaultName.clear();
        m_Status.clear();
        m_Error.clear();
        m_OpenRequested = false;
        m_Visible = false;
    }

    const DiagnosticBundleSelection& DiagnosticBundleDialogController::Selection() const noexcept
    {
        return m_Request.Selection;
    }

    const FrozenDiagnosticBundle* DiagnosticBundleDialogController::Bundle() const noexcept
    {
        return m_Bundle ? &*m_Bundle : nullptr;
    }

    const std::string& DiagnosticBundleDialogController::Status() const noexcept { return m_Status; }

    const std::string& DiagnosticBundleDialogController::Error() const noexcept { return m_Error; }

    void DiagnosticBundleDialogController::CompleteSaveDialog(const SaveFileDialogStatus status,
                                                              const std::filesystem::path& selectedPath,
                                                              std::string diagnostic)
    {
        switch (status)
        {
        case SaveFileDialogStatus::Pending:
            return;
        case SaveFileDialogStatus::Cancelled:
            m_Status = "Save cancelled. No archive was written.";
            m_Error.clear();
            return;
        case SaveFileDialogStatus::Failed:
            m_Status.clear();
            m_Error = "The save dialog failed: " + std::move(diagnostic) + " No archive was written.";
            return;
        case SaveFileDialogStatus::Selected:
            break;
        }
        try
        {
            if (!m_Bundle)
                throw std::logic_error("The frozen diagnostic bundle is unavailable.");
            m_Bundle->Save(selectedPath);
            m_Status = "Diagnostic bundle saved successfully. It remains local until you manually share it.";
            m_Error.clear();
        }
        catch (const std::exception& error)
        {
            m_Status.clear();
            m_Error =
                std::string("Diagnostic bundle save failed: ") + error.what() + " No partial archive was retained.";
        }
    }

    void DiagnosticBundleDialogController::Rebuild()
    {
        try
        {
            m_Bundle = BuildDiagnosticBundle(m_Request);
            m_Status = "Preview rebuilt from frozen, sanitized bytes.";
            m_Error.clear();
        }
        catch (const std::exception& error)
        {
            m_Bundle.reset();
            m_Status.clear();
            m_Error = std::string("Diagnostic collection failed: ") + error.what() + " No archive has been written.";
        }
    }

    void DiagnosticBundleDialogController::PollSaveDialog()
    {
        if (!m_SaveDialog || m_SaveDialog->Status() == SaveFileDialogStatus::Pending)
            return;
        const auto dialog = std::move(m_SaveDialog);
        m_SaveDialog.Reset();
        const auto status = dialog->Status();
        const auto selectedPath =
            status == SaveFileDialogStatus::Selected ? dialog->SelectedPath() : std::filesystem::path{};
        const auto diagnostic = status == SaveFileDialogStatus::Failed ? dialog->Diagnostic() : std::string{};
        CompleteSaveDialog(status, selectedPath, diagnostic);
    }
} // namespace Keire::Internal
