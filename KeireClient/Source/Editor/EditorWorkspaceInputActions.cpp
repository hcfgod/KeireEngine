#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/InputActionsDocument.h"

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

void EditorWorkspaceLayer::RecordInputUndo(const std::string_view name)
{
    m_InputActionsDocument->RecordApplied(name, m_InputActionsDocument->Definition());
}

void EditorWorkspaceLayer::UndoInputEdit() { (void)m_InputActionsDocument->Undo(); }

void EditorWorkspaceLayer::RedoInputEdit() { (void)m_InputActionsDocument->Redo(); }
