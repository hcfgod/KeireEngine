#pragma once

#include "Keire/Audio/AudioAssets.h"

#include <functional>
#include <memory>
#include <optional>

namespace KeireEditor
{
    struct AudioMixerPreviewDependencies final
    {
        std::function<Keire::Ref<Keire::AudioMixerAsset>(Keire::AssetId)> ReadSource;
        std::function<Keire::AudioMixerImpulseResponses(const Keire::AudioMixerDefinition&)> ResolveImpulseResponses;
        std::function<bool(Keire::AssetId, const Keire::Ref<Keire::AudioMixerAsset>&)> Publish;
        std::function<void(Keire::AssetId, const Keire::AudioMixerDefinition&,
                           const Keire::AudioMixerImpulseResponses&)>
            Submit;
    };

    // Owner-thread only. Keeps the authored mix and decoded resources alive until restoration succeeds.
    class AudioMixerPreviewSession final
    {
      public:
        struct PreparedMix final
        {
            Keire::Ref<Keire::AudioMixerAsset> Mixer;
            Keire::AudioMixerImpulseResponses ImpulseResponses;
        };

        using Prepared = std::shared_ptr<const PreparedMix>;

        explicit AudioMixerPreviewSession(AudioMixerPreviewDependencies dependencies);

        AudioMixerPreviewSession(const AudioMixerPreviewSession&) = delete;
        AudioMixerPreviewSession& operator=(const AudioMixerPreviewSession&) = delete;
        AudioMixerPreviewSession(AudioMixerPreviewSession&&) = delete;
        AudioMixerPreviewSession& operator=(AudioMixerPreviewSession&&) = delete;

        void Preview(Keire::AssetId asset, const Keire::AudioMixerDefinition& definition);
        void Stop();
        [[nodiscard]] bool Active() const noexcept { return m_Active.has_value(); }
        [[nodiscard]] Prepared PrepareSavedBaseline(Keire::AssetId asset,
                                                    const Keire::AudioMixerDefinition& definition) const;
        void CommitSavedBaseline(Keire::AssetId asset, Prepared baseline) noexcept;

      private:
        struct ActiveMix final
        {
            Keire::AssetId Asset;
            Prepared Baseline;
            Prepared Current;
        };

        [[nodiscard]] Prepared Prepare(Keire::Ref<Keire::AudioMixerAsset> mixer) const;
        void Apply(const Prepared& candidate);
        void PublishAndSubmit(const Prepared& candidate);
        void RestoreAuthored();

        AudioMixerPreviewDependencies m_Dependencies;
        std::optional<ActiveMix> m_Active;
        bool m_Applying = false;
    };
} // namespace KeireEditor
