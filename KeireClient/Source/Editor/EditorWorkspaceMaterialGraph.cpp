#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/MaterialGraphPanel.h"

#include <algorithm>
#include <array>
#include <exception>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <variant>

namespace
{
    using KeireEditor::Detail::ReadBytes;
    using KeireEditor::Detail::WriteBytesAtomically;
} // namespace

KeireEditor::MaterialGraphDocument& EditorWorkspaceLayer::MaterialGraphState() noexcept
{
    return *m_MaterialGraphDocument;
}

const Keire::UiThemeDefinition& EditorWorkspaceLayer::MaterialGraphTheme() const noexcept { return m_Theme; }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::MaterialGraphAssetRecords() const noexcept
{
    return m_AssetRecords;
}

std::optional<Keire::ShaderGraphDefinition>
EditorWorkspaceLayer::ResolveMaterialGraphFunction(const Keire::AssetId asset) const
{
    return ResolveReusableGraph(asset);
}

void EditorWorkspaceLayer::SaveMaterialGraphDocument() { SaveMaterialGraph(); }

void EditorWorkspaceLayer::UndoMaterialGraphEdit() { (void)m_MaterialGraphDocument->Undo(); }

void EditorWorkspaceLayer::RedoMaterialGraphEdit() { (void)m_MaterialGraphDocument->Redo(); }

