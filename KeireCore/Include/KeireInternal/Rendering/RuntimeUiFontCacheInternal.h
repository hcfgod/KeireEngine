#pragma once

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Ui/RuntimeUi.h"
#include "Keire/Ui/UiFontAssets.h"
#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Keire::RenderBackend
{
    struct RuntimeUiFontCpuCacheEntry final
    {
        AssetId Binding;
        AssetId Family;
        AssetId ResolvedFamily;
        AssetId Face;
        AssetHandle<UiFontFamilyAsset> FamilyHandle;
        AssetHandle<UiFontFaceAsset> FaceHandle;
        std::vector<std::uint32_t> Glyphs;
        std::vector<std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>> AtlasPages;
        std::size_t AtlasGlyphRequestCount = 0;
        std::uint64_t FamilyRevision = 0;
        std::uint64_t FaceRevision = 0;
        std::uint64_t LastUsedFrame = 0;
        std::uint16_t Weight = 400;
        RuntimeUiFontSlant Slant = RuntimeUiFontSlant::Normal;
        std::uint16_t CollectionIndex = 0;
    };
} // namespace Keire::RenderBackend
