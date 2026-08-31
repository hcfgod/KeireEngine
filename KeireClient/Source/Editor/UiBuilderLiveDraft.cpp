#include "KeireClient/Editor/UiBuilderLiveDraft.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    bool PublishUiToolkitAuthoringAsset(const Keire::Ref<Keire::AssetSystem>& assets, const Keire::AssetId asset,
                                        const Keire::AssetTypeId type, const std::span<const std::byte> source,
                                        std::string& diagnostic) noexcept
    {
        try
        {
            if (!assets || !assets->IsOpen() || !asset)
                throw std::invalid_argument("UI authoring publication requires an open asset system and asset ID.");
            Keire::Ref<Keire::Asset> value;
            if (type == Keire::UiVisualTreeAsset::StaticType())
            {
                value = Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(source));
            }
            else if (type == Keire::UiStyleSheetAsset::StaticType())
            {
                value = Keire::CreateRef<Keire::UiStyleSheetAsset>(Keire::UiStyleSheetAsset::ParseSource(source));
            }
            else if (type == Keire::UiPanelSettingsAsset::StaticType())
                value = Keire::UiPanelSettingsAsset::Decode(source);
            else
                throw std::invalid_argument("The asset is not a UI document, style sheet, or Panel Settings asset.");
            if (!assets->PublishDevelopmentAsset(asset, std::move(value)))
            {
                diagnostic = "The current UI asset load must finish before its authoring revision can be published.";
                return false;
            }
            diagnostic.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            diagnostic = error.what();
        }
        catch (...)
        {
            diagnostic = "UI authoring publication failed with an unknown error.";
        }
        return false;
    }

    UiBuilderLiveDraftSession::~UiBuilderLiveDraftSession() { Close(); }

    void UiBuilderLiveDraftSession::Synchronize(Keire::Ref<Keire::AssetSystem> assets, const bool playActive,
                                                const Keire::AssetId asset, const std::uint64_t documentGeneration,
                                                const bool dirty,
                                                const Keire::UiVisualTreeDefinition& definition) noexcept
    {
        if (!playActive || !dirty || !assets || !assets->IsOpen() || !asset || documentGeneration == 0)
        {
            CloseDocument();
            return;
        }
        if (m_Applied && (m_Assets != assets || m_Asset != asset))
            CloseDocument();
        if (m_Applied && m_Asset == asset && m_DocumentGeneration == documentGeneration)
            return;

        try
        {
            Keire::UiVisualTreeAsset::Validate(definition);
            if (!m_Applied)
            {
                const auto baseline = assets->Load<Keire::UiVisualTreeAsset>(asset, Keire::AssetPriority::High);
                const auto loaded = baseline.TryGetLoaded();
                if (!loaded)
                {
                    m_Diagnostic = "Live Play preview is waiting for the imported UI document.";
                    return;
                }
                m_Assets = std::move(assets);
                m_Asset = asset;
                m_Baseline = loaded->Definition();
            }
            if (!Publish(definition))
                return;
            m_DocumentGeneration = documentGeneration;
            m_Applied = true;
            m_Diagnostic.clear();
        }
        catch (const std::exception& error)
        {
            m_Diagnostic = std::string("Live Play UI draft was rejected: ") + error.what();
        }
        catch (...)
        {
            m_Diagnostic = "Live Play UI draft was rejected by an unknown failure.";
        }
    }

    void UiBuilderLiveDraftSession::Commit(Keire::Ref<Keire::AssetSystem> assets, const Keire::AssetId asset,
                                           const Keire::UiVisualTreeDefinition& definition) noexcept
    {
        if (!asset || !assets || !assets->IsOpen())
        {
            ClearState();
            return;
        }
        if (!m_Applied || m_Asset != asset || m_Assets != assets)
            return;
        if (Publish(definition))
            ClearState();
    }

    void UiBuilderLiveDraftSession::SynchronizeStyle(Keire::Ref<Keire::AssetSystem> assets, const Keire::AssetId asset,
                                                     const std::uint64_t documentGeneration, const bool dirty,
                                                     const bool sourceValid,
                                                     const Keire::UiStyleSheetDefinition& definition) noexcept
    {
        if (!dirty || !assets || !assets->IsOpen() || !asset || documentGeneration == 0)
        {
            CloseStyle();
            return;
        }
        if (!sourceValid)
            return;
        if (m_StyleApplied && (m_StyleAssets != assets || m_StyleAsset != asset))
            CloseStyle();
        if (m_StyleApplied && m_StyleAsset == asset && m_StyleDocumentGeneration == documentGeneration)
            return;
        try
        {
            Keire::UiStyleSheetAsset::Validate(definition);
            if (!m_StyleApplied)
            {
                const auto baseline = assets->Load<Keire::UiStyleSheetAsset>(asset, Keire::AssetPriority::High);
                const auto loaded = baseline.TryGetLoaded();
                if (!loaded)
                {
                    m_Diagnostic = "Live style preview is waiting for the imported style sheet.";
                    return;
                }
                m_StyleAssets = std::move(assets);
                m_StyleAsset = asset;
                m_StyleBaseline = loaded->Definition();
            }
            if (!PublishStyle(definition))
                return;
            m_StyleDocumentGeneration = documentGeneration;
            m_StyleApplied = true;
            m_Diagnostic.clear();
        }
        catch (const std::exception& error)
        {
            m_Diagnostic = std::string("Live style draft was rejected: ") + error.what();
        }
        catch (...)
        {
            m_Diagnostic = "Live style draft was rejected by an unknown failure.";
        }
    }

    void UiBuilderLiveDraftSession::CommitStyle(Keire::Ref<Keire::AssetSystem> assets, const Keire::AssetId asset,
                                                const Keire::UiStyleSheetDefinition& definition) noexcept
    {
        if (!asset || !assets || !assets->IsOpen())
        {
            ClearStyleState();
            return;
        }
        if (!m_StyleApplied || m_StyleAsset != asset || m_StyleAssets != assets)
            return;
        if (PublishStyle(definition))
            ClearStyleState();
    }

    void UiBuilderLiveDraftSession::CloseStyle() noexcept
    {
        bool reverted = true;
        if (m_StyleApplied && m_StyleAssets && m_StyleAssets->IsOpen() && m_StyleAsset && m_StyleBaseline)
            reverted = PublishStyle(*m_StyleBaseline);
        ClearStyleState();
        if (reverted)
            m_Diagnostic.clear();
    }

    void UiBuilderLiveDraftSession::Close() noexcept
    {
        CloseDocument();
        CloseStyle();
    }

    void UiBuilderLiveDraftSession::CloseDocument() noexcept
    {
        bool reverted = true;
        if (m_Applied && m_Assets && m_Assets->IsOpen() && m_Asset && m_Baseline)
            reverted = Publish(*m_Baseline);
        ClearState();
        if (reverted)
            m_Diagnostic.clear();
    }

    void UiBuilderLiveDraftSession::ClearState() noexcept
    {
        m_Assets.Reset();
        m_Asset = {};
        m_Baseline.reset();
        m_DocumentGeneration = 0;
        m_Applied = false;
    }

    void UiBuilderLiveDraftSession::ClearStyleState() noexcept
    {
        m_StyleAssets.Reset();
        m_StyleAsset = {};
        m_StyleBaseline.reset();
        m_StyleDocumentGeneration = 0;
        m_StyleApplied = false;
    }

    bool UiBuilderLiveDraftSession::Publish(const Keire::UiVisualTreeDefinition& definition) noexcept
    {
        try
        {
            if (!m_Assets || !m_Assets->IsOpen() || !m_Asset)
                return false;
            if (!m_Assets->PublishDevelopmentAsset(m_Asset, Keire::CreateRef<Keire::UiVisualTreeAsset>(definition)))
            {
                m_Diagnostic = "Live Play UI draft publication is waiting for the current asset load to finish.";
                return false;
            }
            m_Diagnostic.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            m_Diagnostic = std::string("Live Play UI draft publication failed: ") + error.what();
        }
        catch (...)
        {
            m_Diagnostic = "Live Play UI draft publication failed with an unknown error.";
        }
        return false;
    }

    bool UiBuilderLiveDraftSession::PublishStyle(const Keire::UiStyleSheetDefinition& definition) noexcept
    {
        try
        {
            if (!m_StyleAssets || !m_StyleAssets->IsOpen() || !m_StyleAsset)
                return false;
            if (!m_StyleAssets->PublishDevelopmentAsset(m_StyleAsset,
                                                        Keire::CreateRef<Keire::UiStyleSheetAsset>(definition)))
            {
                m_Diagnostic = "Live style draft publication is waiting for the current asset load to finish.";
                return false;
            }
            m_Diagnostic.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            m_Diagnostic = std::string("Live style draft publication failed: ") + error.what();
        }
        catch (...)
        {
            m_Diagnostic = "Live style draft publication failed with an unknown error.";
        }
        return false;
    }
} // namespace KeireEditor
