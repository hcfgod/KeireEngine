#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace Keire::RenderBackend
{
    struct WaterDrawOrderEntry final
    {
        bool Water = false;
        std::size_t Index = 0;
    };

    // Both inputs follow the scene queue's descending +Z view-depth convention. Entries count toward
    // the existing command-buffer chunk budget; adjacent entries of one kind can still be drawn in a run.
    [[nodiscard]] inline std::vector<WaterDrawOrderEntry> BuildWaterDrawOrder(const std::span<const float> meshDepths,
                                                                              const std::span<const float> waterDepths)
    {
        std::vector<WaterDrawOrderEntry> result;
        result.reserve(meshDepths.size() + waterDepths.size());
        std::size_t mesh = 0;
        std::size_t water = 0;
        while (mesh < meshDepths.size() || water < waterDepths.size())
        {
            if (water < waterDepths.size() && (mesh == meshDepths.size() || waterDepths[water] > meshDepths[mesh]))
            {
                result.push_back({true, water++});
            }
            else
            {
                result.push_back({false, mesh++});
            }
        }
        return result;
    }
} // namespace Keire::RenderBackend
