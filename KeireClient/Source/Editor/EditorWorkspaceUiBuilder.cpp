#include "KeireClient/EditorWorkspaceLayer.h"

#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "KeireClient/Editor/AssetBrowserUtilities.h"
#include "KeireClient/Editor/AssetOperationService.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/UiBuilderDocument.h"
#include "KeireClient/Editor/UiBuilderLiveDraft.h"
#include "KeireClient/Editor/UiBuilderPanel.h"
#include "KeireClient/Editor/UiBuilderStyleSheetDocument.h"
#include "KeireInternal/Scripting/ManagedRuntimeUiServices.h"

#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
    [[nodiscard]] std::string DirtyReasonLabel(const Keire::AssetId stableId, const Keire::RuntimeUiDirtyReason reasons)
    {
        constexpr std::array names{
            std::pair{Keire::RuntimeUiDirtyReason::Hierarchy, "hierarchy"},
            std::pair{Keire::RuntimeUiDirtyReason::Style, "style"},
            std::pair{Keire::RuntimeUiDirtyReason::Content, "content"},
            std::pair{Keire::RuntimeUiDirtyReason::Control, "control"},
            std::pair{Keire::RuntimeUiDirtyReason::Visibility, "visibility"},
            std::pair{Keire::RuntimeUiDirtyReason::Interaction, "interaction"},
            std::pair{Keire::RuntimeUiDirtyReason::Transition, "transition"},
            std::pair{Keire::RuntimeUiDirtyReason::LayoutSettings, "layout-settings"},
            std::pair{Keire::RuntimeUiDirtyReason::Descendant, "descendant"},
        };
        std::ostringstream result;
        result << stableId.ToString() << ": ";
        bool first = true;
        const auto encoded = static_cast<std::uint16_t>(reasons);
        for (const auto& [reason, name] : names)
        {
            if ((encoded & static_cast<std::uint16_t>(reason)) == 0U)
                continue;
            if (!first)
                result << ", ";
            result << name;
            first = false;
        }
        return result.str();
    }

    [[nodiscard]] KeireEditor::UiBuilderLiveDebugEvent::Phase
    DebugEventPhase(const Keire::RuntimeUiEventPhase phase) noexcept
    {
        if (phase == Keire::RuntimeUiEventPhase::TrickleDown)
            return KeireEditor::UiBuilderLiveDebugEvent::Phase::Capture;
        if (phase == Keire::RuntimeUiEventPhase::BubbleUp)
            return KeireEditor::UiBuilderLiveDebugEvent::Phase::Bubble;
        return KeireEditor::UiBuilderLiveDebugEvent::Phase::Target;
    }

    [[nodiscard]] std::string SelectorTraceLabel(const Keire::ScenePresentationUiDocumentSelectorTrace& trace)
    {
        std::ostringstream result;
        result << trace.StableId.ToString() << "  " << trace.Selector << "  specificity " << trace.Specificity
               << "  source " << trace.SourceOrder;
        if (!trace.AppliedProperties.empty())
        {
            result << "  applied ";
            for (std::size_t index = 0; index < trace.AppliedProperties.size(); ++index)
            {
                if (index != 0)
                    result << ", ";
                result << trace.AppliedProperties[index];
            }
        }
        return result.str();
    }
} // namespace

bool EditorWorkspaceLayer::CreateAssetBrowserUiDocument(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("UI document name must be one non-empty path component.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keireui");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A UI document with that name already exists in this folder.");

        Keire::UiVisualTreeDefinition definition;
        definition.Name = std::string(name);
        definition.Root.StableId = Keire::AssetId::Generate();
        definition.Root.Type = Keire::UiVisualElementType::VisualElement;
        definition.Root.Name = "root";
        definition.Root.Classes = {"screen"};
        definition.Root.InlineStyles = {{"width", "100%"}, {"height", "100%"}};
        const auto source = Keire::UiVisualTreeAsset::EncodeSource(definition);
        m_AssetOperations->QueueCreateAsset(destination, source, {},
                                            {.FollowUp = KeireEditor::AssetOperationFollowUp::OpenUiBuilder,
                                             .UndoName = "Create UI Document",
                                             .Reason = "ui-document-creation"});
        m_AssetStatus = "Creating and opening " + destination.generic_string() + ".";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("UI document creation failed: ") + error.what());
        return false;
    }
}