void EditorWorkspaceLayer::RevealMaterialGraphAsset(const Keire::AssetId asset)
{
    if (!asset || !m_AssetBrowserPanel)
        return;
    m_SelectedAsset = asset;
    m_AssetBrowserPanel->RevealAsset(asset);
    m_AssetBrowserPanel->Registration().SetVisible(true);
    m_AssetBrowserPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::ReportMaterialGraphError(std::string message) noexcept { SetAssetError(std::move(message)); }

std::optional<Keire::ShaderInterfaceDefinition>
EditorWorkspaceLayer::ResolveMaterialGraphInterface(const Keire::MaterialShaderReference& shader) const
{
    if (!m_AssetDatabase || !shader.Asset)
        return std::nullopt;
    const auto record = m_AssetDatabase->Find(shader.Asset);
    if (!record)
        return std::nullopt;
    try
    {
        const auto& specification = m_AssetDatabase->Specification();
        const auto source = specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
        Keire::ShaderInterfaceDefinition result;
        if (shader.Kind == Keire::MaterialShaderSourceKind::ShaderGraph)
        {
            if (record->Type != Keire::ShaderGraphAsset::StaticType())
                return std::nullopt;
            const auto graph = Keire::ShaderGraphAsset::DecodeSource(ReadBytes(source));
            result.Domain = graph.Output == Keire::ShaderGraphOutput::Fullscreen
                                ? Keire::ShaderInterfaceDomain::Fullscreen
                                : Keire::ShaderInterfaceDomain::Surface;
            for (const auto& node : graph.Nodes)
            {
                if (node.Kind != Keire::ShaderGraphNodeKind::Parameter)
                    continue;
                Keire::ShaderPropertyDefinition property;
                property.Id = node.Id;
                property.Name = node.Symbol;
                property.DisplayName = node.Name;
                property.Category = node.ParameterMetadata.Category;
                property.Type = static_cast<Keire::ShaderPropertyType>(node.ValueType);
                property.Minimum = node.ParameterMetadata.Minimum;
                property.Maximum = node.ParameterMetadata.Maximum;
                property.Step = node.ParameterMetadata.Step;
                property.TextureSemantic = node.TextureSemantic;
                if (const auto* scalar = std::get_if<float>(&node.Value))
                    property.DefaultValue.X = *scalar;
                else if (const auto* vector2 = std::get_if<Keire::Vector2>(&node.Value))
                    property.DefaultValue = {vector2->X, vector2->Y, 0.0F, 0.0F};
                else if (const auto* vector3 = std::get_if<Keire::Vector3>(&node.Value))
                    property.DefaultValue = {vector3->X, vector3->Y, vector3->Z, 0.0F};
                else if (const auto* vector4 = std::get_if<Keire::Vector4>(&node.Value))
                    property.DefaultValue = *vector4;
                else if (const auto* color = std::get_if<Keire::Color>(&node.Value))
                    property.DefaultValue = {color->Red, color->Green, color->Blue, color->Alpha};
                else if (const auto* texture = std::get_if<Keire::AssetId>(&node.Value))
                    property.DefaultTexture = *texture;
                result.Properties.push_back(std::move(property));
            }
            for (const auto& keyword : graph.Keywords)
                result.Keywords.push_back(keyword.Name);
            return result;
        }
        if (shader.Kind != Keire::MaterialShaderSourceKind::ShaderAsset ||
            record->Type != Keire::ShaderAsset::StaticType())
            return std::nullopt;
        result.Properties = Keire::ShaderAsset::DecodeManifest(ReadBytes(source)).Properties;
        return result;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<Keire::ShaderGraphDefinition>
EditorWorkspaceLayer::ResolveMaterialGraphTemplate(const Keire::MaterialShaderReference& shader) const
{
    if (!m_AssetDatabase || !shader.Asset || shader.Kind != Keire::MaterialShaderSourceKind::ShaderGraph)
        return std::nullopt;
    const auto record = m_AssetDatabase->Find(shader.Asset);
    if (!record || record->Type != Keire::ShaderGraphAsset::StaticType())
        return std::nullopt;
    try
    {
        const auto& specification = m_AssetDatabase->Specification();
        return Keire::ShaderGraphAsset::DecodeSource(
            ReadBytes(specification.ProjectRoot / specification.SourceDirectory / record->RelativePath));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<Keire::ShaderGraphDefinition> EditorWorkspaceLayer::ResolveReusableGraph(const Keire::AssetId asset) const
{
    if (!m_AssetDatabase || !asset)
        return std::nullopt;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record)
        return std::nullopt;
    try
    {
        const auto& specification = m_AssetDatabase->Specification();
        const auto bytes = ReadBytes(specification.ProjectRoot / specification.SourceDirectory / record->RelativePath);
        if (record->Type == Keire::MaterialFunctionAsset::StaticType())
            return Keire::MaterialFunctionAsset::DecodeSource(bytes).Body;
        if (record->Type == Keire::ShaderFunctionAsset::StaticType())
            return Keire::ShaderFunctionAsset::DecodeSource(bytes).Body;
        if (record->Type == Keire::MaterialLayerAsset::StaticType())
            return Keire::MaterialLayerAsset::DecodeSource(bytes).Body;
        if (record->Type == Keire::MaterialLayerBlendAsset::StaticType())
            return Keire::MaterialLayerBlendAsset::DecodeSource(bytes).Body;
    }
    catch (...)
    {
    }
    return std::nullopt;
}

Keire::AssetId EditorWorkspaceLayer::ResolveMaterialGraphShader(const Keire::MaterialShaderReference& shader) const
{
    if (!m_AssetDatabase || !shader.Asset)
        return {};
    if (shader.Kind != Keire::MaterialShaderSourceKind::ShaderGraph)
        return shader.Asset;
    const auto record = m_AssetDatabase->Find(shader.Asset);
    if (!record || record->Type != Keire::ShaderGraphAsset::StaticType())
        return {};
    try
    {
        const auto& specification = m_AssetDatabase->Specification();
        const auto source = specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
        const auto graph = Keire::ShaderGraphAsset::DecodeSource(ReadBytes(source));
        Keire::ShaderGraphInstanceDefinition selection;
        selection.Parent = shader.Asset;
        selection.KeywordOverrides = shader.Keywords;
        const std::array ancestry{selection};
        const auto resolved = Keire::ResolveShaderGraphInstance(graph, ancestry);
        const auto variants = Keire::EnumerateShaderGraphKeywordVariants(graph.Keywords);
        const auto variant = std::ranges::find_if(variants, [&](const auto& candidate)
                                                  { return std::ranges::equal(candidate, resolved.Keywords); });
        const auto index = static_cast<std::size_t>(std::distance(variants.begin(), variant));
        return variant != variants.end() && index < record->SubAssets.size() ? record->SubAssets[index]
                                                                             : Keire::AssetId{};
    }
    catch (...)
    {
        return {};
    }
}

void EditorWorkspaceLayer::ApplyMaterialGraphDevelopmentRevision(
    const Keire::AssetId asset, const Keire::MaterialAssetDefinition& material) noexcept
{
    try
    {
        const auto assets = Owner().Assets();
        const auto record = m_AssetDatabase ? m_AssetDatabase->Find(asset) : std::nullopt;
        if (!assets || !record)
            return;
        const auto runtime = std::ranges::find_if(record->SubAssets,
                                                  [&](const Keire::AssetId candidate)
                                                  {
                                                      const auto type = assets->TryGetType(candidate);
                                                      return type && *type == Keire::MaterialAsset::StaticType();
                                                  });
        if (runtime != record->SubAssets.end())
            (void)assets->PublishDevelopmentAsset(*runtime, Keire::CreateRef<Keire::MaterialAsset>(material));
    }
    catch (const std::exception& error)
    {
        KEIRE_CLIENT_ERROR("[Material Graph] Live scene apply failed for {}: {}", asset.ToString(), error.what());
    }
}

void EditorWorkspaceLayer::PersistMaterialGraph(const Keire::AssetId asset, const std::span<const std::byte> bytes)
{
    if (!m_AssetDatabase)
        throw std::runtime_error("The Asset Database is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::MaterialGraphAsset::StaticType() ||
        record->RelativePath.extension() != ".keirematerialgraph")
        throw std::runtime_error("The edited Material Graph source is unavailable.");
    const auto& specification = m_AssetDatabase->Specification();
    WriteBytesAtomically(specification.ProjectRoot / specification.SourceDirectory / record->RelativePath, bytes);
}

void EditorWorkspaceLayer::OpenMaterialGraph(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::MaterialGraphAsset::StaticType() ||
        record->RelativePath.extension() != ".keirematerialgraph")
        throw std::invalid_argument("Only .keirematerialgraph assets can be opened in the Material Graph editor.");
    if (m_MaterialGraphDocument->Dirty() && m_MaterialGraphDocument->Asset() != asset)
        throw std::runtime_error("Save or discard the current Material Graph before opening another one.");
    m_SelectedAsset = asset;
    const auto& specification = m_AssetDatabase->Specification();
    const auto source = specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
    if (const auto context = m_MaterialGraphDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "Material Graph: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    if (++m_MaterialGraphDocumentRevision == 0)
        ++m_MaterialGraphDocumentRevision;
    m_MaterialGraphDocument->Open(asset, ReadBytes(source), m_MaterialGraphDocumentRevision, std::move(context));
    m_ActiveUndoContext = m_MaterialGraphDocument->UndoContext();
    m_MaterialGraphPanel->ResetTransientState();
    m_MaterialGraphPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_MaterialGraphPanel->Registration().SetVisible(true);
    m_MaterialGraphPanel->Registration().RequestFocus();
    if (m_InspectorPanel)
        m_InspectorPanel->Registration().SetVisible(true);
}

void EditorWorkspaceLayer::SaveMaterialGraph()
{
    if (!m_AssetDatabase || !m_MaterialGraphDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_MaterialGraphDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited Material Graph no longer exists.");
    m_MaterialGraphDocument->Save();
    if (!m_AssetOperations)
        throw std::runtime_error("The asset worker is unavailable for Material Graph import.");
    m_AssetOperations->QueueImport(KeireEditor::AssetOperationPriority::ExplicitAction,
                                   {.ReloadAsset = m_MaterialGraphDocument->Asset()});
    m_MaterialGraphPanel->SetMessage("Saved " + record->RelativePath.generic_string() +
                                     "; rebuilding and hot-reloading its runtime material...");
}
