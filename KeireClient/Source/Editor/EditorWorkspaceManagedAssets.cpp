#include "KeireClient/EditorWorkspaceLayer.h"

#include "Keire/Scripting/ManagedAssemblyAsset.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorAssetFileService.h"

#include <filesystem>
#include <stdexcept>
#include <string>

bool EditorWorkspaceLayer::CreateManagedAssembly(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (!KeireEditor::Detail::IsCSharpIdentifier(name))
            throw std::invalid_argument("Managed assembly names must be valid C# identifiers.");
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keireasm");
        const auto script = directory / std::string(name) / (std::string(name) + "Root.cs");
        if (m_AssetDatabase->Find(destination) ||
            std::filesystem::exists(m_AssetDatabase->Specification().ProjectRoot / "Assets" / script))
            throw std::runtime_error("The assembly or its source folder already exists.");

        Keire::ManagedAssemblyDefinition definition;
        definition.Name = name;
        definition.RootNamespace = name;
        definition.SourceRoots = {std::filesystem::path("Assets") / directory / std::string(name)};
        const std::string source = "using Keire;\n\nnamespace " + std::string(name) + ";\n\npublic sealed class " +
                                   std::string(name) +
                                   "Root : Behaviour\n{\n    protected override void Start() => "
                                   "Log.Info(\"Managed assembly loaded.\");\n}\n";
        m_AssetOperations->QueueCreateAssetWithAuxiliary(
            destination, Keire::ManagedAssemblyAsset::Encode(definition), {},
            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal, .UndoName = "Create Managed Assembly"},
            {{script, KeireEditor::Detail::TextBytes(source)}});
        m_AssetStatus = "Creating managed assembly " + destination.generic_string() + ".";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Managed assembly creation failed: ") + error.what());
        return false;
    }
}
