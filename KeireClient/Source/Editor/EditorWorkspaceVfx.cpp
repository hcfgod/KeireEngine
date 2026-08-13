#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditModeVfxPreview.h"
#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    using KeireEditor::Detail::ReadBytes;
    using KeireEditor::Detail::RequireCompiledVfxSystems;
    using KeireEditor::Detail::WriteBytesAtomically;
} // namespace

KeireEditor::VfxEffectDocument& EditorWorkspaceLayer::VfxEffectState() noexcept { return *m_VfxEffectDocument; }

const Keire::UiThemeDefinition& EditorWorkspaceLayer::VfxEffectTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::VfxEffectDatabase() const noexcept { return m_AssetDatabase; }

std::span<const Keire::AssetSourceRecord> EditorWorkspaceLayer::VfxEffectAssetRecords() const noexcept
{
    return m_AssetRecords;
}

std::string_view EditorWorkspaceLayer::VfxEffectPreviewDiagnostic() const noexcept
{
    return m_VfxEffectPreviewDiagnostic;
}

KeireEditor::VfxEffectPreviewStatus EditorWorkspaceLayer::VfxEffectPreviewState() const noexcept
{
    KeireEditor::VfxEffectPreviewStatus result;
    result.Active = m_VfxEffectPreviewWorld && m_VfxEffectPreviewHandle &&
                    m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle);
    result.Paused = m_VfxEffectPreviewPaused;
    result.AutoRestart = m_VfxEffectPreviewAutoRestart;
    result.Backend = m_VfxEffectPreviewBackend;
    result.Speed = m_VfxEffectPreviewSpeed;
    if (m_VfxEffectPreviewWorld && m_VfxEffectPreviewHandle)
    {
        const auto snapshot = m_VfxEffectPreviewWorld->CaptureDebugSnapshot();
        for (std::size_t index = 0; index < snapshot.EffectCount; ++index)
        {
            if (snapshot.Effects[index].Handle != m_VfxEffectPreviewHandle)
                continue;
            result.ActiveParticles = snapshot.Effects[index].ActiveParticles;
            result.DroppedParticles = snapshot.Effects[index].DroppedParticles;
            break;
        }
    }
    return result;
}

void EditorWorkspaceLayer::ActivateVfxEffectHistory() noexcept
{
    m_ActiveUndoContext = m_VfxEffectDocument->UndoContext();
}

void EditorWorkspaceLayer::SaveVfxEffectDocument() { SaveVfxEffect(); }

void EditorWorkspaceLayer::DiscardVfxEffectDocument()
{
    m_VfxEffectDocument->Discard();
    m_VfxEffectPanel->SetMessage("Discarded unsaved VFX Effect changes.");
}

