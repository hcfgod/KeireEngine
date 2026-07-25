#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Assets/RenderingAssets.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Keire::RenderBackend
{
    struct InstanceBatchKey final
    {
        AssetId Mesh;
        AssetId Material;
        std::uint32_t Submesh = 0;
        MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
        bool ReceiveShadows = true;
        bool CastShadows = true;
        bool SupportsInstancing = false;

        auto operator<=>(const InstanceBatchKey&) const noexcept = default;
    };

    struct InstanceBatch final
    {
        std::uint32_t First = 0;
        std::uint32_t Count = 0;

        [[nodiscard]] constexpr std::uint32_t GpuFirstInstance() const noexcept
        {
            // SDL does not portably apply first_instance to shader instance-ID built-ins.
            return 0;
        }
    };

    [[nodiscard]] std::vector<InstanceBatch> BuildInstanceBatches(std::span<const InstanceBatchKey> keys);
} // namespace Keire::RenderBackend
