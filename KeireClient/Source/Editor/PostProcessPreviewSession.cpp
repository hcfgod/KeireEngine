#include "KeireClient/Editor/PostProcessPreviewSession.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        class PreviewOperation final
        {
          public:
            explicit PreviewOperation(bool& applying) : m_Applying(applying)
            {
                if (m_Applying)
                {
                    throw std::logic_error("Post Processing preview cannot change from a publication callback.");
                }
                m_Applying = true;
            }

            ~PreviewOperation() { m_Applying = false; }

            PreviewOperation(const PreviewOperation&) = delete;
            PreviewOperation& operator=(const PreviewOperation&) = delete;

          private:
            bool& m_Applying;
        };
    } // namespace

    PostProcessPreviewSession::PostProcessPreviewSession(PostProcessPreviewDependencies dependencies)
        : m_Dependencies(std::move(dependencies))
    {
        if (!m_Dependencies.ReadSource || !m_Dependencies.Publish)
        {
            throw std::invalid_argument("Post Processing preview requires source and publication services.");
        }
    }

    void PostProcessPreviewSession::Preview(const Keire::AssetId asset,
                                            const Keire::PostProcessProfileDefinition& definition)
    {
        const PreviewOperation operation(m_Applying);
        if (!asset)
        {
            throw std::invalid_argument("Post Processing preview requires an asset identity.");
        }
        const auto candidate = Keire::CreateRef<Keire::PostProcessProfileAsset>(definition);
        if (m_Active && m_Active->Asset != asset)
        {
            RestoreAuthored();
        }
        if (!m_Active)
        {
            const auto baseline = m_Dependencies.ReadSource(asset);
            if (!baseline)
            {
                throw std::runtime_error("The authored Post Processing profile is unavailable.");
            }
            m_Active.emplace(asset, baseline, baseline);
        }
        Apply(candidate);
    }

    void PostProcessPreviewSession::Apply(const Keire::Ref<Keire::PostProcessProfileAsset>& candidate)
    {
        try
        {
            if (!m_Dependencies.Publish(m_Active->Asset, candidate))
            {
                throw std::runtime_error("The Post Processing profile could not be published for live preview.");
            }
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            try
            {
                // A callback may fail after publication. Keep the baseline owned for a later retry.
                (void)m_Dependencies.Publish(m_Active->Asset, m_Active->Current);
            }
            catch (...)
            {
            }
            std::rethrow_exception(failure);
        }
        m_Active->Current = candidate;
    }

    void PostProcessPreviewSession::RestoreAuthored()
    {
        if (!m_Active)
        {
            return;
        }
        auto baseline = m_Active->Baseline;
        try
        {
            if (const auto source = m_Dependencies.ReadSource(m_Active->Asset))
            {
                baseline = source;
            }
        }
        catch (...)
        {
            // Deleted or invalid source data must not leave an unsaved preview installed.
        }
        Apply(baseline);
        m_Active.reset();
    }

    void PostProcessPreviewSession::Stop()
    {
        const PreviewOperation operation(m_Applying);
        RestoreAuthored();
    }

    void PostProcessPreviewSession::CommitSavedBaseline(const Keire::AssetId asset,
                                                        Keire::Ref<Keire::PostProcessProfileAsset> baseline) noexcept
    {
        if (baseline && m_Active && m_Active->Asset == asset)
        {
            m_Active->Baseline = std::move(baseline);
        }
    }
} // namespace KeireEditor
