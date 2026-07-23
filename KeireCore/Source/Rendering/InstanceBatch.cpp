#include "KeireInternal/Rendering/InstanceBatchInternal.h"

#include <limits>
#include <stdexcept>

namespace Keire::RenderBackend
{
    std::vector<InstanceBatch> BuildInstanceBatches(const std::span<const InstanceBatchKey> keys)
    {
        if (keys.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Instance-batch input exceeds the renderer's 32-bit draw limit.");
        std::vector<InstanceBatch> result;
        result.reserve(keys.size());
        for (std::uint32_t first = 0; first < keys.size();)
        {
            std::uint32_t count = 1;
            const auto& key = keys[first];
            if (key.SupportsInstancing && key.AlphaMode != MaterialAlphaMode::Blend)
            {
                while (first + count < keys.size() && keys[first + count] == key)
                    ++count;
            }
            result.push_back({first, count});
            first += count;
        }
        return result;
    }
} // namespace Keire::RenderBackend
