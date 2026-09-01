#include "KeireClient/EditorWorkspaceLayer.h"

#include "Keire/Assets/BuiltinAssetRegistry.h"

#include "KeireClient/Editor/AnimatorControllerDocument.h"
#include "KeireClient/Editor/AnimatorControllerPanel.h"
#include "KeireClient/Editor/AssetBrowserPanel.h"
#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/AssetPackageAuthoring.h"
#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/AudioMixerPanel.h"
#include "KeireClient/Editor/ConsolePanel.h"
#include "KeireClient/Editor/DiagnosticsPanel.h"
#include "KeireClient/Editor/EditModeVfxPreview.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/EditorCommandRouter.h"
#include "KeireClient/Editor/ExternalAssetImportController.h"
#include "KeireClient/Editor/ExternalEditorProfiles.h"
#include "KeireClient/Editor/InputActionsDocument.h"
#include "KeireClient/Editor/MaterialDocument.h"
#include "KeireClient/Editor/MaterialInspectorPanel.h"
#include "KeireClient/Editor/PrefabAuthoring.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/PropertyDrawerRegistry.h"
#include "KeireClient/Editor/SceneCameraController.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/SceneGizmoController.h"
#include "KeireClient/Editor/ScenePicker.h"
#include "KeireClient/Editor/ScenePlayChanges.h"
#include "KeireClient/Editor/ScenePlayChangesPanel.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphPublication.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"
#include "KeireClient/Editor/ViewportAssetDropRouter.h"
#include "KeireInternal/Assets/AssetDatabaseWorkerAccess.h"
#include "KeireInternal/EditorCameraController.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using KeireEditor::Detail::ReadBytes;
    using KeireEditor::Detail::SameOrChild;
    using KeireEditor::Detail::WriteBytesAtomically;
} // namespace

KeireEditor::ShaderGraphDocument& EditorWorkspaceLayer::ShaderGraphState() noexcept { return *m_ShaderGraphDocument; }

const Keire::UiThemeDefinition& EditorWorkspaceLayer::ShaderGraphTheme() const noexcept { return m_Theme; }

void EditorWorkspaceLayer::SaveShaderGraphDocument() { SaveShaderGraph(); }

void EditorWorkspaceLayer::UndoShaderGraphEdit() { (void)m_ShaderGraphDocument->Undo(); }

void EditorWorkspaceLayer::RedoShaderGraphEdit() { (void)m_ShaderGraphDocument->Redo(); }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::ShaderGraphAssetRecords() const noexcept
{
    return m_AssetRecords;
}

Keire::Ref<const Keire::MeshAsset> EditorWorkspaceLayer::ResolveShaderGraphPreviewMesh(const Keire::AssetId asset)
{
    if (auto builtin = Keire::MeshAsset::ResolveBuiltin(asset))
        return builtin;
    const auto assets = Owner().Assets();
    if (!asset || !assets)
        return {};
    return assets->Load<Keire::MeshAsset>(asset, Keire::AssetPriority::High).TryGetLoaded();
}

std::optional<Keire::ShaderGraphDefinition>
EditorWorkspaceLayer::ResolveShaderGraphFunction(const Keire::AssetId asset) const
{
    return ResolveReusableGraph(asset);
}

void EditorWorkspaceLayer::ApplyShaderGraphDevelopmentRevision(
    const Keire::AssetId asset, const Keire::ShaderGraphDefinition& definition,
    const Keire::ShaderGraphCompilation& compilation,
    const std::span<const Keire::Ref<Keire::ShaderAsset>> developmentShaders) noexcept
{
    try
    {
        const auto assets = Owner().Assets();
        const auto record = m_AssetDatabase ? m_AssetDatabase->Find(asset) : std::nullopt;
        if (!assets || !record || !compilation.Succeeded() || compilation.Variants.empty())
            return;

        std::vector<Keire::AssetId> shaderAssets;
        Keire::AssetId materialAsset;
        for (const auto subAsset : record->SubAssets)
        {
            const auto type = assets->TryGetType(subAsset);
            if (type && *type == Keire::ShaderAsset::StaticType())
                shaderAssets.push_back(subAsset);
            else if (type && *type == Keire::MaterialAsset::StaticType() && !materialAsset)
                materialAsset = subAsset;
        }
        if (!materialAsset || shaderAssets.size() != compilation.Variants.size())
            return;
        if (!developmentShaders.empty() && developmentShaders.size() != shaderAssets.size())
            throw std::logic_error("A live Shader Graph shader revision does not match its catalog variants.");

        for (std::size_t index = 0; index < developmentShaders.size(); ++index)
        {
            if (!developmentShaders[index] ||
                !assets->PublishDevelopmentAsset(shaderAssets[index], developmentShaders[index]))
            {
                throw std::runtime_error("A live Shader Graph shader variant could not be published.");
            }
        }

        Keire::ShaderGraphInstanceDefinition defaults;
        defaults.Parent = asset;
        const std::array ancestry{defaults};
        const auto resolved = Keire::ResolveShaderGraphInstance(definition, ancestry);
        const auto material = Keire::BakeShaderGraphInstance(
            definition, resolved,
            [&compilation, &shaderAssets](const std::span<const std::string> keywords)
            {
                for (std::size_t index = 0; index < compilation.Variants.size(); ++index)
                    if (std::ranges::equal(compilation.Variants[index].Keywords, keywords))
                        return shaderAssets[index];
                return Keire::AssetId{};
            });
        if (!assets->PublishDevelopmentAsset(materialAsset, Keire::CreateRef<Keire::MaterialAsset>(material)))
            throw std::runtime_error("The live Shader Graph material revision could not be published.");
    }
    catch (const std::exception& error)
    {
        KEIRE_CLIENT_ERROR("[Shader Graph] Live scene apply failed for {}: {}", asset.ToString(), error.what());
    }
    catch (...)
    {
        KEIRE_CLIENT_ERROR("[Shader Graph] Live scene apply failed for {}.", asset.ToString());
    }
}

