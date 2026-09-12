#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Undo.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace KeireEditor
{
    [[nodiscard]] Keire::MaterialPropertyResolution
    ResolveMaterialVariantOverrides(const Keire::MaterialInstanceDefinition& instance,
                                    const Keire::ShaderAssetDefinition& shader);
    [[nodiscard]] bool RemoveInactiveMaterialVariantProperties(Keire::MaterialInstanceDefinition& instance,
                                                               const Keire::ShaderAssetDefinition& shader);
    [[nodiscard]] bool HasMaterialVariantProperty(const Keire::MaterialInstanceDefinition& instance,
                                                  const Keire::ShaderPropertyDefinition& property);
    /// A missing value restores inheritance, retaining incompatible historical overrides.
    [[nodiscard]] bool SetMaterialVariantProperty(Keire::MaterialInstanceDefinition& instance,
                                                  const Keire::ShaderPropertyDefinition& property,
                                                  std::optional<Keire::MaterialPropertyValue> value);
    /// Publishes compatible clipboard overrides together, retaining incompatible saved history.
    [[nodiscard]] std::size_t
    PasteMaterialVariantProperties(Keire::MaterialInstanceDefinition& instance,
                                   const Keire::ShaderAssetDefinition& shader,
                                   std::span<const Keire::MaterialPropertyOverride> properties);
    /// Explicit cross-shader paste using code symbols instead of source property identities.
    [[nodiscard]] std::size_t
    PasteMaterialVariantPropertiesByName(Keire::MaterialInstanceDefinition& instance,
                                         const Keire::ShaderAssetDefinition& shader,
                                         std::span<const Keire::MaterialPropertyOverride> properties);

    struct MaterialVariantAncestor
    {
        Keire::AssetTypeId Type;
        Keire::AssetId Parent;
    };

    using MaterialVariantParentResolver = std::function<std::optional<MaterialVariantAncestor>(Keire::AssetId)>;

    void ValidateMaterialVariantParent(Keire::AssetId variant, Keire::AssetId parent,
                                       const MaterialVariantParentResolver& resolve);

    [[nodiscard]] std::unique_ptr<Keire::UndoCommand>
    CreateMaterialVariantEdit(Keire::AssetId asset, std::vector<std::byte> before, std::vector<std::byte> after,
                              std::uint64_t editSerial, std::function<void(std::span<const std::byte>)> apply,
                              Keire::UndoAvailability available);
} // namespace KeireEditor
