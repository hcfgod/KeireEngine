#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

namespace Keire::Detail
{
    // Call under the registry's existing lock, before appending a job. Expired weak references
    // retain control blocks; compact periodically without scanning every live job on every submit.
    template <typename Job>
    void CompactJobRegistry(std::vector<std::weak_ptr<Job>>& records, std::size_t& nextCompaction)
    {
        if (records.size() < nextCompaction)
        {
            return;
        }

        std::erase_if(records, [](const auto& record) { return record.expired(); });
        constexpr std::size_t minimumInterval = 64;
        constexpr auto maximumSize = std::numeric_limits<std::size_t>::max();
        nextCompaction = records.size() > maximumSize / 2 ? maximumSize : std::max(minimumInterval, records.size() * 2);
    }
} // namespace Keire::Detail
