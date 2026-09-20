#include "Keire/Assets/RenderingAssets.h"

namespace Keire
{
    Ref<ShaderAsset> ShaderAsset::Error() { return CreateRef<ShaderAsset>(); }

    Ref<MaterialAsset> MaterialAsset::Error()
    {
        MaterialAssetDefinition definition;
        definition.Properties.emplace("ErrorColor", Color{1.0F, 0.0F, 1.0F, 1.0F});
        return CreateRef<MaterialAsset>(std::move(definition));
    }

    AssetDecoderRegistration CreateShaderAssetDecoder()
    {
        return {ShaderAsset::StaticType(), ShaderAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return ShaderAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateMaterialAssetDecoder()
    {
        return {MaterialAsset::StaticType(), MaterialAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return MaterialAsset::Decode(bytes); }};
    }
} // namespace Keire