void EditorWorkspaceLayer::ReloadVfxEffectDocument(const Keire::AssetId asset)
{
    if (!m_AssetDatabase || asset != m_VfxEffectDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::VfxEffectAsset::StaticType() ||
        record->RelativePath.extension() != ".keirevfx")
        throw std::invalid_argument("Only .keirevfx assets can be reloaded in the VFX Effect editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    if (++m_VfxEffectDocumentRevision == 0)
        ++m_VfxEffectDocumentRevision;
    const auto result = m_VfxEffectDocument->Reload(ReadBytes(source), m_VfxEffectDocumentRevision);
    switch (result)
    {
    case KeireEditor::AssetDocumentReloadResult::Applied:
        m_VfxEffectPanel->SetMessage("Reloaded " + record->RelativePath.generic_string() + ".");
        break;
    case KeireEditor::AssetDocumentReloadResult::Unchanged:
        m_VfxEffectPanel->SetMessage("VFX Effect source is unchanged.");
        break;
    case KeireEditor::AssetDocumentReloadResult::LocalChanges:
        m_VfxEffectPanel->SetMessage("Reload skipped because the VFX Effect has unsaved local changes.");
        break;
    }
}

void EditorWorkspaceLayer::UndoVfxEffectEdit() { (void)m_VfxEffectDocument->Undo(); }

void EditorWorkspaceLayer::RedoVfxEffectEdit() { (void)m_VfxEffectDocument->Redo(); }

void EditorWorkspaceLayer::RevealVfxEffectAsset(const Keire::AssetId asset)
{
    if (!asset || !m_AssetBrowserPanel)
        return;
    m_SelectedAsset = asset;
    m_AssetBrowserPanel->RevealAsset(asset);
    m_AssetBrowserPanel->Registration().SetVisible(true);
    m_AssetBrowserPanel->Registration().RequestFocus();
}

Keire::VfxRenderSnapshot EditorWorkspaceLayer::SceneViewportEditVfx() const
{
    return m_VfxEffectPreviewWorld ? m_VfxEffectPreviewWorld->CaptureRenderSnapshot() : Keire::VfxRenderSnapshot{};
}

void EditorWorkspaceLayer::EnsureEditorVfxPreviewWorld(const std::uint32_t minimumParticleCapacity)
{
    constexpr std::uint32_t defaultParticleCapacity =
        static_cast<std::uint32_t>(Keire::VfxRenderSnapshot::MaximumParticles);
    const auto capacity = std::max(defaultParticleCapacity, minimumParticleCapacity);
    if (m_VfxEffectPreviewWorld && m_VfxEffectPreviewCapacity >= capacity)
        return;

    ResetEditorVfxPreviewWorld();
    Keire::VfxWorldSpecification specification;
    specification.Backend = m_VfxEffectPreviewBackend;
    specification.MaximumEffects = 512;
    specification.MaximumParticles = capacity;
    m_VfxEffectPreviewWorld = Keire::CreateRef<Keire::VfxWorld>(std::move(specification));
    m_VfxEffectPreviewCapacity = capacity;

    if (!m_VfxEffectPreviewEffect)
        return;
    if (m_VfxEffectPreviewRevision == 0)
        m_VfxEffectPreviewRevision = 1;
    m_VfxEffectPreviewHandle = m_VfxEffectPreviewWorld->Activate(
        {m_VfxEffectPreviewEffect, m_VfxEffectPreviewRevision, m_VfxEffectPreviewPosition, m_VfxEffectPreviewRotation,
         m_VfxEffectPreviewSeedOffset, m_VfxEffectPreviewParameterOverrides});
    if (!m_VfxEffectPreviewHandle)
        throw std::runtime_error("The editor VFX preview world rejected the authored effect.");
    m_VfxEffectPreviewWorld->SetSimulationSpeed(m_VfxEffectPreviewHandle,
                                                m_VfxEffectPreviewPaused ? 0.0F : m_VfxEffectPreviewSpeed);
}

void EditorWorkspaceLayer::ResetEditorVfxPreviewWorld() noexcept
{
    if (m_VfxEffectPreviewWorld)
        m_VfxEffectPreviewWorld->Clear();
    m_VfxEffectPreviewWorld.Reset();
    m_VfxEffectPreviewHandle = {};
    m_VfxEffectPreviewCapacity = 0;
    m_VfxEffectPreviewRestartHandle = {};
    m_VfxEffectPreviewRestartTransformInitialized = false;
    for (auto& [entity, preview] : m_EditModeVfxPreviews)
    {
        (void)entity;
        preview.Handle = {};
        preview.Revision = 0;
        preview.RestartTransformInitialized = false;
    }
}

void EditorWorkspaceLayer::StopEditModeVfxPreviews() noexcept
{
    if (m_VfxEffectPreviewWorld)
    {
        for (const auto& [entity, preview] : m_EditModeVfxPreviews)
        {
            (void)entity;
            try
            {
                if (preview.Handle && m_VfxEffectPreviewWorld->IsAlive(preview.Handle))
                    m_VfxEffectPreviewWorld->Stop(preview.Handle);
            }
            catch (...)
            {
            }
        }
    }
    m_EditModeVfxPreviews.clear();
    m_EditModeVfxPreviewScene.Reset();
}

void EditorWorkspaceLayer::SynchronizeEditModeVfxPreviews()
{
    const auto scene = m_SceneDocument ? m_SceneDocument->EditingScene() : Keire::Ref<Keire::Scene>{};
    const auto assets = Owner().Assets();
    if (!scene || !assets)
    {
        StopEditModeVfxPreviews();
        return;
    }

    if (const auto previousScene = m_EditModeVfxPreviewScene.Lock(); previousScene != scene)
    {
        StopEditModeVfxPreviews();
        m_EditModeVfxPreviewScene = scene;
    }

    const auto stopPreview = [this](EditModeVfxPreviewState& preview) noexcept
    {
        try
        {
            if (m_VfxEffectPreviewWorld && preview.Handle && m_VfxEffectPreviewWorld->IsAlive(preview.Handle))
                m_VfxEffectPreviewWorld->Stop(preview.Handle);
        }
        catch (...)
        {
        }
        preview.Handle = {};
        preview.Revision = 0;
        preview.RestartTransformInitialized = false;
    };

    const auto emitters = KeireEditor::CollectEditModeVfxEmitters(scene);
    const auto preferred =
        m_SceneDocument->Selection() ? Keire::EntityId(m_SceneDocument->Selection()) : Keire::EntityId{};
    const auto draftHost = m_VfxEffectPreviewEffect
                               ? KeireEditor::SelectEditModeVfxDraftHost(emitters, m_VfxEffectPreviewAsset, preferred)
                               : std::optional<KeireEditor::EditModeVfxEmitterSnapshot>{};
    if (draftHost)
    {
        if (const auto existing = m_EditModeVfxPreviews.find(draftHost->Entity);
            existing != m_EditModeVfxPreviews.end())
        {
            stopPreview(existing->second);
            m_EditModeVfxPreviews.erase(existing);
        }
    }

    if (m_VfxEffectPreviewEffect && m_VfxEffectPreviewAsset)
    {
        const Keire::EntityId routedEntity = draftHost ? draftHost->Entity : Keire::EntityId{};
        const Keire::Vector3 position = draftHost ? draftHost->Position : Keire::Vector3{};
        const Keire::Quaternion rotation = draftHost ? draftHost->Rotation : Keire::Quaternion{};
        const std::uint32_t seedOffset = draftHost ? draftHost->SeedOffset : 0;
        const auto parameterOverrides =
            draftHost ? KeireEditor::CompatibleEditModeVfxOverrides(m_VfxEffectPreviewEffect->Definition(),
                                                                    draftHost->ParameterOverrides)
                      : std::vector<Keire::VfxParameterOverride>{};
        const bool alive = m_VfxEffectPreviewWorld && m_VfxEffectPreviewHandle &&
                           m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle);
        const bool routeChanged =
            m_VfxEffectPreviewRoutedEntity != routedEntity || m_VfxEffectPreviewSeedOffset != seedOffset;
        const bool parametersChanged = m_VfxEffectPreviewParameterOverrides != parameterOverrides;
        const auto space = m_VfxEffectPreviewEffect->Definition().Space;
        const bool restartForTransform =
            m_VfxEffectPreviewRestartTransformInitialized &&
            KeireEditor::EditModeVfxPreviewRequiresRestart(space, m_VfxEffectPreviewRestartPosition,
                                                           m_VfxEffectPreviewRestartRotation, position, rotation);
        const bool shouldActivate = KeireEditor::EditModeVfxDraftShouldActivate(
            alive, routeChanged || restartForTransform, m_VfxEffectPreviewAutoRestart);
        if (alive && shouldActivate)
        {
            m_VfxEffectPreviewWorld->Stop(m_VfxEffectPreviewHandle);
            m_VfxEffectPreviewHandle = {};
        }

        EnsureEditorVfxPreviewWorld(static_cast<std::uint32_t>(
            std::clamp<std::size_t>(m_VfxEffectPreviewEffect->Definition().Capacity, 1U, 1'000'000U)));
        bool currentAlive = m_VfxEffectPreviewHandle && m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle);
        bool activated = false;
        if (!currentAlive && shouldActivate)
        {
            m_VfxEffectPreviewHandle = m_VfxEffectPreviewWorld->Activate(
                {m_VfxEffectPreviewEffect, std::max<std::uint64_t>(m_VfxEffectPreviewRevision, 1), position, rotation,
                 seedOffset, parameterOverrides});
            currentAlive = m_VfxEffectPreviewHandle && m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle);
            activated = currentAlive;
        }
        if (currentAlive)
        {
            if (parametersChanged)
                m_VfxEffectPreviewWorld->SetParameterOverrides(m_VfxEffectPreviewHandle, parameterOverrides);
            m_VfxEffectPreviewWorld->SetTransform(m_VfxEffectPreviewHandle, position, rotation);
            m_VfxEffectPreviewWorld->SetSimulationSpeed(m_VfxEffectPreviewHandle,
                                                        m_VfxEffectPreviewPaused ? 0.0F : m_VfxEffectPreviewSpeed);
        }
        else
        {
            m_VfxEffectPreviewHandle = {};
        }
        const bool newlyActivated = currentAlive && m_VfxEffectPreviewRestartHandle != m_VfxEffectPreviewHandle;
        m_VfxEffectPreviewRoutedEntity = routedEntity;
        m_VfxEffectPreviewPosition = position;
        m_VfxEffectPreviewRotation = rotation;
        m_VfxEffectPreviewSeedOffset = seedOffset;
        m_VfxEffectPreviewParameterOverrides = parameterOverrides;
        if (space == Keire::VfxSimulationSpace::Local || routeChanged || activated || newlyActivated ||
            (!m_VfxEffectPreviewRestartTransformInitialized && currentAlive))
        {
            m_VfxEffectPreviewRestartHandle = currentAlive ? m_VfxEffectPreviewHandle : Keire::VfxHandle{};
            m_VfxEffectPreviewRestartPosition = position;
            m_VfxEffectPreviewRestartRotation = rotation;
            m_VfxEffectPreviewRestartTransformInitialized = true;
        }
    }

    std::unordered_set<Keire::EntityId> seen;
    seen.reserve(emitters.size());
    for (const auto& emitter : emitters)
    {
        if (draftHost && emitter.Entity == draftHost->Entity)
            continue;
        seen.emplace(emitter.Entity);
        auto preview = m_EditModeVfxPreviews.find(emitter.Entity);

        try
        {
            if (preview == m_EditModeVfxPreviews.end() || preview->second.Effect != emitter.Effect ||
                preview->second.SeedOffset != emitter.SeedOffset)
            {
                auto effectHandle = assets->Load<Keire::VfxEffectAsset>(emitter.Effect, Keire::AssetPriority::High);
                if (preview != m_EditModeVfxPreviews.end())
                    stopPreview(preview->second);
                EditModeVfxPreviewState replacement;
                replacement.Effect = emitter.Effect;
                replacement.EffectHandle = std::move(effectHandle);
                replacement.SeedOffset = emitter.SeedOffset;
                preview = m_EditModeVfxPreviews.insert_or_assign(emitter.Entity, std::move(replacement)).first;
            }

            const auto effect = preview->second.EffectHandle.TryGetLoaded();
            if (!effect)
            {
                stopPreview(preview->second);
                continue;
            }
            const auto parameterOverrides =
                KeireEditor::CompatibleEditModeVfxOverrides(effect->Definition(), emitter.ParameterOverrides);

            EnsureEditorVfxPreviewWorld(1);
            auto& state = preview->second;
            const auto revision = std::max<std::uint64_t>(state.EffectHandle.Revision(), 1);
            const bool worldSpaceRelocation =
                state.RestartTransformInitialized && KeireEditor::EditModeVfxPreviewRequiresRestart(
                                                         effect->Definition().Space, state.RestartPosition,
                                                         state.RestartRotation, emitter.Position, emitter.Rotation);
            if (worldSpaceRelocation && state.Handle && m_VfxEffectPreviewWorld->IsAlive(state.Handle))
                stopPreview(state);

            if (!state.Handle || !m_VfxEffectPreviewWorld->IsAlive(state.Handle))
            {
                state.Handle = {};
                state.RestartTransformInitialized = false;
                state.Handle = m_VfxEffectPreviewWorld->Activate(
                    {effect, revision, emitter.Position, emitter.Rotation, emitter.SeedOffset, parameterOverrides});
                state.Revision = revision;
                state.ParameterOverrides = parameterOverrides;
            }
            else if (revision != state.Revision)
            {
                if (!m_VfxEffectPreviewWorld->Reload(state.Handle, effect, revision))
                {
                    stopPreview(state);
                    state.Handle = m_VfxEffectPreviewWorld->Activate(
                        {effect, revision, emitter.Position, emitter.Rotation, emitter.SeedOffset, parameterOverrides});
                }
                state.Revision = revision;
            }

            if (state.Handle)
            {
                if (state.ParameterOverrides != parameterOverrides)
                {
                    m_VfxEffectPreviewWorld->SetParameterOverrides(state.Handle, parameterOverrides);
                    state.ParameterOverrides = parameterOverrides;
                }
                m_VfxEffectPreviewWorld->SetTransform(state.Handle, emitter.Position, emitter.Rotation);
                m_VfxEffectPreviewWorld->SetSimulationSpeed(state.Handle, emitter.SimulationSpeed);
                if (effect->Definition().Space == Keire::VfxSimulationSpace::Local ||
                    !state.RestartTransformInitialized)
                {
                    state.RestartPosition = emitter.Position;
                    state.RestartRotation = emitter.Rotation;
                    state.RestartTransformInitialized = true;
                }
            }
        }
        catch (...)
        {
            if (const auto failed = m_EditModeVfxPreviews.find(emitter.Entity); failed != m_EditModeVfxPreviews.end())
            {
                stopPreview(failed->second);
            }
        }
    }

    for (auto preview = m_EditModeVfxPreviews.begin(); preview != m_EditModeVfxPreviews.end();)
    {
        if (seen.contains(preview->first))
        {
            ++preview;
            continue;
        }
        stopPreview(preview->second);
        preview = m_EditModeVfxPreviews.erase(preview);
    }
}

