#pragma once

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Audio/AudioAssets.h"

#include <optional>
#include <stdexcept>

namespace KeireEditor::Detail
{
    [[nodiscard]] inline Keire::Ref<const Keire::AudioClipAsset>
    RequirePreviewAudioClip(const Keire::AssetHandle<Keire::AudioClipAsset>& handle)
    {
        // Runtime fallback silence is not a successful editor preview of the selected source.
        const auto clip = handle.TryGetLoaded();
        if (!clip)
            throw std::runtime_error(
                "No playable imported audio is ready. Wait for import or correct the source and reimport.");
        return clip;
    }

    [[nodiscard]] inline bool ShouldDeferAssetPreviewLoad(const std::optional<Keire::AssetTypeId> mountedType,
                                                          const std::optional<Keire::AssetTypeId> sourceType,
                                                          const Keire::AssetImportState importState,
                                                          const Keire::AssetTypeId expectedType) noexcept
    {
        // Initial imports publish their catalog asynchronously. Missing or failed sources must still diagnose.
        return !mountedType && sourceType == expectedType && importState == Keire::AssetImportState::NotImported;
    }
} // namespace KeireEditor::Detail
