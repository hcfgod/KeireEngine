#pragma once

#include "Keire/Assets/AssetPipeline.h"

#include <optional>

namespace KeireEditor::Detail
{
    [[nodiscard]] inline bool ShouldDeferAssetPreviewLoad(const std::optional<Keire::AssetTypeId> mountedType,
                                                          const std::optional<Keire::AssetTypeId> sourceType,
                                                          const Keire::AssetImportState importState,
                                                          const Keire::AssetTypeId expectedType) noexcept
    {
        // Initial imports publish their catalog asynchronously. Missing or failed sources must still diagnose.
        return !mountedType && sourceType == expectedType && importState == Keire::AssetImportState::NotImported;
    }
} // namespace KeireEditor::Detail