void EditorWorkspaceLayer::RestartVfxEffectPreview()
{
    if (!m_VfxEffectDocument || !m_VfxEffectDocument->IsOpen() || !m_VfxEffectDocument->Publishable())
        return;
    RequireCompiledVfxSystems(m_VfxEffectDocument->Definition(), m_VfxEffectPreviewBackend);
    const auto paused = m_VfxEffectPreviewPaused;
    const auto autoRestart = m_VfxEffectPreviewAutoRestart;
    const auto speed = m_VfxEffectPreviewSpeed;
    const auto backend = m_VfxEffectPreviewBackend;
    const auto routedEntity = m_VfxEffectPreviewRoutedEntity;
    const auto position = m_VfxEffectPreviewPosition;
    const auto rotation = m_VfxEffectPreviewRotation;
    const auto seedOffset = m_VfxEffectPreviewSeedOffset;
    const auto parameterOverrides = m_VfxEffectPreviewParameterOverrides;
    const auto asset = m_VfxEffectDocument->Asset();
    StopVfxEffectPreview();
    m_VfxEffectPreviewPaused = paused;
    m_VfxEffectPreviewAutoRestart = autoRestart;
    m_VfxEffectPreviewSpeed = speed;
    m_VfxEffectPreviewBackend = backend;
    m_VfxEffectPreviewAsset = asset;
    m_VfxEffectPreviewRoutedEntity = routedEntity;
    m_VfxEffectPreviewPosition = position;
    m_VfxEffectPreviewRotation = rotation;
    m_VfxEffectPreviewSeedOffset = seedOffset;
    m_VfxEffectPreviewParameterOverrides = parameterOverrides;
    PreviewVfxEffect(asset, m_VfxEffectDocument->Definition());
}

