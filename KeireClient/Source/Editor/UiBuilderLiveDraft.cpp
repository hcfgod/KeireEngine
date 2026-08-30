#include "KeireClient/Editor/UiBuilderLiveDraft.h"

#include <exception>
#include <utility>

namespace KeireEditor
{
    UiBuilderLiveDraftSession::~UiBuilderLiveDraftSession() { Close(); }

    void UiBuilderLiveDraftSession::Synchronize(Keire::Ref<Keire::AssetSystem> assets, const bool playActive,
                                                const Keire::AssetId asset, const std::uint64_t documentGeneration,
                                                const bool dirty,
                                                const Keire::UiVisualTreeDefinition& definition) noexcept
    {
        if (!playActive || !dirty || !assets || !assets->IsOpen() || !asset || documentGeneration == 0)
        {
            Close();
            return;
        }
        if (m_Applied && (m_Assets != assets || m_Asset != asset))
            Close();
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

    void UiBuilderLiveDraftSession::Close() noexcept
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
} // namespace KeireEditor
