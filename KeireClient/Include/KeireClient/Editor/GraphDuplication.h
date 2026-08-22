#pragma once

#include "Keire/Rendering/MaterialEcosystem.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"
#include "Keire/Vfx/VfxSystem.h"

#include <span>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct ShaderGraphFunctionExtraction
    {
        Keire::GraphFunctionDefinition Function;
        Keire::ShaderGraphDefinition Parent;
        Keire::AssetId CallNode;
    };

    struct MaterialGraphFunctionExtraction
    {
        Keire::GraphFunctionDefinition Function;
        Keire::MaterialGraphDefinition Parent;
        Keire::AssetId CallNode;
    };

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

    [[nodiscard]] ShaderGraphFunctionExtraction
    ExtractShaderGraphSelection(const Keire::ShaderGraphDefinition& definition,
                                std::span<const Keire::AssetId> selection, Keire::AssetId functionAsset,
                                std::string_view functionName = "Extracted Shader Function");
    [[nodiscard]] MaterialGraphFunctionExtraction
    ExtractMaterialGraphSelection(const Keire::MaterialGraphDefinition& definition,
                                  std::span<const Keire::AssetId> selection, Keire::AssetId functionAsset,
                                  std::string_view functionName = "Extracted Material Function");
} // namespace KeireEditor