void EditorWorkspaceLayer::SetVfxEffectPreviewPaused(const bool paused) noexcept
{
    m_VfxEffectPreviewPaused = paused;
    try
    {
        if (m_VfxEffectPreviewWorld && m_VfxEffectPreviewHandle &&
            m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle))
        {
            m_VfxEffectPreviewWorld->SetSimulationSpeed(m_VfxEffectPreviewHandle,
                                                        paused ? 0.0F : m_VfxEffectPreviewSpeed);
        }
    }
    catch (...)
    {
    }
}

void EditorWorkspaceLayer::SetVfxEffectPreviewAutoRestart(const bool enabled) noexcept
{
    m_VfxEffectPreviewAutoRestart = enabled;
}

void EditorWorkspaceLayer::SetVfxEffectPreviewBackend(const Keire::VfxBackend backend)
{
    if (backend != Keire::VfxBackend::Cpu && backend != Keire::VfxBackend::Gpu)
        throw std::invalid_argument("VFX preview backend is unsupported.");
    if (m_VfxEffectPreviewBackend == backend)
        return;
    if (m_VfxEffectDocument && m_VfxEffectDocument->IsOpen())
    {
        if (!m_VfxEffectDocument->Publishable())
            throw std::logic_error("Repair the VFX graph before changing the preview backend.");
        RequireCompiledVfxSystems(m_VfxEffectDocument->Definition(), backend);
    }
    m_VfxEffectPreviewBackend = backend;
    ResetEditorVfxPreviewWorld();
    RestartVfxEffectPreview();
}

