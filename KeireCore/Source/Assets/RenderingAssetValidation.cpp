#include "KeireInternal/Assets/RenderingAssetValidation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace Keire::Detail
{
    bool ValidShaderIdentifier(const std::string_view value)
    {
        if (value.empty() || value.size() > 128 ||
            !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
            return false;
        return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                   { return std::isalnum(character) || character == '_'; });
    }

    void ValidateShaderDefinition(const ShaderAssetDefinition& definition, const bool requireVariants,
                                  const bool allowMissingVariants)
    {
        constexpr auto OcclusionSupportMask = static_cast<std::uint8_t>(ShaderOcclusionSupport::ConservativeBounds |
                                                                        ShaderOcclusionSupport::DepthOnlyGeometryMatch);
        const auto occlusionSupport = static_cast<std::uint8_t>(definition.OcclusionSupport);
        if (definition.SchemaVersion != 2 ||
            (definition.VertexLayoutVersion < 1 || definition.VertexLayoutVersion > 3) ||
            definition.Topology > ShaderPrimitiveTopology::PointList || definition.Culling > ShaderCullMode::Back ||
            (definition.SpatialLightingAbiVersion != 0U && definition.SpatialLightingAbiVersion != 2U &&
             definition.SpatialLightingAbiVersion != 3U) ||
            (definition.InstanceAddressingAbiVersion != 0U && definition.InstanceAddressingAbiVersion != 2U) ||
            (definition.InstanceAddressingAbiVersion == 2U && !definition.UsesInstancing) ||
            (occlusionSupport & static_cast<std::uint8_t>(~OcclusionSupportMask)) != 0U ||
            definition.UserResourceSlots > 16U || definition.UserReadOnlyBuffers > 8U ||
            (definition.SpatialLightingAbiVersion >= 2U &&
             (!definition.UsesImageBasedLighting || definition.VertexLayoutVersion != 3U)) ||
            (definition.SpatialLightingAbiVersion == 3U && !definition.UsesForwardPlus) || definition.Source.empty() ||
            definition.Source.is_absolute() ||
            definition.Source.lexically_normal().generic_string().starts_with("..") ||
            !ValidShaderIdentifier(definition.VertexEntry) || !ValidShaderIdentifier(definition.FragmentEntry))
            throw std::invalid_argument("Shader definition contains an unsupported schema, path, or entry point.");
        if (definition.MaximumWorldPositionDisplacementRadius &&
            (!std::isfinite(*definition.MaximumWorldPositionDisplacementRadius) ||
             *definition.MaximumWorldPositionDisplacementRadius < 0.0F))
        {
            throw std::invalid_argument(
                "Shader maximum world-position displacement radius must be a finite nonnegative value.");
        }
        if (!definition.MaximumWorldPositionDisplacementRadius &&
            definition.OcclusionSupport != ShaderOcclusionSupport::None)
        {
            throw std::invalid_argument(
                "Shader occlusion support requires a known maximum world-position displacement radius.");
        }
        if (definition.MaximumWorldPositionDisplacementRadius &&
            *definition.MaximumWorldPositionDisplacementRadius > 0.0F &&
            HasShaderOcclusionSupport(definition.OcclusionSupport, ShaderOcclusionSupport::DepthOnlyGeometryMatch))
        {
            throw std::invalid_argument(
                "Displaced shaders may use conservative bounds but cannot enter the depth-only occluder path.");
        }
        if (definition.Properties.size() > MaximumShaderProperties ||
            definition.Dependencies.size() > MaximumShaderDependencies ||
            (!allowMissingVariants && definition.Variants.empty()) ||
            (requireVariants && definition.Variants.size() != 3))
            throw std::invalid_argument("Shader definition exceeds a bounded collection or lacks variants.");

        std::set<std::string, std::less<>> propertyNames;
        std::set<AssetId> propertyIds;
        std::size_t numericProperties = 0;
        std::size_t textureProperties = 0;
        for (const auto& property : definition.Properties)
        {
            if (!ValidShaderIdentifier(property.Name) || !propertyNames.insert(property.Name).second ||
                property.Type > ShaderPropertyType::Texture2D ||
                (property.Id && !propertyIds.insert(property.Id).second))
                throw std::invalid_argument("Shader property names and types must be unique supported identifiers.");
            if (property.Type == ShaderPropertyType::Texture2D)
            {
                ++textureProperties;
                if (property.TextureSemantic > ShaderTextureSemantic::Roughness)
                    throw std::invalid_argument("Shader texture property semantic is invalid.");
            }
            else
            {
                ++numericProperties;
                if (!Math::IsFinite(property.DefaultValue))
                    throw std::invalid_argument("Shader numeric property defaults must be finite.");
                if ((property.Minimum && !std::isfinite(*property.Minimum)) ||
                    (property.Maximum && !std::isfinite(*property.Maximum)) ||
                    (property.Step && (!std::isfinite(*property.Step) || *property.Step <= 0.0F)) ||
                    (property.Minimum && property.Maximum && *property.Minimum > *property.Maximum))
                    throw std::invalid_argument("Shader numeric property range is invalid.");
            }
            if (property.DisplayName.size() > 128 || property.Category.size() > 128)
                throw std::invalid_argument("Shader property editor metadata exceeds its limit.");
        }
        if (numericProperties > MaximumShaderNumericProperties || textureProperties > MaximumShaderTextureProperties)
            throw std::invalid_argument("Shader exceeds the 64 numeric slot or 16 texture slot ABI limit.");
        constexpr std::size_t portableFragmentSamplerLimit = 16;
        const auto reservedSamplers = (definition.ReceivesShadows ? 2U : 0U) +
                                      (definition.UsesImageBasedLighting ? 2U : 0U) +
                                      (definition.SpatialLightingAbiVersion >= 2U ? 5U : 0U);
        if (textureProperties + definition.UserResourceSlots + reservedSamplers > portableFragmentSamplerLimit)
            throw std::invalid_argument(
                "Shader material textures and fixed lighting resources exceed the portable 16-sampler limit.");
        const auto reservedReadOnlyBuffers =
            (definition.UsesForwardPlus ? 3U : 0U) + (definition.SpatialLightingAbiVersion == 3U ? 1U : 0U);
        if (definition.UserReadOnlyBuffers + reservedReadOnlyBuffers > 8U)
            throw std::invalid_argument("Shader read-only buffers exceed the portable eight-buffer limit.");
        std::set<ShaderBinaryFormat> formats;
        for (const auto& variant : definition.Variants)
        {
            if (variant.Vertex.empty() || variant.Fragment.empty() || !formats.insert(variant.Format).second)
                throw std::invalid_argument("Shader variants must be non-empty and have unique formats.");
        }
    }

    void ValidateMaterialDefinition(const MaterialAssetDefinition& definition)
    {
        if (definition.SchemaVersion < 1 || definition.SchemaVersion > 3 ||
            definition.Properties.size() > MaximumShaderProperties ||
            definition.Surface.AlphaMode > MaterialAlphaMode::AlphaHoldout ||
            !std::isfinite(definition.Surface.AlphaCutoff) || definition.Surface.AlphaCutoff < 0.0F ||
            definition.Surface.AlphaCutoff > 1.0F || !std::isfinite(definition.EmissiveGIIntensity) ||
            definition.EmissiveGIIntensity < 0.0F || definition.EmissiveGIIntensity > 100'000.0F)
            throw std::invalid_argument("Material definition is invalid or exceeds its property limit.");
        for (const auto& [name, value] : definition.Properties)
        {
            if (!ValidShaderIdentifier(name))
                throw std::invalid_argument("Material property name is invalid.");
            std::visit(
                [](const auto& typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (!std::same_as<T, AssetId>)
                    {
                        Vector4 packed;
                        if constexpr (std::same_as<T, float>)
                            packed.X = typed;
                        else if constexpr (std::same_as<T, Vector2>)
                            packed = {typed.X, typed.Y, 0.0F, 0.0F};
                        else if constexpr (std::same_as<T, Vector3>)
                            packed = {typed.X, typed.Y, typed.Z, 0.0F};
                        else if constexpr (std::same_as<T, Vector4>)
                            packed = typed;
                        else
                            packed = {typed.Red, typed.Green, typed.Blue, typed.Alpha};
                        if (!Math::IsFinite(packed))
                            throw std::invalid_argument("Material property value is not finite.");
                    }
                },
                value);
        }
    }
} // namespace Keire::Detail
