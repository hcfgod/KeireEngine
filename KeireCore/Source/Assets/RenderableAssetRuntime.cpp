#include "Keire/Assets/RenderingAssets.h"

namespace Keire
{
    std::size_t MeshAsset::ResidentBytes() const noexcept
    {
        auto result = sizeof(*this) + m_Vertices.size() * sizeof(MeshVertex) +
                      m_Indices.size() * sizeof(std::uint32_t) + m_Submeshes.size() * sizeof(MeshSubmesh) +
                      m_Lods.size() * sizeof(MeshLod) + m_MaterialSlots.size() * sizeof(MeshMaterialSlot);
        for (const auto& slot : m_MaterialSlots)
            result += slot.Name.size();
        return result;
    }

    std::size_t Texture2DAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& mip : m_Mips)
            result += sizeof(TextureMipLevel) + mip.Pixels.size();
        return result;
    }

    Ref<Texture2DAsset> Texture2DAsset::Checkerboard()
    {
        TextureMipLevel mip{2,
                            2,
                            {std::byte{255}, std::byte{0}, std::byte{255}, std::byte{255}, std::byte{32}, std::byte{32},
                             std::byte{32}, std::byte{255}, std::byte{32}, std::byte{32}, std::byte{32}, std::byte{255},
                             std::byte{255}, std::byte{0}, std::byte{255}, std::byte{255}}};
        TextureImportSettings settings;
        settings.Mips = TextureMipPolicy::None;
        return CreateRef<Texture2DAsset>(settings, std::vector<TextureMipLevel>{std::move(mip)});
    }

    AssetDecoderRegistration CreateMeshAssetDecoder()
    {
        return {MeshAsset::StaticType(), MeshAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return MeshAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateTexture2DAssetDecoder()
    {
        return {Texture2DAsset::StaticType(), Texture2DAsset::Checkerboard(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return Texture2DAsset::Decode(bytes); }};
    }
} // namespace Keire