void EditorWorkspaceLayer::SetVfxEffectPreviewSpeed(const float speed)
{
    if (!std::isfinite(speed) || speed < 0.05F || speed > 4.0F)
        throw std::invalid_argument("VFX preview speed must be finite and in the range 0.05..4.");
    m_VfxEffectPreviewSpeed = speed;
    if (m_VfxEffectPreviewWorld && m_VfxEffectPreviewHandle &&
        m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle))
    {
        m_VfxEffectPreviewWorld->SetSimulationSpeed(m_VfxEffectPreviewHandle, m_VfxEffectPreviewPaused ? 0.0F : speed);
    }
}

void EditorWorkspaceLayer::StopVfxEffectPreview() noexcept
{
    try
    {
        if (m_VfxEffectPreviewWorld && m_VfxEffectPreviewHandle &&
            m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle))
        {
            m_VfxEffectPreviewWorld->Stop(m_VfxEffectPreviewHandle);
        }
    }
    catch (...)
    {
    }
    m_VfxEffectPreviewHandle = {};
    m_VfxEffectPreviewAsset = {};
    m_VfxEffectPreviewEffect.Reset();
    m_VfxEffectPreviewRevision = 0;
    m_VfxEffectPreviewRoutedEntity = {};
    m_VfxEffectPreviewPosition = {};
    m_VfxEffectPreviewRotation = {};
    m_VfxEffectPreviewSeedOffset = 0;
    m_VfxEffectPreviewParameterOverrides.clear();
    m_VfxEffectPreviewRestartHandle = {};
    m_VfxEffectPreviewRestartPosition = {};
    m_VfxEffectPreviewRestartRotation = {};
    m_VfxEffectPreviewRestartTransformInitialized = false;
    m_VfxEffectPreviewDiagnostic.clear();
}