bool EditorWorkspaceLayer::CreateAssetBrowserUiStyleSheet(const std::string_view name)
{
    if (!m_AssetDatabase || !m_AssetOperations)
        return false;
    try
    {
        if (m_AssetOperations->Busy())
            (void)m_AssetOperations->PreemptBackgroundImports();
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string_view::npos)
            throw std::invalid_argument("UI style sheet name must be one non-empty path component.");
        const auto directory = m_AssetBrowserPanel ? m_AssetBrowserPanel->CurrentFolder() : std::filesystem::path{};
        const auto destination = directory / (std::string(name) + ".keirestyle");
        if (m_AssetDatabase->Find(destination))
            throw std::runtime_error("A UI style sheet with that name already exists in this folder.");

        constexpr std::string_view templateSource = R"(@keire-style 1;

.root {
  width: 100%;
  height: 100%;
}
)";
        const auto bytes = std::as_bytes(std::span(templateSource));
        m_AssetOperations->QueueCreateAsset(destination, std::vector<std::byte>(bytes.begin(), bytes.end()), {},
                                            {.FollowUp = KeireEditor::AssetOperationFollowUp::Reveal,
                                             .UndoName = "Create UI Style Sheet",
                                             .Reason = "ui-style-sheet-creation"});
        m_AssetStatus = "Creating " + destination.generic_string() + ".";
        return true;
    }
    catch (const std::exception& error)
    {
        SetAssetError(std::string("UI style sheet creation failed: ") + error.what());
        return false;
    }
}

void EditorWorkspaceLayer::OpenAssetBrowserUiDocument(const Keire::AssetId asset) { OpenUiBuilder(asset); }

