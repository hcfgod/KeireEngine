#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/AudioMixerDocument.h"
#include "KeireClient/Editor/AudioMixerPanel.h"
#include "KeireClient/Editor/EditorAssetFileService.h"

#include <stdexcept>
#include <utility>

namespace
{
    using KeireEditor::Detail::ReadBytes;
    using KeireEditor::Detail::WriteBytesAtomically;

    [[nodiscard]] Keire::AudioMixerImpulseResponses
    ResolveImpulseResponses(const Keire::Ref<Keire::AssetSystem>& assets, const Keire::AudioMixerDefinition& definition)
    {
        Keire::AudioMixerImpulseResponses result;
        const auto dependencies = Keire::AudioMixerDependencies(definition);
        if (!dependencies.empty() && !assets)
            throw std::runtime_error("Asset services are unavailable for Audio Mixer impulse responses.");
        for (const auto id : dependencies)
        {
            const auto loaded = assets->Load<Keire::AudioClipAsset>(id, Keire::AssetPriority::High).TryGetLoaded();
            if (!loaded)
                throw std::runtime_error("An Audio Mixer impulse response is still loading.");
            result.emplace(id, loaded->Clip());
        }
        return result;
    }
} // namespace

KeireEditor::AudioMixerDocument& EditorWorkspaceLayer::AudioMixerState() noexcept { return *m_AudioMixerDocument; }

const Keire::UiThemeDefinition& EditorWorkspaceLayer::AudioMixerTheme() const noexcept { return m_Theme; }

Keire::Ref<Keire::AssetDatabase> EditorWorkspaceLayer::AudioMixerDatabase() const noexcept { return m_AssetDatabase; }

std::string_view EditorWorkspaceLayer::AudioMixerPreviewDiagnostic() const noexcept
{
    return m_AudioMixerPreviewDiagnostic;
}

Keire::AudioMeterSnapshot EditorWorkspaceLayer::AudioMixerMeters() const noexcept
{
    try
    {
        if (const auto audio = Owner().Audio())
            return audio->LatestMeterSnapshot();
    }
    catch (...)
    {
    }
    return {};
}

void EditorWorkspaceLayer::ActivateAudioMixerHistory() noexcept
{
    m_ActiveUndoContext = m_AudioMixerDocument->UndoContext();
}

void EditorWorkspaceLayer::SaveAudioMixerDocument() { SaveAudioMixer(); }

void EditorWorkspaceLayer::DiscardAudioMixerDocument()
{
    m_AudioMixerDocument->Discard();
    m_AudioMixerPanel->SetMessage("Discarded unsaved Audio Mixer changes.");
}

