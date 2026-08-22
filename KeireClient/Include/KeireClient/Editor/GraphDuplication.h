#pragma once

#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"
#include "Keire/Vfx/VfxSystem.h"

#include <span>
#include <vector>

namespace KeireEditor
{
    /// Duplicates the editable portion of a selection, its internal cables, annotations, and fully-contained comments.
    /// Every persisted identity is regenerated and the returned node identities preserve source selection order.
    [[nodiscard]] std::vector<Keire::AssetId> DuplicateShaderGraphSelection(Keire::ShaderGraphDefinition& definition,
                                                                            std::span<const Keire::AssetId> selection,
                                                                            Keire::Vector2 offset = {32.0F, 32.0F});
    [[nodiscard]] std::vector<Keire::AssetId>
    DuplicateMaterialGraphSelection(Keire::MaterialGraphDefinition& definition,
                                    std::span<const Keire::AssetId> selection, Keire::Vector2 offset = {32.0F, 32.0F});
    [[nodiscard]] std::vector<Keire::AssetId> DuplicateVfxGraphSelection(Keire::VfxEffectDefinition& definition,
                                                                         Keire::AssetId system,
                                                                         std::span<const Keire::AssetId> selection,
                                                                         Keire::Vector2 offset = {32.0F, 32.0F});
} // namespace KeireEditor
