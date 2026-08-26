#include "KeireClient/Editor/EditorPackageCoordinator.h"

#include "KeireInternal/Process.h"

#include <chrono>
#include <stdexcept>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::string AssetPackageFileName(const std::string_view displayName)
        {
            std::string result(displayName);
            for (char& character : result)
            {
                if (static_cast<unsigned char>(character) < 32U ||
                    std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos)
                {
                    character = '-';
                }
            }
            while (!result.empty() && (result.back() == ' ' || result.back() == '.'))
                result.pop_back();
            if (result.empty())
                result = "Asset Package";
            return result + ".keireassetpackage";
        }

        void RequireDependencies(const EditorPackageCoordinatorDependencies& dependencies)
        {
            if (!dependencies.ShutdownPanel || !dependencies.AssetDatabase || !dependencies.AssetRecords ||
                !dependencies.Windows || !dependencies.MainWindow || !dependencies.SetStatus || !dependencies.SetError)
            {
                throw std::invalid_argument("Editor package coordinator dependencies are incomplete.");
            }
        }
    } // namespace

    EditorPackageCoordinator::EditorPackageCoordinator(EditorPackageCoordinatorDependencies dependencies)
        : m_Dependencies(std::move(dependencies))
    {
        RequireDependencies(m_Dependencies);
    }

    EditorPackageCoordinator::~EditorPackageCoordinator() noexcept { Shutdown(); }

    void EditorPackageCoordinator::CreateAssetPackage(AssetPackageSelection selection, AssetPackageDraft draft)
    {
        m_Lifetime.RequireOwnerThread("create asset package");
        try
        {
            const auto database = m_Dependencies.AssetDatabase();
            if (!database)
                throw std::logic_error("No project asset database is available.");
            if (Busy())
                throw std::logic_error("An asset-package export is already active.");
            static_cast<void>(ResolveAssetPackageRecords(m_Dependencies.AssetRecords(), selection));
            Keire::SaveFileDialogSpecification dialog;
            dialog.Title = "Create Kéire Asset Package";
            dialog.DefaultLocation = database->Specification().ProjectRoot;
            dialog.DefaultName = AssetPackageFileName(draft.DisplayName);
            dialog.FilterName = "Kéire Asset Package";
            dialog.Extension = "keireassetpackage";
            auto operation = m_Dependencies.Windows()->ShowSaveFileDialog(m_Dependencies.MainWindow(), dialog);
            m_PendingDialog = {
                .Selection = std::move(selection), .Draft = std::move(draft), .Dialog = std::move(operation)};
            m_Dependencies.SetStatus("Choose a destination for the asset package.");
        }
        catch (const std::exception& error)
        {
            m_Dependencies.SetError(std::string("Asset-package export failed: ") + error.what());
        }
    }

    void EditorPackageCoordinator::Update()
    {
        m_Lifetime.RequireOwnerThread("update");
        CompleteExport();
        CompleteDialog();
    }

    void EditorPackageCoordinator::ShutdownPanel() noexcept
    {
        if (m_PanelShutdown)
            return;
        try
        {
            m_Lifetime.RequireOwnerThread("shut down package panel");
            m_Dependencies.ShutdownPanel();
            m_PanelShutdown = true;
        }
        catch (...)
        {
            ReportShutdownFailure("package-panel", std::current_exception());
        }
    }

    void EditorPackageCoordinator::Shutdown() noexcept
    {
        if (!m_Lifetime.BeginShutdown())
            return;
        if (!m_PanelShutdown)
        {
            try
            {
                m_Dependencies.ShutdownPanel();
                m_PanelShutdown = true;
            }
            catch (...)
            {
                ReportShutdownFailure("package-panel", std::current_exception());
            }
        }
        m_PendingDialog.reset();
        if (m_Export.valid())
        {
            try
            {
                static_cast<void>(m_Export.get());
            }
            catch (...)
            {
                ReportShutdownFailure("asset-package-export", std::current_exception());
            }
        }
        m_Output.clear();
    }

    bool EditorPackageCoordinator::Busy() const
    {
        m_Lifetime.RequireOwnerThread("read busy state");
        return m_PendingDialog.has_value() || m_Export.valid();
    }

    EditorWorkspaceCallbackToken EditorPackageCoordinator::CaptureCallbackToken() const
    {
        return m_Lifetime.CaptureCallbackToken();
    }

    bool EditorPackageCoordinator::ShutdownComplete() const { return m_Lifetime.ShutdownComplete(); }

    void EditorPackageCoordinator::CompleteExport()
    {
        if (!m_Export.valid() || m_Export.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try
        {
            const auto result = m_Export.get();
            std::string status = "Created " + m_Output.filename().string() + " with " +
                                 std::to_string(result.Manifest.Assets.size()) + " asset(s).";
            std::string diagnostic;
            if (!Keire::Detail::RevealInFileManager(m_Output, diagnostic))
                status += " Reveal failed: " + diagnostic;
            m_Dependencies.SetStatus(std::move(status));
        }
        catch (const std::exception& error)
        {
            m_Dependencies.SetError(std::string("Asset-package export failed: ") + error.what());
        }
        m_Output.clear();
    }

    void EditorPackageCoordinator::CompleteDialog()
    {
        if (!m_PendingDialog || m_PendingDialog->Dialog->Status() == Keire::SaveFileDialogStatus::Pending)
            return;

        auto pending = std::move(*m_PendingDialog);
        m_PendingDialog.reset();
        if (pending.Dialog->Status() == Keire::SaveFileDialogStatus::Cancelled)
        {
            m_Dependencies.SetStatus("Asset-package export cancelled.");
            return;
        }
        if (pending.Dialog->Status() == Keire::SaveFileDialogStatus::Failed)
        {
            m_Dependencies.SetError("Asset-package save dialog failed: " + pending.Dialog->Diagnostic());
            return;
        }

        try
        {
            auto output = pending.Dialog->SelectedPath();
            if (output.extension() != ".keireassetpackage")
                output += ".keireassetpackage";
            if (std::filesystem::exists(output))
                throw std::invalid_argument("Asset-package export will not overwrite an existing file.");
            const auto database = m_Dependencies.AssetDatabase();
            if (!database)
                throw std::logic_error("The project closed before asset-package export began.");
            const auto& specification = database->Specification();
            const auto records = m_Dependencies.AssetRecords();
            AssetPackageAuthoringRequest request{
                .ProjectRoot = specification.ProjectRoot,
                .SourceDirectory = specification.SourceDirectory,
                .StagingParent = specification.ProjectRoot / "Library/AssetPackageExports",
                .Output = output,
                .Selection = std::move(pending.Selection),
                .Draft = std::move(pending.Draft),
                .Records = std::vector<Keire::AssetSourceRecord>(records.begin(), records.end()),
            };
            m_Output = std::move(output);
            m_Export = std::async(std::launch::async,
                                  [request = std::move(request)] { return CreateAssetPackageArchive(request); });
            m_Dependencies.SetStatus("Creating the asset package in the background...");
        }
        catch (const std::exception& error)
        {
            m_Dependencies.SetError(std::string("Asset-package export failed: ") + error.what());
        }
    }

    void EditorPackageCoordinator::ReportShutdownFailure(const std::string_view operation,
                                                         const std::exception_ptr& failure) noexcept
    {
        if (!m_Dependencies.ReportShutdownFailure)
            return;
        try
        {
            m_Dependencies.ReportShutdownFailure(operation, failure);
        }
        catch (...)
        {
        }
    }
} // namespace KeireEditor