void EditorWorkspaceLayer::ReloadAudioMixerDocument(const Keire::AssetId asset)
{
    if (!m_AssetDatabase || asset != m_AudioMixerDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::AudioMixerAsset::StaticType() ||
        record->RelativePath.extension() != ".keiremixer")
        throw std::invalid_argument("Only .keiremixer assets can be reloaded in the Audio Mixer editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    if (++m_AudioMixerDocumentRevision == 0)
        ++m_AudioMixerDocumentRevision;
    const auto result = m_AudioMixerDocument->Reload(ReadBytes(source), m_AudioMixerDocumentRevision);
    switch (result)
    {
    case KeireEditor::AssetDocumentReloadResult::Applied:
        m_AudioMixerPanel->SetMessage("Reloaded " + record->RelativePath.generic_string() + ".");
        break;
    case KeireEditor::AssetDocumentReloadResult::Unchanged:
        m_AudioMixerPanel->SetMessage("Audio Mixer source is unchanged.");
        break;
    case KeireEditor::AssetDocumentReloadResult::LocalChanges:
        m_AudioMixerPanel->SetMessage("Reload skipped because the Audio Mixer has unsaved local changes.");
        break;
    }
}

void EditorWorkspaceLayer::UndoAudioMixerEdit() { (void)m_AudioMixerDocument->Undo(); }

void EditorWorkspaceLayer::RedoAudioMixerEdit() { (void)m_AudioMixerDocument->Redo(); }

void EditorWorkspaceLayer::StopAudioMixerPreview() noexcept
{
    const auto asset = std::exchange(m_AudioMixerPreviewAsset, {});
    m_AudioMixerPreviewDiagnostic.clear();
    if (!asset || !m_AssetDatabase)
        return;
    try
    {
        const auto record = m_AssetDatabase->Find(asset);
        if (!record)
            return;
        const auto& specification = m_AssetDatabase->Specification();
        const auto source = specification.ProjectRoot / specification.SourceDirectory / record->RelativePath;
        const auto mixer = Keire::AudioMixerAsset::Decode(ReadBytes(source));
        const auto assets = Owner().Assets();
        if (assets)
            (void)assets->PublishDevelopmentAsset(asset, mixer);
        if (const auto audio = Owner().Audio())
            audio->SubmitMixer(asset, mixer->Definition(), ResolveImpulseResponses(assets, mixer->Definition()));
    }
    catch (...)
    {
        // Preview teardown is advisory and must remain noexcept during panel and application shutdown.
    }
}

void EditorWorkspaceLayer::ReportAudioMixerError(std::string message) noexcept { SetAssetError(std::move(message)); }

void EditorWorkspaceLayer::PreviewAudioMixer(const Keire::AssetId asset, const Keire::AudioMixerDefinition& definition)
{
    Keire::ValidateAudioMixer(definition);
    const auto mixer = Keire::CreateRef<Keire::AudioMixerAsset>(definition);
    const auto assets = Owner().Assets();
    if (!assets || !assets->PublishDevelopmentAsset(asset, mixer))
        throw std::runtime_error("The transient Audio Mixer asset could not be published for live preview.");
    if (const auto audio = Owner().Audio())
        audio->SubmitMixer(asset, definition, ResolveImpulseResponses(assets, definition));
    m_AudioMixerPreviewAsset = asset;
    m_AudioMixerPreviewDiagnostic =
        "Live routing and fader preview is active; headless previews also process effects, sends, ducking, reverb, "
        "and meters. Unsaved edits remain transient.";
}

void EditorWorkspaceLayer::PersistAudioMixer(const Keire::AssetId asset, const std::span<const std::byte> bytes)
{
    if (!m_AssetDatabase)
        throw std::runtime_error("The Asset Database is unavailable.");
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::AudioMixerAsset::StaticType() ||
        record->RelativePath.extension() != ".keiremixer")
        throw std::runtime_error("The edited Audio Mixer source is unavailable.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    WriteBytesAtomically(source, bytes);
}

void EditorWorkspaceLayer::OpenAudioMixer(const Keire::AssetId asset)
{
    if (!m_AssetDatabase)
        return;
    const auto record = m_AssetDatabase->Find(asset);
    if (!record || record->Type != Keire::AudioMixerAsset::StaticType() ||
        record->RelativePath.extension() != ".keiremixer")
        throw std::invalid_argument("Only .keiremixer assets can be opened in the Audio Mixer editor.");
    const auto source = m_AssetDatabase->Specification().ProjectRoot /
                        m_AssetDatabase->Specification().SourceDirectory / record->RelativePath;
    const auto bytes = ReadBytes(source);
    if (const auto context = m_AudioMixerDocument->UndoContext())
        context->Close();
    Keire::Ref<Keire::UndoContext> context;
    if (const auto undo = Owner().Undo())
        context = undo->CreateContext(
            {.Name = "Audio Mixer: " + record->RelativePath.stem().string(), .MaximumCommands = 128});
    if (++m_AudioMixerDocumentRevision == 0)
        ++m_AudioMixerDocumentRevision;
    m_AudioMixerDocument->Open(asset, bytes, m_AudioMixerDocumentRevision, std::move(context));
    m_ActiveUndoContext = m_AudioMixerDocument->UndoContext();
    m_AudioMixerPanel->ResetTransientState();
    m_AudioMixerPanel->SetMessage("Loaded " + record->RelativePath.generic_string() + ".");
    m_AudioMixerPanel->Registration().SetVisible(true);
    m_AudioMixerPanel->Registration().RequestFocus();
}

void EditorWorkspaceLayer::SaveAudioMixer()
{
    if (!m_AssetDatabase || !m_AudioMixerDocument->Asset())
        return;
    const auto record = m_AssetDatabase->Find(m_AudioMixerDocument->Asset());
    if (!record)
        throw std::runtime_error("The edited Audio Mixer no longer exists.");
    m_AudioMixerDocument->Save();
    ImportAssets();
    if (const auto assets = Owner().Assets())
        (void)assets->Reload(m_AudioMixerDocument->Asset());
    m_AudioMixerPanel->SetMessage("Saved and queued import for " + record->RelativePath.generic_string() + ".");
}