void EditorWorkspaceLayer::OpenAssetBrowserUiStyleSheet(const Keire::AssetId asset)
{
    OpenUiBuilderStyleSheet(asset);
    m_UiBuilderPanel->Registration().SetVisible(true);
    m_UiBuilderPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::OpenInspectorUiDocument(const Keire::AssetId asset) { OpenUiBuilder(asset); }

void EditorWorkspaceLayer::NotifyInspectorUiToolkitAssetAssigned(const Keire::AssetId asset) noexcept
{
    try
    {
        const auto assets = Owner().Assets();
        if (!asset || !assets || !assets->IsOpen() || !m_AssetDatabase)
            return;
        const auto& specification = m_AssetDatabase->Specification();
        std::set<Keire::AssetId> visited;
        std::string diagnostic;
        std::function<void(Keire::AssetId)> publish = [&](const Keire::AssetId current)
        {
            if (!current || !visited.insert(current).second)
                return;
            const auto record = m_AssetDatabase->Find(current);
            if (!record)
                return;
            const bool supported = record->Type == Keire::UiVisualTreeAsset::StaticType() ||
                                   record->Type == Keire::UiStyleSheetAsset::StaticType() ||
                                   record->Type == Keire::UiPanelSettingsAsset::StaticType();
            if (!supported)
                return;
            for (const auto dependency : record->Dependencies)
                publish(dependency);
            const auto source = specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
            const auto bytes = KeireEditor::Detail::ReadBytes(source, "UI authoring asset");
            if (!KeireEditor::PublishUiToolkitAuthoringAsset(assets, current, record->Type, bytes, diagnostic))
                throw std::runtime_error(diagnostic);
        };
        publish(asset);
    }
    catch (const std::exception& error)
    {
        try
        {
            AddConsoleMessage("UI Toolkit",
                              std::string("Immediate Game View publication was deferred: ") + error.what(),
                              m_Theme.Warning, Keire::LogLevel::Warn);
        }
        catch (...)
        {
        }
    }
    catch (...)
    {
    }
}

void EditorWorkspaceLayer::OpenSceneViewportUiDocument(const Keire::AssetId asset)
{
    try
    {
        OpenUiBuilder(asset);
    }
    catch (const std::exception& error)
    {
        ReportUiBuilderError(error.what());
    }
}

void EditorWorkspaceLayer::OpenUiBuilder(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::UiVisualTreeAsset::StaticType() ||
        KeireEditor::ResolveAssetBrowserOpenAction(record->RelativePath) !=
            KeireEditor::AssetBrowserOpenAction::UiDocument)
        throw std::invalid_argument("Only imported .keireui assets can be opened in UI Builder.");
    if (m_UiBuilderDocument->Dirty() && m_UiBuilderDocument->Asset() != asset)
        throw std::runtime_error("Save or revert the current UI document before opening another one.");
    if (m_UiBuilderDocument->Asset() != asset)
        m_UiBuilderLiveDraft->Close();

    const auto& specification = m_AssetDatabase->Specification();
    const auto source = specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
    const auto bytes = KeireEditor::Detail::ReadBytes(source, "UI document");
    auto definition = Keire::UiVisualTreeAsset::ParseSource(bytes);
    if (const auto context = m_UiBuilderDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "UI Builder: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    if (++m_UiBuilderDocumentRevision == 0)
        ++m_UiBuilderDocumentRevision;
    m_UiBuilderDocument->Open(asset, std::move(definition), m_UiBuilderDocumentRevision, source, std::move(context));
    NotifyInspectorUiToolkitAssetAssigned(asset);
    m_SelectedAsset = asset;
    m_ActiveUndoContext = m_UiBuilderDocument->UndoContext();
    m_UiBuilderPanel->ResetTransientState();
    m_UiBuilderPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_UiBuilderPanel->Registration().SetVisible(true);
    m_UiBuilderPanel->Registration().RequestFocus();
}

KeireEditor::UiBuilderDocument& EditorWorkspaceLayer::UiBuilderState() noexcept { return *m_UiBuilderDocument; }

const Keire::UiThemeDefinition& EditorWorkspaceLayer::UiBuilderTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetSystem> EditorWorkspaceLayer::UiBuilderAssets() const noexcept { return Owner().Assets(); }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::UiBuilderAssetRecords() const noexcept
{
    return m_AssetRecords;
}

void EditorWorkspaceLayer::RevealUiBuilderAsset(const Keire::AssetId asset)
{
    if (m_AssetBrowserPanel && asset)
        m_AssetBrowserPanel->RevealAsset(asset);
}

std::optional<Keire::UiSize> EditorWorkspaceLayer::UiBuilderGameViewSize() const noexcept
{
    if (m_GameRenderView && m_GameRenderView->Surface() && m_GameRenderView->Surface()->Available())
    {
        const auto surface = m_GameRenderView->Surface();
        if (surface->Width() != 0 && surface->Height() != 0)
            return Keire::UiSize{static_cast<float>(surface->Width()), static_cast<float>(surface->Height())};
    }
    const auto size = m_GameViewportRect.Size();
    return size.Width > 0.0F && size.Height > 0.0F ? std::optional(size) : std::nullopt;
}

KeireEditor::UiBuilderStyleSheetDocument& EditorWorkspaceLayer::UiBuilderStyleSheetState() noexcept
{
    return *m_UiBuilderStyleSheetDocument;
}

void EditorWorkspaceLayer::ActivateUiBuilderHistory() noexcept
{
    if (m_UiBuilderDocument->UndoContext() && m_UiBuilderDocument->UndoContext()->IsOpen())
        m_ActiveUndoContext = m_UiBuilderDocument->UndoContext();
}

void EditorWorkspaceLayer::ActivateUiBuilderStyleSheetHistory() noexcept
{
    if (m_UiBuilderStyleSheetDocument->UndoContext() && m_UiBuilderStyleSheetDocument->UndoContext()->IsOpen())
        m_ActiveUndoContext = m_UiBuilderStyleSheetDocument->UndoContext();
}

void EditorWorkspaceLayer::OpenUiBuilderStyleSheet(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::UiStyleSheetAsset::StaticType() ||
        record->RelativePath.extension() != ".keirestyle")
    {
        throw std::invalid_argument("Only imported .keirestyle assets can be edited in UI Builder.");
    }
    if (m_UiBuilderStyleSheetDocument->Dirty() && m_UiBuilderStyleSheetDocument->Asset() != asset)
        throw std::runtime_error("Save or revert the current UI style sheet before opening another one.");

    const auto& specification = m_AssetDatabase->Specification();
    const auto source = specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
    const auto bytes = KeireEditor::Detail::ReadBytes(source, "UI style sheet");
    auto definition = Keire::UiStyleSheetAsset::ParseSource(bytes);
    if (const auto context = m_UiBuilderStyleSheetDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context =
            undo->CreateContext({.Name = "UI Style: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    if (++m_UiBuilderStyleSheetRevision == 0)
        ++m_UiBuilderStyleSheetRevision;
    m_UiBuilderStyleSheetDocument->Open(asset, std::move(definition), m_UiBuilderStyleSheetRevision, source,
                                        std::move(context));
    NotifyInspectorUiToolkitAssetAssigned(asset);
    m_ActiveUndoContext = m_UiBuilderStyleSheetDocument->UndoContext();
    m_UiBuilderPanel->SetMessage("Editing " + record->RelativePath.generic_string() + ".");
}

void EditorWorkspaceLayer::SaveUiBuilderDocument()
{
    if (!m_AssetDatabase || !m_UiBuilderDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_UiBuilderDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited UI document no longer exists.");
    const bool dirty = m_UiBuilderDocument->Dirty();
    m_UiBuilderDocument->Save();
    m_UiBuilderLiveDraft->Commit(Owner().Assets(), m_UiBuilderDocument->Asset(), m_UiBuilderDocument->Definition());
    ImportAssets(KeireEditor::AssetOperationPriority::AutomaticRefresh);
    m_UiBuilderPanel->SetMessage(dirty ? "Saved " + record->RelativePath.generic_string() + "; importing changes."
                                       : record->RelativePath.generic_string() + " is already saved.");
}

void EditorWorkspaceLayer::ReloadUiBuilderDocument()
{
    m_UiBuilderLiveDraft->Close();
    m_UiBuilderDocument->ReloadFromSource(true);
}

void EditorWorkspaceLayer::SaveUiBuilderStyleSheet()
{
    if (!m_AssetDatabase || !m_UiBuilderStyleSheetDocument->Asset())
        return;
    m_UiBuilderStyleSheetDocument->Save();
    ImportAssets(KeireEditor::AssetOperationPriority::AutomaticRefresh);
}

void EditorWorkspaceLayer::ReloadUiBuilderStyleSheet() { m_UiBuilderStyleSheetDocument->ReloadFromSource(true); }

void EditorWorkspaceLayer::SynchronizeUiBuilderLiveDraft() noexcept
{
    const auto playSession =
        m_SceneDocument ? m_SceneDocument->PlaySession() : Keire::Ref<Keire::SceneRuntimeSession>{};
    const bool playActive = m_PlayRuntimeWorld && m_PlayRuntimeWorld->IsOpen() && playSession &&
                            playSession->State() != Keire::ScenePlayState::Stopped;
    m_UiBuilderLiveDraft->Synchronize(Owner().Assets(), playActive, m_UiBuilderDocument->Asset(),
                                      m_UiBuilderDocument->Generation(), m_UiBuilderDocument->Dirty(),
                                      m_UiBuilderDocument->Definition());
    if (m_UiBuilderLiveDraftDiagnostic == m_UiBuilderLiveDraft->Diagnostic())
        return;
    m_UiBuilderLiveDraftDiagnostic = m_UiBuilderLiveDraft->Diagnostic();
    if (!m_UiBuilderLiveDraftDiagnostic.empty())
        m_UiBuilderPanel->SetMessage(m_UiBuilderLiveDraftDiagnostic);
}

void EditorWorkspaceLayer::ReportUiBuilderError(std::string message) noexcept
{
    ReportError("UI Builder", std::move(message));
}

KeireEditor::UiBuilderLiveDebugCapture EditorWorkspaceLayer::CaptureUiBuilderLiveDebug(const Keire::AssetId visualTree)
{
    if (!visualTree)
        return {{}, "Open an imported UI document before attaching the live debugger."};
    if (!m_PlayRuntimeWorld || !m_PlayRuntimeWorld->IsOpen())
        return {{}, "Enter Play Mode to attach the live debugger."};

    try
    {
        auto snapshot = std::make_shared<KeireEditor::UiBuilderLiveDebugSnapshot>();
        snapshot->VisualTree = visualTree;
        if (++m_UiBuilderLiveDebugSequence == 0)
            ++m_UiBuilderLiveDebugSequence;
        snapshot->Sequence = m_UiBuilderLiveDebugSequence;

        for (const auto& session : m_PlayRuntimeWorld->Sessions())
        {
            const auto scene = session ? session->RuntimeScene() : Keire::Ref<Keire::Scene>{};
            const auto presentation = session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
            if (!scene || !presentation)
                continue;

            for (const auto& entity : scene->Query<Keire::UiDocumentComponent>())
            {
                const auto component = entity.GetComponent<Keire::UiDocumentComponent>();
                if (!component || component->VisualTree() != visualTree)
                    continue;
                const auto runtime = presentation->UiDocumentDebugSnapshot(entity.Id());
                if (!runtime || runtime->VisualTree != visualTree)
                    continue;

                KeireEditor::UiBuilderLiveDebugDocument document;
                document.Entity = runtime->Document;
                document.DocumentGeneration = runtime->DocumentGeneration;
                document.PresentationStatistics = runtime->Statistics;
                document.FocusedElement = runtime->Focused;
                for (std::size_t index = 0; index < document.PresentationPointerCaptures.size(); ++index)
                    document.PresentationPointerCaptures[index] =
                        m_GameRuntimeUiPointer.PointerCaptures[index] == presentation;
                document.Elements.reserve(runtime->Elements.size());
                for (const auto& element : runtime->Elements)
                {
                    document.Elements.push_back({element.StableId, element.State});
                    if (element.State.DirtyReasons != Keire::RuntimeUiDirtyReason::None)
                        snapshot->DirtyReasons.push_back(
                            DirtyReasonLabel(element.StableId, element.State.DirtyReasons));
                }
                document.PendingTargetEvents.reserve(runtime->PendingTargetEvents.size());
                for (const auto& event : runtime->PendingTargetEvents)
                {
                    document.PendingTargetEvents.push_back(
                        {.Type = event.Type,
                         .PropagationPhase = KeireEditor::UiBuilderLiveDebugEvent::Phase::Target,
                         .Target = event.Target,
                         .PointerX = event.PointerX,
                         .PointerY = event.PointerY,
                         .Button = event.Button});
                }
                document.EventTrace.reserve(runtime->EventRouteHistory.size());
                for (const auto& event : runtime->EventRouteHistory)
                {
                    document.EventTrace.push_back({.Type = event.Type,
                                                   .PropagationPhase = DebugEventPhase(event.Phase),
                                                   .Sequence = event.Sequence,
                                                   .Target = event.Target,
                                                   .CurrentTarget = event.CurrentTarget,
                                                   .PointerX = event.PointerX,
                                                   .PointerY = event.PointerY,
                                                   .Button = event.Button});
                }
                for (const auto& selector : runtime->SelectorTrace)
                    snapshot->SelectorPrecedence.push_back(SelectorTraceLabel(selector));
                snapshot->StyleMilliseconds =
                    std::max(snapshot->StyleMilliseconds.value_or(0.0F), runtime->Statistics.StyleMilliseconds);
                snapshot->LayoutMilliseconds =
                    std::max(snapshot->LayoutMilliseconds.value_or(0.0F), runtime->Statistics.LayoutMilliseconds);
                snapshot->Documents.push_back(std::move(document));
            }
        }

        if (snapshot->Documents.empty())
            return {{}, "No active Play presentation contains this UI document."};
        snapshot->DirtyReasonsAvailable = true;
        snapshot->SelectorPrecedenceAvailable = true;
        snapshot->EventPropagationTraceAvailable = true;
        if (const auto renderer = Owner().Renderer())
        {
            const auto statistics = renderer->Statistics().RuntimeUiRenderer;
            snapshot->VertexCount = statistics.RenderedVertices;
            snapshot->AtlasTextureCount = statistics.GlyphAtlasEntries + statistics.ImageAtlasEntries;
            snapshot->AtlasBytes = statistics.GlyphAtlasBytes + statistics.ImageAtlasBytes;
            snapshot->RepaintMilliseconds = statistics.RepaintCpuMilliseconds;
        }
        return {std::move(snapshot), {}};
    }
    catch (const std::exception& error)
    {
        return {{}, std::string("Live UI capture failed: ") + error.what()};
    }
    catch (...)
    {
        return {{}, "Live UI capture failed with an unknown error."};
    }
}

void EditorWorkspaceLayer::SetUiBuilderLivePicking(const Keire::AssetId visualTree, const bool enabled) noexcept
{
    if (enabled && visualTree)
    {
        m_UiBuilderLivePickVisualTree = visualTree;
        m_UiBuilderLivePickedElement.reset();
        m_UiBuilderLivePicking = true;
        return;
    }
    if (!visualTree || visualTree == m_UiBuilderLivePickVisualTree)
    {
        m_UiBuilderLivePickVisualTree = {};
        m_UiBuilderLivePickedElement.reset();
        m_UiBuilderLivePicking = false;
    }
}

std::optional<Keire::AssetId> EditorWorkspaceLayer::ConsumeUiBuilderLivePick(const Keire::AssetId visualTree) noexcept
{
    if (!visualTree || visualTree != m_UiBuilderLivePickVisualTree)
        return std::nullopt;
    return std::exchange(m_UiBuilderLivePickedElement, std::nullopt);
}

bool EditorWorkspaceLayer::TryPickUiBuilderLiveElement(
    const std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations, const Keire::UiItemRect& viewport,
    const Keire::UiPointerState& pointer) noexcept
{
    if (!m_UiBuilderLivePicking || !m_UiBuilderLivePickVisualTree || !pointer.LeftPressed ||
        !viewport.Contains(pointer.Position))
        return false;

    KeireEditor::CancelRuntimeUiPointer(presentations, m_GameRuntimeUiPointer);
    const float x = pointer.Position.X - viewport.Minimum.X;
    const float y = pointer.Position.Y - viewport.Minimum.Y;
    for (auto current = presentations.rbegin(); current != presentations.rend(); ++current)
    {
        const auto& presentation = *current;
        if (!presentation)
            continue;
        const auto hit = presentation->HitTestUiDocument(x, y);
        if (!hit)
            continue;
        try
        {
            const auto document = presentation->UiDocumentDebugSnapshot(hit->Document);
            if (!document || document->VisualTree != m_UiBuilderLivePickVisualTree)
                continue;
            m_UiBuilderLivePickedElement = hit->StableId;
            m_UiBuilderLivePicking = false;
            return true;
        }
        catch (...)
        {
        }
    }
    return true;
}

std::optional<Keire::ManagedUiDocumentElement>
EditorWorkspaceLayer::ManagedUiDocumentRoot(const Keire::AssetId document) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::ManagedUiDocumentRoot(
        session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{}, document);
}

std::optional<Keire::ManagedUiDocumentElement>
EditorWorkspaceLayer::FindManagedUiDocumentElement(const Keire::AssetId document,
                                                   const Keire::AssetId stableId) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::FindManagedUiDocumentElement(
        session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{}, document, stableId);
}

std::optional<Keire::ManagedUiDocumentElement>
EditorWorkspaceLayer::FindManagedUiDocumentElement(const Keire::AssetId document, const std::string_view name) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::FindManagedUiDocumentElement(
        session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{}, document, name);
}

bool EditorWorkspaceLayer::ManagedUiDocumentElementAlive(const Keire::AssetId document,
                                                         const std::uint64_t documentGeneration,
                                                         const std::uint64_t element) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::ManagedUiDocumentElementAlive(session ? session->Presentation()
                                                                : Keire::Ref<Keire::ScenePresentationRuntime>{},
                                                        document, documentGeneration, element);
}

