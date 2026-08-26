#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/InputActionsCodeGenerator.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireInternal/FileSystem.h"

#include <stdexcept>
#include <utility>

void EditorWorkspaceLayer::OpenInputActions(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->RelativePath.extension() != ".keireinput")
        throw std::invalid_argument("Only .keireinput assets can be opened in the Input Actions editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    auto definition = Keire::InputActionAsset::Decode(KeireEditor::Detail::ReadBytes(source))->Definition();
    if (const auto context = m_InputActionsDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "Input Actions: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    m_InputActionsDocument->Open(asset, std::move(definition), std::move(context), source);
    if (!m_InputActionsDocument->Definition().ActionMaps.empty())
        m_InputActionsDocument->SelectMap(m_InputActionsDocument->Definition().ActionMaps.front().Id);
    m_ActiveUndoContext = m_InputActionsDocument->UndoContext();
    m_InputActionsPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_InputActionsPanel->ResetTransientState();
    m_InputContext.Reset();
    if (const auto input = Owner().Input(); input && m_EditorInputUser)
        m_InputContext = input->CreateActionContext(asset, m_EditorInputUser, Keire::InputContextRole::EditorControl);
    m_InputActionsPanel->Registration().SetVisible(true);
}

void EditorWorkspaceLayer::SaveInputActions()
{
    if (!m_AssetDatabase || !m_InputActionsDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_InputActionsDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited input asset no longer exists.");
    m_InputActionsDocument->Save();
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_InputActionsDocument->Asset());
    m_InputActionsPanel->SetMessage("Saved and imported " + record->RelativePath.generic_string() + ".");
}

std::filesystem::path EditorWorkspaceLayer::GenerateInputActionsWrapper(const std::string_view className,
                                                                        const std::string_view nameSpace)
{
    const auto project = Owner().GetProject();
    if (!project || !m_AssetDatabase || !m_InputActionsDocument->Asset())
        throw std::logic_error("Open an input action asset before generating its C# wrapper.");
    const auto source =
        KeireEditor::GenerateInputActionsCSharp(m_InputActionsDocument->Definition(), className, nameSpace);
    std::vector<KeireEditor::ManagedScriptAssemblyCandidate> assemblies;
    for (const auto& record : m_AssetDatabase->Records())
    {
        if (record.Type != Keire::ManagedAssemblyAsset::StaticType())
            continue;
        const auto assembly = Keire::ManagedAssemblyAsset::Decode(
            KeireEditor::Detail::ReadBytes(project->Root() / "Assets" / record.RelativePath));
        assemblies.push_back({record.Id, assembly->Definition()});
    }
    const auto generatedFolder = std::filesystem::path("Scripts") / "Generated";
    const auto placement = KeireEditor::ResolveManagedScriptPlacement(assemblies, generatedFolder);
    const auto relative =
        std::filesystem::path("Assets") / generatedFolder / (std::string(className) + ".InputActions.g.cs");
    const auto destination = project->Root() / relative;
    std::filesystem::create_directories(destination.parent_path());
    (void)Keire::Detail::WriteTextFileAtomicallyIfChanged(destination, source);
    if (!placement.SourceRootToAdd.empty())
        ExtendManagedAssemblySourceRoot(placement.Assembly, placement.SourceRootToAdd);
    ImportAssets(KeireEditor::AssetOperationPriority::AutomaticRefresh);
    return relative;
}

void EditorWorkspaceLayer::RecordInputUndo(const std::string_view name)
{
    m_InputActionsDocument->RecordApplied(name, m_InputActionsDocument->Definition());
}

void EditorWorkspaceLayer::UndoInputEdit() { (void)m_InputActionsDocument->Undo(); }

void EditorWorkspaceLayer::RedoInputEdit() { (void)m_InputActionsDocument->Redo(); }
