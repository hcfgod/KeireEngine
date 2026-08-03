#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Assets/LightingAssets.h"
#include "Keire/Math/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Keire::Detail
{
    struct SpatialReflectionProbe
    {
        AssetId Entity;
        Matrix4 LocalToWorld;
        Matrix4 WorldToLocal;
        Vector3 BoxExtents{1.0F, 1.0F, 1.0F};
        float BlendDistance = 0.0F;
        std::int32_t Importance = 0;
        std::uint32_t CubeIndex = 0;
        float Intensity = 1.0F;
        bool BoxProjection = true;
    };

    struct SelectedReflectionProbe
    {
        const SpatialReflectionProbe* Probe = nullptr;
        float Weight = 0.0F;
    };

    [[nodiscard]] std::vector<SelectedReflectionProbe>
    SelectReflectionProbes(Vector3 worldPosition, std::span<const SpatialReflectionProbe> probes,
                           std::size_t maximumCount = 2);
    [[nodiscard]] Vector3 BoxProjectedReflection(Vector3 worldPosition, Vector3 worldDirection,
                                                 const SpatialReflectionProbe& probe) noexcept;
    [[nodiscard]] std::optional<std::array<Vector3, 9>>
    SampleLightProbeCoefficients(const LightProbeVolumeDefinition& volume, Vector3 localPosition);
    [[nodiscard]] std::optional<Vector3> SampleLightProbeIrradiance(const LightProbeVolumeDefinition& volume,
                                                                    Vector3 localPosition, Vector3 localNormal);

    struct ShadowAtlasKey
    {
        AssetId Light;
        std::uint8_t Subresource = 0;

        auto operator<=>(const ShadowAtlasKey&) const noexcept = default;
    };

    struct ShadowAtlasRequest
    {
        ShadowAtlasKey Key;
        std::uint16_t Resolution = 1024;
        std::int32_t Importance = 0;
    };

    struct ShadowAtlasKeyHash
    {
        std::size_t operator()(const ShadowAtlasKey& value) const noexcept
        {
            return std::hash<AssetId>{}(value.Light) ^ (static_cast<std::size_t>(value.Subresource) << 1U);
        }
    };

    struct ShadowAtlasAllocation
    {
        ShadowAtlasKey Key;
        std::uint16_t X = 0;
        std::uint16_t Y = 0;
        std::uint16_t Size = 0;
        Vector4 ScaleOffset;
    };

    class ShadowAtlasAllocator final
    {
      public:
        explicit ShadowAtlasAllocator(std::uint16_t atlasSize = 4096, std::uint16_t minimumTileSize = 256);

        [[nodiscard]] std::span<const ShadowAtlasAllocation> Allocate(std::span<const ShadowAtlasRequest> requests);
        [[nodiscard]] std::uint16_t AtlasSize() const noexcept { return m_AtlasSize; }
        [[nodiscard]] std::uint16_t MinimumTileSize() const noexcept { return m_MinimumTileSize; }

      private:
        std::uint16_t m_AtlasSize;
        std::uint16_t m_MinimumTileSize;
        std::vector<ShadowAtlasAllocation> m_Allocations;
        std::unordered_map<ShadowAtlasKey, ShadowAtlasAllocation, ShadowAtlasKeyHash> m_Previous;
    };
} // namespace Keire::Detail