std::optional<std::string> EditorWorkspaceLayer::ReadManagedUiDocumentElementText(
    const Keire::AssetId document, const std::uint64_t documentGeneration, const std::uint64_t element) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::ReadManagedUiDocumentElementText(session ? session->Presentation()
                                                                   : Keire::Ref<Keire::ScenePresentationRuntime>{},
                                                           document, documentGeneration, element);
}

bool EditorWorkspaceLayer::SetManagedUiDocumentElementText(const Keire::AssetId document,
                                                           const std::uint64_t documentGeneration,
                                                           const std::uint64_t element,
                                                           const std::string_view text) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::SetManagedUiDocumentElementText(session ? session->Presentation()
                                                                  : Keire::Ref<Keire::ScenePresentationRuntime>{},
                                                          document, documentGeneration, element, text);
}

std::optional<float> EditorWorkspaceLayer::ReadManagedUiDocumentElementValue(const Keire::AssetId document,
                                                                             const std::uint64_t documentGeneration,
                                                                             const std::uint64_t element) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::ReadManagedUiDocumentElementValue(session ? session->Presentation()
                                                                    : Keire::Ref<Keire::ScenePresentationRuntime>{},
                                                            document, documentGeneration, element);
}

bool EditorWorkspaceLayer::SetManagedUiDocumentElementValue(const Keire::AssetId document,
                                                            const std::uint64_t documentGeneration,
                                                            const std::uint64_t element, const float value) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::SetManagedUiDocumentElementValue(session ? session->Presentation()
                                                                   : Keire::Ref<Keire::ScenePresentationRuntime>{},
                                                           document, documentGeneration, element, value);
}

