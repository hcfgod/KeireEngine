#pragma once

#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"
#include "Keire/Vfx/VfxSystem.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    inline constexpr std::size_t MaximumGraphFragmentBytes = 1024U * 1024U;

    [[nodiscard]] std::string CopyShaderGraphFragment(const Keire::ShaderGraphDefinition& definition,
                                                      std::span<const Keire::AssetId> selection);
    [[nodiscard]] std::string CopyMaterialGraphFragment(const Keire::MaterialGraphDefinition& definition,
                                                        std::span<const Keire::AssetId> selection);
    [[nodiscard]] std::string CopyVfxGraphFragment(const Keire::VfxEffectDefinition& definition, Keire::AssetId system,
                                                   std::span<const Keire::AssetId> selection);

    [[nodiscard]] std::vector<Keire::AssetId> PasteShaderGraphFragment(Keire::ShaderGraphDefinition& definition,
                                                                       std::string_view fragment,
                                                                       Keire::Vector2 offset = {32.0F, 32.0F});
    [[nodiscard]] std::vector<Keire::AssetId> PasteMaterialGraphFragment(Keire::MaterialGraphDefinition& definition,
                                                                         std::string_view fragment,
                                                                         Keire::Vector2 offset = {32.0F, 32.0F});
    [[nodiscard]] std::vector<Keire::AssetId> PasteVfxGraphFragment(Keire::VfxEffectDefinition& definition,
                                                                    Keire::AssetId system, std::string_view fragment,
                                                                    Keire::Vector2 offset = {32.0F, 32.0F});
} // namespace KeireEditor
