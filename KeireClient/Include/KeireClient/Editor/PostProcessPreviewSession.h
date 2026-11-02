#pragma once

#include "Keire/Assets/PostProcessProfileAsset.h"

#include <functional>
#include <optional>

namespace KeireEditor
{
    struct PostProcessPreviewDependencies final
    {
        std::function<Keire::Ref<Keire::PostProcessProfileAsset>(Keire::AssetId)> ReadSource;
        std::function<bool(Keire::AssetId, const Keire::Ref<Keire::PostProcessProfileAsset>&)> Publish;
    };

    class PostProcessPreviewSession final
    {
      public:
        explicit PostProcessPreviewSession(PostProcessPreviewDependencies dependencies);
        void Preview(Keire::AssetId asset, const Keire::PostProcessProfileDefinition& definition);
        void Stop();
        void CommitSavedBaseline(Keire::AssetId asset, Keire::Ref<Keire::PostProcessProfileAsset> baseline) noexcept;

      private:
        struct ActivePreview
        {
            Keire::AssetId Asset;
            Keire::Ref<Keire::PostProcessProfileAsset> Baseline;
            Keire::Ref<Keire::PostProcessProfileAsset> Current;
        };

        void Apply(const Keire::Ref<Keire::PostProcessProfileAsset>& candidate);
        void RestoreAuthored();

        PostProcessPreviewDependencies m_Dependencies;
        std::optional<ActivePreview> m_Active;
        bool m_Applying = false;
    };
} // namespace KeireEditor
