#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Keire
{
    /// One constant-density cell in a sampled VFX volume. Cells may be sparse; Density controls their relative
    /// probability after accounting for cell volume.
    struct VfxVolumeCell
    {
        Vector3 Minimum{-0.5F, -0.5F, -0.5F};
        Vector3 Maximum{0.5F, 0.5F, 0.5F};
        float Density = 1.0F;

        [[nodiscard]] bool operator==(const VfxVolumeCell&) const noexcept = default;
    };

    struct VfxVolumeDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::vector<VfxVolumeCell> Cells{{}};

        [[nodiscard]] bool operator==(const VfxVolumeDefinition&) const noexcept = default;
    };

    /// Cooked sparse density volume shared by CPU and GPU VFX shape sampling.
    class KEIRE_API VfxVolumeAsset final : public Asset
    {
      public:
        explicit VfxVolumeAsset(VfxVolumeDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245564658ULL, 0x564f4c554d450001ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const VfxVolumeDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] std::span<const float> CumulativeWeights() const noexcept { return m_CumulativeWeights; }
        [[nodiscard]] float TotalWeight() const noexcept { return m_TotalWeight; }
        [[nodiscard]] Vector3 Sample(std::uint32_t randomValue) const noexcept;

        [[nodiscard]] static Ref<VfxVolumeAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const VfxVolumeDefinition& definition);
        static void Validate(const VfxVolumeDefinition& definition);

      private:
        VfxVolumeDefinition m_Definition;
        std::vector<float> m_CumulativeWeights;
        float m_TotalWeight = 0.0F;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateVfxVolumeAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateVfxVolumeAssetDecoder();
} // namespace Keire