void EditorWorkspaceLayer::ReportVfxEffectError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::PreviewVfxEffect(const Keire::AssetId asset, const Keire::VfxEffectDefinition& definition)
{
    try
    {
        RequireCompiledVfxSystems(definition, m_VfxEffectPreviewBackend);
        const auto effect = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
        const auto capacity = static_cast<std::uint32_t>(std::clamp<std::size_t>(definition.Capacity, 1U, 1'000'000U));
        const bool sameAsset = m_VfxEffectPreviewAsset == asset;
        if (!sameAsset)
        {
            m_VfxEffectPreviewRoutedEntity = {};
            m_VfxEffectPreviewPosition = {};
            m_VfxEffectPreviewRotation = {};
            m_VfxEffectPreviewSeedOffset = 0;
            m_VfxEffectPreviewParameterOverrides.clear();
            m_VfxEffectPreviewRestartHandle = {};
            m_VfxEffectPreviewRestartPosition = {};
            m_VfxEffectPreviewRestartRotation = {};
            m_VfxEffectPreviewRestartTransformInitialized = false;
        }
        const bool canReload = m_VfxEffectPreviewWorld && sameAsset && m_VfxEffectPreviewHandle &&
                               m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle) &&
                               m_VfxEffectPreviewCapacity >= capacity;
        if (!canReload)
        {
            if (m_VfxEffectPreviewWorld && m_VfxEffectPreviewHandle &&
                m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle))
            {
                m_VfxEffectPreviewWorld->Stop(m_VfxEffectPreviewHandle);
            }
            m_VfxEffectPreviewHandle = {};
            m_VfxEffectPreviewRestartHandle = {};
            m_VfxEffectPreviewRestartTransformInitialized = false;
        }

        m_VfxEffectPreviewAsset = asset;
        m_VfxEffectPreviewEffect = effect;
        if (canReload)
        {
            if (++m_VfxEffectPreviewRevision == 0)
                ++m_VfxEffectPreviewRevision;
            if (!m_VfxEffectPreviewWorld->Reload(m_VfxEffectPreviewHandle, effect, m_VfxEffectPreviewRevision))
                throw std::runtime_error("The editor VFX preview world rejected the authored effect update.");
        }
        else
        {
            m_VfxEffectPreviewRevision = 1;
            EnsureEditorVfxPreviewWorld(capacity);
            if (!m_VfxEffectPreviewHandle)
            {
                m_VfxEffectPreviewHandle = m_VfxEffectPreviewWorld->Activate(
                    {m_VfxEffectPreviewEffect, m_VfxEffectPreviewRevision, m_VfxEffectPreviewPosition,
                     m_VfxEffectPreviewRotation, m_VfxEffectPreviewSeedOffset, m_VfxEffectPreviewParameterOverrides});
            }
            if (!m_VfxEffectPreviewHandle)
                throw std::runtime_error("The editor VFX preview world rejected the authored effect.");
        }
        m_VfxEffectPreviewWorld->SetSimulationSpeed(m_VfxEffectPreviewHandle,
                                                    m_VfxEffectPreviewPaused ? 0.0F : m_VfxEffectPreviewSpeed);
        m_VfxEffectPreviewDiagnostic =
            m_VfxEffectPreviewBackend == Keire::VfxBackend::Cpu
                ? "Stable CPU authoring preview is active. Switch to GPU to inspect runtime compute behavior."
                : "GPU runtime preview is active. Preview transport is independent of Play Mode.";
    }
    catch (const std::exception& error)
    {
        m_VfxEffectPreviewDiagnostic = std::string("Preview retained its last-good program: ") + error.what();
    }
}