void EditorWorkspaceLayer::RevealShaderGraphAsset(const Keire::AssetId asset)
{
    if (!asset || !m_AssetBrowserPanel)
        return;
    m_SelectedAsset = asset;
    m_AssetBrowserPanel->RevealAsset(asset);
    m_AssetBrowserPanel->Registration().SetVisible(true);
    m_AssetBrowserPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::ReportShaderGraphError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::PersistShaderGraph(const Keire::AssetId asset, const std::span<const std::byte> bytes)
{
    if (!m_AssetDatabase)
        throw std::runtime_error("The Asset Database is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record)
        throw std::runtime_error("The edited graph source is unavailable.");
    if (record->Type != Keire::ShaderGraphAsset::StaticType())
    {
        const bool reusable = record->Type == Keire::ShaderSubgraphAsset::StaticType() ||
                              record->Type == Keire::MaterialFunctionAsset::StaticType() ||
                              record->Type == Keire::ShaderFunctionAsset::StaticType() ||
                              record->Type == Keire::MaterialLayerAsset::StaticType() ||
                              record->Type == Keire::MaterialLayerBlendAsset::StaticType();
        if (!reusable || m_ShaderGraphDocument->Asset() != asset || !m_ShaderGraphDocument->Publishable())
            throw std::runtime_error("The edited reusable graph is unavailable or invalid.");
        const auto& specification = m_AssetDatabase->Specification();
        WriteBytesAtomically(specification.ProjectRoot / specification.SourceDirectory / record->RelativePath, bytes);
        return;
    }
    if (record->RelativePath.extension() != ".keireshadergraph" || m_ShaderGraphDocument->Asset() != asset ||
        !m_ShaderGraphDocument->Compilation().Succeeded() || m_ShaderGraphDocument->Compilation().Variants.empty())
        throw std::runtime_error("The Shader Graph has no publishable generated shader variants.");

    const auto& specification = m_AssetDatabase->Specification();
    KeireEditor::PublishShaderGraph({.ProjectRoot = specification.ProjectRoot,
                                     .SourceDirectory = specification.SourceDirectory,
                                     .GraphRelativePath = record->RelativePath,
                                     .Asset = asset,
                                     .Variants = m_ShaderGraphDocument->Compilation().Variants,
                                     .GraphBytes = bytes});
}

void EditorWorkspaceLayer::OpenShaderGraph(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    const bool reusable = record && (record->Type == Keire::ShaderSubgraphAsset::StaticType() ||
                                     record->Type == Keire::MaterialFunctionAsset::StaticType() ||
                                     record->Type == Keire::ShaderFunctionAsset::StaticType() ||
                                     record->Type == Keire::MaterialLayerAsset::StaticType() ||
                                     record->Type == Keire::MaterialLayerBlendAsset::StaticType());
    if (!record || (record->Type != Keire::ShaderGraphAsset::StaticType() && !reusable))
        throw std::invalid_argument("Only Shader Graph, function, and material-layer assets can be opened here.");

    m_SelectedAsset = asset;
    const auto& specification = m_AssetDatabase->Specification();
    const auto source = specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
    const auto bytes = ReadBytes(source);
    if (const auto context = m_ShaderGraphDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "Shader Graph: " + record->RelativePath.stem().string(), .MaximumCommands = 128});

    Keire::ShaderGraphCompileOptions options;
    options.GeneratedSource =
        specification.SourceDirectory / "Generated" / "ShaderGraphs" / asset.ToString() / "ShaderGraph.hlsl";
    options.ResolveFunction = [this](const Keire::AssetId dependency) { return ResolveReusableGraph(dependency); };
    const auto allowedRoot = specification.SourceDirectory.lexically_normal();
    const auto projectRoot = specification.ProjectRoot;
    options.ReadInclude = [allowedRoot,
                           projectRoot](const std::filesystem::path& requested) -> std::optional<std::string>
    {
        const auto relative = requested.lexically_normal();
        if (relative.empty() || relative.is_absolute() || !SameOrChild(allowedRoot, relative))
            return std::nullopt;
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(projectRoot / allowedRoot, error);
        if (error)
            return std::nullopt;
        const auto path = std::filesystem::weakly_canonical(projectRoot / relative, error);
        if (error || !SameOrChild(root, path) || !std::filesystem::is_regular_file(path, error) || error)
            return std::nullopt;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > std::uintmax_t{1024} * 1024U)
            return std::nullopt;
        try
        {
            const auto include = ReadBytes(path);
            return std::string(reinterpret_cast<const char*>(include.data()), include.size());
        }
        catch (...)
        {
            return std::nullopt;
        }
    };
    m_ShaderGraphDocument->SetCompileOptions(std::move(options));
    if (++m_ShaderGraphDocumentRevision == 0)
        ++m_ShaderGraphDocumentRevision;
    if (record->Type == Keire::ShaderGraphAsset::StaticType())
        m_ShaderGraphDocument->Open(asset, bytes, m_ShaderGraphDocumentRevision, std::move(context));
    else
    {
        Keire::GraphFunctionDefinition definition;
        if (record->Type == Keire::ShaderSubgraphAsset::StaticType())
            definition = Keire::ShaderSubgraphAsset::DecodeSource(bytes);
        else if (record->Type == Keire::MaterialFunctionAsset::StaticType())
            definition = Keire::MaterialFunctionAsset::DecodeSource(bytes);
        else if (record->Type == Keire::ShaderFunctionAsset::StaticType())
            definition = Keire::ShaderFunctionAsset::DecodeSource(bytes);
        else if (record->Type == Keire::MaterialLayerAsset::StaticType())
            definition = Keire::MaterialLayerAsset::DecodeSource(bytes);
        else
            definition = Keire::MaterialLayerBlendAsset::DecodeSource(bytes);
        m_ShaderGraphDocument->Open(asset, std::move(definition), m_ShaderGraphDocumentRevision, std::move(context));
    }
    m_ActiveUndoContext = m_ShaderGraphDocument->UndoContext();
    m_ShaderGraphPanel->ResetTransientState();
    m_ShaderGraphPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_ShaderGraphPanel->Registration().SetVisible(true);
    m_ShaderGraphPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::SaveShaderGraph()
{
    if (!m_AssetDatabase || !m_ShaderGraphDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_ShaderGraphDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited Shader Graph no longer exists.");
    m_ShaderGraphDocument->Save();
    if (!m_AssetOperations)
        throw std::runtime_error("The asset worker is unavailable for Shader Graph compilation.");
    m_AssetOperations->QueueAssetImport(m_ShaderGraphDocument->Asset(),
                                        KeireEditor::AssetOperationPriority::ExplicitAction,
                                        {.ReloadAsset = m_ShaderGraphDocument->Asset()});
    m_ShaderGraphPanel->SetMessage(m_ShaderGraphDocument->ReusableGraph()
                                       ? "Saved " + record->RelativePath.generic_string() +
                                             "; recompiling dependent graphs..."
                                       : "Saved " + record->RelativePath.generic_string() +
                                             "; compiling and hot-reloading its runtime shader variants...");
}

void EditorWorkspaceLayer::OpenAnimationGraph(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::AnimationGraphAsset::StaticType() ||
        record->RelativePath.extension() != ".keireanimgraph")
    {
        throw std::invalid_argument("Only .keireanimgraph assets can be opened in the Animator Controller editor.");
    }
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    auto definition = Keire::AnimationGraphAsset::Decode(ReadBytes(source))->Definition();
    if (const auto context = m_AnimatorControllerDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
    {
        context = undo->CreateContext(
            {.Name = "Animator Controller: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    }
    m_AnimatorControllerDocument->Open(asset, std::move(definition), std::move(context), source);
    m_ActiveUndoContext = m_AnimatorControllerDocument->UndoContext();
    m_AnimatorControllerPanel->ResetTransientState();
    m_AnimatorControllerPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_AnimatorControllerPanel->Registration().SetVisible(true);
    m_AnimatorControllerPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::SaveAnimationGraph()
{
    if (!m_AssetDatabase || !m_AnimatorControllerDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_AnimatorControllerDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited Animator Controller no longer exists.");
    m_AnimatorControllerDocument->Save();
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_AnimatorControllerDocument->Asset());
    m_AnimatorControllerPanel->SetMessage("Saved and imported " + record->RelativePath.generic_string() + ".");
}
