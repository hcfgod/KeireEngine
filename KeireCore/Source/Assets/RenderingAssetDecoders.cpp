#include "Keire/Assets/RenderingAssets.h"

namespace Keire
{
    Ref<ShaderAsset> ShaderAsset::Error() { return CreateRef<ShaderAsset>(); }

    Ref<MaterialAsset> MaterialAsset::Error() { return CreateRef<MaterialAsset>(); }

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
