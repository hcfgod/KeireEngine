#include "KeireClient/Editor/AudioMixerPreviewSession.h"

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
                    throw std::logic_error("Audio Mixer preview cannot be changed from a publication callback.");
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

    AudioMixerPreviewSession::AudioMixerPreviewSession(AudioMixerPreviewDependencies dependencies)
        : m_Dependencies(std::move(dependencies))
    {
        if (!m_Dependencies.ReadSource || !m_Dependencies.ResolveImpulseResponses || !m_Dependencies.Publish ||
            !m_Dependencies.Submit)
        {
            throw std::invalid_argument(
                "Audio Mixer preview requires source, resource, publication and device services.");
        }
    }

    AudioMixerPreviewSession::Prepared AudioMixerPreviewSession::Prepare(Keire::Ref<Keire::AudioMixerAsset> mixer) const
    {
        if (!mixer)
        {
            throw std::runtime_error("The authored Audio Mixer source is unavailable.");
        }
        auto responses = m_Dependencies.ResolveImpulseResponses(mixer->Definition());
        return std::make_shared<const PreparedMix>(std::move(mixer), std::move(responses));
    }

    void AudioMixerPreviewSession::Preview(const Keire::AssetId asset, const Keire::AudioMixerDefinition& definition)
    {
        const PreviewOperation operation(m_Applying);
        if (!asset)
        {
            throw std::invalid_argument("Audio Mixer preview requires an asset identity.");
        }
        const auto candidate = Prepare(Keire::CreateRef<Keire::AudioMixerAsset>(definition));
        if (m_Active && m_Active->Asset != asset)
        {
            RestoreAuthored();
        }
        if (!m_Active)
        {
            const auto baseline = Prepare(m_Dependencies.ReadSource(asset));
            m_Active.emplace(asset, baseline, baseline);
        }
        Apply(candidate);
    }

    void AudioMixerPreviewSession::PublishAndSubmit(const Prepared& candidate)
    {
        if (!m_Dependencies.Publish(m_Active->Asset, candidate->Mixer))
        {
            throw std::runtime_error("The transient Audio Mixer asset could not be published for live preview.");
        }
        m_Dependencies.Submit(m_Active->Asset, candidate->Mixer->Definition(), candidate->ImpulseResponses);
    }

    void AudioMixerPreviewSession::Apply(const Prepared& candidate)
    {
        try
        {
            PublishAndSubmit(candidate);
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            try
            {
                // Publication callbacks can throw after committing. Roll back both consumers, retaining ownership
                // even if rollback fails so Stop can retry with the retained baseline and resources.
                PublishAndSubmit(m_Active->Current);
            }
            catch (...)
            {
            }
            std::rethrow_exception(failure);
        }
        m_Active->Current = candidate;
    }

    void AudioMixerPreviewSession::Stop()
    {
        const PreviewOperation operation(m_Applying);
        RestoreAuthored();
    }

    void AudioMixerPreviewSession::RestoreAuthored()
    {
        if (!m_Active)
        {
            return;
        }
        auto baseline = m_Active->Baseline;
        try
        {
            baseline = Prepare(m_Dependencies.ReadSource(m_Active->Asset));
        }
        catch (...)
        {
            // A deleted/unreadable source or unavailable new IR must not strand the last preview in the runtime.
        }
        Apply(baseline);
        m_Active.reset();
    }

    AudioMixerPreviewSession::Prepared
    AudioMixerPreviewSession::PrepareSavedBaseline(const Keire::AssetId asset,
                                                   const Keire::AudioMixerDefinition& definition) const
    {
        if (m_Applying)
        {
            throw std::logic_error("Audio Mixer preview cannot be saved from a publication callback.");
        }
        if (!m_Active || m_Active->Asset != asset)
        {
            return {};
        }
        return Prepare(Keire::CreateRef<Keire::AudioMixerAsset>(definition));
    }

    void AudioMixerPreviewSession::CommitSavedBaseline(const Keire::AssetId asset, Prepared baseline) noexcept
    {
        if (baseline && m_Active && m_Active->Asset == asset)
        {
            m_Active->Baseline = std::move(baseline);
        }
    }
} // namespace KeireEditor