std::optional<bool> EditorWorkspaceLayer::ReadManagedUiDocumentElementFlag(
    const Keire::AssetId document, const std::uint64_t documentGeneration, const std::uint64_t element,
    const Keire::ManagedUiDocumentFlag property) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::ReadManagedUiDocumentElementFlag(session ? session->Presentation()
                                                                   : Keire::Ref<Keire::ScenePresentationRuntime>{},
                                                           document, documentGeneration, element, property);
}

bool EditorWorkspaceLayer::SetManagedUiDocumentElementFlag(const Keire::AssetId document,
                                                           const std::uint64_t documentGeneration,
                                                           const std::uint64_t element,
                                                           const Keire::ManagedUiDocumentFlag property,
                                                           const bool value) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::SetManagedUiDocumentElementFlag(session ? session->Presentation()
                                                                  : Keire::Ref<Keire::ScenePresentationRuntime>{},
                                                          document, documentGeneration, element, property, value);
}

bool EditorWorkspaceLayer::ConsumeManagedUiDocumentElementEvent(const Keire::AssetId document,
                                                                const std::uint64_t documentGeneration,
                                                                const std::uint64_t element,
                                                                const Keire::RuntimeUiEventType type) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::ConsumeManagedUiDocumentElementEvent(session ? session->Presentation()
                                                                       : Keire::Ref<Keire::ScenePresentationRuntime>{},
                                                               document, documentGeneration, element, type);
}

bool EditorWorkspaceLayer::FocusManagedUiDocumentElement(const Keire::AssetId document,
                                                         const std::uint64_t documentGeneration,
                                                         const std::uint64_t element) noexcept
{
    const auto session = ManagedRuntimeSession(document);
    return Keire::Detail::FocusManagedUiDocumentElement(session ? session->Presentation()
                                                                : Keire::Ref<Keire::ScenePresentationRuntime>{},
                                                        document, documentGeneration, element);
}
