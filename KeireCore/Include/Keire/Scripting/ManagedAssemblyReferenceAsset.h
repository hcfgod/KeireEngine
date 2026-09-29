#pragma once
#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    class KEIRE_API ManagedAssemblyReferenceAsset final : public Asset
    {
      public:
        explicit ManagedAssemblyReferenceAsset(std::string reference = {});
        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d414eULL, 0x4147454452454601ULL));
        }
        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override { return m_Reference.size(); }
        [[nodiscard]] const std::string& Reference() const noexcept { return m_Reference; }
        [[nodiscard]] static Ref<ManagedAssemblyReferenceAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(std::string_view reference);

      private:
        std::string m_Reference;
    };
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateManagedAssemblyReferenceAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateManagedAssemblyReferenceAssetImporter();
} // namespace Keire
