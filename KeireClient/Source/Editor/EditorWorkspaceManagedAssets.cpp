#include "KeireClient/EditorWorkspaceLayer.h"

#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "Keire/Scripting/ManagedAssemblyReferenceAsset.h"
#include "KeireClient/Editor/EditorManagedRuntimeCoordinator.h"
#include "KeireClient/Editor/ManagedAssemblyEditing.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorAssetFileService.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

bool EditorWorkspaceLayer::CreateManagedAssembly(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        Keire::ManagedAssemblyDefinition definition;
        definition.Name = name;
        definition.RootNamespace = name;
        std::ranges::replace(definition.RootNamespace, '-', '_');
        Keire::ManagedAssemblyAsset::Validate(definition);
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keireasm");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("An assembly with that name already exists in this folder.");

        m_AssetOperations->QueueCreateAsset(
            destination, Keire::ManagedAssemblyAsset::Encode(definition), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Managed Assembly"});
        m_AssetStatus = "Creating managed assembly " + destination.generic_string() + ".";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Managed assembly creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateAssetBrowserManagedAssemblyReference(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".asmref");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("An assembly reference already exists at that path.");
        m_AssetOperations->QueueCreateAsset(
            destination, Keire::ManagedAssemblyReferenceAsset::Encode(""), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Assembly Reference"});
        m_AssetStatus = "Creating assembly reference; choose its assembly in the Inspector.";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(error.what());
        return false;
    }
}

void EditorWorkspaceLayer::PersistInspectorManagedAssembly(const Keire::AssetId asset,
                                                           const std::span<const std::byte> bytes,
                                                           const std::span<const std::byte> expected)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        throw std::runtime_error("Assembly editing services are unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || (record->RelativePath.extension() != ".keireasm" && record->RelativePath.extension() != ".asmref"))
        throw std::invalid_argument("Only assembly definitions and references can be saved here.");
    const auto& spec = m_AssetDatabase->Specification();
    KeireEditor::SaveManagedAssemblySettings(spec.ProjectRoot / spec.SourceDirectory / record->RelativePath, bytes,
                                             expected);
    m_AssetOperations->QueueAssetImport(asset, KeireEditor::AssetOperationPriority::ExplicitAction,
                                        {.ReloadAsset = asset});
    m_ManagedRuntimeCoordinator->ScheduleBuild(0.1);
    m_AssetStatus = "Saved assembly settings and scheduled a script rebuild.";
}
