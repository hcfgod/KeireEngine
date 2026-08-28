#include "KeireClient/Editor/AssetBrowserPanel.h"

#include "KeireClientInternal/Editor/AssetBrowserPanelInternal.h"

#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace KeireEditor
{
    AssetBrowserPanel::AssetBrowserPanel(IAssetBrowserController& controller)
        : m_Impl(std::make_unique<Impl>(controller))
    {
    }
    AssetBrowserPanel::~AssetBrowserPanel() { Close(); }
    void AssetBrowserPanel::SetProjectRoot(const std::filesystem::path& root) { m_Impl->SetProjectRoot(root); }
    void AssetBrowserPanel::SetJobSystem(Keire::Ref<Keire::JobSystem> jobs) { m_Impl->Scheduler = std::move(jobs); }
    void AssetBrowserPanel::SetUndoContext(Keire::Ref<Keire::UndoContext> context)
    {
        m_Impl->SetUndoContext(std::move(context));
    }
    Keire::Ref<Keire::UndoContext> AssetBrowserPanel::UndoContext() const { return m_Impl->Undo; }
    bool AssetBrowserPanel::Focused() const noexcept { return m_Impl->Focused; }
    std::filesystem::path AssetBrowserPanel::CurrentFolder() const { return m_Impl->CurrentFolder; }
    std::filesystem::path AssetBrowserPanel::ResolveExternalDropFolder(const Keire::UiPosition position) const
    {
        return ResolveAssetBrowserDropFolder(m_Impl->ExternalDropTargets, position, m_Impl->CurrentFolder);
    }

    std::vector<Keire::AssetId> AssetBrowserPanel::DecodeDragPayload(const std::span<const std::byte> bytes)
    {
        return DecodeAssetPayload(bytes);
    }
    void AssetBrowserPanel::InvalidateThumbnail(const Keire::AssetId asset) { m_Impl->InvalidateThumbnail(asset); }
    void AssetBrowserPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Impl->Registration = workspace.RegisterPanel({"editor.project", "Project"});
    }
    Keire::UiPanelRegistration& AssetBrowserPanel::Registration() noexcept { return m_Impl->Registration; }
    void AssetBrowserPanel::RevealAsset(const Keire::AssetId asset) { m_Impl->Reveal(asset, m_Impl->Controller); }
    void AssetBrowserPanel::OpenAsset(const Keire::AssetId asset)
    {
        if (const auto record = m_Impl->Controller.AssetBrowserDatabase()->Find(asset))
            m_Impl->Open(*record, m_Impl->Controller);
    }
    void AssetBrowserPanel::RequestCreateMaterial() { m_Impl->RequestCreateMaterial(); }
    void AssetBrowserPanel::Draw(Keire::UiFrame& ui) { m_Impl->Draw(ui, m_Impl->Controller); }
    void AssetBrowserPanel::Close() noexcept
    {
        if (m_Impl)
            m_Impl->Close();
    }
} // namespace KeireEditor
