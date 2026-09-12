#include "KeireInternal/Rendering/RuntimeUiMaterialInternal.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    void ValidateRuntimeUiShader(const ShaderAssetDefinition& shader)
    {
        if (shader.VertexLayoutVersion != UiShaderVertexLayoutVersion || shader.UsesInstancing ||
            shader.ReceivesShadows || shader.UsesForwardPlus || shader.UsesImageBasedLighting ||
            shader.SpatialLightingAbiVersion != 0U || shader.InstanceAddressingAbiVersion != 0U ||
            shader.UserResourceSlots != 0U || shader.UserReadOnlyBuffers != 0U ||
            shader.Topology != ShaderPrimitiveTopology::TriangleList)
            throw std::invalid_argument("Runtime UI material requires an unlit runtime UI shader ABI.");
        const auto textures =
            std::ranges::count(shader.Properties, ShaderPropertyType::Texture2D, &ShaderPropertyDefinition::Type);
        if (textures > 15 || shader.Properties.size() - static_cast<std::size_t>(textures) > 64U)
            throw std::invalid_argument("Runtime UI shader exceeds 15 material textures or 64 numeric properties.");
    }

    RuntimeUiMaterialValues BuildRuntimeUiMaterialValues(const ShaderAssetDefinition& shader,
                                                         const MaterialAssetDefinition& material)
    {
        ValidateRuntimeUiShader(shader);
        for (const auto& [name, value] : material.Properties)
        {
            (void)value;
            if (std::ranges::find(shader.Properties, name, &ShaderPropertyDefinition::Name) == shader.Properties.end())
                throw std::invalid_argument("Runtime UI material contains an undeclared shader property.");
        }
        RuntimeUiMaterialValues result;
        for (const auto& property : shader.Properties)
        {
            const auto found = material.Properties.find(property.Name);
            if (property.Type == ShaderPropertyType::Texture2D)
            {
                auto texture = property.DefaultTexture;
                if (found != material.Properties.end())
                {
                    const auto* value = std::get_if<AssetId>(&found->second);
                    if (!value)
                        throw std::invalid_argument("Runtime UI texture property requires a texture asset.");
                    texture = *value;
                }
                result.Textures.push_back(texture);
                continue;
            }
            Vector4 packed = property.DefaultValue;
            if (found != material.Properties.end())
            {
                const auto& value = found->second;
                if (const auto* scalar = std::get_if<float>(&value);
                    scalar && property.Type == ShaderPropertyType::Scalar)
                    packed = {*scalar, 0.0F, 0.0F, 0.0F};
                else if (const auto* vector = std::get_if<Vector2>(&value);
                         vector && property.Type == ShaderPropertyType::Vector2)
                    packed = {vector->X, vector->Y, 0.0F, 0.0F};
                else if (const auto* vector = std::get_if<Vector3>(&value);
                         vector && property.Type == ShaderPropertyType::Vector3)
                    packed = {vector->X, vector->Y, vector->Z, 0.0F};
                else if (const auto* vector = std::get_if<Vector4>(&value);
                         vector && property.Type == ShaderPropertyType::Vector4)
                    packed = *vector;
                else if (const auto* color = std::get_if<Color>(&value);
                         color && property.Type == ShaderPropertyType::Color)
                    packed = {color->Red, color->Green, color->Blue, color->Alpha};
                else
                    throw std::invalid_argument("Runtime UI property type does not match the shader declaration.");
            }
            if (!std::isfinite(packed.X) || !std::isfinite(packed.Y) || !std::isfinite(packed.Z) ||
                !std::isfinite(packed.W))
                throw std::invalid_argument("Runtime UI numeric properties must be finite.");
            result.Numeric.push_back(packed);
        }
        if (result.Numeric.empty())
            result.Numeric.emplace_back();
        return result;
    }
} // namespace Keire::Detail
