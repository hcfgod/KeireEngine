#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/GraphDuplication.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/MaterialGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"

#include <algorithm>
#include <stdexcept>

void EditorWorkspaceLayer::SetGraphClipboard(const std::string_view text) { Owner().Windows()->SetClipboardText(text); }

std::string EditorWorkspaceLayer::GraphClipboard() const { return Owner().Windows()->ClipboardText(); }

bool EditorWorkspaceLayer::ExtractShaderGraphSelectionToFunction(const std::span<const Keire::AssetId> selection,
                                                                 const std::string_view name)
{
    try
    {
        if (!m_AssetDatabase || !m_AssetOperations || !m_ShaderGraphDocument->Asset())
            throw std::runtime_error("Shader function extraction requires an open project graph and asset worker.");
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Extracted function name must be one non-empty path component.");
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto source = m_AssetDatabase->Find(m_ShaderGraphDocument->Asset());
        if (!source)
            throw std::runtime_error("The open Shader Graph source is unavailable.");
        const auto destination = source->RelativePath.parent_path() /
                                 (std::string(name) + std::string(Keire::ShaderFunctionAssetSourceExtension));
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A Shader Function with this name already exists beside the graph.");

        const auto placeholder = Keire::AssetId::Generate();
        const auto before = m_ShaderGraphDocument->Definition();
        auto extracted = KeireEditor::ExtractShaderGraphSelection(before, selection, placeholder, name);
        auto state = std::make_shared<KeireEditor::GraphFunctionExtractionState>();
        state->Kind = KeireEditor::GraphFunctionExtractionKind::Shader;
        state->SourceAsset = m_ShaderGraphDocument->Asset();
        state->PlaceholderAsset = placeholder;
        state->ShaderBefore = before;
        state->ShaderAfter = std::move(extracted.Parent);
        m_AssetOperations->QueueCreateAsset(destination, Keire::ShaderFunctionAsset::Encode(extracted.Function), {},
                                            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal,
                                             .UndoName = "Extract Shader Function",
                                             .GraphFunctionExtraction = std::move(state),
                                             .ParentSource = m_ShaderGraphDocument->Asset(),
                                             .Reason = "shader-function-extraction"});
        m_ShaderGraphPanel->SetMessage("Creating " + destination.generic_string() +
                                       " and preparing parent rewiring...");
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Shader function extraction failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::ExtractMaterialGraphSelectionToFunction(const std::span<const Keire::AssetId> selection,
                                                                   const std::string_view name)
{
    try
    {
        if (!m_AssetDatabase || !m_AssetOperations || !m_MaterialGraphDocument->Asset())
            throw std::runtime_error("Material function extraction requires an open project graph and asset worker.");
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("Extracted function name must be one non-empty path component.");
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        const auto source = m_AssetDatabase->Find(m_MaterialGraphDocument->Asset());
        if (!source)
            throw std::runtime_error("The open Material Graph source is unavailable.");
        const auto destination = source->RelativePath.parent_path() /
                                 (std::string(name) + std::string(Keire::MaterialFunctionAssetSourceExtension));
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A Material Function with this name already exists beside the graph.");

        const auto placeholder = Keire::AssetId::Generate();
        const auto before = m_MaterialGraphDocument->Definition();
        auto extracted = KeireEditor::ExtractMaterialGraphSelection(before, selection, placeholder, name);
        auto state = std::make_shared<KeireEditor::GraphFunctionExtractionState>();
        state->Kind = KeireEditor::GraphFunctionExtractionKind::Material;
        state->SourceAsset = m_MaterialGraphDocument->Asset();
        state->PlaceholderAsset = placeholder;
        state->MaterialBefore = before;
        state->MaterialAfter = std::move(extracted.Parent);
        m_AssetOperations->QueueCreateAsset(destination, Keire::MaterialFunctionAsset::Encode(extracted.Function), {},
                                            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal,
                                             .UndoName = "Extract Material Function",
                                             .GraphFunctionExtraction = std::move(state),
                                             .ParentSource = m_MaterialGraphDocument->Asset(),
                                             .Reason = "material-function-extraction"});
        m_MaterialGraphPanel->SetMessage("Creating " + destination.generic_string() +
                                         " and preparing parent rewiring...");
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("Material function extraction failed: ") + error.what());
        return false;
    }
}

void EditorWorkspaceLayer::CompleteGraphFunctionExtraction(const KeireEditor::GraphFunctionExtractionState& state,
                                                           const Keire::AssetId created)
{
    const auto replacePlaceholder = [&](auto& graph)
    {
        const auto call = std::ranges::find_if(graph.Nodes,
                                               [&](const Keire::ShaderGraphNode& node)
                                               {
                                                   return node.Kind == Keire::ShaderGraphNodeKind::FunctionCall &&
                                                          node.ReferencedAsset == state.PlaceholderAsset;
                                               });
        if (call == graph.Nodes.end())
            throw std::logic_error("Prepared graph extraction lost its function-call placeholder.");
        call->ReferencedAsset = created;
    };

    if (state.Kind == KeireEditor::GraphFunctionExtractionKind::Shader)
    {
        if (!state.ShaderBefore || !state.ShaderAfter)
            throw std::logic_error("Shader function extraction completion is incomplete.");
        if (m_ShaderGraphDocument->Asset() != state.SourceAsset ||
            m_ShaderGraphDocument->Definition() != *state.ShaderBefore)
        {
            m_ShaderGraphPanel->SetMessage(
                "Created the Shader Function, but kept the parent because it changed during extraction.");
            return;
        }
        auto after = *state.ShaderAfter;
        replacePlaceholder(after);
        (void)m_ShaderGraphDocument->Edit("Extract Shader Graph selection to function",
                                          [after = std::move(after)](auto& definition) { definition = after; });
        m_ShaderGraphPanel->SetMessage("Created the Shader Function and rewired the selected nodes.");
        return;
    }

    if (!state.MaterialBefore || !state.MaterialAfter)
        throw std::logic_error("Material function extraction completion is incomplete.");
    if (m_MaterialGraphDocument->Asset() != state.SourceAsset ||
        m_MaterialGraphDocument->Definition() != *state.MaterialBefore)
    {
        m_MaterialGraphPanel->SetMessage(
            "Created the Material Function, but kept the parent because it changed during extraction.");
        return;
    }
    auto after = *state.MaterialAfter;
    replacePlaceholder(after.SurfaceGraph);
    (void)m_MaterialGraphDocument->Edit("Extract Material Graph selection to function",
                                        [after = std::move(after)](auto& definition) { definition = after; });
    m_MaterialGraphPanel->SetMessage("Created the Material Function and rewired the selected nodes.");
}