void EditorWorkspaceLayer::PersistVfxEffect(const Keire::AssetId asset, const std::span<const std::byte> bytes)
{
    if (!m_AssetDatabase)
        throw std::runtime_error("The Asset Database is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::VfxEffectAsset::StaticType() ||
        record->RelativePath.extension() != ".keirevfx")
        throw std::runtime_error("The edited VFX Effect source is unavailable.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    WriteBytesAtomically(source, bytes);
}

void EditorWorkspaceLayer::OpenVfxEffect(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::VfxEffectAsset::StaticType() ||
        record->RelativePath.extension() != ".keirevfx")
        throw std::invalid_argument("Only .keirevfx assets can be opened in the VFX Effect editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    const auto bytes = ReadBytes(source);
    if (const auto context = m_VfxEffectDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "VFX Effect: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    if (++m_VfxEffectDocumentRevision == 0)
        ++m_VfxEffectDocumentRevision;
    m_VfxEffectDocument->Open(asset, bytes, m_VfxEffectDocumentRevision, std::move(context));
    m_ActiveUndoContext = m_VfxEffectDocument->UndoContext();
    m_VfxEffectPanel->ResetTransientState();
    m_VfxEffectPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_VfxEffectPanel->Registration().SetVisible(true);
    m_VfxEffectPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::SaveVfxEffect()
{
    if (!m_AssetDatabase || !m_VfxEffectDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_VfxEffectDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited VFX Effect no longer exists.");
    m_VfxEffectDocument->Save();
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_VfxEffectDocument->Asset());
    m_VfxEffectPanel->SetMessage("Saved and queued import for " + record->RelativePath.generic_string() + ".");
}
