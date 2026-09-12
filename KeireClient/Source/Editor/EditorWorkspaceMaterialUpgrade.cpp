#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/PlayerBuildService.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <exception>
#include <utility>

bool EditorWorkspaceLayer::MaterialUpgradeAvailable() const
{
    return m_AssetDatabase && (!m_AssetOperations || !m_AssetOperations->Busy()) &&
           (!m_PlayerBuildService || !m_PlayerBuildService->Busy()) && !InspectorPlayModeActive() &&
           (!m_MaterialGraphDocument || !m_MaterialGraphDocument->Dirty()) &&
           (!m_ShaderGraphDocument || !m_ShaderGraphDocument->Dirty()) &&
           (!m_ShaderGraphPanel || !m_ShaderGraphPanel->HasPendingNodeProperties()) &&
           m_PendingMaterialSelectionImports.empty() &&
           (!m_MaterialDocument || !m_MaterialDocument->PendingCatalogRefresh(true));
}

void EditorWorkspaceLayer::DrawMaterialUpgradeDialogs(Keire::UiFrame& ui)
{
    const auto inspectMigration = [this]
    {
        m_MaterialUpgradeStatus.clear();
        try
        {
            m_MaterialMigrationReview.Refresh(m_AssetDatabase->Specification().ProjectRoot,
                                              Keire::InspectShaderGraphMigration);
        }
        catch (const std::exception& error)
        {
            m_MaterialUpgradeStatus = error.what();
        }
    };
    const auto inspectShared = [this]
    {
        m_SharedShaderReview.reset();
        m_MaterialUpgradeStatus.clear();
        try
        {
            m_SharedShaderReview = Keire::ReviewSharedShaderInputs(m_AssetDatabase->Specification().ProjectRoot);
        }
        catch (const std::exception& error)
        {
            m_MaterialUpgradeStatus = error.what();
        }
    };
    if (std::exchange(m_OpenMaterialMigrationReview, false) && MaterialUpgradeAvailable())
    {
        inspectMigration();
        ui.OpenPopup("Review Legacy Material Conversion");
    }
    if (std::exchange(m_OpenSharedShaderReview, false) && MaterialUpgradeAvailable())
    {
        inspectShared();
        ui.OpenPopup("Review Shared Shader Inputs");
    }
    if (auto popup = ui.BeginPopupModal("Review Legacy Material Conversion"); popup)
    {
        ui.TextWrapped("Review the complete conversion before applying. Sources, metadata, source index and runtime "
                       "catalog are validated in a staging area. Unsupported materials retain their legacy editor.");
        ui.TextWrapped("Applying waits for validation. Backups remain in Library/MaterialShaderUpgrade. Successful "
                       "conversion clears Project asset undo history; use the retained backups for recovery.");
        if (const auto& review = m_MaterialMigrationReview.Report(); review)
        {
            ui.Text(std::to_string(review->PendingCount()) + " material(s) to convert");
            if (auto entries = ui.BeginChild("Migration entries", {620.0F, 240.0F}, true); entries)
            {
                for (const auto& item : review->Items)
                {
                    ui.TextWrapped(Keire::Detail::PathToUtf8(item.MaterialGraph));
                    if (item.CreatesShader)
                        ui.TextWrapped("Extract shader: " + Keire::Detail::PathToUtf8(item.ShaderGraph));
                    if (!item.Diagnostic.empty())
                        ui.TextWrapped(item.Diagnostic);
                    ui.Separator();
                }
            }
        }
        if (!m_MaterialUpgradeStatus.empty())
            ui.TextWrapped(m_MaterialUpgradeStatus);
        if (!MaterialUpgradeAvailable())
            ui.TextWrapped("Finish asset work, save graph drafts and stop Play before applying.");
        {
            auto disabled = ui.BeginDisabled(!MaterialUpgradeAvailable() || !m_MaterialMigrationReview.CanApply());
            if (ui.Button("Validate and Apply Reviewed Conversion"))
            {
                bool published = false;
                try
                {
                    const auto specification = m_AssetDatabase->Specification();
                    const auto result = m_MaterialMigrationReview.Apply(
                        specification.ProjectRoot, [&specification](const auto& root, const auto& reviewed)
                        { return Keire::ApplyShaderGraphMigration(root, reviewed, specification); });
                    published = true;
                    if (m_MaterialGraphDocument && m_MaterialGraphDocument->IsOpen() &&
                        std::ranges::any_of(result.Items, [this](const auto& item)
                                            { return item.SourceAsset == m_MaterialGraphDocument->Asset(); }))
                        m_MaterialGraphDocument->Close();
                    if (m_AssetBrowserPanel)
                        if (const auto history = m_AssetBrowserPanel->UndoContext())
                            history->Clear();
                    const auto runtime = specification.ProjectRoot / specification.CacheDirectory / "Runtime";
                    (void)Keire::Detail::AssetDatabaseWorkerAccess::ReloadSourceIndex(*m_AssetDatabase,
                                                                                      runtime / "source-index.json");
                    ApplyAssetImportResult({.CatalogPath = runtime / "catalog.json"}, true);
                    m_MaterialUpgradeStatus = "Converted and validated " + std::to_string(result.PendingCount()) +
                                              " material(s). Sources and runtime catalog are published.";
                }
                catch (const std::exception& error)
                {
                    m_MaterialUpgradeStatus =
                        std::string(published ? "Conversion committed, but editor reload failed. Reopen the project: "
                                              : "Conversion did not complete. Resolve any reported recovery errors "
                                                "before reviewing again: ") +
                        error.what();
                }
            }
        }
        {
            auto disabled = ui.BeginDisabled(!MaterialUpgradeAvailable());
            if (ui.Button("Refresh Review"))
                inspectMigration();
        }
        ui.SameLine();
        if (ui.Button("Close"))
        {
            m_MaterialMigrationReview.Clear();
            ui.CloseCurrentPopup();
        }
    }
    if (auto popup = ui.BeginPopupModal("Review Shared Shader Inputs"); popup)
    {
        ui.TextWrapped("Adopt reviewed compiler, include, package-lock and visual-fixture inputs for the installed "
                       "shared shaders. This preserves shader sources, version and identities. It does not certify "
                       "visual acceptance or install a new shader package version.");
        for (const auto& input : Keire::DefaultSharedShaderInputs())
            ui.TextWrapped(Keire::Detail::PathToUtf8(input.Path));
        if (m_SharedShaderReview)
        {
            ui.Text("Graph importer version: " + std::to_string(m_SharedShaderReview->GraphImporterVersion));
            for (const auto& input : m_SharedShaderReview->Inputs)
                ui.TextWrapped(Keire::Detail::PathToUtf8(input.Path) + " : " + input.Sha256);
        }
        if (!m_MaterialUpgradeStatus.empty())
            ui.TextWrapped(m_MaterialUpgradeStatus);
        {
            auto disabled = ui.BeginDisabled(!MaterialUpgradeAvailable() || !m_SharedShaderReview);
            if (ui.Button("Apply Reviewed Inputs"))
            {
                auto reviewed = std::move(*m_SharedShaderReview);
                m_SharedShaderReview.reset();
                try
                {
                    (void)Keire::ApplySharedShaderInputs(m_AssetDatabase->Specification().ProjectRoot, reviewed);
                    m_MaterialUpgradeStatus = "Shared shader inputs pinned. Reimporting assets...";
                    ImportAssets();
                }
                catch (const std::exception& error)
                {
                    m_MaterialUpgradeStatus = std::string("Review shared shader inputs again: ") + error.what();
                }
            }
        }
        {
            auto disabled = ui.BeginDisabled(!MaterialUpgradeAvailable());
            if (ui.Button("Refresh Input Review"))
                inspectShared();
        }
        ui.SameLine();
        if (ui.Button("Close Input Review"))
        {
            m_SharedShaderReview.reset();
            ui.CloseCurrentPopup();
        }
    }
}
