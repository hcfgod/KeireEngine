#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Physics/PhysicsSystem.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Keire
{
    struct PhysicsMaterialDefinition
    {
        std::uint32_t SchemaVersion = 1;
        float Friction = 0.5F;
        float Restitution = 0.0F;
        PhysicsMaterialCombineMode FrictionCombine = PhysicsMaterialCombineMode::Average;
        PhysicsMaterialCombineMode RestitutionCombine = PhysicsMaterialCombineMode::Average;
    };

    class KEIRE_API PhysicsMaterialAsset final : public Asset
    {
      public:
        explicit PhysicsMaterialAsset(PhysicsMaterialDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245504859ULL, 0x534d415445520001ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override { return sizeof(*this); }
        [[nodiscard]] const PhysicsMaterialDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<PhysicsMaterialAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const PhysicsMaterialDefinition& definition);
        static void Validate(const PhysicsMaterialDefinition& definition);

      private:
        PhysicsMaterialDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreatePhysicsMaterialAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreatePhysicsMaterialAssetDecoder();
} // namespace Keire
